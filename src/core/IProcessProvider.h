#pragma once

#include <memory>
#include <vector>

#include "ProcessInfo.h"

namespace core {

// Platform-agnostic source of process information. Each OS backend
// (Windows, Linux, ...) implements this on top of its native APIs.
class IProcessProvider {
public:
    virtual ~IProcessProvider() = default;

    // Returns a fresh snapshot of all visible processes. CPU% is computed
    // relative to the previous call, so callers should poll on an interval.
    virtual std::vector<ProcessInfo> snapshot() = 0;

    virtual bool terminate(uint64_t pid) = 0;
    virtual bool setPriority(uint64_t pid, ProcessPriority priority) = 0;
};

std::unique_ptr<IProcessProvider> createDefaultProcessProvider();

} // namespace core
