#include "ServiceManagerLinux.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace core {

namespace {

// Runs a command via popen and returns its stdout. No shell interpolation
// of caller-controlled data happens here - every call site below passes a
// fixed, literal command string.
std::string runAndCapture(const char *command) {
    std::string output;
    std::array<char, 4096> buffer;
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) {
        return output;
    }
    size_t n;
    while ((n = fread(buffer.data(), 1, buffer.size(), pipe)) > 0) {
        output.append(buffer.data(), n);
    }
    pclose(pipe);
    return output;
}

std::unordered_map<std::string, std::string> loadUnitFileStates() {
    std::unordered_map<std::string, std::string> states;
    const std::string output =
        runAndCapture("systemctl list-unit-files --type=service --no-legend --no-pager 2>/dev/null");

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string unit, state;
        lineStream >> unit >> state;
        if (!unit.empty()) {
            states[unit] = state;
        }
    }
    return states;
}

} // namespace

std::vector<ServiceInfo> ServiceManagerLinux::list() {
    std::vector<ServiceInfo> result;

    const auto unitFileStates = loadUnitFileStates();
    const std::string output = runAndCapture(
        "systemctl list-units --type=service --all --no-legend --no-pager --plain 2>/dev/null");

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string unit, load, active, sub;
        lineStream >> unit >> load >> active >> sub;
        if (unit.empty()) continue;

        std::string description;
        std::getline(lineStream, description);
        const auto firstNonSpace = description.find_first_not_of(' ');
        if (firstNonSpace != std::string::npos) {
            description = description.substr(firstNonSpace);
        } else {
            description.clear();
        }

        ServiceInfo info;
        info.name = unit;
        info.displayName = description.empty() ? unit : description;
        info.state = sub.empty() ? active : sub; // e.g. "running", "dead", "failed"

        auto it = unitFileStates.find(unit);
        info.startType = it != unitFileStates.end() ? it->second : "";

        result.push_back(std::move(info));
    }

    return result;
}

bool ServiceManagerLinux::runSystemctl(const std::string &action, const std::string &unitName) {
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // No shell involved, so unitName can't be used for command
        // injection regardless of its contents.
        execlp("systemctl", "systemctl", action.c_str(), unitName.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool ServiceManagerLinux::start(const std::string &name) { return runSystemctl("start", name); }
bool ServiceManagerLinux::stop(const std::string &name) { return runSystemctl("stop", name); }
bool ServiceManagerLinux::restart(const std::string &name) { return runSystemctl("restart", name); }

} // namespace core
