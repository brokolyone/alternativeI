#pragma once

#include <cstdint>

namespace core {

struct SystemStats {
    double cpuPercent = 0.0;
    uint64_t memoryUsedBytes = 0;
    uint64_t memoryTotalBytes = 0;
    double diskReadBytesPerSec = 0.0;
    double diskWriteBytesPerSec = 0.0;
    double netRecvBytesPerSec = 0.0;
    double netSentBytesPerSec = 0.0;
};

} // namespace core
