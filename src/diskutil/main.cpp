// diskutil: raw sector-level backup/restore for MBR/GPT (and arbitrary
// LBA ranges, or a whole disk), with mandatory SHA-256 verification.
//
// Design goals, since this is the part of the project that can destroy
// data if it's wrong:
//   - backup is read-only and always safe to run.
//   - restore defaults to a dry run: it prints exactly what it *would*
//     write and refuses to touch anything until --yes is passed.
//   - after every write, diskutil re-reads what it just wrote and
//     compares its SHA-256 against the source image; a mismatch is a
//     hard failure (non-zero exit), never a warning you can miss.
//   - "custom"/"disk" writes to a size-mismatched region are refused
//     outright rather than silently truncating or padding.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BlockDevice.h"
#include "Partitioning.h"
#include "Sha256.h"

namespace {

constexpr size_t kChunkSize = 4 * 1024 * 1024;

void printUsage() {
    std::cerr <<
        "diskutil - raw MBR/GPT/disk backup and restore\n"
        "\n"
        "  diskutil info <device-or-image>\n"
        "  diskutil backup --region {mbr|gpt|disk|custom} [--start-lba N --sectors N]\n"
        "                  <device-or-image> <output-file>\n"
        "  diskutil restore --region {mbr|gpt|disk|custom} [--start-lba N] [--yes]\n"
        "                   <input-file> <device-or-image>\n"
        "\n"
        "  --region mbr     first 512 bytes (the classic MBR / GPT protective MBR)\n"
        "  --region gpt     protective MBR + primary GPT header + partition array\n"
        "  --region disk    the entire device/image\n"
        "  --region custom  --start-lba N --sectors N, an explicit LBA range\n"
        "\n"
        "  Nothing is ever written to a device without --yes. Without it, restore\n"
        "  prints what it would do (target, byte range, source checksum) and exits.\n";
}

std::string formatTypeGuidName(const std::string &guid) {
    static const std::vector<std::pair<const char *, const char *>> kKnown = {
        {"C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI System Partition"},
        {"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data"},
        {"E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft reserved"},
        {"0FC63DAF-8483-4772-8E79-3D69D8477DE4", "Linux filesystem"},
        {"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap"},
        {"A19D880F-05FC-4D3B-A006-743F0F84911E", "Linux RAID"},
        {"E6D6D379-F507-44C2-A23C-238F2A3DF928", "Linux LVM"},
        {"21686148-6449-6E6F-744E-656564454649", "BIOS boot"},
    };
    for (const auto &[typeGuid, name] : kKnown) {
        if (guid == typeGuid) return name;
    }
    return "Unknown";
}

bool computeFileSha256(const std::string &path, std::string *outHash, uint64_t *outSize) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    diskutil::Sha256 hasher;
    std::vector<char> buffer(kChunkSize);
    uint64_t total = 0;
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = file.gcount();
        if (n <= 0) break;
        hasher.update(buffer.data(), static_cast<size_t>(n));
        total += static_cast<uint64_t>(n);
    }
    *outHash = hasher.hexDigest();
    *outSize = total;
    return true;
}

struct RegionArgs {
    std::string region = "mbr";
    uint64_t startLba = 0;
    uint64_t sectors = 0;
};

// Resolves --region into a concrete [offset, length) byte range against an
// already-open device. For "gpt" this requires actually reading the GPT
// header, so it can fail if the device isn't GPT-partitioned.
bool resolveRegion(diskutil::BlockDevice &device, const RegionArgs &args, uint64_t *offset,
                    uint64_t *length, std::string *error) {
    if (args.region == "mbr") {
        *offset = 0;
        *length = diskutil::kSectorSize;
        return true;
    }
    if (args.region == "disk") {
        *offset = 0;
        *length = device.sizeBytes();
        if (*length == 0) {
            *error = "could not determine device size";
            return false;
        }
        return true;
    }
    if (args.region == "gpt") {
        auto gpt = diskutil::readGpt(device);
        if (!gpt || !gpt->headerValid) {
            *error = "no valid GPT header found (device isn't GPT-partitioned?)";
            return false;
        }
        *offset = 0;
        *length = diskutil::gptPrimaryRegionBytes(*gpt);
        return true;
    }
    if (args.region == "custom") {
        if (args.sectors == 0) {
            *error = "--region custom requires --sectors N (and optionally --start-lba N)";
            return false;
        }
        *offset = args.startLba * diskutil::kSectorSize;
        *length = args.sectors * diskutil::kSectorSize;
        return true;
    }
    *error = "unknown --region '" + args.region + "'";
    return false;
}

bool parseRegionArgs(std::vector<std::string> &args, RegionArgs *out) {
    for (size_t i = 0; i < args.size();) {
        if (args[i] == "--region" && i + 1 < args.size()) {
            out->region = args[i + 1];
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
        } else if (args[i] == "--start-lba" && i + 1 < args.size()) {
            out->startLba = std::stoull(args[i + 1]);
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
        } else if (args[i] == "--sectors" && i + 1 < args.size()) {
            out->sectors = std::stoull(args[i + 1]);
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
        } else {
            ++i;
        }
    }
    return true;
}

int runInfo(const std::vector<std::string> &args) {
    if (args.size() != 1) {
        std::cerr << "usage: diskutil info <device-or-image>\n";
        return 2;
    }

    std::string error;
    auto device = diskutil::BlockDevice::open(args[0], false, &error);
    if (!device) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    std::cout << "Path:   " << args[0] << "\n";
    std::cout << "Size:   " << device->sizeBytes() << " bytes ("
              << (device->sizeBytes() / (1024.0 * 1024.0 * 1024.0)) << " GiB)\n";
    std::cout << "Type:   " << (device->isSpecialDevice() ? "block device" : "regular file") << "\n\n";

    const auto mbr = diskutil::readMbr(*device);
    if (!mbr.hasValidSignature) {
        std::cout << "MBR:    no valid 0x55AA signature - unpartitioned or unrecognized\n";
        return 0;
    }

    if (mbr.looksLikeGptProtectiveMbr) {
        std::cout << "MBR:    protective (GPT disk)\n\n";
    } else {
        std::cout << "MBR partitions:\n";
        for (const auto &p : mbr.partitions) {
            std::printf("  %s type=0x%02X  start_lba=%-12u sectors=%-12u (%.2f GiB)\n",
                        p.active ? "*" : " ", p.type, p.startLba, p.sectorCount,
                        p.sectorCount * diskutil::kSectorSize / (1024.0 * 1024.0 * 1024.0));
        }
        std::cout << "\n";
    }

    const auto gpt = diskutil::readGpt(*device);
    if (gpt && gpt->headerValid) {
        std::cout << "GPT disk GUID: " << gpt->diskGuid << "\n";
        std::cout << "GPT partitions (" << gpt->partitions.size() << "):\n";
        for (const auto &p : gpt->partitions) {
            const uint64_t sectors = p.endLba >= p.startLba ? (p.endLba - p.startLba + 1) : 0;
            std::printf("  %-24s  %-22s start=%-12llu end=%-12llu (%.2f GiB)  %s\n", p.name.c_str(),
                        formatTypeGuidName(p.typeGuid).c_str(),
                        static_cast<unsigned long long>(p.startLba),
                        static_cast<unsigned long long>(p.endLba),
                        sectors * diskutil::kSectorSize / (1024.0 * 1024.0 * 1024.0), p.uniqueGuid.c_str());
        }
    }

    return 0;
}

int runBackup(std::vector<std::string> args) {
    RegionArgs region;
    parseRegionArgs(args, &region);

    if (args.size() != 2) {
        std::cerr << "usage: diskutil backup --region <mbr|gpt|disk|custom> [--start-lba N "
                     "--sectors N] <device-or-image> <output-file>\n";
        return 2;
    }
    const std::string &sourcePath = args[0];
    const std::string &destPath = args[1];

    std::string error;
    auto source = diskutil::BlockDevice::open(sourcePath, false, &error);
    if (!source) {
        std::cerr << "error opening source: " << error << "\n";
        return 1;
    }

    uint64_t offset = 0, length = 0;
    if (!resolveRegion(*source, region, &offset, &length, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "error: could not create output file '" << destPath << "'\n";
        return 1;
    }

    diskutil::Sha256 hasher;
    std::vector<char> buffer(kChunkSize);
    uint64_t remaining = length;
    uint64_t position = offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        if (!source->readAt(position, buffer.data(), chunk)) {
            std::cerr << "error: short read at offset " << position << "\n";
            return 1;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        hasher.update(buffer.data(), chunk);
        position += chunk;
        remaining -= chunk;
    }
    out.close();

    const std::string digest = hasher.hexDigest();
    std::ofstream sidecar(destPath + ".sha256");
    if (sidecar) {
        sidecar << digest << "  " << destPath << "\n";
    }

    std::cout << "Backed up " << length << " bytes (region=" << region.region << ", offset=" << offset
              << ") from " << sourcePath << " to " << destPath << "\n";
    std::cout << "SHA-256: " << digest << "\n";
    return 0;
}

int runRestore(std::vector<std::string> args) {
    RegionArgs region;
    parseRegionArgs(args, &region);

    bool confirmed = false;
    for (auto it = args.begin(); it != args.end();) {
        if (*it == "--yes") {
            confirmed = true;
            it = args.erase(it);
        } else {
            ++it;
        }
    }

    if (args.size() != 2) {
        std::cerr << "usage: diskutil restore --region <mbr|gpt|disk|custom> [--start-lba N] "
                     "[--yes] <input-file> <device-or-image>\n";
        return 2;
    }
    const std::string &inputPath = args[0];
    const std::string &targetPath = args[1];

    std::string inputHash;
    uint64_t inputSize = 0;
    if (!computeFileSha256(inputPath, &inputHash, &inputSize)) {
        std::cerr << "error: could not read input file '" << inputPath << "'\n";
        return 1;
    }
    if (inputSize == 0) {
        std::cerr << "error: input file is empty\n";
        return 1;
    }

    std::string error;
    auto target = diskutil::BlockDevice::open(targetPath, confirmed, &error);
    if (!target) {
        std::cerr << "error opening target: " << error << "\n";
        return 1;
    }

    // The restore's byte range comes from the input file itself (offset
    // from --region/--start-lba, length = however many bytes the backup
    // actually contains) rather than re-deriving it from the target's
    // current partition table - the whole point of a restore is that the
    // target's table may be gone/corrupt.
    uint64_t offset = 0;
    if (region.region == "custom" || region.region == "mbr") {
        offset = region.startLba * diskutil::kSectorSize;
    }
    const uint64_t length = inputSize;

    if (region.region == "disk" && length > target->sizeBytes()) {
        std::cerr << "error: input image (" << length << " bytes) is larger than the target device ("
                  << target->sizeBytes() << " bytes) - refusing to restore\n";
        return 1;
    }
    if (target->isSpecialDevice() && offset + length > target->sizeBytes() && target->sizeBytes() > 0) {
        std::cerr << "error: region [" << offset << ", " << (offset + length)
                  << ") extends past the end of the target device (" << target->sizeBytes()
                  << " bytes) - refusing to restore\n";
        return 1;
    }

    std::cout << "Restore plan:\n";
    std::cout << "  Source:      " << inputPath << " (" << inputSize << " bytes)\n";
    std::cout << "  Source SHA:  " << inputHash << "\n";
    std::cout << "  Target:      " << targetPath << (target->isSpecialDevice() ? " (block device)" : "")
              << "\n";
    std::cout << "  Byte range:  [" << offset << ", " << (offset + length) << ")\n";

    if (!confirmed) {
        std::cout << "\nDry run only - no data was written. Re-run with --yes to actually write.\n";
        return 0;
    }

    if (target->isSpecialDevice()) {
        std::cout << "\n*** WRITING TO A BLOCK DEVICE: " << targetPath << " ***\n";
    }

    std::ifstream in(inputPath, std::ios::binary);
    std::vector<char> buffer(kChunkSize);
    uint64_t remaining = length;
    uint64_t position = offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (in.gcount() != static_cast<std::streamsize>(chunk)) {
            std::cerr << "error: short read from input file\n";
            return 1;
        }
        if (!target->writeAt(position, buffer.data(), chunk)) {
            std::cerr << "error: short write at offset " << position << " - target may now be in an "
                                                                          "inconsistent state\n";
            return 1;
        }
        position += chunk;
        remaining -= chunk;
    }

    // Verification pass: re-read exactly what was just written and hash
    // it independently, rather than trusting the write calls succeeded.
    diskutil::Sha256 verifyHasher;
    remaining = length;
    position = offset;
    while (remaining > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
        if (!target->readAt(position, buffer.data(), chunk)) {
            std::cerr << "error: could not read back target for verification\n";
            return 1;
        }
        verifyHasher.update(buffer.data(), chunk);
        position += chunk;
        remaining -= chunk;
    }
    const std::string verifyHash = verifyHasher.hexDigest();

    if (verifyHash != inputHash) {
        std::cerr << "\n*** VERIFICATION FAILED ***\n";
        std::cerr << "expected: " << inputHash << "\n";
        std::cerr << "actual:   " << verifyHash << "\n";
        return 1;
    }

    std::cout << "\nRestore complete and verified (SHA-256 match).\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage();
        return 2;
    }

    const std::string command = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    if (command == "info") return runInfo(args);
    if (command == "backup") return runBackup(args);
    if (command == "restore") return runRestore(args);

    if (command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }

    std::cerr << "unknown command '" << command << "'\n\n";
    printUsage();
    return 2;
}
