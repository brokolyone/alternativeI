#include "IProcessProvider.h"

#if defined(_WIN32)
#include "windows/ProcessProviderWin.h"
#elif defined(__linux__)
#include "linux/ProcessProviderLinux.h"
#endif

namespace core {

std::unique_ptr<IProcessProvider> createDefaultProcessProvider() {
#if defined(_WIN32)
    return std::make_unique<ProcessProviderWin>();
#elif defined(__linux__)
    return std::make_unique<ProcessProviderLinux>();
#else
#error "No process provider implemented for this platform"
#endif
}

} // namespace core
