#include "AirtoolsTypes.h"

String macToString(const uint8_t *mac)
{
    char text[18];
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(text);
}

static int hexNibbleValue(char value)
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

bool parseMac(const String &value, uint8_t *mac)
{
    // Strict "xx:xx:xx:xx:xx:xx" parsing: fixed length and explicit separator
    // checks avoid the undefined behavior and lax matching of a %x scanf.
    if (!mac || value.length() != 17) {
        return false;
    }

    for (int i = 0; i < 6; ++i) {
        int offset = i * 3;
        if (i < 5 && value[offset + 2] != ':') {
            return false;
        }

        int high = hexNibbleValue(value[offset]);
        int low = hexNibbleValue(value[offset + 1]);
        if (high < 0 || low < 0) {
            return false;
        }

        mac[i] = static_cast<uint8_t>((high << 4) | low);
    }

    return true;
}

bool macEquals(const uint8_t *left, const uint8_t *right)
{
    for (int i = 0; i < 6; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

bool isZeroMac(const uint8_t *mac)
{
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0) {
            return false;
        }
    }

    return true;
}
