#include "DiskDeviceList.h"

#include "../diskutil/BlockDevice.h"

#include <cctype>

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace gui {

namespace {

#if defined(__linux__)
// Same whole-disk-vs-partition heuristic used by the system performance
// monitor's disk I/O aggregation (SystemMonitorLinux.cpp) - kept as a
// separate small copy here rather than a shared dependency, since this
// one only needs to answer "is this a candidate to list", not aggregate
// anything.
bool looksLikePartition(const std::string &name) {
    if (name.empty()) return false;
    if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 || name.rfind("dm-", 0) == 0) {
        return true; // not a physical disk at all - exclude via "is a partition"
    }
    if (name.find("nvme") != std::string::npos || name.rfind("mmcblk", 0) == 0) {
        const auto p = name.find_last_of('p');
        return p != std::string::npos && p > 0 && std::isdigit(static_cast<unsigned char>(name.back()));
    }
    return std::isdigit(static_cast<unsigned char>(name.back()));
}
#endif

} // namespace

QList<DiskDeviceEntry> enumerateDiskDevices() {
    QList<DiskDeviceEntry> result;

#if defined(_WIN32)
    for (int i = 0; i < 16; ++i) {
        const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(i);
        std::string error;
        auto device = diskutil::BlockDevice::open(path.toStdString(), false, &error);
        if (!device) continue;
        DiskDeviceEntry entry;
        entry.path = path;
        entry.sizeBytes = device->sizeBytes();
        result.push_back(entry);
    }
#elif defined(__linux__)
    std::ifstream file("/proc/partitions");
    std::string line;
    std::getline(file, line); // header
    std::getline(file, line); // blank
    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        std::string major, minor, blocks, name;
        lineStream >> major >> minor >> blocks >> name;
        if (name.empty() || looksLikePartition(name)) continue;

        const std::string path = "/dev/" + name;
        std::string error;
        auto device = diskutil::BlockDevice::open(path, false, &error);
        if (!device) continue;

        DiskDeviceEntry entry;
        entry.path = QString::fromStdString(path);
        entry.sizeBytes = device->sizeBytes();
        result.push_back(entry);
    }
#endif

    return result;
}

} // namespace gui
