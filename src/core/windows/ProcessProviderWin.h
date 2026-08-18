#pragma once

#include <chrono>
#include <unordered_map>

#include "../IProcessProvider.h"

namespace core {

// Windows backend built on Toolhelp32 + WinAPI (user-mode only for now).
// A future revision can switch to NtQuerySystemInformation for handles/
// modules and add a signed kernel driver (like Process Hacker's KPH) for
// operations that require elevated access beyond what user-mode allows.
class ProcessProviderWin : public IProcessProvider {
public:
    ProcessProviderWin();

    std::vector<ProcessInfo> snapshot() override;
    bool terminate(uint64_t pid) override;
    bool setPriority(uint64_t pid, ProcessPriority priority) override;

private:
    struct CpuSample {
        uint64_t kernelAndUserTime100ns = 0;
        std::chrono::steady_clock::time_point wallClock;
    };

    double computeCpuPercent(uint64_t pid, uint64_t kernelAndUserTime100ns);

    std::unordered_map<uint64_t, CpuSample> previousSamples_;
    std::chrono::steady_clock::time_point lastSnapshotTime_;
    unsigned long processorCount_ = 1;
};

} // namespace core
