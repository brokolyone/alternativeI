#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "BlockDevice.h"

namespace diskutil {

constexpr uint64_t kSectorSize = 512;

struct MbrPartitionEntry {
    bool active = false;
    uint8_t type = 0;
    uint32_t startLba = 0;
    uint32_t sectorCount = 0;
};

struct MbrInfo {
    bool hasValidSignature = false; // 0x55AA at offset 510
    std::vector<MbrPartitionEntry> partitions; // up to 4, non-empty (type != 0) entries only
    bool looksLikeGptProtectiveMbr = false; // a single 0xEE entry spanning (most of) the disk
};

struct GptPartitionEntry {
    std::string typeGuid;   // formatted "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
    std::string uniqueGuid;
    uint64_t startLba = 0;
    uint64_t endLba = 0;
    std::string name; // UTF-8, decoded from the on-disk UTF-16LE name field
};

struct GptInfo {
    bool headerValid = false; // signature "EFI PART" matched (CRC not re-verified)
    std::string diskGuid;
    uint64_t partitionEntryLba = 0;
    uint32_t numberOfPartitionEntries = 0;
    uint32_t sizeOfPartitionEntry = 0;
    std::vector<GptPartitionEntry> partitions; // non-empty (all-zero GUID) entries only
};

MbrInfo readMbr(BlockDevice &device);
// Reads the primary GPT header at LBA 1 and its partition entry array.
// Returns std::nullopt if the signature doesn't match (not a GPT disk, or
// the header is corrupt).
std::optional<GptInfo> readGpt(BlockDevice &device);

// Total byte length of "the GPT region" at the start of the disk: LBA0
// (protective MBR) through the end of the primary partition entry array.
// This is what backup --region gpt captures.
uint64_t gptPrimaryRegionBytes(const GptInfo &gpt);

} // namespace diskutil
