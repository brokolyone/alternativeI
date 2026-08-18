#pragma once

#include <cstdint>
#include <string>

namespace core {

enum class ProcessPriority {
    Idle,
    BelowNormal,
    Normal,
    AboveNormal,
    High,
    Realtime,
    Unknown,
};

struct ProcessInfo {
    uint64_t pid = 0;
    uint64_t ppid = 0;
    std::string name;
    std::string exePath;
    std::string user;
    uint64_t privateBytes = 0;   // working set / RSS in bytes
    uint64_t threadCount = 0;
    double cpuPercent = 0.0;
    ProcessPriority priority = ProcessPriority::Unknown;
};

} // namespace core
