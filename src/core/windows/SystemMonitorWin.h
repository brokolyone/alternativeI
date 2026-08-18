#pragma once

#include "../ISystemMonitor.h"

namespace core {

class SystemMonitorWin : public ISystemMonitor {
public:
    SystemMonitorWin();
    ~SystemMonitorWin() override;

    SystemStats sample() override;

private:
    uint64_t prevIdleTime_ = 0;
    uint64_t prevKernelTime_ = 0;
    uint64_t prevUserTime_ = 0;
    bool hasPreviousCpu_ = false;

    // Opaque PDH_HQUERY/PDH_HCOUNTER, stored as void* so this header
    // doesn't have to drag in <pdh.h> (and its Windows-only types) for
    // every translation unit that just wants ISystemMonitor.
    void *pdhQuery_ = nullptr;
    void *diskReadCounter_ = nullptr;
    void *diskWriteCounter_ = nullptr;
    void *netRecvCounter_ = nullptr;
    void *netSentCounter_ = nullptr;
};

} // namespace core
