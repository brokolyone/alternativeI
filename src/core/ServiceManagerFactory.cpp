#include "IServiceManager.h"

#if defined(_WIN32)
#include "windows/ServiceManagerWin.h"
#elif defined(__linux__)
#include "linux/ServiceManagerLinux.h"
#endif

namespace core {

std::unique_ptr<IServiceManager> createDefaultServiceManager() {
#if defined(_WIN32)
    return std::make_unique<ServiceManagerWin>();
#elif defined(__linux__)
    return std::make_unique<ServiceManagerLinux>();
#else
#error "No service manager implemented for this platform"
#endif
}

} // namespace core
