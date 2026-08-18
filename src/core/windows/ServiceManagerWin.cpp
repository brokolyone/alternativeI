#include "ServiceManagerWin.h"

#include <windows.h>

#include <vector>

namespace core {

namespace {

std::string wideToUtf8(const wchar_t *wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int len = static_cast<int>(wcslen(wide));
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, len, result.data(), size, nullptr, nullptr);
    return result;
}

std::string stateToString(DWORD state) {
    switch (state) {
        case SERVICE_STOPPED: return "stopped";
        case SERVICE_START_PENDING: return "starting";
        case SERVICE_STOP_PENDING: return "stopping";
        case SERVICE_RUNNING: return "running";
        case SERVICE_CONTINUE_PENDING: return "resuming";
        case SERVICE_PAUSE_PENDING: return "pausing";
        case SERVICE_PAUSED: return "paused";
        default: return "unknown";
    }
}

std::string startTypeToString(DWORD startType) {
    switch (startType) {
        case SERVICE_BOOT_START:
        case SERVICE_SYSTEM_START:
        case SERVICE_AUTO_START: return "Auto";
        case SERVICE_DEMAND_START: return "Manual";
        case SERVICE_DISABLED: return "Disabled";
        default: return "";
    }
}

// RAII wrapper so every early return below still closes the handle.
class ScHandle {
public:
    explicit ScHandle(SC_HANDLE h) : handle_(h) {}
    ~ScHandle() {
        if (handle_ != nullptr) CloseServiceHandle(handle_);
    }
    ScHandle(const ScHandle &) = delete;
    ScHandle &operator=(const ScHandle &) = delete;
    operator SC_HANDLE() const { return handle_; }
    bool valid() const { return handle_ != nullptr; }

private:
    SC_HANDLE handle_;
};

} // namespace

std::vector<ServiceInfo> ServiceManagerWin::list() {
    std::vector<ServiceInfo> result;

    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE));
    if (!scm.valid()) {
        return result;
    }

    DWORD bytesNeeded = 0;
    DWORD serviceCount = 0;
    DWORD resumeHandle = 0;

    // First call always fails with ERROR_MORE_DATA to report the required
    // buffer size; that's expected, not an error path to bail out of.
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0,
                           &bytesNeeded, &serviceCount, &resumeHandle, nullptr);

    std::vector<unsigned char> buffer(bytesNeeded);
    if (!EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                buffer.data(), static_cast<DWORD>(buffer.size()), &bytesNeeded,
                                &serviceCount, &resumeHandle, nullptr)) {
        return result;
    }

    auto *entries = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
    for (DWORD i = 0; i < serviceCount; ++i) {
        const auto &entry = entries[i];

        ServiceInfo info;
        info.name = wideToUtf8(entry.lpServiceName);
        info.displayName = wideToUtf8(entry.lpDisplayName);
        info.state = stateToString(entry.ServiceStatusProcess.dwCurrentState);

        ScHandle service(OpenServiceW(scm, entry.lpServiceName, SERVICE_QUERY_CONFIG));
        if (service.valid()) {
            DWORD configBytesNeeded = 0;
            QueryServiceConfigW(service, nullptr, 0, &configBytesNeeded);
            std::vector<unsigned char> configBuffer(configBytesNeeded);
            auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(configBuffer.data());
            if (configBytesNeeded > 0 &&
                QueryServiceConfigW(service, config, configBytesNeeded, &configBytesNeeded)) {
                info.startType = startTypeToString(config->dwStartType);
            }
        }

        result.push_back(std::move(info));
    }

    return result;
}

bool ServiceManagerWin::start(const std::string &name) {
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return false;

    const std::wstring wideName(name.begin(), name.end());
    ScHandle service(OpenServiceW(scm, wideName.c_str(), SERVICE_START));
    if (!service.valid()) return false;

    return StartServiceW(service, 0, nullptr) != 0;
}

bool ServiceManagerWin::stop(const std::string &name) {
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm.valid()) return false;

    const std::wstring wideName(name.begin(), name.end());
    ScHandle service(OpenServiceW(scm, wideName.c_str(), SERVICE_STOP));
    if (!service.valid()) return false;

    SERVICE_STATUS status{};
    return ControlService(service, SERVICE_CONTROL_STOP, &status) != 0;
}

bool ServiceManagerWin::restart(const std::string &name) {
    // No native "restart" verb in the SCM API - stop and start, same as
    // `sc.exe` scripts do. Give the stop a moment to land before starting
    // again; a production version would poll SERVICE_STOPPED instead of
    // sleeping a fixed amount.
    stop(name);
    Sleep(1000);
    return start(name);
}

} // namespace core
