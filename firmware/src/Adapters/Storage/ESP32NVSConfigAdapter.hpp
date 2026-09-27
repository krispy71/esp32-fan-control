#pragma once

#include "../../Services/Ports/ConfigStoragePort.hpp"

#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace SmokerController::Adapters::Storage {

class ESP32NVSConfigAdapter : public Services::Ports::IConfigStoragePort {
public:
    explicit ESP32NVSConfigAdapter(const char* nvs_namespace = "smoker_cfg")
        : namespace_(nvs_namespace) {}

    bool loadConfig(Domain::SmokerConfig& out_config) override {
#ifdef ARDUINO
        Preferences prefs;
        if (!prefs.begin(namespace_, true)) { // Read-only mode
            return false;
        }

        Domain::SmokerConfig cfg;
        cfg.setpoint_f = prefs.getFloat("setpoint", 225.0f);
        cfg.pid_kp = prefs.getFloat("kp", 3.0f);
        cfg.pid_ki = prefs.getFloat("ki", 0.02f);
        cfg.pid_kd = prefs.getFloat("kd", 15.0f);
        cfg.airflow_threshold_pct = prefs.getFloat("thresh", 40.0f);
        cfg.lid_drop_threshold_deg = prefs.getFloat("lid_drop", 15.0f);
        cfg.lid_pause_duration_ms = prefs.getUInt("lid_ms", 180000);
        prefs.end();

        if (cfg.isValid()) {
            out_config = cfg;
            return true;
        }
        return false;
#else
        if (mock_has_data_ && mock_config_.isValid()) {
            out_config = mock_config_;
            return true;
        }
        return false;
#endif
    }

    bool saveConfig(const Domain::SmokerConfig& config) override {
        if (!config.isValid()) {
            return false;
        }
#ifdef ARDUINO
        Preferences prefs;
        if (!prefs.begin(namespace_, false)) { // Read-write mode
            return false;
        }

        prefs.putFloat("setpoint", config.setpoint_f);
        prefs.putFloat("kp", config.pid_kp);
        prefs.putFloat("ki", config.pid_ki);
        prefs.putFloat("kd", config.pid_kd);
        prefs.putFloat("thresh", config.airflow_threshold_pct);
        prefs.putFloat("lid_drop", config.lid_drop_threshold_deg);
        prefs.putUInt("lid_ms", config.lid_pause_duration_ms);
        prefs.end();
        return true;
#else
        mock_config_ = config;
        mock_has_data_ = true;
        return true;
#endif
    }

private:
    const char* namespace_;
#ifndef ARDUINO
    Domain::SmokerConfig mock_config_{};
    bool mock_has_data_{false};
#endif
};

} // namespace SmokerController::Adapters::Storage
