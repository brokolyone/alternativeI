#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "BlockDevice.h"

namespace diskutil {

enum class RegionKind { Mbr, Gpt, Disk, Custom };

struct RegionSpec {
    RegionKind kind = RegionKind::Mbr;
    uint64_t startLba = 0;
    uint64_t sectors = 0; // only used for Custom
};

struct ResolvedRegion {
    bool ok = false;
    std::string error;
    uint64_t offset = 0;
    uint64_t length = 0;
};

// Resolves a RegionSpec into a concrete [offset, length) byte range against
// an already-open device. For Gpt this requires actually reading the GPT
// header, so it can fail if the device isn't GPT-partitioned.
ResolvedRegion resolveRegion(BlockDevice &device, const RegionSpec &spec);

// Called periodically during a backup/restore/verify pass with
// (bytesDoneInThisPass, totalBytesInThisPass). May be called from whatever
// thread performs the copy - callers marshalling to a UI thread must do
// their own dispatch.
using ProgressFn = std::function<void(uint64_t, uint64_t)>;

struct BackupOutcome {
    bool ok = false;
    std::string error;
    uint64_t bytesCopied = 0;
    std::string sha256;
};

// Reads [offset, offset+length) from `source` into `outputPath`, writes a
// `<outputPath>.sha256` sidecar, and returns the digest. Read-only on the
// source; always safe to call.
BackupOutcome backupRegion(BlockDevice &source, uint64_t offset, uint64_t length,
                            const std::string &outputPath, const ProgressFn &progress = {});

// Hashes an existing file (used to compute the source image's checksum
// before restoring it, and reusable for a standalone "verify backup"
// action).
struct FileHashResult {
    bool ok = false;
    std::string error;
    uint64_t sizeBytes = 0;
    std::string sha256;
};
FileHashResult hashFile(const std::string &path, const ProgressFn &progress = {});

struct RestorePlan {
    bool ok = false;
    std::string error;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t inputSizeBytes = 0;
    std::string inputSha256;
    bool targetIsSpecialDevice = false;
};

// Read-only: computes what a restore *would* do (and validates it isn't
// obviously unsafe - oversized image, region past the end of the device)
// without writing anything. Every restore path, CLI or GUI, must go
// through this before performRestore().
RestorePlan planRestore(const std::string &inputPath, BlockDevice &target, const RegionSpec &regionSpec);

struct RestoreOutcome {
    bool ok = false;
    std::string error;
    bool verified = false;
    std::string expectedSha256;
    std::string actualSha256;
};

// Writes the plan's byte range from `inputPath` into `target`, then reads
// it back and re-hashes independently, failing hard (ok=false) on any
// mismatch. This is the only function in the library that writes to a
// device - callers are responsible for having gotten explicit user
// confirmation before calling it.
RestoreOutcome performRestore(const std::string &inputPath, BlockDevice &target, const RestorePlan &plan,
                               const ProgressFn &progress = {});

} // namespace diskutil
