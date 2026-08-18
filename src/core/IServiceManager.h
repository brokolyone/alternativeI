#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ServiceInfo.h"

namespace core {

// Linux: systemd units, via systemctl. Windows: Service Control Manager.
// start/stop/restart return false (and leave the service state unchanged)
// when the caller lacks the privilege to control the service - same as a
// non-root/non-admin user hitting "Access denied" in the real tools.
class IServiceManager {
public:
    virtual ~IServiceManager() = default;

    virtual std::vector<ServiceInfo> list() = 0;
    virtual bool start(const std::string &name) = 0;
    virtual bool stop(const std::string &name) = 0;
    virtual bool restart(const std::string &name) = 0;
};

std::unique_ptr<IServiceManager> createDefaultServiceManager();

} // namespace core
