#include "ProcessProviderWin.h"

// windows.h pulls in the legacy winsock.h unless told not to, which then
// conflicts (duplicate/incompatible struct and function declarations)
// with winsock2.h-family headers like iphlpapi.h/ws2tcpip.h below.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <cwchar>
#include <vector>

namespace core {

namespace {

// --- NtQuerySystemInformation(SystemHandleInformation) ---------------------
// Undocumented but stable since XP; this is the same technique Process
// Hacker itself uses for user-mode handle enumeration. Not declared in any
// public SDK header, so we declare the pieces we need ourselves.

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO {
    ULONG ProcessId;
    UCHAR ObjectTypeNumber;
    UCHAR Flags;
    USHORT Handle;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG NumberOfHandles;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[1];
} SYSTEM_HANDLE_INFORMATION;

constexpr ULONG SystemHandleInformationClass = 16;

typedef NTSTATUS(NTAPI *NtQuerySystemInformationFn)(ULONG SystemInformationClass,
                                                      PVOID SystemInformation,
                                                      ULONG SystemInformationLength,
                                                      PULONG ReturnLength);

NtQuerySystemInformationFn resolveNtQuerySystemInformation() {
    static NtQuerySystemInformationFn fn = reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    return fn;
}

// Locally-declared mirror of PUBLIC_OBJECT_TYPE_INFORMATION so we don't
// depend on it being present in whatever winternl.h ships with the build
// toolchain - only UNICODE_STRING (which is reliably there) is needed.
typedef struct _LOCAL_OBJECT_TYPE_INFORMATION {
    UNICODE_STRING TypeName;
    ULONG Reserved[22];
} LOCAL_OBJECT_TYPE_INFORMATION;

constexpr ULONG ObjectTypeInformationClass = 2;

typedef NTSTATUS(NTAPI *NtQueryObjectFn)(HANDLE Handle, ULONG ObjectInformationClass,
                                          PVOID ObjectInformation, ULONG ObjectInformationLength,
                                          PULONG ReturnLength);

NtQueryObjectFn resolveNtQueryObject() {
    static NtQueryObjectFn fn = reinterpret_cast<NtQueryObjectFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    return fn;
}

// --- NtSuspendProcess/NtResumeProcess ---------------------------------
// Undocumented but stable since XP (same pair Process Hacker uses for its
// Suspend/Resume actions); not declared in any public SDK header.

typedef NTSTATUS(NTAPI *NtSuspendProcessFn)(HANDLE ProcessHandle);
typedef NTSTATUS(NTAPI *NtResumeProcessFn)(HANDLE ProcessHandle);

NtSuspendProcessFn resolveNtSuspendProcess() {
    static NtSuspendProcessFn fn = reinterpret_cast<NtSuspendProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess"));
    return fn;
}

NtResumeProcessFn resolveNtResumeProcess() {
    static NtResumeProcessFn fn = reinterpret_cast<NtResumeProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess"));
    return fn;
}

std::string tcpStateToStringWin(DWORD state) {
    switch (state) {
        case 1: return "CLOSED";
        case 2: return "LISTEN";
        case 3: return "SYN_SENT";
        case 4: return "SYN_RCVD";
        case 5: return "ESTABLISHED";
        case 6: return "FIN_WAIT1";
        case 7: return "FIN_WAIT2";
        case 8: return "CLOSE_WAIT";
        case 9: return "CLOSING";
        case 10: return "LAST_ACK";
        case 11: return "TIME_WAIT";
        case 12: return "DELETE_TCB";
        default: return "UNKNOWN";
    }
}

// Locally-declared mirrors of MIB_TCP6TABLE_OWNER_PID/MIB_UDP6TABLE_OWNER_PID
// (documented, ABI-stable since Vista) rather than relying on the SDK to
// expose them under those names - the toolchain used for CI builds
// doesn't declare them via <iphlpapi.h> even with _WIN32_WINNT set high
// enough, and chasing the exact header/macro combination that would
// expose the SDK's own versions isn't worth it when the layout is public
// and fixed. Named distinctly to avoid clashing with anything the SDK
// does declare.
typedef struct _LOCAL_MIB_TCP6ROW_OWNER_PID {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    UCHAR ucRemoteAddr[16];
    DWORD dwRemoteScopeId;
    DWORD dwRemotePort;
    DWORD dwState;
    DWORD dwOwningPid;
} LOCAL_MIB_TCP6ROW_OWNER_PID;

typedef struct _LOCAL_MIB_TCP6TABLE_OWNER_PID {
    DWORD dwNumEntries;
    LOCAL_MIB_TCP6ROW_OWNER_PID table[1];
} LOCAL_MIB_TCP6TABLE_OWNER_PID;

typedef struct _LOCAL_MIB_UDP6ROW_OWNER_PID {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    DWORD dwOwningPid;
} LOCAL_MIB_UDP6ROW_OWNER_PID;

typedef struct _LOCAL_MIB_UDP6TABLE_OWNER_PID {
    DWORD dwNumEntries;
    LOCAL_MIB_UDP6ROW_OWNER_PID table[1];
} LOCAL_MIB_UDP6TABLE_OWNER_PID;

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

bool ProcessProviderWin::suspend(uint64_t pid) {
    auto ntSuspendProcess = resolveNtSuspendProcess();
    if (ntSuspendProcess == nullptr) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool ok = ntSuspendProcess(process) >= 0; // NT_SUCCESS: high bit of NTSTATUS clear
    CloseHandle(process);
    return ok;
}

bool ProcessProviderWin::resume(uint64_t pid) {
    auto ntResumeProcess = resolveNtResumeProcess();
    if (ntResumeProcess == nullptr) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool ok = ntResumeProcess(process) >= 0;
    CloseHandle(process);
    return ok;
}

uint64_t ProcessProviderWin::affinityMask(uint64_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return 0;
    }
    DWORD_PTR processMask = 0;
    DWORD_PTR systemMask = 0;
    const bool ok = GetProcessAffinityMask(process, &processMask, &systemMask) != 0;
    CloseHandle(process);
    return ok ? static_cast<uint64_t>(processMask) : 0;
}

bool ProcessProviderWin::setAffinityMask(uint64_t pid, uint64_t mask) {
    HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool ok = SetProcessAffinityMask(process, static_cast<DWORD_PTR>(mask)) != 0;
    CloseHandle(process);
    return ok;
}

std::vector<ThreadInfo> ProcessProviderWin::threads(uint64_t pid) {
    std::vector<ThreadInfo> result;

    HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshotHandle == INVALID_HANDLE_VALUE) {
        return result;
    }

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshotHandle, &entry)) {
        do {
            if (entry.th32OwnerProcessID != static_cast<DWORD>(pid)) {
                continue;
            }

            ThreadInfo info;
            info.tid = entry.th32ThreadID;
            info.priority = entry.tpBasePri;
            // Toolhelp32 doesn't expose scheduling state; a real value
            // needs NtQuerySystemInformation(SystemProcessInformation)'s
            // per-thread SYSTEM_THREAD_INFORMATION.ThreadState, which is a
            // large variable-length structure not worth hand-declaring
            // blind in a build we can't test here.
            info.state = "Unknown";

            result.push_back(std::move(info));
        } while (Thread32Next(snapshotHandle, &entry));
    }

    CloseHandle(snapshotHandle);
    return result;
}

std::vector<ModuleInfo> ProcessProviderWin::modules(uint64_t pid) {
    std::vector<ModuleInfo> result;

    HANDLE process =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return result;
    }

    std::vector<HMODULE> moduleHandles(256);
    DWORD needed = 0;
    for (;;) {
        if (!EnumProcessModulesEx(process, moduleHandles.data(),
                                   static_cast<DWORD>(moduleHandles.size() * sizeof(HMODULE)), &needed,
                                   LIST_MODULES_ALL)) {
            CloseHandle(process);
            return result;
        }
        const size_t count = needed / sizeof(HMODULE);
        if (count <= moduleHandles.size()) {
            moduleHandles.resize(count);
            break;
        }
        moduleHandles.resize(count);
    }

    for (HMODULE mod : moduleHandles) {
        wchar_t pathBuf[MAX_PATH] = {};
        MODULEINFO info{};
        if (GetModuleFileNameExW(process, mod, pathBuf, MAX_PATH) &&
            GetModuleInformation(process, mod, &info, sizeof(info))) {
            ModuleInfo module;
            module.path = wideToUtf8(pathBuf);
            const auto slash = module.path.find_last_of('\\');
            module.name = slash == std::string::npos ? module.path : module.path.substr(slash + 1);
            module.baseAddress = reinterpret_cast<uint64_t>(info.lpBaseOfDll);
            module.sizeBytes = info.SizeOfImage;
            result.push_back(std::move(module));
        }
    }

    CloseHandle(process);
    return result;
}

std::vector<MemoryRegionInfo> ProcessProviderWin::memoryRegions(uint64_t pid) {
    std::vector<MemoryRegionInfo> result;

    HANDLE process =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return result;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    auto *address = static_cast<unsigned char *>(sysInfo.lpMinimumApplicationAddress);
    auto *maxAddress = static_cast<unsigned char *>(sysInfo.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi;
    while (address < maxAddress && VirtualQueryEx(process, address, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT) {
            MemoryRegionInfo region;
            region.baseAddress = reinterpret_cast<uint64_t>(mbi.BaseAddress);
            region.sizeBytes = mbi.RegionSize;

            const DWORD p = mbi.Protect;
            std::string prot;
            prot += (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))
                        ? 'r'
                        : '-';
            prot += (p & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
                        ? 'w'
                        : '-';
            prot += (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                        ? 'x'
                        : '-';
            region.protection = prot;

            if (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED) {
                wchar_t fileName[MAX_PATH] = {};
                if (GetMappedFileNameW(process, mbi.BaseAddress, fileName, MAX_PATH) > 0) {
                    region.mappedFile = wideToUtf8(fileName);
                }
            }

            result.push_back(std::move(region));
        }

        auto *next = static_cast<unsigned char *>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address) {
            break; // guard against a zero-size region wedging the loop
        }
        address = next;
    }

    CloseHandle(process);
    return result;
}

std::vector<HandleInfo> ProcessProviderWin::handles(uint64_t pid) {
    std::vector<HandleInfo> result;

    auto ntQuerySystemInformation = resolveNtQuerySystemInformation();
    if (ntQuerySystemInformation == nullptr) {
        return result;
    }

    std::vector<unsigned char> buffer(1 << 20); // 1 MiB initial guess
    for (;;) {
        ULONG returnLength = 0;
        const NTSTATUS status = ntQuerySystemInformation(
            SystemHandleInformationClass, buffer.data(), static_cast<ULONG>(buffer.size()), &returnLength);
        if (status == 0) {
            break; // STATUS_SUCCESS
        }
        if (status == static_cast<NTSTATUS>(0xC0000004L)) { // STATUS_INFO_LENGTH_MISMATCH
            buffer.resize(buffer.size() * 2);
            continue;
        }
        return result; // unexpected failure
    }

    auto *info = reinterpret_cast<SYSTEM_HANDLE_INFORMATION *>(buffer.data());
    auto ntQueryObject = resolveNtQueryObject();
    HANDLE targetProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, static_cast<DWORD>(pid));

    for (ULONG i = 0; i < info->NumberOfHandles; ++i) {
        const auto &entry = info->Handles[i];
        if (entry.ProcessId != static_cast<DWORD>(pid)) {
            continue;
        }

        HandleInfo handleInfo;
        handleInfo.handleValueOrFd = entry.Handle;

        if (targetProcess != nullptr && ntQueryObject != nullptr) {
            HANDLE dup = nullptr;
            if (DuplicateHandle(targetProcess,
                                 reinterpret_cast<HANDLE>(static_cast<uintptr_t>(entry.Handle)),
                                 GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                unsigned char typeBuffer[1024];
                ULONG returnLength = 0;
                if (ntQueryObject(dup, ObjectTypeInformationClass, typeBuffer, sizeof(typeBuffer),
                                   &returnLength) == 0) {
                    auto *typeInfo = reinterpret_cast<LOCAL_OBJECT_TYPE_INFORMATION *>(typeBuffer);
                    if (typeInfo->TypeName.Buffer != nullptr) {
                        handleInfo.type = wideToUtf8(std::wstring(
                            typeInfo->TypeName.Buffer, typeInfo->TypeName.Length / sizeof(wchar_t)));
                    }
                }
                CloseHandle(dup);
            }
        }

        result.push_back(std::move(handleInfo));
    }

    if (targetProcess != nullptr) {
        CloseHandle(targetProcess);
    }
    return result;
}

std::vector<std::string> ProcessProviderWin::environment(uint64_t pid) {
    std::vector<std::string> result;

    HANDLE process =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return result;
    }

    auto ntQueryInformationProcess = reinterpret_cast<NTSTATUS(NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (ntQueryInformationProcess == nullptr) {
        CloseHandle(process);
        return result;
    }

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG returned = 0;
    if (ntQueryInformationProcess(process, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &returned) != 0) {
        CloseHandle(process);
        return result;
    }

    // Native-bitness PEB layout only (matches a 64-bit build inspecting
    // another 64-bit process): ProcessParameters at PEB+0x20, and
    // RTL_USER_PROCESS_PARAMETERS.Environment at +0x80 within that.
    // A WOW64 target (32-bit process on 64-bit Windows) uses the 32-bit
    // PEB layout instead and isn't handled here.
    auto *pebProcessParamsPtr = reinterpret_cast<BYTE *>(pbi.PebBaseAddress) + 0x20;
    PVOID processParameters = nullptr;
    if (!ReadProcessMemory(process, pebProcessParamsPtr, &processParameters, sizeof(processParameters),
                            nullptr)) {
        CloseHandle(process);
        return result;
    }

    auto *environmentPtrAddress = static_cast<BYTE *>(processParameters) + 0x80;
    PVOID environmentBlock = nullptr;
    if (!ReadProcessMemory(process, environmentPtrAddress, &environmentBlock, sizeof(environmentBlock),
                            nullptr)) {
        CloseHandle(process);
        return result;
    }

    constexpr SIZE_T maxEnvBytes = 64 * 1024;
    std::vector<wchar_t> envBuffer(maxEnvBytes / sizeof(wchar_t));
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(process, environmentBlock, envBuffer.data(), maxEnvBytes, &bytesRead)) {
        CloseHandle(process);
        return result;
    }

    size_t start = 0;
    const size_t count = bytesRead / sizeof(wchar_t);
    for (size_t i = 0; i < count; ++i) {
        if (envBuffer[i] == L'\0') {
            if (i > start) {
                result.push_back(wideToUtf8(std::wstring(envBuffer.data() + start, i - start)));
                start = i + 1;
            } else {
                break; // double-NUL terminator
            }
        }
    }

    CloseHandle(process);
    return result;
}

std::vector<NetworkConnectionInfo> ProcessProviderWin::networkConnections(uint64_t pid) {
    std::vector<NetworkConnectionInfo> result;
    const DWORD targetPid = static_cast<DWORD>(pid);

    {
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<unsigned char> buffer(size);
        if (size > 0 &&
            GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) ==
                NO_ERROR) {
            auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (row.dwOwningPid != targetPid) continue;

                NetworkConnectionInfo info;
                info.protocol = NetworkProtocol::Tcp;
                char addrBuf[INET_ADDRSTRLEN];
                in_addr local{};
                local.s_addr = row.dwLocalAddr;
                inet_ntop(AF_INET, &local, addrBuf, sizeof(addrBuf));
                info.localAddress = addrBuf;
                info.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
                in_addr remote{};
                remote.s_addr = row.dwRemoteAddr;
                inet_ntop(AF_INET, &remote, addrBuf, sizeof(addrBuf));
                info.remoteAddress = addrBuf;
                info.remotePort = ntohs(static_cast<u_short>(row.dwRemotePort));
                info.state = tcpStateToStringWin(row.dwState);
                result.push_back(std::move(info));
            }
        }
    }

    {
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<unsigned char> buffer(size);
        if (size > 0 &&
            GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) ==
                NO_ERROR) {
            auto *table = reinterpret_cast<LOCAL_MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (row.dwOwningPid != targetPid) continue;

                NetworkConnectionInfo info;
                info.protocol = NetworkProtocol::Tcp6;
                char addrBuf[INET6_ADDRSTRLEN];
                in6_addr local{};
                std::memcpy(&local, row.ucLocalAddr, sizeof(local));
                inet_ntop(AF_INET6, &local, addrBuf, sizeof(addrBuf));
                info.localAddress = addrBuf;
                info.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
                in6_addr remote{};
                std::memcpy(&remote, row.ucRemoteAddr, sizeof(remote));
                inet_ntop(AF_INET6, &remote, addrBuf, sizeof(addrBuf));
                info.remoteAddress = addrBuf;
                info.remotePort = ntohs(static_cast<u_short>(row.dwRemotePort));
                info.state = tcpStateToStringWin(row.dwState);
                result.push_back(std::move(info));
            }
        }
    }

    {
        ULONG size = 0;
        GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        std::vector<unsigned char> buffer(size);
        if (size > 0 &&
            GetExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            auto *table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (row.dwOwningPid != targetPid) continue;

                NetworkConnectionInfo info;
                info.protocol = NetworkProtocol::Udp;
                char addrBuf[INET_ADDRSTRLEN];
                in_addr local{};
                local.s_addr = row.dwLocalAddr;
                inet_ntop(AF_INET, &local, addrBuf, sizeof(addrBuf));
                info.localAddress = addrBuf;
                info.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
                result.push_back(std::move(info));
            }
        }
    }

    {
        ULONG size = 0;
        GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
        std::vector<unsigned char> buffer(size);
        if (size > 0 &&
            GetExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            auto *table = reinterpret_cast<LOCAL_MIB_UDP6TABLE_OWNER_PID *>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto &row = table->table[i];
                if (row.dwOwningPid != targetPid) continue;

                NetworkConnectionInfo info;
                info.protocol = NetworkProtocol::Udp6;
                char addrBuf[INET6_ADDRSTRLEN];
                in6_addr local{};
                std::memcpy(&local, row.ucLocalAddr, sizeof(local));
                inet_ntop(AF_INET6, &local, addrBuf, sizeof(addrBuf));
                info.localAddress = addrBuf;
                info.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
                result.push_back(std::move(info));
            }
        }
    }

    return result;
}

} // namespace core
