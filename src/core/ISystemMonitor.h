#pragma once

#include <memory>

#include "SystemStats.h"

namespace core {

// System-wide (not per-process) resource usage, sampled on an interval by
// the caller. Rate fields (disk/network bytes-per-sec) are computed from
// the delta since the previous sample() call, same convention as
// IProcessProvider::snapshot()'s CPU%.
class ISystemMonitor {
public:
    virtual ~ISystemMonitor() = default;
    virtual SystemStats sample() = 0;
};

std::unique_ptr<ISystemMonitor> createDefaultSystemMonitor();

} // namespace core
