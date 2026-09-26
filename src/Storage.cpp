#include "Storage.h"
#include "AirtoolsTypes.h"

#if AIRTOOLS_HAS_SD
#include <SD.h>
#else
#include <LittleFS.h>
#endif

#define PCAP_MAGIC 0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define PCAP_LINKTYPE_IEEE802_11 105

struct PcapGlobalHeader
{
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    int32_t zone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct PcapPacketHeader
{
    uint32_t seconds;
    uint32_t microseconds;
    uint32_t capturedLength;
    uint32_t originalLength;
};

namespace
{
/**
 * RAII guard for the handshake index mutex.
 */
class EntriesLockGuard
{
public:
    explicit EntriesLockGuard(SemaphoreHandle_t mutex) : mutex(mutex), held(false)
    {
        if (mutex) {
            held = xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
        }
    }

    ~EntriesLockGuard()
    {
        if (held && mutex) {
            xSemaphoreGive(mutex);
        }
    }

    bool acquired() const
    {
        return held;
    }

private:
    SemaphoreHandle_t mutex;
    bool held;
};

void writeLiteral(HexChunkWriter writer, void *context, const char *literal)
{
    if (writer) {
        writer(literal, strlen(literal), context);
    }
}
} // namespace

void Storage::begin()
{
#if AIRTOOLS_HAS_SD
    mounted = SD.begin();
    backend = mounted ? "sd" : "sd-failed";
#else
    mounted = LittleFS.begin(true);
    backend = mounted ? "littlefs" : "littlefs-failed";
#endif

    entriesMutex = xSemaphoreCreateMutex();
    if (!entriesMutex) {
        Serial.println("STORAGE_ENTRIES_MUTEX_FAILED");
    }
}

String Storage::formatStatus() const
{
    String response;
    response += "storage=";
    response += backend;
    response += " mounted=";
    response += mounted ? "1" : "0";
    return response;
}

String Storage::formatHandshakes() const
{
    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        return "ERR storage_lock_unavailable\n";
    }

    String response = "OK handshakes\n";
    response += "# bssid,stored_tick,captured_epoch,generation,frames,file,essid\n";

    for (const auto &entry : entries) {
        if (!entry.used || !entry.saved) {
            continue;
        }

        response += macToString(entry.bssid);
        response += ",";
        response += entry.storedTick;
        response += ",";
        response += entry.capturedEpoch;
        response += ",";
        response += entry.generation;
        response += ",";
        response += entry.frameCount;
        response += ",";
        response += entry.fileName;
        response += ",";
        response += entry.essid[0] ? entry.essid : "<hidden/unknown>";
        response += "\n";
    }

    return response;
}

void Storage::streamHandshakeDownload(const String &fileName, HexChunkWriter writer, void *context) const
{
    if (!writer) {
        return;
    }
    if (!mounted) {
        writeLiteral(writer, context, "ERR storage_not_mounted\n");
        return;
    }

    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        writeLiteral(writer, context, "ERR storage_lock_unavailable\n");
        return;
    }

    if (!isSafeHandshakeFileName(fileName) || !isKnownHandshakeFile(fileName)) {
        writeLiteral(writer, context, "ERR invalid_handshake_file\n");
        return;
    }

    String path = pathFor(fileName.c_str());
#if AIRTOOLS_HAS_SD
    File file = SD.open(path, FILE_READ);
#else
    File file = LittleFS.open(path, "r");
#endif
    if (!file) {
        writeLiteral(writer, context, "ERR handshake_file_missing\n");
        return;
    }

    size_t size = file.size();
    if (size == 0 || size > AIRTOOLS_MAX_HEX_DOWNLOAD_BYTES) {
        file.close();
        writeLiteral(writer, context, "ERR handshake_size_unsupported\n");
        return;
    }

    String header;
    header.reserve(64);
    header += "OK handshake_hex file=";
    header += fileName;
    header += " bytes=";
    header += size;
    header += "\n";
    writer(header.c_str(), header.length(), context);

    const char hex[] = "0123456789abcdef";
    uint8_t buffer[128];
    char chunk[256];
    while (file.available()) {
        size_t count = file.read(buffer, sizeof(buffer));
        size_t pos = 0;
        for (size_t i = 0; i < count; ++i) {
            chunk[pos++] = hex[buffer[i] >> 4];
            chunk[pos++] = hex[buffer[i] & 0x0f];
        }
        writer(chunk, pos, context);
    }
    writer("\n", 1, context);
    file.close();
}

void Storage::recordManagementFrame(const uint8_t *bssid, const char *essid, const uint8_t *frame, uint32_t length)
{
    if (!mounted || !bssid || !essid || !essid[0] || !frame || length == 0) {
        return;
    }

    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        return;
    }

    HandshakeEntry *entry = findOrCreateEntry(bssid);
    if (!entry) {
        return;
    }

    copyEssid(*entry, essid);

    uint16_t captureLength = length > AIRTOOLS_MANAGEMENT_CAPTURE_BYTES
        ? AIRTOOLS_MANAGEMENT_CAPTURE_BYTES
        : static_cast<uint16_t>(length);
    memcpy(entry->managementFrame, frame, captureLength);
    entry->managementFrameLength = captureLength;
    entry->hasManagementFrame = true;

    if (!entry->saved) {
        persistCompleteHandshake(*entry);
    }
}

void Storage::recordCaptureFrame(const uint8_t *bssid, const char *essid, const uint8_t *frame, uint32_t length, bool hasMic, bool hasAck)
{
    if (!mounted || !bssid || !frame || length == 0) {
        return;
    }

    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        return;
    }

    HandshakeEntry *entry = findOrCreateEntry(bssid);
    if (!entry) {
        return;
    }

    copyEssid(*entry, essid);

    uint16_t captureLength = length > AIRTOOLS_HANDSHAKE_CAPTURE_BYTES
        ? AIRTOOLS_HANDSHAKE_CAPTURE_BYTES
        : static_cast<uint16_t>(length);

    uint16_t slot;
    if (entry->hsFrameCount < AIRTOOLS_HANDSHAKE_MAX_FRAMES) {
        slot = entry->hsFrameCount++;
    }
    else {
        for (uint16_t i = 1; i < AIRTOOLS_HANDSHAKE_MAX_FRAMES; ++i) {
            memcpy(entry->hsFrames[i - 1], entry->hsFrames[i], AIRTOOLS_HANDSHAKE_CAPTURE_BYTES);
            entry->hsFrameLength[i - 1] = entry->hsFrameLength[i];
        }
        slot = AIRTOOLS_HANDSHAKE_MAX_FRAMES - 1;
    }
    memcpy(entry->hsFrames[slot], frame, captureLength);
    entry->hsFrameLength[slot] = captureLength;

    if (hasMic) {
        entry->hasMic = true;
    }
    if (hasAck) {
        entry->hasAck = true;
    }

    persistCompleteHandshake(*entry);
}

Storage::HandshakeEntry *Storage::findOrCreateEntry(const uint8_t *bssid)
{
    HandshakeEntry *freeSlot = nullptr;

    for (auto &entry : entries) {
        if (entry.used && macEquals(entry.bssid, bssid)) {
            return &entry;
        }

        if (!entry.used && !freeSlot) {
            freeSlot = &entry;
        }
    }

    if (!freeSlot) {
        return nullptr;
    }

    freeSlot->used = true;
    freeSlot->saved = false;
    memcpy(freeSlot->bssid, bssid, 6);
    freeSlot->storedTick = millis();
    freeSlot->generation = 0;
    freeSlot->frameCount = 0;
    freeSlot->hasMic = false;
    freeSlot->hasAck = false;
    freeSlot->hasManagementFrame = false;
    freeSlot->managementFrameLength = 0;
    freeSlot->hsFrameCount = 0;
    String fileName = macFileName(bssid);
    strncpy(freeSlot->fileName, fileName.c_str(), sizeof(freeSlot->fileName) - 1);
    freeSlot->fileName[sizeof(freeSlot->fileName) - 1] = 0;
    freeSlot->essid[0] = 0;
    return freeSlot;
}

bool Storage::persistCompleteHandshake(HandshakeEntry &entry)
{
    if (entry.hsFrameCount < 2 || !entry.hasMic || !entry.hasAck ||
        !entry.hasManagementFrame || !entry.essid[0]) {
        return false;
    }
    if (!writeHandshakePcap(entry)) {
        return false;
    }

    entry.saved = true;
    entry.frameCount = entry.hsFrameCount + 1;
    entry.storedTick = millis();
    entry.generation = entry.generation == 0 ? 1 : entry.generation + 1;
    return true;
}

bool Storage::writeHandshakePcap(HandshakeEntry &entry)
{
    String path = pathFor(entry.fileName);
#if AIRTOOLS_HAS_SD
    if (SD.exists(path.c_str())) {
        SD.remove(path.c_str());
    }
    File file = SD.open(path.c_str(), FILE_WRITE);
#else
    File file = LittleFS.open(path.c_str(), "w");
#endif
    if (!file) {
        return false;
    }

    PcapGlobalHeader globalHeader;
    globalHeader.magic = PCAP_MAGIC;
    globalHeader.major = PCAP_VERSION_MAJOR;
    globalHeader.minor = PCAP_VERSION_MINOR;
    globalHeader.zone = 0;
    globalHeader.sigfigs = 0;
    globalHeader.snaplen = 2048;
    globalHeader.network = PCAP_LINKTYPE_IEEE802_11;
    if (file.write(reinterpret_cast<const uint8_t *>(&globalHeader), sizeof(globalHeader)) != sizeof(globalHeader)) {
        file.close();
        return false;
    }

    uint32_t now = millis();

    PcapPacketHeader managementHeader;
    managementHeader.seconds = now / 1000;
    managementHeader.microseconds = (now % 1000) * 1000;
    managementHeader.capturedLength = entry.managementFrameLength;
    managementHeader.originalLength = entry.managementFrameLength;
    bool managementOk = file.write(reinterpret_cast<const uint8_t *>(&managementHeader), sizeof(managementHeader)) == sizeof(managementHeader) &&
        file.write(entry.managementFrame, entry.managementFrameLength) == entry.managementFrameLength;
    if (!managementOk) {
        file.close();
        return false;
    }

    for (uint16_t index = 0; index < entry.hsFrameCount; ++index) {
        PcapPacketHeader packetHeader;
        packetHeader.seconds = now / 1000;
        packetHeader.microseconds = (now % 1000) * 1000;
        packetHeader.capturedLength = entry.hsFrameLength[index];
        packetHeader.originalLength = entry.hsFrameLength[index];

        bool ok = file.write(reinterpret_cast<const uint8_t *>(&packetHeader), sizeof(packetHeader)) == sizeof(packetHeader) &&
            file.write(entry.hsFrames[index], entry.hsFrameLength[index]) == entry.hsFrameLength[index];
        if (!ok) {
            file.close();
            return false;
        }
    }
    file.close();
    return true;
}

String Storage::pathFor(const char *fileName) const
{
    String path = "/";
    path += fileName;
    return path;
}

String Storage::macFileName(const uint8_t *bssid) const
{
    char text[18];
    snprintf(text, sizeof(text), "%02x%02x%02x%02x%02x%02x.pcap",
        bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(text);
}

bool Storage::isKnownHandshakeFile(const String &fileName) const
{
    for (const auto &entry : entries) {
        if (entry.used && entry.saved && fileName == entry.fileName) {
            return true;
        }
    }

    return false;
}

bool Storage::isSafeHandshakeFileName(const String &fileName) const
{
    if (fileName.length() != 17 || !fileName.endsWith(".pcap")) {
        return false;
    }

    for (int i = 0; i < 12; ++i) {
        char value = fileName[i];
        bool hex = (value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F');
        if (!hex) {
            return false;
        }
    }

    return true;
}

void Storage::copyEssid(HandshakeEntry &entry, const char *essid)
{
    if (!essid || !essid[0]) {
        return;
    }

    strncpy(entry.essid, essid, sizeof(entry.essid) - 1);
    entry.essid[sizeof(entry.essid) - 1] = 0;
}
