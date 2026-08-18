#pragma once

#include "../IServiceManager.h"

namespace core {

class ServiceManagerLinux : public IServiceManager {
public:
    std::vector<ServiceInfo> list() override;
    bool start(const std::string &name) override;
    bool stop(const std::string &name) override;
    bool restart(const std::string &name) override;

private:
    bool runSystemctl(const std::string &action, const std::string &unitName);
};

} // namespace core
