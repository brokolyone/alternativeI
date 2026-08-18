#include "ISystemMonitor.h"

#if defined(_WIN32)
#include "windows/SystemMonitorWin.h"
#elif defined(__linux__)
#include "linux/SystemMonitorLinux.h"
#endif

namespace core {

std::unique_ptr<ISystemMonitor> createDefaultSystemMonitor() {
#if defined(_WIN32)
    return std::make_unique<SystemMonitorWin>();
#elif defined(__linux__)
    return std::make_unique<SystemMonitorLinux>();
#else
#error "No system monitor implemented for this platform"
#endif
}

} // namespace core
