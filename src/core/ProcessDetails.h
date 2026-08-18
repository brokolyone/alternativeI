#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

struct ThreadInfo {
    uint64_t tid = 0;
    std::string state;       // e.g. "Running", "Sleeping"
    uint64_t startAddress = 0;
    double cpuPercent = 0.0;
    int priority = 0;
};

struct ModuleInfo {
    std::string name;
    std::string path;
    uint64_t baseAddress = 0;
    uint64_t sizeBytes = 0;
};

struct MemoryRegionInfo {
    uint64_t baseAddress = 0;
    uint64_t sizeBytes = 0;
    std::string protection; // e.g. "rwxp", "r--p"
    std::string mappedFile; // path, or empty for anonymous
};

struct HandleInfo {
    uint64_t handleValueOrFd = 0;
    std::string type; // "File", "Socket", "Directory", "Event", ...
    std::string name; // resolved target, when available
};

enum class NetworkProtocol { Tcp, Udp, Tcp6, Udp6 };

struct NetworkConnectionInfo {
    NetworkProtocol protocol = NetworkProtocol::Tcp;
    std::string localAddress;
    uint16_t localPort = 0;
    std::string remoteAddress;
    uint16_t remotePort = 0;
    std::string state; // TCP state, e.g. "ESTABLISHED"; empty for UDP
};

} // namespace core
