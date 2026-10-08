#include <Arduino.h>
#include "BoardSupport.h"
#include "Storage.h"
#include "WifiAir.h"
#include "BleTransport.h"
#include "AirtoolsCommandRouter.h"
#include "DisplayStatus.h"

BoardSupport board;
Storage storage;
WifiAir wifiAir;
BleTransport transport;
AirtoolsCommandRouter router(wifiAir, storage, board);
DisplayStatus displayStatus;

/**
 * Starts board peripherals, storage, Wi-Fi capture and BLE command transport.
 */
void setup()
{
    board.begin();
    displayStatus.begin();
    storage.begin();
    wifiAir.begin(&storage);
    transport.begin(&router);
    router.setTransport(&transport);
}

/**
 * Runs cooperative maintenance for board peripherals, Wi-Fi channel hopping, BLE transport and display status.
 */
void loop()
{
    board.loop();
    wifiAir.loop();
    transport.loop();

    char targetLabel[25];
    wifiAir.getDisplayTargetLabel(targetLabel, sizeof(targetLabel));

    displayStatus.loop(
        transport.isConnected(),
        transport.isAdvertising(),
        wifiAir.isScanning(),
        wifiAir.getCurrentChannel(),
        targetLabel,
        wifiAir.isReplayActive(),
        board.readBatteryMillivolts()
    );
    delay(10);
}
