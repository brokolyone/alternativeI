#include "DiskWorker.h"

#include "../diskutil/BlockDevice.h"
#include "../diskutil/DiskOperations.h"
#include "../diskutil/Partitioning.h"

#include <QLocale>
#include <QTextStream>
#include <utility>
#include <vector>

namespace gui {

namespace {

QString typeGuidName(const std::string &guid) {
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
        if (guid == typeGuid) return QString::fromLatin1(name);
    }
    return QStringLiteral("Unknown");
}

diskutil::RegionSpec makeRegionSpec(int regionKind, quint64 startLba, quint64 sectors) {
    diskutil::RegionSpec spec;
    spec.kind = static_cast<diskutil::RegionKind>(regionKind);
    spec.startLba = startLba;
    spec.sectors = sectors;
    return spec;
}

} // namespace

DiskWorker::DiskWorker(QObject *parent) : QObject(parent) {}

void DiskWorker::doInfo(const QString &path) {
    std::string error;
    auto device = diskutil::BlockDevice::open(path.toStdString(), false, &error);
    if (!device) {
        emit infoReady(false, QString::fromStdString(error), {});
        return;
    }

    QString text;
    QTextStream out(&text);
    out << "Path:  " << path << "\n";
    out << "Size:  " << QLocale().formattedDataSize(static_cast<qint64>(device->sizeBytes())) << " ("
        << device->sizeBytes() << " bytes)\n";
    out << "Type:  " << (device->isSpecialDevice() ? "block device" : "regular file") << "\n\n";

    const auto mbr = diskutil::readMbr(*device);
    if (!mbr.hasValidSignature) {
        out << "MBR:   no valid 0x55AA signature - unpartitioned or unrecognized\n";
        emit infoReady(true, {}, text);
        return;
    }

    if (mbr.looksLikeGptProtectiveMbr) {
        out << "MBR:   protective (GPT disk)\n\n";
    } else {
        out << "MBR partitions:\n";
        for (const auto &p : mbr.partitions) {
            out << QStringLiteral("  %1 type=0x%2  start_lba=%3  sectors=%4  (%5)\n")
                       .arg(p.active ? "*" : " ")
                       .arg(p.type, 2, 16, QChar('0'))
                       .arg(p.startLba)
                       .arg(p.sectorCount)
                       .arg(QLocale().formattedDataSize(
                           static_cast<qint64>(static_cast<uint64_t>(p.sectorCount) * diskutil::kSectorSize)));
        }
        out << "\n";
    }

    const auto gpt = diskutil::readGpt(*device);
    if (gpt && gpt->headerValid) {
        out << "GPT disk GUID: " << QString::fromStdString(gpt->diskGuid) << "\n";
        out << "GPT partitions (" << static_cast<int>(gpt->partitions.size()) << "):\n";
        for (const auto &p : gpt->partitions) {
            const uint64_t sectors = p.endLba >= p.startLba ? (p.endLba - p.startLba + 1) : 0;
            out << QStringLiteral("  %1  %2  start=%3  end=%4  (%5)  %6\n")
                       .arg(QString::fromStdString(p.name), -24)
                       .arg(typeGuidName(p.typeGuid), -22)
                       .arg(p.startLba)
                       .arg(p.endLba)
                       .arg(QLocale().formattedDataSize(static_cast<qint64>(sectors * diskutil::kSectorSize)))
                       .arg(QString::fromStdString(p.uniqueGuid));
        }
    }

    emit infoReady(true, {}, text);
}

void DiskWorker::doBackup(const QString &sourcePath, int regionKind, quint64 startLba, quint64 sectors,
                           const QString &outputPath) {
    std::string error;
    auto source = diskutil::BlockDevice::open(sourcePath.toStdString(), false, &error);
    if (!source) {
        emit backupFinished(false, QString::fromStdString(error), 0, {});
        return;
    }

    const auto spec = makeRegionSpec(regionKind, startLba, sectors);
    const auto region = diskutil::resolveRegion(*source, spec);
    if (!region.ok) {
        emit backupFinished(false, QString::fromStdString(region.error), 0, {});
        return;
    }

    auto progress = [this](uint64_t done, uint64_t total) { emit backupProgress(done, total); };
    const auto outcome =
        diskutil::backupRegion(*source, region.offset, region.length, outputPath.toStdString(), progress);

    emit backupFinished(outcome.ok, QString::fromStdString(outcome.error), outcome.bytesCopied,
                         QString::fromStdString(outcome.sha256));
}

void DiskWorker::doPlanRestore(const QString &inputPath, const QString &targetPath, int regionKind,
                                quint64 startLba) {
    std::string error;
    auto target = diskutil::BlockDevice::open(targetPath.toStdString(), false, &error);
    if (!target) {
        emit restorePlanReady(false, QString::fromStdString(error), 0, 0, 0, {}, false);
        return;
    }

    const auto spec = makeRegionSpec(regionKind, startLba, 0);
    const auto plan = diskutil::planRestore(inputPath.toStdString(), *target, spec);
    emit restorePlanReady(plan.ok, QString::fromStdString(plan.error), plan.offset, plan.length,
                           plan.inputSizeBytes, QString::fromStdString(plan.inputSha256),
                           plan.targetIsSpecialDevice);
}

void DiskWorker::doPerformRestore(const QString &inputPath, const QString &targetPath, int regionKind,
                                   quint64 startLba) {
    std::string error;
    // Opened with write access only now, at the point the caller has
    // already gotten explicit user confirmation - mirrors the CLI, which
    // only opens for read-write once --yes is present.
    auto target = diskutil::BlockDevice::open(targetPath.toStdString(), true, &error);
    if (!target) {
        emit restoreFinished(false, QString::fromStdString(error), false, {}, {});
        return;
    }

    const auto spec = makeRegionSpec(regionKind, startLba, 0);
    const auto plan = diskutil::planRestore(inputPath.toStdString(), *target, spec);
    if (!plan.ok) {
        emit restoreFinished(false, QString::fromStdString(plan.error), false, {}, {});
        return;
    }

    auto progress = [this](uint64_t done, uint64_t total) { emit restoreProgress(done, total); };
    const auto outcome = diskutil::performRestore(inputPath.toStdString(), *target, plan, progress);

    emit restoreFinished(outcome.ok, QString::fromStdString(outcome.error), outcome.verified,
                          QString::fromStdString(outcome.expectedSha256),
                          QString::fromStdString(outcome.actualSha256));
}

} // namespace gui
