#include "ProcessProviderLinux.h"

#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <arpa/inet.h>
#include <sys/stat.h>

namespace core {

namespace {

bool isAllDigits(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string readFile(const std::string &path) {
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string usernameForUid(uid_t uid) {
    struct passwd *entry = getpwuid(uid);
    if (entry != nullptr && entry->pw_name != nullptr) {
        return entry->pw_name;
    }
    return std::to_string(uid);
}

ProcessPriority niceValueToEnum(int nice) {
    if (nice <= -15) return ProcessPriority::High;
    if (nice <= -5) return ProcessPriority::AboveNormal;
    if (nice < 5) return ProcessPriority::Normal;
    if (nice < 15) return ProcessPriority::BelowNormal;
    return ProcessPriority::Idle;
}

int priorityEnumToNiceValue(ProcessPriority priority) {
    switch (priority) {
        case ProcessPriority::Realtime: return -20;
        case ProcessPriority::High: return -15;
        case ProcessPriority::AboveNormal: return -10;
        case ProcessPriority::Normal: return 0;
        case ProcessPriority::BelowNormal: return 10;
        case ProcessPriority::Idle: return 19;
        default: return 0;
    }
}

// Splits the tail of /proc/[pid]/stat that follows the comm field. That
// field is parenthesized and may itself contain spaces/parens, so we find
// the *last* ')' before splitting the remaining whitespace-separated
// fields (state, ppid, ..., utime, stime, ..., nice, num_threads, ...).
std::vector<std::string> statFieldsAfterComm(const std::string &statContents) {
    const auto closeParen = statContents.rfind(')');
    if (closeParen == std::string::npos) {
        return {};
    }
    std::istringstream stream(statContents.substr(closeParen + 1));
    std::vector<std::string> fields;
    std::string token;
    while (stream >> token) {
        fields.push_back(token);
    }
    return fields;
}

std::string extractComm(const std::string &statContents) {
    const auto openParen = statContents.find('(');
    const auto closeParen = statContents.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos ||
        closeParen <= openParen) {
        return {};
    }
    return statContents.substr(openParen + 1, closeParen - openParen - 1);
}

uint64_t vmRssBytesFromStatus(const std::string &statusContents) {
    std::istringstream stream(statusContents);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream lineStream(line.substr(6));
            uint64_t kilobytes = 0;
            lineStream >> kilobytes;
            return kilobytes * 1024;
        }
    }
    return 0;
}

uid_t uidFromStatus(const std::string &statusContents) {
    std::istringstream stream(statusContents);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("Uid:", 0) == 0) {
            std::istringstream lineStream(line.substr(4));
            uid_t uid = 0;
            lineStream >> uid;
            return uid;
        }
    }
    return static_cast<uid_t>(-1);
}

std::string threadStateToString(char state) {
    switch (state) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk sleep";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Tracing stop";
        case 'X':
        case 'x': return "Dead";
        case 'I': return "Idle";
        default: return std::string(1, state);
    }
}

struct MapsEntry {
    uint64_t start = 0;
    uint64_t end = 0;
    std::string perms;
    std::string path; // may be empty (anonymous mapping) or a pseudo-path like [heap]
};

std::vector<MapsEntry> parseMaps(const std::string &pid) {
    std::vector<MapsEntry> entries;
    std::ifstream file("/proc/" + pid + "/maps");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        std::string range, perms, offset, dev, inode;
        lineStream >> range >> perms >> offset >> dev >> inode;

        const auto dash = range.find('-');
        if (dash == std::string::npos) continue;

        MapsEntry entry;
        entry.start = std::stoull(range.substr(0, dash), nullptr, 16);
        entry.end = std::stoull(range.substr(dash + 1), nullptr, 16);
        entry.perms = perms;

        std::string rest;
        std::getline(lineStream, rest);
        const auto firstNonSpace = rest.find_first_not_of(' ');
        if (firstNonSpace != std::string::npos) {
            entry.path = rest.substr(firstNonSpace);
        }

        entries.push_back(std::move(entry));
    }
    return entries;
}

// Decodes a "IP:PORT" pair as it appears in /proc/net/{tcp,udp}[6]: both
// are hex, and the IPv4 address is stored in host-endian-reversed bytes
// (i.e. as the raw little-endian word), which inet_ntop expects verbatim
// on a little-endian machine.
bool decodeHexAddressPort(const std::string &field, bool isV6, std::string *address, uint16_t *port) {
    const auto colon = field.find(':');
    if (colon == std::string::npos) return false;

    const std::string addrHex = field.substr(0, colon);
    const std::string portHex = field.substr(colon + 1);
    *port = static_cast<uint16_t>(std::stoul(portHex, nullptr, 16));

    if (isV6) {
        if (addrHex.size() != 32) return false;
        unsigned char bytes[16];
        for (int i = 0; i < 16; ++i) {
            // Each 4-byte little-endian word is stored in order; reverse
            // within each 32-bit chunk, matching the kernel's in6_addr dump.
            const int chunk = i / 4;
            const int byteInChunk = i % 4;
            const int srcIndex = chunk * 4 + (3 - byteInChunk);
            bytes[i] = static_cast<unsigned char>(
                std::stoul(addrHex.substr(srcIndex * 2, 2), nullptr, 16));
        }
        char buf[INET6_ADDRSTRLEN] = {};
        inet_ntop(AF_INET6, bytes, buf, sizeof(buf));
        *address = buf;
    } else {
        if (addrHex.size() != 8) return false;
        uint32_t addr = static_cast<uint32_t>(std::stoul(addrHex, nullptr, 16));
        struct in_addr inAddr;
        inAddr.s_addr = addr; // already in the correct wire byte order
        char buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &inAddr, buf, sizeof(buf));
        *address = buf;
    }
    return true;
}

std::string tcpStateToString(int state) {
    switch (state) {
        case 0x01: return "ESTABLISHED";
        case 0x02: return "SYN_SENT";
        case 0x03: return "SYN_RECV";
        case 0x04: return "FIN_WAIT1";
        case 0x05: return "FIN_WAIT2";
        case 0x06: return "TIME_WAIT";
        case 0x07: return "CLOSE";
        case 0x08: return "CLOSE_WAIT";
        case 0x09: return "LAST_ACK";
        case 0x0A: return "LISTEN";
        case 0x0B: return "CLOSING";
        default: return "UNKNOWN";
    }
}

// Scans one /proc/net/{tcp,udp}[6] table and appends connections whose
// socket inode is in `inodesOfInterest` (the fds this process holds).
void collectNetworkTable(const std::string &path, core::NetworkProtocol protocol, bool isV6,
                          bool hasState, const std::unordered_map<uint64_t, bool> &inodesOfInterest,
                          std::vector<core::NetworkConnectionInfo> *out) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line)) {
        std::istringstream lineStream(line);
        std::string slot, local, remote, stateHex, extra;
        lineStream >> slot >> local >> remote >> stateHex;

        // Skip to the "inode" column: tx_queue:rx_queue tr:tm->when retrnsmt
        // uid timeout inode ...
        std::string field;
        std::vector<std::string> rest;
        while (lineStream >> field) rest.push_back(field);
        if (rest.size() < 6) continue;
        const uint64_t inode = std::stoull(rest[5]);

        if (inodesOfInterest.find(inode) == inodesOfInterest.end()) continue;

        core::NetworkConnectionInfo info;
        info.protocol = protocol;
        if (hasState) {
            info.state = tcpStateToString(static_cast<int>(std::stoul(stateHex, nullptr, 16)));
        }
        decodeHexAddressPort(local, isV6, &info.localAddress, &info.localPort);
        decodeHexAddressPort(remote, isV6, &info.remoteAddress, &info.remotePort);
        out->push_back(std::move(info));
    }
}

} // namespace

ProcessProviderLinux::ProcessProviderLinux() {
    const long ticks = sysconf(_SC_CLK_TCK);
    clockTicksPerSecond_ = ticks > 0 ? ticks : 100;

    const long processors = sysconf(_SC_NPROCESSORS_ONLN);
    processorCount_ = processors > 0 ? processors : 1;
}

double ProcessProviderLinux::computeCpuPercent(uint64_t pid, uint64_t totalTimeTicks) {
    const auto now = std::chrono::steady_clock::now();
    auto it = previousSamples_.find(pid);

    double percent = 0.0;
    if (it != previousSamples_.end()) {
        const double elapsedSeconds =
            std::chrono::duration<double>(now - it->second.wallClock).count();
        if (elapsedSeconds > 0.0) {
            const double cpuSeconds =
                static_cast<double>(totalTimeTicks - it->second.totalTimeTicks) /
                static_cast<double>(clockTicksPerSecond_);
            percent = (cpuSeconds / elapsedSeconds) * 100.0 / static_cast<double>(processorCount_);
        }
    }

    previousSamples_[pid] = CpuSample{totalTimeTicks, now};
    return percent < 0.0 ? 0.0 : percent;
}

std::vector<ProcessInfo> ProcessProviderLinux::snapshot() {
    std::vector<ProcessInfo> result;

    DIR *procDir = opendir("/proc");
    if (procDir == nullptr) {
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(procDir)) != nullptr) {
        const std::string dirName = entry->d_name;
        if (!isAllDigits(dirName)) {
            continue;
        }

        const uint64_t pid = std::stoull(dirName);
        const std::string base = "/proc/" + dirName;

        const std::string statContents = readFile(base + "/stat");
        if (statContents.empty()) {
            continue; // process exited between readdir and read
        }

        const auto fields = statFieldsAfterComm(statContents);
        // fields[0]=state fields[1]=ppid ... fields[11]=utime fields[12]=stime
        // fields[16]=nice fields[17]=num_threads
        if (fields.size() < 18) {
            continue;
        }

        ProcessInfo info;
        info.pid = pid;
        info.ppid = std::stoull(fields[1]);
        info.name = extractComm(statContents);
        info.threadCount = std::stoull(fields[17]);

        const uint64_t utime = std::stoull(fields[11]);
        const uint64_t stime = std::stoull(fields[12]);
        info.cpuPercent = computeCpuPercent(pid, utime + stime);
        info.priority = niceValueToEnum(std::stoi(fields[16]));

        const std::string statusContents = readFile(base + "/status");
        info.privateBytes = vmRssBytesFromStatus(statusContents);
        const uid_t uid = uidFromStatus(statusContents);
        if (uid != static_cast<uid_t>(-1)) {
            info.user = usernameForUid(uid);
        }

        std::vector<char> exeBuffer(4096);
        const ssize_t len = readlink((base + "/exe").c_str(), exeBuffer.data(), exeBuffer.size() - 1);
        if (len > 0) {
            info.exePath.assign(exeBuffer.data(), static_cast<size_t>(len));
        }

        result.push_back(std::move(info));
    }

    closedir(procDir);
    return result;
}

bool ProcessProviderLinux::terminate(uint64_t pid) {
    return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool ProcessProviderLinux::setPriority(uint64_t pid, ProcessPriority priority) {
    return setpriority(PRIO_PROCESS, static_cast<id_t>(pid), priorityEnumToNiceValue(priority)) == 0;
}

bool ProcessProviderLinux::suspend(uint64_t pid) {
    return kill(static_cast<pid_t>(pid), SIGSTOP) == 0;
}

bool ProcessProviderLinux::resume(uint64_t pid) {
    return kill(static_cast<pid_t>(pid), SIGCONT) == 0;
}

std::vector<ThreadInfo> ProcessProviderLinux::threads(uint64_t pid) {
    std::vector<ThreadInfo> result;

    const std::string taskDir = "/proc/" + std::to_string(pid) + "/task";
    DIR *dir = opendir(taskDir.c_str());
    if (dir == nullptr) {
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string tidName = entry->d_name;
        if (!isAllDigits(tidName)) continue;

        const std::string statContents = readFile(taskDir + "/" + tidName + "/stat");
        if (statContents.empty()) continue;

        const auto fields = statFieldsAfterComm(statContents);
        if (fields.size() < 18) continue;

        ThreadInfo info;
        info.tid = std::stoull(tidName);
        info.state = threadStateToString(fields[0].empty() ? '?' : fields[0][0]);
        info.priority = std::stoi(fields[15]);
        result.push_back(std::move(info));
    }

    closedir(dir);
    return result;
}

std::vector<ModuleInfo> ProcessProviderLinux::modules(uint64_t pid) {
    std::vector<ModuleInfo> result;
    std::unordered_map<std::string, size_t> indexByPath;

    for (const auto &entry : parseMaps(std::to_string(pid))) {
        if (entry.path.empty() || entry.path[0] == '[') {
            continue; // anonymous mapping or pseudo-region like [heap]/[stack]
        }

        auto it = indexByPath.find(entry.path);
        if (it == indexByPath.end()) {
            ModuleInfo module;
            const auto slash = entry.path.find_last_of('/');
            module.name = slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
            module.path = entry.path;
            module.baseAddress = entry.start;
            module.sizeBytes = entry.end - entry.start;
            indexByPath[entry.path] = result.size();
            result.push_back(std::move(module));
        } else {
            ModuleInfo &module = result[it->second];
            module.baseAddress = std::min(module.baseAddress, entry.start);
            const uint64_t newEnd = std::max(module.baseAddress + module.sizeBytes, entry.end);
            module.sizeBytes = newEnd - module.baseAddress;
        }
    }

    return result;
}

std::vector<MemoryRegionInfo> ProcessProviderLinux::memoryRegions(uint64_t pid) {
    std::vector<MemoryRegionInfo> result;
    for (const auto &entry : parseMaps(std::to_string(pid))) {
        MemoryRegionInfo region;
        region.baseAddress = entry.start;
        region.sizeBytes = entry.end - entry.start;
        region.protection = entry.perms;
        if (!entry.path.empty() && entry.path[0] != '[') {
            region.mappedFile = entry.path;
        }
        result.push_back(std::move(region));
    }
    return result;
}

std::vector<HandleInfo> ProcessProviderLinux::handles(uint64_t pid) {
    std::vector<HandleInfo> result;

    const std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR *dir = opendir(fdDir.c_str());
    if (dir == nullptr) {
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fdName = entry->d_name;
        if (!isAllDigits(fdName)) continue;

        char target[4096];
        const ssize_t len = readlink((fdDir + "/" + fdName).c_str(), target, sizeof(target) - 1);
        if (len <= 0) continue;

        HandleInfo info;
        info.handleValueOrFd = std::stoull(fdName);
        info.name.assign(target, static_cast<size_t>(len));

        if (info.name.rfind("socket:", 0) == 0) {
            info.type = "Socket";
        } else if (info.name.rfind("pipe:", 0) == 0) {
            info.type = "Pipe";
        } else if (info.name.rfind("anon_inode:", 0) == 0) {
            info.type = "Anonymous";
        } else {
            struct stat st;
            if (stat(info.name.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                info.type = "Directory";
            } else {
                info.type = "File";
            }
        }

        result.push_back(std::move(info));
    }

    closedir(dir);
    return result;
}

std::vector<std::string> ProcessProviderLinux::environment(uint64_t pid) {
    std::vector<std::string> result;
    const std::string contents = readFile("/proc/" + std::to_string(pid) + "/environ");

    size_t start = 0;
    for (size_t i = 0; i < contents.size(); ++i) {
        if (contents[i] == '\0') {
            if (i > start) {
                result.push_back(contents.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return result;
}

std::vector<NetworkConnectionInfo> ProcessProviderLinux::networkConnections(uint64_t pid) {
    std::vector<NetworkConnectionInfo> result;

    // Collect the socket inodes this process actually holds open, so we
    // only report connections that belong to it (the /proc/net/* tables
    // are system-wide).
    std::unordered_map<uint64_t, bool> ownInodes;
    const std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR *dir = opendir(fdDir.c_str());
    if (dir == nullptr) {
        return result;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fdName = entry->d_name;
        if (!isAllDigits(fdName)) continue;
        char target[256];
        const ssize_t len = readlink((fdDir + "/" + fdName).c_str(), target, sizeof(target) - 1);
        if (len <= 0) continue;
        std::string linkTarget(target, static_cast<size_t>(len));
        if (linkTarget.rfind("socket:[", 0) == 0) {
            const uint64_t inode =
                std::stoull(linkTarget.substr(8, linkTarget.size() - 9));
            ownInodes[inode] = true;
        }
    }
    closedir(dir);

    if (ownInodes.empty()) {
        return result;
    }

    collectNetworkTable("/proc/net/tcp", NetworkProtocol::Tcp, false, true, ownInodes, &result);
    collectNetworkTable("/proc/net/tcp6", NetworkProtocol::Tcp6, true, true, ownInodes, &result);
    collectNetworkTable("/proc/net/udp", NetworkProtocol::Udp, false, false, ownInodes, &result);
    collectNetworkTable("/proc/net/udp6", NetworkProtocol::Udp6, true, false, ownInodes, &result);

    return result;
}

} // namespace core
