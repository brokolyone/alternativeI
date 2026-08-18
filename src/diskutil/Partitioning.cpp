#include "Partitioning.h"

#include <cstdio>
#include <cstring>

namespace diskutil {

namespace {

uint32_t readLe32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readLe64(const uint8_t *p) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | p[i];
    }
    return value;
}

// GPT GUIDs are stored mixed-endian: the first three fields are
// little-endian, the last two are big-endian byte strings - this matches
// how every partitioning tool (parted, fdisk, Windows diskpart) prints
// them.
std::string formatGuid(const uint8_t *g) {
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", g[3], g[2],
                  g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14],
                  g[15]);
    return buf;
}

std::string decodeUtf16LeName(const uint8_t *bytes, size_t byteLength) {
    std::string result;
    for (size_t i = 0; i + 1 < byteLength; i += 2) {
        const uint16_t unit = static_cast<uint16_t>(bytes[i]) | (static_cast<uint16_t>(bytes[i + 1]) << 8);
        if (unit == 0) break;
        if (unit < 0x80) {
            result += static_cast<char>(unit);
        } else {
            // Non-ASCII partition names are rare in practice; represent
            // them as a codepoint escape rather than mis-decoding UTF-16
            // surrogate pairs we don't need for an inspection tool.
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04X", unit);
            result += buf;
        }
    }
    return result;
}

bool isAllZero(const uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

} // namespace

MbrInfo readMbr(BlockDevice &device) {
    MbrInfo info;

    uint8_t sector[kSectorSize];
    if (!device.readAt(0, sector, sizeof(sector))) {
        return info;
    }

    info.hasValidSignature = sector[510] == 0x55 && sector[511] == 0xAA;
    if (!info.hasValidSignature) {
        return info;
    }

    for (int i = 0; i < 4; ++i) {
        const uint8_t *entry = sector + 446 + i * 16;
        const uint8_t type = entry[4];
        if (type == 0) continue;

        MbrPartitionEntry partition;
        partition.active = entry[0] == 0x80;
        partition.type = type;
        partition.startLba = readLe32(entry + 8);
        partition.sectorCount = readLe32(entry + 12);
        info.partitions.push_back(partition);
    }

    info.looksLikeGptProtectiveMbr =
        info.partitions.size() == 1 && info.partitions[0].type == 0xEE && info.partitions[0].startLba == 1;

    return info;
}

std::optional<GptInfo> readGpt(BlockDevice &device) {
    uint8_t header[kSectorSize];
    if (!device.readAt(kSectorSize, header, sizeof(header))) {
        return std::nullopt;
    }

    if (std::memcmp(header, "EFI PART", 8) != 0) {
        return std::nullopt;
    }

    GptInfo info;
    info.headerValid = true;
    info.diskGuid = formatGuid(header + 56);
    info.partitionEntryLba = readLe64(header + 72);
    info.numberOfPartitionEntries = readLe32(header + 80);
    info.sizeOfPartitionEntry = readLe32(header + 84);

    if (info.sizeOfPartitionEntry == 0 || info.numberOfPartitionEntries == 0 ||
        info.numberOfPartitionEntries > 4096 || info.sizeOfPartitionEntry > kSectorSize) {
        // Sanity bounds against a corrupt header rather than trying to
        // read gigabytes of "partition entries".
        return info;
    }

    const uint64_t arrayBytes =
        static_cast<uint64_t>(info.numberOfPartitionEntries) * info.sizeOfPartitionEntry;
    std::vector<uint8_t> arrayBuffer(arrayBytes);
    if (!device.readAt(info.partitionEntryLba * kSectorSize, arrayBuffer.data(), arrayBuffer.size())) {
        return info;
    }

    for (uint32_t i = 0; i < info.numberOfPartitionEntries; ++i) {
        const uint8_t *entry = arrayBuffer.data() + static_cast<size_t>(i) * info.sizeOfPartitionEntry;
        if (isAllZero(entry, 16)) continue; // empty slot (all-zero type GUID)

        GptPartitionEntry partition;
        partition.typeGuid = formatGuid(entry);
        partition.uniqueGuid = formatGuid(entry + 16);
        partition.startLba = readLe64(entry + 32);
        partition.endLba = readLe64(entry + 40);
        partition.name = decodeUtf16LeName(entry + 56, 72);
        info.partitions.push_back(std::move(partition));
    }

    return info;
}

uint64_t gptPrimaryRegionBytes(const GptInfo &gpt) {
    const uint64_t arrayEndLba =
        gpt.partitionEntryLba +
        (static_cast<uint64_t>(gpt.numberOfPartitionEntries) * gpt.sizeOfPartitionEntry + kSectorSize - 1) /
            kSectorSize;
    return arrayEndLba * kSectorSize;
}

} // namespace diskutil
