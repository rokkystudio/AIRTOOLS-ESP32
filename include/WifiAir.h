#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "AirtoolsTypes.h"
#include "Storage.h"

/**
 * Captures 2.4 GHz management and data metadata through ESP32 promiscuous Wi-Fi.
 */
class WifiAir
{
public:
    /**
     * Initializes the Wi-Fi radio in station mode without connecting to an AP.
     */
    void begin(Storage *storage);

    /**
     * Starts channel hopping and promiscuous packet accounting.
     */
    void startScan();

    /**
     * Stops promiscuous capture and keeps collected runtime indexes in memory.
     */
    void stopScan();

    /**
     * Drains queued capture frames to storage and keeps channel hopping active.
     */
    void loop();

    /**
     * Selects one BSSID and channel as the active capture target.
     */
    bool selectTarget(const uint8_t *bssid, int channel);

    /**
     * Clears the active capture target and returns to discovery mode.
     */
    void clearTarget();

    /**
     * Returns a text index compatible with AIRTOOLS /networks responses.
     */
    String formatNetworks() const;

    /**
     * Returns a text index compatible with AIRTOOLS /clients responses.
     */
    String formatClients() const;

    /**
     * Returns current runtime mode and selected target details.
     */
    String formatStatus() const;

    /**
     * Returns whether promiscuous scan is currently active.
     */
    bool isScanning() const;

    /**
     * Copies the active capture target BSSID and channel into the supplied
     * buffers. Returns false when no target is selected.
     */
    bool getTarget(uint8_t *bssid, int *channel) const;

    /**
     * Transmits deauthentication frames spoofing the supplied BSSID. When station
     * is nullptr the deauth is broadcast, otherwise it addresses that station MAC.
     * Returns the number of frames accepted by the radio.
     */
    int sendDeauth(const uint8_t *bssid, int channel, const uint8_t *station, int count);

private:
    struct CaptureFrame
    {
        uint8_t bssid[6];
        uint32_t length;
        bool hasMic;
        bool hasAck;
        uint8_t data[AIRTOOLS_MAX_CAPTURE_FRAME_BYTES];
    };

    static constexpr int CAPTURE_QUEUE_DEPTH = 8;

    static void promiscuousThunk(void *buffer, wifi_promiscuous_pkt_type_t type);
    void handlePacket(const wifi_promiscuous_pkt_t *packet, wifi_promiscuous_pkt_type_t type);
    void accountAp(const uint8_t *bssid, const char *essid, int channel, int rssi, bool beacon, bool probe);
    void accountClient(const uint8_t *bssid, const uint8_t *station, int rssi);
    void recordEapolIfPresent(const uint8_t *bssid, const uint8_t *frame, int length, uint8_t subtype, bool toDs, bool fromDs);
    void enqueueCaptureFrame(const uint8_t *bssid, const uint8_t *frame, uint32_t length, bool hasMic, bool hasAck);
    void drainCaptureQueue();
    const char *essidForLocked(const uint8_t *bssid) const;
    void getEssid(const uint8_t *bssid, char *out, size_t outLength) const;
    AirtoolsNetwork *findOrCreateNetwork(const uint8_t *bssid);
    AirtoolsClient *findOrCreateClient(const uint8_t *bssid, const uint8_t *station);
    bool targetAccepts(const uint8_t *bssid, int channel) const;
    void hopChannelLocked();
    bool transmitRawFrame(const uint8_t *frame, int length);

    AirtoolsNetwork networks[AIRTOOLS_MAX_NETWORKS];
    AirtoolsClient clients[AIRTOOLS_MAX_CLIENTS];
    AirtoolsTarget target;
    Storage *storage = nullptr;
    bool scanning = false;
    int currentChannel = 1;
    uint32_t lastHopMs = 0;

    mutable SemaphoreHandle_t stateMutex = nullptr;
    QueueHandle_t captureQueue = nullptr;
    CaptureFrame pendingCaptureFrame = {};
    CaptureFrame drainCaptureFrame = {};
};
