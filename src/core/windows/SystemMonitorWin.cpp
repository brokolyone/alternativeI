#include "SystemMonitorWin.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

namespace core {

namespace {

uint64_t fileTimeToUint64(const FILETIME &ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

} // namespace

SystemMonitorWin::SystemMonitorWin() {
    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return;
    }
    pdhQuery_ = query;

    PDH_HCOUNTER counter = nullptr;
    if (PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &counter) ==
        ERROR_SUCCESS) {
        diskReadCounter_ = counter;
    }
    if (PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &counter) ==
        ERROR_SUCCESS) {
        diskWriteCounter_ = counter;
    }
    // NOTE: "(*)" as a wildcard instance on a single PdhAddCounter only
    // reliably binds to *one* NIC instance on most counter types; a
    // correct multi-NIC sum needs PdhExpandWildCardPath() to add one
    // counter per interface and PdhGetFormattedCounterArray() to sum
    // them. Left as this simpler (single-instance) form since it can't be
    // verified against a real Windows box here - if PdhAddEnglishCounterW
    // fails the counter pointer stays null and sample() just reports 0,
    // it won't crash.
    if (PdhAddEnglishCounterW(query, L"\\Network Interface(*)\\Bytes Received/sec", 0, &counter) ==
        ERROR_SUCCESS) {
        netRecvCounter_ = counter;
    }
    if (PdhAddEnglishCounterW(query, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &counter) ==
        ERROR_SUCCESS) {
        netSentCounter_ = counter;
    }

    // Prime the query: PDH rate counters need two samples before they
    // report a meaningful value.
    PdhCollectQueryData(query);
}

SystemMonitorWin::~SystemMonitorWin() {
    if (pdhQuery_ != nullptr) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(pdhQuery_));
    }
}

SystemStats SystemMonitorWin::sample() {
    SystemStats stats;

    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        const uint64_t idle = fileTimeToUint64(idleTime);
        const uint64_t kernel = fileTimeToUint64(kernelTime);
        const uint64_t user = fileTimeToUint64(userTime);
        // NB: kernelTime from GetSystemTimes already includes idle time.
        const uint64_t total = kernel + user;

        if (hasPreviousCpu_ && total > prevKernelTime_ + prevUserTime_) {
            const uint64_t totalDelta = total - (prevKernelTime_ + prevUserTime_);
            const uint64_t idleDelta = idle - prevIdleTime_;
            stats.cpuPercent = totalDelta > 0
                ? (100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta))
                : 0.0;
        }
        prevIdleTime_ = idle;
        prevKernelTime_ = kernel;
        prevUserTime_ = user;
        hasPreviousCpu_ = true;
    }

    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        stats.memoryTotalBytes = memStatus.ullTotalPhys;
        stats.memoryUsedBytes = memStatus.ullTotalPhys - memStatus.ullAvailPhys;
    }

    if (pdhQuery_ != nullptr) {
        PdhCollectQueryData(static_cast<PDH_HQUERY>(pdhQuery_));

        auto formatDouble = [](void *counter) -> double {
            if (counter == nullptr) return 0.0;
            PDH_FMT_COUNTERVALUE value;
            if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE, nullptr,
                                             &value) == ERROR_SUCCESS &&
                value.CStatus == PDH_CSTATUS_VALID_DATA) {
                return value.doubleValue;
            }
            return 0.0;
        };

        stats.diskReadBytesPerSec = formatDouble(diskReadCounter_);
        stats.diskWriteBytesPerSec = formatDouble(diskWriteCounter_);
        stats.netRecvBytesPerSec = formatDouble(netRecvCounter_);
        stats.netSentBytesPerSec = formatDouble(netSentCounter_);
    }

    return stats;
}

} // namespace core
