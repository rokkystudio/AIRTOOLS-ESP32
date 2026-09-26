#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "AirtoolsConfig.h"

/** Receives one chunk of a streamed text response. */
typedef void (*HexChunkWriter)(const char *data, size_t length, void *context);

/**
 * Stores passive capture artifacts and reports the runtime handshake index.
 */
class Storage
{
public:
    /**
     * Mounts SD when enabled, otherwise mounts LittleFS for boards without removable storage.
     */
    void begin();

    /**
     * Returns a short status string for /status diagnostics.
     */
    String formatStatus() const;

    /**
     * Returns the current handshake index in AIRTOOLS CSV format.
     */
    String formatHandshakes() const;

    /**
     * Streams one stored PCAP file as bounded hex text chunks through the supplied
     * writer instead of building one large in-memory String.
     */
    void streamHandshakeDownload(const String &fileName, HexChunkWriter writer, void *context) const;

    /**
     * Stores the latest real beacon or probe-response frame carrying a non-empty
     * ESSID for the supplied BSSID. A complete handshake is persisted only after
     * this management frame is available.
     */
    void recordManagementFrame(const uint8_t *bssid, const char *essid, const uint8_t *frame, uint32_t length);

    /**
     * Stores one captured EAPOL-Key frame for the supplied BSSID.
     */
    void recordCaptureFrame(const uint8_t *bssid, const char *essid, const uint8_t *frame, uint32_t length, bool hasMic, bool hasAck);

private:
    struct HandshakeEntry
    {
        bool used = false;
        bool saved = false;
        uint8_t bssid[6] = {};
        uint32_t storedTick = 0;
        uint32_t capturedEpoch = 0;
        uint16_t generation = 0;
        uint16_t frameCount = 0;
        char fileName[18] = {};
        char essid[33] = {};
        bool hasMic = false;
        bool hasAck = false;
        bool hasManagementFrame = false;
        uint16_t managementFrameLength = 0;
        uint8_t managementFrame[AIRTOOLS_MANAGEMENT_CAPTURE_BYTES] = {};
        uint16_t hsFrameCount = 0;
        uint16_t hsFrameLength[AIRTOOLS_HANDSHAKE_MAX_FRAMES] = {};
        uint8_t hsFrames[AIRTOOLS_HANDSHAKE_MAX_FRAMES][AIRTOOLS_HANDSHAKE_CAPTURE_BYTES] = {};
    };

    HandshakeEntry *findOrCreateEntry(const uint8_t *bssid);
    bool persistCompleteHandshake(HandshakeEntry &entry);
    bool writeHandshakePcap(HandshakeEntry &entry);
    String pathFor(const char *fileName) const;
    String macFileName(const uint8_t *bssid) const;
    bool isKnownHandshakeFile(const String &fileName) const;
    bool isSafeHandshakeFileName(const String &fileName) const;
    void copyEssid(HandshakeEntry &entry, const char *essid);

    HandshakeEntry entries[AIRTOOLS_MAX_HANDSHAKES];
    bool mounted = false;
    const char *backend = "none";
    mutable SemaphoreHandle_t entriesMutex = nullptr;
};
