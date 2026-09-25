#include <Arduino.h>
#include "BoardSupport.h"
#include "Storage.h"
#include "WifiAir.h"
#include "BleTransport.h"
#include "AirtoolsCommandRouter.h"

BoardSupport board;
Storage storage;
WifiAir wifiAir;
BleTransport transport;
AirtoolsCommandRouter router(wifiAir, storage, board);

/**
 * Starts board peripherals, storage, Wi-Fi capture and BLE command transport.
 */
void setup()
{
    board.begin();
    storage.begin();
    wifiAir.begin(&storage);
    transport.begin(&router);
    router.setTransport(&transport);
}

/**
 * Runs cooperative maintenance for board peripherals, Wi-Fi channel hopping and BLE advertising.
 */
void loop()
{
    board.loop();
    wifiAir.loop();
    transport.loop();
    delay(10);
}
