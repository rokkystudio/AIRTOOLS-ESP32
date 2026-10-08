#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "AirtoolsTransport.h"
#include "AirtoolsConfig.h"

class AirtoolsCommandRouter;

/**
 * Exposes the AIRTOOLS command channel as a BLE GATT service.
 */
class BleTransport : public AirtoolsTransport, private NimBLEServerCallbacks, private NimBLECharacteristicCallbacks
{
public:
    /**
     * Creates an idle BLE transport that starts advertising in begin().
     */
    BleTransport();

    /**
     * Starts BLE advertising and prepares RX/TX characteristics for command exchange.
     */
    void begin(AirtoolsCommandRouter *router) override;

    /**
     * Restarts advertising when the Android client disconnects.
     */
    void loop() override;

    /**
     * Notifies the connected client with a chunked UTF-8 response.
     */
    void sendResponse(const String &response) override;

    /**
     * Starts a streamed response without the end-of-response marker.
     */
    void beginStream(const String &header) override;

    /**
     * Appends one chunk to the currently streamed response.
     */
    void streamChunk(const char *data, size_t length) override;

    /**
     * Finishes the streamed response and appends the end-of-response marker.
     */
    void endStream() override;

    /**
     * Returns true while a BLE client is connected to the command service.
     */
    bool isConnected() const;

    /**
     * Returns true while the BLE command service is advertising.
     */
    bool isAdvertising() const;

private:
    /**
     * Updates transport state when a BLE client connects.
     */
#if AIRTOOLS_NIMBLE_V2
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
#else
    void onConnect(NimBLEServer *server) override;
#endif

    /**
     * Marks the transport for advertising restart after a BLE client disconnects.
     */
#if AIRTOOLS_NIMBLE_V2
    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
#else
    void onDisconnect(NimBLEServer *server) override;
#endif

    /**
     * Queues a command written to the BLE RX characteristic.
     */
#if AIRTOOLS_NIMBLE_V2
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;
#else
    void onWrite(NimBLECharacteristic *characteristic) override;
#endif

    void handlePendingCommands();
    void flushStreamBuffer();
    void notifyBytes(const uint8_t *data, size_t length);

    AirtoolsCommandRouter *router = nullptr;
    NimBLEServer *server = nullptr;
    NimBLECharacteristic *txCharacteristic = nullptr;
    bool connected = false;
    bool advertising = false;

    struct PendingCommand
    {
        char text[256];
    };

    static constexpr size_t NOTIFY_CHUNK_SIZE = 180;
    static constexpr size_t COMMAND_BUFFER_SIZE = sizeof(PendingCommand::text);
    static constexpr int COMMAND_QUEUE_DEPTH = 4;

    QueueHandle_t commandQueue = nullptr;
    uint8_t streamBuffer[NOTIFY_CHUNK_SIZE];
    size_t streamBufferLength = 0;
};
