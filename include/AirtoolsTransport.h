#pragma once

#include <Arduino.h>

class AirtoolsCommandRouter;

/**
 * Provides a command transport that receives text commands and sends text responses.
 */
class AirtoolsTransport
{
public:
    virtual ~AirtoolsTransport() = default;

    /**
     * Starts the transport and binds received commands to the supplied router.
     */
    virtual void begin(AirtoolsCommandRouter *router) = 0;

    /**
     * Runs transport maintenance tasks from the firmware loop.
     */
    virtual void loop() = 0;

    /**
     * Sends one complete command response to the connected peer.
     */
    virtual void sendResponse(const String &response) = 0;

    /**
     * Starts a streamed response. The header is sent immediately without the
     * end-of-response marker.
     */
    virtual void beginStream(const String &header) = 0;

    /**
     * Appends one chunk to the currently streamed response.
     */
    virtual void streamChunk(const char *data, size_t length) = 0;

    /**
     * Finishes the streamed response and appends the end-of-response marker.
     */
    virtual void endStream() = 0;
};
