// diskutil: raw sector-level backup/restore for MBR/GPT (and arbitrary
// LBA ranges, or a whole disk), with mandatory SHA-256 verification.
//
// This is a thin CLI wrapper: all the actual copy/verify logic lives in
// DiskOperations, shared with the GUI's "Disk" tab so both interfaces go
// through the exact same safety-critical code path (see that header's
// comments for the invariants: backup is read-only, restore defaults to
// a dry run, a size-mismatched write is refused, every write is
// independently re-read and re-hashed).

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "BlockDevice.h"
#include "DiskOperations.h"
#include "Partitioning.h"

namespace {

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

struct RegionArgs {
    std::string region = "mbr";
    uint64_t startLba = 0;
    uint64_t sectors = 0;
};

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

bool regionSpecFromArgs(const RegionArgs &args, diskutil::RegionSpec *spec, std::string *error) {
    if (args.region == "mbr") {
        spec->kind = diskutil::RegionKind::Mbr;
    } else if (args.region == "gpt") {
        spec->kind = diskutil::RegionKind::Gpt;
    } else if (args.region == "disk") {
        spec->kind = diskutil::RegionKind::Disk;
    } else if (args.region == "custom") {
        spec->kind = diskutil::RegionKind::Custom;
    } else {
        *error = "unknown --region '" + args.region + "'";
        return false;
    }
    spec->startLba = args.startLba;
    spec->sectors = args.sectors;
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
    RegionArgs regionArgs;
    parseRegionArgs(args, &regionArgs);

    if (args.size() != 2) {
        std::cerr << "usage: diskutil backup --region <mbr|gpt|disk|custom> [--start-lba N "
                     "--sectors N] <device-or-image> <output-file>\n";
        return 2;
    }
    const std::string &sourcePath = args[0];
    const std::string &destPath = args[1];

    std::string error;
    diskutil::RegionSpec spec;
    if (!regionSpecFromArgs(regionArgs, &spec, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    auto source = diskutil::BlockDevice::open(sourcePath, false, &error);
    if (!source) {
        std::cerr << "error opening source: " << error << "\n";
        return 1;
    }

    const auto region = diskutil::resolveRegion(*source, spec);
    if (!region.ok) {
        std::cerr << "error: " << region.error << "\n";
        return 1;
    }

    const auto outcome = diskutil::backupRegion(*source, region.offset, region.length, destPath);
    if (!outcome.ok) {
        std::cerr << "error: " << outcome.error << "\n";
        return 1;
    }

    std::cout << "Backed up " << outcome.bytesCopied << " bytes (region=" << regionArgs.region
              << ", offset=" << region.offset << ") from " << sourcePath << " to " << destPath << "\n";
    std::cout << "SHA-256: " << outcome.sha256 << "\n";
    return 0;
}

int runRestore(std::vector<std::string> args) {
    RegionArgs regionArgs;
    parseRegionArgs(args, &regionArgs);

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

    std::string error;
    diskutil::RegionSpec spec;
    if (!regionSpecFromArgs(regionArgs, &spec, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    auto target = diskutil::BlockDevice::open(targetPath, confirmed, &error);
    if (!target) {
        std::cerr << "error opening target: " << error << "\n";
        return 1;
    }

    const auto plan = diskutil::planRestore(inputPath, *target, spec);
    if (!plan.ok) {
        std::cerr << "error: " << plan.error << "\n";
        return 1;
    }

    std::cout << "Restore plan:\n";
    std::cout << "  Source:      " << inputPath << " (" << plan.inputSizeBytes << " bytes)\n";
    std::cout << "  Source SHA:  " << plan.inputSha256 << "\n";
    std::cout << "  Target:      " << targetPath << (plan.targetIsSpecialDevice ? " (block device)" : "")
              << "\n";
    std::cout << "  Byte range:  [" << plan.offset << ", " << (plan.offset + plan.length) << ")\n";

    if (!confirmed) {
        std::cout << "\nDry run only - no data was written. Re-run with --yes to actually write.\n";
        return 0;
    }

    if (plan.targetIsSpecialDevice) {
        std::cout << "\n*** WRITING TO A BLOCK DEVICE: " << targetPath << " ***\n";
    }

    const auto outcome = diskutil::performRestore(inputPath, *target, plan);
    if (!outcome.ok) {
        std::cerr << "\n*** " << outcome.error << " ***\n";
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
