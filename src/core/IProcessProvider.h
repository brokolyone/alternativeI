#pragma once

#include <memory>
#include <vector>

#include "ProcessDetails.h"
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

    // Per-process detail views, queried on demand (e.g. when a properties
    // dialog is opened) rather than on every snapshot() poll, since they
    // are more expensive to gather. Each returns an empty vector if the
    // process is gone or the caller lacks permission to inspect it.
    virtual std::vector<ThreadInfo> threads(uint64_t pid) = 0;
    virtual std::vector<ModuleInfo> modules(uint64_t pid) = 0;
    virtual std::vector<MemoryRegionInfo> memoryRegions(uint64_t pid) = 0;
    virtual std::vector<HandleInfo> handles(uint64_t pid) = 0;
    virtual std::vector<std::string> environment(uint64_t pid) = 0;
    virtual std::vector<NetworkConnectionInfo> networkConnections(uint64_t pid) = 0;
};

std::unique_ptr<IProcessProvider> createDefaultProcessProvider();

} // namespace core
