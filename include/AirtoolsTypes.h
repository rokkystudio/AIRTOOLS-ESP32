#pragma once

#include <Arduino.h>
#include "AirtoolsConfig.h"

struct AirtoolsNetwork
{
    bool used = false;
    bool selected = false;
    uint8_t bssid[6] = {};
    char essid[33] = {};
    int channel = 0;
    int rssi = -127;
    uint32_t beacons = 0;
    uint32_t probes = 0;
    uint32_t data = 0;
    uint32_t lastSeenMs = 0;
    bool managementQueued = false;
};

struct AirtoolsClient
{
    bool used = false;
    uint8_t bssid[6] = {};
    uint8_t station[6] = {};
    int rssi = -127;
    uint32_t frames = 0;
    uint32_t lastSeenMs = 0;
};

struct AirtoolsTarget
{
    bool active = false;
    uint8_t bssid[6] = {};
    int channel = 1;
};

String macToString(const uint8_t *mac);
bool parseMac(const String &value, uint8_t *mac);
bool macEquals(const uint8_t *left, const uint8_t *right);
bool isZeroMac(const uint8_t *mac);
