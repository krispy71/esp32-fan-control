#pragma once

#include "../../Domain/Configuration.hpp"

namespace SmokerController::Services::Ports {

class IConfigStoragePort {
public:
    virtual ~IConfigStoragePort() = default;

    /**
     * Load stored configuration into out_config.
     * Returns true if valid configuration was loaded, false otherwise.
     */
    virtual bool loadConfig(Domain::SmokerConfig& out_config) = 0;

    /**
     * Persist configuration to non-volatile storage.
     * Returns true on successful write.
     */
    virtual bool saveConfig(const Domain::SmokerConfig& config) = 0;
};

} // namespace SmokerController::Services::Ports
