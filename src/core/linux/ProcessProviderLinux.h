#pragma once

#include <chrono>
#include <unordered_map>

#include "../IProcessProvider.h"

namespace core {

// Linux backend built on /proc. No kernel module involved: everything here
// is readable/writable by an unprivileged process for its own processes,
// and by root for everything else (same model ps/top/htop rely on).
class ProcessProviderLinux : public IProcessProvider {
public:
    ProcessProviderLinux();

    std::vector<ProcessInfo> snapshot() override;
    bool terminate(uint64_t pid) override;
    bool setPriority(uint64_t pid, ProcessPriority priority) override;
    bool suspend(uint64_t pid) override;
    bool resume(uint64_t pid) override;

    std::vector<ThreadInfo> threads(uint64_t pid) override;
    std::vector<ModuleInfo> modules(uint64_t pid) override;
    std::vector<MemoryRegionInfo> memoryRegions(uint64_t pid) override;
    std::vector<HandleInfo> handles(uint64_t pid) override;
    std::vector<std::string> environment(uint64_t pid) override;
    std::vector<NetworkConnectionInfo> networkConnections(uint64_t pid) override;

private:
    struct CpuSample {
        uint64_t totalTimeTicks = 0;
        std::chrono::steady_clock::time_point wallClock;
    };

    double computeCpuPercent(uint64_t pid, uint64_t totalTimeTicks);

    std::unordered_map<uint64_t, CpuSample> previousSamples_;
    long clockTicksPerSecond_ = 100;
    long processorCount_ = 1;
};

} // namespace core
