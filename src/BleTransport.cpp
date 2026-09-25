#include "BleTransport.h"
#include "AirtoolsCommandRouter.h"

BleTransport::BleTransport()
{
}

void BleTransport::begin(AirtoolsCommandRouter *router)
{
    this->router = router;
    commandQueue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(PendingCommand));
    if (!commandQueue) {
        Serial.println("BLE_COMMAND_QUEUE_FAILED");
    }

    NimBLEDevice::init(AIRTOOLS_BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(AIRTOOLS_BLE_MTU);

    server = NimBLEDevice::createServer();
    server->setCallbacks(this);

    NimBLEService *service = server->createService(AIRTOOLS_SERVICE_UUID);

    txCharacteristic = service->createCharacteristic(
        AIRTOOLS_TX_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    NimBLECharacteristic *rxCharacteristic = service->createCharacteristic(
        AIRTOOLS_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rxCharacteristic->setCallbacks(this);

    service->start();

    NimBLEAdvertising *advertisingHandle = NimBLEDevice::getAdvertising();
    advertisingHandle->addServiceUUID(AIRTOOLS_SERVICE_UUID);
    advertisingHandle->setScanResponse(true);
    advertisingHandle->start();
    advertising = true;

    Serial.println("BLE_READY name=" AIRTOOLS_BLE_DEVICE_NAME);
}

void BleTransport::loop()
{
    if (!connected && !advertising) {
        NimBLEDevice::getAdvertising()->start();
        advertising = true;
    }

    handlePendingCommands();
}

void BleTransport::sendResponse(const String &response)
{
    beginStream("");
    streamChunk(response.c_str(), response.length());
    endStream();
}

void BleTransport::beginStream(const String &header)
{
    streamBufferLength = 0;
    streamChunk(header.c_str(), header.length());
}

void BleTransport::streamChunk(const char *data, size_t length)
{
    if (!data || length == 0) {
        return;
    }

    size_t offset = 0;
    while (offset < length) {
        size_t space = NOTIFY_CHUNK_SIZE - streamBufferLength;
        size_t take = length - offset;
        if (take > space) {
            take = space;
        }

        memcpy(streamBuffer + streamBufferLength, data + offset, take);
        streamBufferLength += take;
        offset += take;

        if (streamBufferLength == NOTIFY_CHUNK_SIZE) {
            flushStreamBuffer();
        }
    }
}

void BleTransport::endStream()
{
    flushStreamBuffer();

    const char *endMarker = "\n--airtools-eof--\n";
    notifyBytes(reinterpret_cast<const uint8_t *>(endMarker), strlen(endMarker));
}

void BleTransport::flushStreamBuffer()
{
    if (streamBufferLength == 0) {
        return;
    }

    notifyBytes(streamBuffer, streamBufferLength);
    streamBufferLength = 0;
}

void BleTransport::notifyBytes(const uint8_t *data, size_t length)
{
    if (!txCharacteristic || !data || length == 0) {
        return;
    }

    txCharacteristic->setValue(data, length);
    txCharacteristic->notify();
    delay(8);
}

void BleTransport::onConnect(NimBLEServer *server)
{
    (void)server;
    connected = true;
    advertising = false;
    Serial.println("BLE_CONNECTED");
}

void BleTransport::onDisconnect(NimBLEServer *server)
{
    (void)server;
    connected = false;
    advertising = false;
    Serial.println("BLE_DISCONNECTED");
}

void BleTransport::onWrite(NimBLECharacteristic *characteristic)
{
    if (!commandQueue) {
        return;
    }

    std::string value = characteristic->getValue();
    String command(value.c_str());
    command.trim();

    if (command.length() == 0) {
        return;
    }

    if (command.length() >= COMMAND_BUFFER_SIZE) {
        Serial.println("CMD_TOO_LONG");
        return;
    }

    PendingCommand item;
    strncpy(item.text, command.c_str(), COMMAND_BUFFER_SIZE - 1);
    item.text[COMMAND_BUFFER_SIZE - 1] = 0;

    if (xQueueSend(commandQueue, &item, 0) != pdTRUE) {
        Serial.println("CMD_QUEUE_FULL");
    }
}

void BleTransport::handlePendingCommands()
{
    if (!router || !commandQueue) {
        return;
    }

    PendingCommand item;
    while (xQueueReceive(commandQueue, &item, 0) == pdTRUE) {
        String command(item.text);
        Serial.print("CMD ");
        Serial.println(command);

        String response;
        if (router->handleCommand(command, response)) {
            sendResponse(response);
        }
    }
}
