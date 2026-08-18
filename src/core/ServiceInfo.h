#pragma once

#include <string>

namespace core {

struct ServiceInfo {
    std::string name;
    std::string displayName;
    std::string state;     // e.g. "running", "stopped", "failed"
    std::string startType; // e.g. "enabled", "disabled", "static" (Linux) / "Auto", "Manual" (Windows)
};

} // namespace core
