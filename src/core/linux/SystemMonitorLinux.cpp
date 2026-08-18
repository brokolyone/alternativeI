#include "SystemMonitorLinux.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace core {

namespace {

// Whole-disk vs partition heuristic: skip virtual devices entirely (loop,
// ram, device-mapper, since dm- targets double-count their backing
// devices), and skip partitions of real disks so a whole-disk row isn't
// summed together with its own partitions.
bool isVirtualDevice(const std::string &name) {
    return name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 || name.rfind("dm-", 0) == 0;
}

bool isPartition(const std::string &name) {
    if (name.empty()) return false;
    if (name.find("nvme") != std::string::npos || name.rfind("mmcblk", 0) == 0) {
        // Whole disk: nvme0n1 / mmcblk0. Partition: nvme0n1p1 / mmcblk0p1.
        const auto p = name.find_last_of('p');
        return p != std::string::npos && p > 0 && std::isdigit(static_cast<unsigned char>(name.back()));
    }
    // Whole disk: sda / vda / hda (letters only). Partition: sda1.
    return std::isdigit(static_cast<unsigned char>(name.back()));
}

} // namespace

SystemStats SystemMonitorLinux::sample() {
    SystemStats stats;

    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds =
        hasPrevious_ ? std::chrono::duration<double>(now - prevTime_).count() : 0.0;

    // --- CPU: aggregate line from /proc/stat ---
    {
        std::ifstream file("/proc/stat");
        std::string label;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        file >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

        const uint64_t idleTime = idle + iowait;
        const uint64_t totalTime = user + nice + system + idle + iowait + irq + softirq + steal;

        if (hasPrevious_ && totalTime > prevCpuTotal_) {
            const uint64_t totalDelta = totalTime - prevCpuTotal_;
            const uint64_t idleDelta = idleTime - prevCpuIdle_;
            stats.cpuPercent = totalDelta > 0
                ? (100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta))
                : 0.0;
        }
        prevCpuIdle_ = idleTime;
        prevCpuTotal_ = totalTime;
    }

    // --- Memory: /proc/meminfo ---
    {
        std::ifstream file("/proc/meminfo");
        std::string line;
        uint64_t memTotalKb = 0;
        uint64_t memAvailableKb = 0;
        while (std::getline(file, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::istringstream(line.substr(9)) >> memTotalKb;
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::istringstream(line.substr(13)) >> memAvailableKb;
            }
        }
        stats.memoryTotalBytes = memTotalKb * 1024;
        stats.memoryUsedBytes =
            memAvailableKb <= memTotalKb ? (memTotalKb - memAvailableKb) * 1024 : 0;
    }

    // --- Disk I/O: /proc/diskstats, summed across whole physical disks ---
    {
        std::ifstream file("/proc/diskstats");
        std::string line;
        uint64_t totalReadSectors = 0;
        uint64_t totalWriteSectors = 0;
        while (std::getline(file, line)) {
            std::istringstream lineStream(line);
            std::vector<std::string> fields;
            std::string field;
            while (lineStream >> field) fields.push_back(field);
            if (fields.size() < 10) continue;

            const std::string &name = fields[2];
            if (isVirtualDevice(name) || isPartition(name)) continue;

            totalReadSectors += std::stoull(fields[5]);
            totalWriteSectors += std::stoull(fields[9]);
        }

        if (hasPrevious_ && elapsedSeconds > 0.0) {
            constexpr double sectorSize = 512.0;
            stats.diskReadBytesPerSec =
                static_cast<double>(totalReadSectors - prevDiskReadSectors_) * sectorSize / elapsedSeconds;
            stats.diskWriteBytesPerSec =
                static_cast<double>(totalWriteSectors - prevDiskWriteSectors_) * sectorSize / elapsedSeconds;
        }
        prevDiskReadSectors_ = totalReadSectors;
        prevDiskWriteSectors_ = totalWriteSectors;
    }

    // --- Network: /proc/net/dev, summed across interfaces except loopback ---
    {
        std::ifstream file("/proc/net/dev");
        std::string line;
        std::getline(file, line); // header x2
        std::getline(file, line);

        uint64_t totalRecvBytes = 0;
        uint64_t totalSentBytes = 0;
        while (std::getline(file, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string iface = line.substr(0, colon);
            iface.erase(0, iface.find_first_not_of(' '));
            if (iface == "lo") continue;

            std::istringstream lineStream(line.substr(colon + 1));
            std::vector<uint64_t> values;
            uint64_t value;
            while (lineStream >> value) values.push_back(value);
            if (values.size() < 16) continue;

            totalRecvBytes += values[0];
            totalSentBytes += values[8];
        }

        if (hasPrevious_ && elapsedSeconds > 0.0) {
            stats.netRecvBytesPerSec =
                static_cast<double>(totalRecvBytes - prevNetRecvBytes_) / elapsedSeconds;
            stats.netSentBytesPerSec =
                static_cast<double>(totalSentBytes - prevNetSentBytes_) / elapsedSeconds;
        }
        prevNetRecvBytes_ = totalRecvBytes;
        prevNetSentBytes_ = totalSentBytes;
    }

    prevTime_ = now;
    hasPrevious_ = true;
    return stats;
}

} // namespace core
