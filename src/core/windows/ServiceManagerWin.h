#pragma once

#include "../IServiceManager.h"

namespace core {

class ServiceManagerWin : public IServiceManager {
public:
    std::vector<ServiceInfo> list() override;
    bool start(const std::string &name) override;
    bool stop(const std::string &name) override;
    bool restart(const std::string &name) override;
};

} // namespace core
