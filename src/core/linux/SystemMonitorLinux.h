#pragma once

#include <chrono>

#include "../ISystemMonitor.h"

namespace core {

class SystemMonitorLinux : public ISystemMonitor {
public:
    SystemStats sample() override;

private:
    uint64_t prevCpuIdle_ = 0;
    uint64_t prevCpuTotal_ = 0;
    uint64_t prevDiskReadSectors_ = 0;
    uint64_t prevDiskWriteSectors_ = 0;
    uint64_t prevNetRecvBytes_ = 0;
    uint64_t prevNetSentBytes_ = 0;
    std::chrono::steady_clock::time_point prevTime_;
    bool hasPrevious_ = false;
};

} // namespace core
