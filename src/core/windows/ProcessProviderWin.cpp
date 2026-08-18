#include "ProcessProviderWin.h"

#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <array>
#include <cwchar>

namespace core {

namespace {

std::string wideToUtf8(const std::wstring &wide) {
    if (wide.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                    nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(),
                         size, nullptr, nullptr);
    return result;
}

uint64_t fileTimeTo100ns(const FILETIME &ft) {
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

ProcessPriority priorityClassToEnum(DWORD priorityClass) {
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS: return ProcessPriority::Idle;
        case BELOW_NORMAL_PRIORITY_CLASS: return ProcessPriority::BelowNormal;
        case NORMAL_PRIORITY_CLASS: return ProcessPriority::Normal;
        case ABOVE_NORMAL_PRIORITY_CLASS: return ProcessPriority::AboveNormal;
        case HIGH_PRIORITY_CLASS: return ProcessPriority::High;
        case REALTIME_PRIORITY_CLASS: return ProcessPriority::Realtime;
        default: return ProcessPriority::Unknown;
    }
}

DWORD priorityEnumToClass(ProcessPriority priority) {
    switch (priority) {
        case ProcessPriority::Idle: return IDLE_PRIORITY_CLASS;
        case ProcessPriority::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
        case ProcessPriority::Normal: return NORMAL_PRIORITY_CLASS;
        case ProcessPriority::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
        case ProcessPriority::High: return HIGH_PRIORITY_CLASS;
        case ProcessPriority::Realtime: return REALTIME_PRIORITY_CLASS;
        default: return NORMAL_PRIORITY_CLASS;
    }
}

std::string lookupProcessOwner(HANDLE process) {
    HANDLE tokenHandle = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &tokenHandle)) {
        return {};
    }

    DWORD infoSize = 0;
    GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &infoSize);
    if (infoSize == 0) {
        CloseHandle(tokenHandle);
        return {};
    }

    std::vector<BYTE> buffer(infoSize);
    std::string owner;
    if (GetTokenInformation(tokenHandle, TokenUser, buffer.data(), infoSize, &infoSize)) {
        auto *tokenUser = reinterpret_cast<TOKEN_USER *>(buffer.data());

        wchar_t name[256];
        wchar_t domain[256];
        DWORD nameSize = 256;
        DWORD domainSize = 256;
        SID_NAME_USE sidType;
        if (LookupAccountSidW(nullptr, tokenUser->User.Sid, name, &nameSize, domain,
                               &domainSize, &sidType)) {
            owner = wideToUtf8(domain) + "\\" + wideToUtf8(name);
        }
    }

    CloseHandle(tokenHandle);
    return owner;
}

std::string queryImagePath(HANDLE process) {
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        return wideToUtf8(std::wstring(path, size));
    }
    return {};
}

} // namespace

ProcessProviderWin::ProcessProviderWin() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    processorCount_ = sysInfo.dwNumberOfProcessors > 0 ? sysInfo.dwNumberOfProcessors : 1;
    lastSnapshotTime_ = std::chrono::steady_clock::now();
}

double ProcessProviderWin::computeCpuPercent(uint64_t pid, uint64_t kernelAndUserTime100ns) {
    const auto now = std::chrono::steady_clock::now();
    auto it = previousSamples_.find(pid);

    double percent = 0.0;
    if (it != previousSamples_.end()) {
        const double elapsedSeconds =
            std::chrono::duration<double>(now - it->second.wallClock).count();
        if (elapsedSeconds > 0.0) {
            const double cpuSeconds =
                static_cast<double>(kernelAndUserTime100ns - it->second.kernelAndUserTime100ns) /
                1e7;
            percent = (cpuSeconds / elapsedSeconds) * 100.0 / static_cast<double>(processorCount_);
        }
    }

    previousSamples_[pid] = CpuSample{kernelAndUserTime100ns, now};
    return percent < 0.0 ? 0.0 : percent;
}

std::vector<ProcessInfo> ProcessProviderWin::snapshot() {
    std::vector<ProcessInfo> result;

    HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshotHandle == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshotHandle, &entry)) {
        do {
            ProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.ppid = entry.th32ParentProcessID;
            info.name = wideToUtf8(entry.szExeFile);
            info.threadCount = entry.cntThreads;

            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
            if (process != nullptr) {
                info.exePath = queryImagePath(process);
                info.user = lookupProcessOwner(process);

                PROCESS_MEMORY_COUNTERS_EX memCounters;
                if (GetProcessMemoryInfo(process,
                                          reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memCounters),
                                          sizeof(memCounters))) {
                    info.privateBytes = memCounters.PrivateUsage;
                }

                FILETIME creationTime, exitTime, kernelTime, userTime;
                if (GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime)) {
                    const uint64_t totalTime100ns =
                        fileTimeTo100ns(kernelTime) + fileTimeTo100ns(userTime);
                    info.cpuPercent = computeCpuPercent(info.pid, totalTime100ns);
                }

                DWORD priorityClass = GetPriorityClass(process);
                info.priority = priorityClassToEnum(priorityClass);

                CloseHandle(process);
            }

            result.push_back(std::move(info));
        } while (Process32NextW(snapshotHandle, &entry));
    }

    CloseHandle(snapshotHandle);
    lastSnapshotTime_ = std::chrono::steady_clock::now();
    return result;
}

bool ProcessProviderWin::terminate(uint64_t pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool ok = TerminateProcess(process, 1) != 0;
    CloseHandle(process);
    return ok;
}

bool ProcessProviderWin::setPriority(uint64_t pid, ProcessPriority priority) {
    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool ok = SetPriorityClass(process, priorityEnumToClass(priority)) != 0;
    CloseHandle(process);
    return ok;
}

} // namespace core
