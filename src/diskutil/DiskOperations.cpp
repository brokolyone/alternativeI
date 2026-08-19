#include "DiskOperations.h"

#include "Partitioning.h"
#include "Sha256.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace diskutil {

namespace {
constexpr size_t kChunkSize = 4 * 1024 * 1024;
}

ResolvedRegion resolveRegion(BlockDevice &device, const RegionSpec &spec) {
    ResolvedRegion result;

    switch (spec.kind) {
        case RegionKind::Mbr:
            result.offset = 0;
            result.length = kSectorSize;
            result.ok = true;
            return result;

        case RegionKind::Disk:
            result.offset = 0;
            result.length = device.sizeBytes();
            if (result.length == 0) {
                result.error = "could not determine device size";
                return result;
            }
            result.ok = true;
            return result;

        case RegionKind::Gpt: {
            auto gpt = readGpt(device);
            if (!gpt || !gpt->headerValid) {
                result.error = "no valid GPT header found (device isn't GPT-partitioned?)";
                return result;
            }
            result.offset = 0;
            result.length = gptPrimaryRegionBytes(*gpt);
            result.ok = true;
            return result;
        }

        case RegionKind::Custom:
            if (spec.sectors == 0) {
                result.error = "custom region requires a non-zero sector count";
                return result;
            }
            result.offset = spec.startLba * kSectorSize;
            result.length = spec.sectors * kSectorSize;
            result.ok = true;
            return result;
    }

    result.error = "unknown region kind";
    return result;
}

BackupOutcome backupRegion(BlockDevice &source, uint64_t offset, uint64_t length,
                            const std::string &outputPath, const ProgressFn &progress) {
    BackupOutcome outcome;

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outcome.error = "could not create output file '" + outputPath + "'";
        return outcome;
    }

    Sha256 hasher;
    std::vector<char> buffer(kChunkSize);
    uint64_t remaining = length;
    uint64_t position = offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        if (!source.readAt(position, buffer.data(), chunk)) {
            outcome.error = "short read at offset " + std::to_string(position);
            return outcome;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        hasher.update(buffer.data(), chunk);
        position += chunk;
        remaining -= chunk;
        if (progress) progress(length - remaining, length);
    }
    out.close();

    outcome.sha256 = hasher.hexDigest();
    outcome.bytesCopied = length;
    outcome.ok = true;

    std::ofstream sidecar(outputPath + ".sha256");
    if (sidecar) {
        sidecar << outcome.sha256 << "  " << outputPath << "\n";
    }

    return outcome;
}

FileHashResult hashFile(const std::string &path, const ProgressFn &progress) {
    FileHashResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "could not open '" + path + "'";
        return result;
    }

    file.seekg(0, std::ios::end);
    const auto totalSize = file.tellg();
    file.seekg(0, std::ios::beg);
    const uint64_t total = totalSize > 0 ? static_cast<uint64_t>(totalSize) : 0;

    Sha256 hasher;
    std::vector<char> buffer(kChunkSize);
    uint64_t done = 0;
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = file.gcount();
        if (n <= 0) break;
        hasher.update(buffer.data(), static_cast<size_t>(n));
        done += static_cast<uint64_t>(n);
        if (progress) progress(done, total);
    }

    result.sha256 = hasher.hexDigest();
    result.sizeBytes = done;
    result.ok = true;
    return result;
}

RestorePlan planRestore(const std::string &inputPath, BlockDevice &target, const RegionSpec &regionSpec) {
    RestorePlan plan;

    const FileHashResult inputHash = hashFile(inputPath);
    if (!inputHash.ok) {
        plan.error = inputHash.error;
        return plan;
    }
    if (inputHash.sizeBytes == 0) {
        plan.error = "input file is empty";
        return plan;
    }

    // The restore's byte range comes from the input file itself (offset
    // from the region spec, length = however many bytes the backup
    // actually contains) rather than re-deriving it from the target's
    // current partition table - the whole point of a restore is that the
    // target's table may be gone/corrupt.
    uint64_t offset = 0;
    if (regionSpec.kind == RegionKind::Custom || regionSpec.kind == RegionKind::Mbr) {
        offset = regionSpec.startLba * kSectorSize;
    }
    const uint64_t length = inputHash.sizeBytes;

    if (regionSpec.kind == RegionKind::Disk && length > target.sizeBytes()) {
        plan.error = "input image (" + std::to_string(length) +
                      " bytes) is larger than the target device (" + std::to_string(target.sizeBytes()) +
                      " bytes) - refusing to restore";
        return plan;
    }
    if (target.isSpecialDevice() && target.sizeBytes() > 0 && offset + length > target.sizeBytes()) {
        plan.error = "region [" + std::to_string(offset) + ", " + std::to_string(offset + length) +
                      ") extends past the end of the target device (" + std::to_string(target.sizeBytes()) +
                      " bytes) - refusing to restore";
        return plan;
    }

    plan.offset = offset;
    plan.length = length;
    plan.inputSizeBytes = inputHash.sizeBytes;
    plan.inputSha256 = inputHash.sha256;
    plan.targetIsSpecialDevice = target.isSpecialDevice();
    plan.ok = true;
    return plan;
}

RestoreOutcome performRestore(const std::string &inputPath, BlockDevice &target, const RestorePlan &plan,
                               const ProgressFn &progress) {
    RestoreOutcome outcome;
    outcome.expectedSha256 = plan.inputSha256;

    if (!plan.ok) {
        outcome.error = "restore plan is not valid: " + plan.error;
        return outcome;
    }

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) {
        outcome.error = "could not reopen input file '" + inputPath + "'";
        return outcome;
    }

    std::vector<char> buffer(kChunkSize);
    uint64_t remaining = plan.length;
    uint64_t position = plan.offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (in.gcount() != static_cast<std::streamsize>(chunk)) {
            outcome.error = "short read from input file";
            return outcome;
        }
        if (!target.writeAt(position, buffer.data(), chunk)) {
            outcome.error = "short write at offset " + std::to_string(position) +
                             " - target may now be in an inconsistent state";
            return outcome;
        }
        position += chunk;
        remaining -= chunk;
        if (progress) progress(plan.length - remaining, plan.length);
    }

    // Verification pass: re-read exactly what was just written and hash it
    // independently, rather than trusting the write calls succeeded.
    Sha256 verifyHasher;
    remaining = plan.length;
    position = plan.offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        if (!target.readAt(position, buffer.data(), chunk)) {
            outcome.error = "could not read back target for verification";
            return outcome;
        }
        verifyHasher.update(buffer.data(), chunk);
        position += chunk;
        remaining -= chunk;
    }

    outcome.actualSha256 = verifyHasher.hexDigest();
    outcome.verified = outcome.actualSha256 == plan.inputSha256;
    outcome.ok = outcome.verified;
    if (!outcome.verified) {
        outcome.error = "verification failed: expected " + plan.inputSha256 + ", got " + outcome.actualSha256;
    }
    return outcome;
}

} // namespace diskutil
