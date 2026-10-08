#include "Storage.h"
#include "AirtoolsTypes.h"

#if AIRTOOLS_HAS_SD
#include <SD.h>
#include <SPI.h>
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

void writeHexBytes(HexChunkWriter writer, void *context, const uint8_t *data, size_t length)
{
    if (!writer || !data || length == 0) {
        return;
    }

    static const char hex[] = "0123456789abcdef";
    char chunk[256];
    size_t offset = 0;

    while (offset < length) {
        size_t count = length - offset;
        if (count > sizeof(chunk) / 2) {
            count = sizeof(chunk) / 2;
        }

        size_t out = 0;
        for (size_t index = 0; index < count; ++index) {
            uint8_t value = data[offset + index];
            chunk[out++] = hex[value >> 4];
            chunk[out++] = hex[value & 0x0f];
        }

        writer(chunk, out, context);
        offset += count;
    }
}
} // namespace

/**
 * Initializes the RAM-backed capture index and attempts optional persistent storage.
 */
void Storage::begin()
{
    entriesMutex = xSemaphoreCreateMutex();
    if (!entriesMutex) {
        Serial.println("STORAGE_ENTRIES_MUTEX_FAILED");
    }

#if AIRTOOLS_HAS_SD
    if (AIRTOOLS_SD_SCK_PIN >= 0 &&
        AIRTOOLS_SD_MOSI_PIN >= 0 &&
        AIRTOOLS_SD_MISO_PIN >= 0 &&
        AIRTOOLS_SD_CS_PIN >= 0) {
        SPI.begin(AIRTOOLS_SD_SCK_PIN, AIRTOOLS_SD_MISO_PIN, AIRTOOLS_SD_MOSI_PIN, AIRTOOLS_SD_CS_PIN);
        mounted = SD.begin(AIRTOOLS_SD_CS_PIN, SPI);
    }
    else {
        mounted = SD.begin();
    }
    backend = mounted ? "ram+sd" : "ram";
#else
    mounted = LittleFS.begin(true);
    backend = mounted ? "ram+littlefs" : "ram";
#endif
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

String Storage::formatCaptureStatus() const
{
    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        return "capture_lock=0";
    }

    uint32_t eapolFrames = 0;
    uint32_t entriesUsed = 0;
    uint32_t complete = 0;
    uint32_t withMic = 0;
    uint32_t withAck = 0;
    uint32_t withManagement = 0;

    for (const auto &entry : entries) {
        if (!entry.used) {
            continue;
        }

        ++entriesUsed;
        eapolFrames += entry.hsFrameCount;
        if (entry.saved) {
            ++complete;
        }
        if (entry.hasMic) {
            ++withMic;
        }
        if (entry.hasAck) {
            ++withAck;
        }
        if (entry.hasManagementFrame) {
            ++withManagement;
        }
    }

    String response;
    response += "capture_entries=";
    response += entriesUsed;
    response += " eapol=";
    response += eapolFrames;
    response += " mic=";
    response += withMic;
    response += " ack=";
    response += withAck;
    response += " mgmt=";
    response += withManagement;
    response += " complete=";
    response += complete;
    return response;
}

/**
 * Streams a complete PCAP directly from the RAM-backed handshake entry.
 */
void Storage::streamHandshakeDownload(const String &fileName, HexChunkWriter writer, void *context) const
{
    if (!writer) {
        return;
    }

    EntriesLockGuard lock(entriesMutex);
    if (!lock.acquired()) {
        writeLiteral(writer, context, "ERR storage_lock_unavailable\n");
        return;
    }

    if (!isSafeHandshakeFileName(fileName)) {
        writeLiteral(writer, context, "ERR invalid_handshake_file\n");
        return;
    }

    const HandshakeEntry *entry = nullptr;
    for (const auto &candidate : entries) {
        if (candidate.used && candidate.saved && fileName == candidate.fileName) {
            entry = &candidate;
            break;
        }
    }

    if (!entry) {
        writeLiteral(writer, context, "ERR handshake_file_missing\n");
        return;
    }

    size_t size = sizeof(PcapGlobalHeader);
    size += sizeof(PcapPacketHeader) + entry->managementFrameLength;
    for (uint16_t index = 0; index < entry->hsFrameCount; ++index) {
        size += sizeof(PcapPacketHeader) + entry->hsFrameLength[index];
    }

    if (size == 0 || size > AIRTOOLS_MAX_HEX_DOWNLOAD_BYTES) {
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

    PcapGlobalHeader globalHeader;
    globalHeader.magic = PCAP_MAGIC;
    globalHeader.major = PCAP_VERSION_MAJOR;
    globalHeader.minor = PCAP_VERSION_MINOR;
    globalHeader.zone = 0;
    globalHeader.sigfigs = 0;
    globalHeader.snaplen = 2048;
    globalHeader.network = PCAP_LINKTYPE_IEEE802_11;
    writeHexBytes(writer, context, reinterpret_cast<const uint8_t *>(&globalHeader), sizeof(globalHeader));

    uint32_t now = entry->storedTick;

    PcapPacketHeader managementHeader;
    managementHeader.seconds = now / 1000;
    managementHeader.microseconds = (now % 1000) * 1000;
    managementHeader.capturedLength = entry->managementFrameLength;
    managementHeader.originalLength = entry->managementFrameLength;
    writeHexBytes(writer, context, reinterpret_cast<const uint8_t *>(&managementHeader), sizeof(managementHeader));
    writeHexBytes(writer, context, entry->managementFrame, entry->managementFrameLength);

    for (uint16_t index = 0; index < entry->hsFrameCount; ++index) {
        PcapPacketHeader packetHeader;
        packetHeader.seconds = now / 1000;
        packetHeader.microseconds = (now % 1000) * 1000;
        packetHeader.capturedLength = entry->hsFrameLength[index];
        packetHeader.originalLength = entry->hsFrameLength[index];
        writeHexBytes(writer, context, reinterpret_cast<const uint8_t *>(&packetHeader), sizeof(packetHeader));
        writeHexBytes(writer, context, entry->hsFrames[index], entry->hsFrameLength[index]);
    }

    writer("\n", 1, context);
}

void Storage::recordManagementFrame(const uint8_t *bssid, const char *essid, const uint8_t *frame, uint32_t length)
{
    if (!bssid || !essid || !essid[0] || !frame || length == 0) {
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

void Storage::recordCaptureFrame(
    const uint8_t *bssid,
    const char *essid,
    const uint8_t *frame,
    uint32_t length,
    bool hasMic,
    bool hasAck
)
{
    if (!bssid || !frame || length == 0) {
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
        for (uint16_t index = 1; index < AIRTOOLS_HANDSHAKE_MAX_FRAMES; ++index) {
            memcpy(entry->hsFrames[index - 1], entry->hsFrames[index], AIRTOOLS_HANDSHAKE_CAPTURE_BYTES);
            entry->hsFrameLength[index - 1] = entry->hsFrameLength[index];
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

    *freeSlot = HandshakeEntry{};
    freeSlot->used = true;
    memcpy(freeSlot->bssid, bssid, 6);
    freeSlot->storedTick = millis();

    String fileName = macFileName(bssid);
    strncpy(freeSlot->fileName, fileName.c_str(), sizeof(freeSlot->fileName) - 1);
    freeSlot->fileName[sizeof(freeSlot->fileName) - 1] = 0;
    return freeSlot;
}

/**
 * Marks a complete handshake available in RAM and mirrors it to persistent storage when mounted.
 */
bool Storage::persistCompleteHandshake(HandshakeEntry &entry)
{
    if (entry.hsFrameCount < 2 || !entry.hasMic || !entry.hasAck ||
        !entry.hasManagementFrame || !entry.essid[0]) {
        return false;
    }

    bool firstCompletion = !entry.saved;
    entry.saved = true;
    entry.frameCount = entry.hsFrameCount + 1;
    entry.storedTick = millis();
    if (firstCompletion) {
        entry.generation = 1;
    }

    if (mounted) {
        writeHandshakePcap(entry);
    }

    return true;
}

/**
 * Mirrors a complete RAM entry to the optional persistent backend.
 */
bool Storage::writeHandshakePcap(const HandshakeEntry &entry)
{
    if (!mounted) {
        return false;
    }

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

    uint32_t now = entry.storedTick;

    PcapPacketHeader managementHeader;
    managementHeader.seconds = now / 1000;
    managementHeader.microseconds = (now % 1000) * 1000;
    managementHeader.capturedLength = entry.managementFrameLength;
    managementHeader.originalLength = entry.managementFrameLength;

    bool managementOk =
        file.write(reinterpret_cast<const uint8_t *>(&managementHeader), sizeof(managementHeader)) == sizeof(managementHeader) &&
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

        bool ok =
            file.write(reinterpret_cast<const uint8_t *>(&packetHeader), sizeof(packetHeader)) == sizeof(packetHeader) &&
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
    snprintf(
        text,
        sizeof(text),
        "%02x%02x%02x%02x%02x%02x.pcap",
        bssid[0],
        bssid[1],
        bssid[2],
        bssid[3],
        bssid[4],
        bssid[5]
    );
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

    for (int index = 0; index < 12; ++index) {
        char value = fileName[index];
        bool hex =
            (value >= '0' && value <= '9') ||
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
