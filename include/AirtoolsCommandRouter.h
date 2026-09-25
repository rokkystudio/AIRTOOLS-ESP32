#pragma once

#include <Arduino.h>
#include "WifiAir.h"
#include "Storage.h"
#include "BoardSupport.h"

class AirtoolsTransport;

/**
 * Routes AIRTOOLS text commands to ESP32 firmware subsystems.
 */
class AirtoolsCommandRouter
{
public:
    /**
     * Binds the router to Wi-Fi, storage and board support subsystems.
     */
    AirtoolsCommandRouter(WifiAir &wifiAir, Storage &storage, BoardSupport &board);

    /**
     * Binds the response transport used to stream large downloads.
     */
    void setTransport(AirtoolsTransport *transport);

    /**
     * Executes one AIRTOOLS command path. Returns true when a complete text
     * response was written into response and should be sent. Returns false when
     * the command was streamed directly through the transport instead.
     */
    bool handleCommand(const String &command, String &response);

private:
    String handleStatus() const;
    String handleStart();
    String handleSelect(const String &command);
    String handleAireplay(const String &command) const;
    String handleReplay(const String &command);
    bool handleHandshakeDownload(const String &command, String &response);
    String valueOf(const String &command, const String &key) const;
    String urlDecode(const String &value) const;
    int hexNibble(char value) const;
    static void streamChunkThunk(const char *data, size_t length, void *context);

    WifiAir &wifiAir;
    Storage &storage;
    BoardSupport &board;
    AirtoolsTransport *transport = nullptr;
};
