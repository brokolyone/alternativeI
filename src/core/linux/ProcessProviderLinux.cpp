#include "ProcessProviderLinux.h"

#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

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

} // namespace core
