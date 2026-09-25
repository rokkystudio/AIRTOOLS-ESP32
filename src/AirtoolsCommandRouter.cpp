#include "AirtoolsCommandRouter.h"
#include "AirtoolsConfig.h"
#include "AirtoolsTransport.h"

AirtoolsCommandRouter::AirtoolsCommandRouter(WifiAir &wifiAir, Storage &storage, BoardSupport &board)
    : wifiAir(wifiAir),
      storage(storage),
      board(board)
{
}

void AirtoolsCommandRouter::setTransport(AirtoolsTransport *transport)
{
    this->transport = transport;
}

bool AirtoolsCommandRouter::handleCommand(const String &command, String &response)
{
    response = "";

    if (command.startsWith("/status")) {
        response = handleStatus();
        return true;
    }

    if (command.startsWith("/start")) {
        response = handleStart();
        return true;
    }

    if (command.startsWith("/scan/start")) {
        wifiAir.clearTarget();
        wifiAir.startScan();
        response = "OK scan started\n";
        return true;
    }

    if (command.startsWith("/scan/stop")) {
        wifiAir.stopScan();
        response = "OK scan stopped\n";
        return true;
    }

    if (command.startsWith("/networks")) {
        response = wifiAir.formatNetworks();
        return true;
    }

    if (command.startsWith("/select")) {
        response = handleSelect(command);
        return true;
    }

    if (command.startsWith("/clients")) {
        response = wifiAir.formatClients();
        return true;
    }

    if (command.startsWith("/handshakes")) {
        response = storage.formatHandshakes();
        return true;
    }

    if (command.startsWith("/handshake/download")) {
        return handleHandshakeDownload(command, response);
    }

    if (command.startsWith("/aireplay")) {
        response = handleAireplay(command);
        return true;
    }

    if (command.startsWith("/replay")) {
        response = handleReplay(command);
        return true;
    }

    response = "ERR unknown_command\n";
    return true;
}

String AirtoolsCommandRouter::handleStatus() const
{
    String response;
    response += "OK airtools=1 transport=ble firmware=";
    response += AIRTOOLS_FIRMWARE_VERSION_TEXT;
    response += "\n";
    response += wifiAir.formatStatus();
    response += " ";
    response += storage.formatStatus();
    response += " ";
    response += board.formatStatus();
    response += "\n";
    return response;
}

String AirtoolsCommandRouter::handleStart()
{
    wifiAir.startScan();
    return wifiAir.isScanning() ? "OK started\n" : "ERR start_failed\n";
}

bool AirtoolsCommandRouter::handleHandshakeDownload(const String &command, String &response)
{
    String fileName = valueOf(command, "file");
    if (fileName.length() == 0) {
        response = "ERR missing_file\n";
        return true;
    }

    if (!transport) {
        response = "ERR transport_unavailable\n";
        return true;
    }

    transport->beginStream("");
    storage.streamHandshakeDownload(fileName, &AirtoolsCommandRouter::streamChunkThunk, transport);
    transport->endStream();
    return false;
}

void AirtoolsCommandRouter::streamChunkThunk(const char *data, size_t length, void *context)
{
    AirtoolsTransport *transport = static_cast<AirtoolsTransport *>(context);
    if (transport) {
        transport->streamChunk(data, length);
    }
}

String AirtoolsCommandRouter::handleSelect(const String &command)
{
    uint8_t bssid[6];
    String bssidText = valueOf(command, "bssid");
    String channelText = valueOf(command, "channel");

    if (!parseMac(bssidText, bssid)) {
        return "ERR invalid_bssid\n";
    }

    int channel = channelText.toInt();
    if (!wifiAir.selectTarget(bssid, channel)) {
        return "ERR invalid_channel\n";
    }

    String response = "OK selected bssid=";
    response += macToString(bssid);
    response += " channel=";
    response += channel;
    response += "\n";
    return response;
}

String AirtoolsCommandRouter::handleAireplay(const String &command) const
{
    String mode = valueOf(command, "mode");
    if (mode == "test") {
        String response = "OK aireplay mode=test raw_tx=supported count=";
        response += valueOf(command, "count").toInt();
        response += "\n";
        return response;
    }

    return "ERR unsupported feature=aireplay reason=esp32_aireplay_mode_unsupported\n";
}

String AirtoolsCommandRouter::handleReplay(const String &command)
{
    String stationText = valueOf(command, "station");
    uint8_t station[6];
    bool hasStation = false;
    if (stationText.length() > 0) {
        if (!parseMac(stationText, station)) {
            return "ERR invalid_station\n";
        }
        hasStation = true;
    }

    uint8_t bssid[6];
    int channel = 0;
    if (!wifiAir.getTarget(bssid, &channel)) {
        return "ERR replay_no_target\n";
    }

    int count = valueOf(command, "count").toInt();
    if (count < 1) {
        count = 5;
    }
    else if (count > 128) {
        count = 128;
    }

    int sent = wifiAir.sendDeauth(bssid, channel, hasStation ? station : nullptr, count);

    String response = "OK replay bssid=";
    response += macToString(bssid);
    response += " channel=";
    response += channel;
    response += " station=";
    response += hasStation ? macToString(station) : "broadcast";
    response += " count=";
    response += count;
    response += " sent=";
    response += sent;
    response += " raw_tx=supported\n";
    return response;
}

String AirtoolsCommandRouter::valueOf(const String &command, const String &key) const
{
    String pattern = key + "=";
    int start = command.indexOf(pattern);
    if (start < 0) {
        return "";
    }

    start += pattern.length();
    int end = command.indexOf("&", start);
    if (end < 0) {
        end = command.length();
    }

    return urlDecode(command.substring(start, end));
}

String AirtoolsCommandRouter::urlDecode(const String &value) const
{
    String decoded;
    decoded.reserve(value.length());

    for (int index = 0; index < value.length(); ++index) {
        char current = value[index];
        if (current == '+') {
            decoded += ' ';
            continue;
        }

        if (current == '%' && index + 2 < value.length()) {
            int high = hexNibble(value[index + 1]);
            int low = hexNibble(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded += static_cast<char>((high << 4) | low);
                index += 2;
                continue;
            }
        }

        decoded += current;
    }

    return decoded;
}

int AirtoolsCommandRouter::hexNibble(char value) const
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }

    return -1;
}
