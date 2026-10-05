#pragma once
#include "../../Services/Ports/ConfigStoragePort.hpp"
#include <cstring>
#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace SmokerController::Adapters::Storage {
class ESP32NVSConfigAdapter : public Services::Ports::IConfigStoragePort {
public:
    explicit ESP32NVSConfigAdapter(const char* nvs_namespace = "smoker_cfg")
        : namespace_(nvs_namespace) {}
    bool loadConfig(Domain::SmokerConfig& out) override {
#ifdef ARDUINO
        Preferences prefs;
        // Open/create the namespace so a genuinely missing record is distinct
        // from a read failure. Never overwrite unread calibration during reserve.
        write_blocked_ = true;
        if (!prefs.begin(namespace_, false)) return false;
        Domain::SmokerConfig cfg;
        if (prefs.isKey("config_v2") || prefs.isKey("config_v1")) {
            Record record{};
            bool ok = false;
            if (prefs.isKey("config_v2")) {
                RecordV2 stored{};
                ok = prefs.getBytesLength("config_v2") == sizeof(stored) &&
                    prefs.getBytes("config_v2", &stored, sizeof(stored)) == sizeof(stored) &&
                    stored.config.version == 2;
                record = stored.config;
                cfg.next_config_version = stored.next_config_version;
            } else {
                // V1 records predate durable concurrency revision reservations.
                ok = prefs.getBytesLength("config_v1") == sizeof(record) &&
                    prefs.getBytes("config_v1", &record, sizeof(record)) == sizeof(record) &&
                    record.version == 1;
            }
            prefs.end();
            if (!ok || record.inverted > 1) return false;
            cfg.setpoint_f = record.setpoint;
            cfg.pid_kp = record.kp; cfg.pid_ki = record.ki; cfg.pid_kd = record.kd;
            cfg.airflow_threshold_pct = record.threshold;
            cfg.lid_drop_threshold_deg = record.lid_drop;
            cfg.lid_pause_duration_ms = record.lid_ms;
            cfg.servo_min_pulse_us = record.min_us; cfg.servo_max_pulse_us = record.max_us;
            cfg.servo_inverted = record.inverted != 0;
            cfg.meat_probe_mode = static_cast<Domain::MeatProbeMode>(record.mode);
            std::memcpy(cfg.meater_cloud_token, record.token, sizeof(record.token));
            std::memcpy(cfg.meater_mac_filter, record.mac, sizeof(record.mac));
        } else {
            // Migrate older scalar records; absent namespaces are not successful loads.
            if (!prefs.isKey("setpoint")) { prefs.end(); write_blocked_ = false; return false; }
            cfg.setpoint_f = prefs.getFloat("setpoint", cfg.setpoint_f);
            cfg.pid_kp = prefs.getFloat("kp", cfg.pid_kp);
            cfg.pid_ki = prefs.getFloat("ki", cfg.pid_ki);
            cfg.pid_kd = prefs.getFloat("kd", cfg.pid_kd);
            cfg.airflow_threshold_pct = prefs.getFloat("thresh", cfg.airflow_threshold_pct);
            cfg.lid_drop_threshold_deg = prefs.getFloat("lid_drop", cfg.lid_drop_threshold_deg);
            cfg.lid_pause_duration_ms = prefs.getUInt("lid_ms", cfg.lid_pause_duration_ms);
            prefs.end();
        }
        if (!cfg.isValid()) return false;
        out = cfg;
        write_blocked_ = false;
        return true;
#else
        if (!mock_has_data_ || !mock_config_.isValid()) return false;
        out = mock_config_;
        return true;
#endif
    }
    bool saveConfig(const Domain::SmokerConfig& cfg) override {
        if (write_blocked_ || !cfg.isValid()) return false;
#ifdef ARDUINO
        Record record{};
        record.version = 2;
        record.setpoint = cfg.setpoint_f;
        record.kp = cfg.pid_kp; record.ki = cfg.pid_ki; record.kd = cfg.pid_kd;
        record.threshold = cfg.airflow_threshold_pct;
        record.lid_drop = cfg.lid_drop_threshold_deg; record.lid_ms = cfg.lid_pause_duration_ms;
        record.min_us = cfg.servo_min_pulse_us; record.max_us = cfg.servo_max_pulse_us;
        record.inverted = cfg.servo_inverted ? 1 : 0;
        record.mode = static_cast<uint8_t>(cfg.meat_probe_mode);
        std::memcpy(record.token, cfg.meater_cloud_token, sizeof(record.token));
        std::memcpy(record.mac, cfg.meater_mac_filter, sizeof(record.mac));
        Preferences prefs;
        if (!prefs.begin(namespace_, false)) return false;
        // One versioned NVS blob prevents partially updated multi-key configurations.
        const RecordV2 stored{record, cfg.next_config_version};
        const bool saved = prefs.putBytes("config_v2", &stored, sizeof(stored)) == sizeof(stored);
        prefs.end();
        return saved;
#else
        mock_config_ = cfg; mock_has_data_ = true;
        return true;
#endif
    }
private:
    struct Record {
        uint32_t version;
        float setpoint, kp, ki, kd, threshold, lid_drop;
        uint32_t lid_ms;
        uint16_t min_us, max_us;
        uint8_t inverted, mode;
        char token[96];
        char mac[18];
    };
    static_assert(sizeof(Record) == 152, "NVS v1 layout changed: require migration");
    struct RecordV2 {
        Record config;
        uint32_t next_config_version;
    };
    static_assert(sizeof(RecordV2) == 156, "NVS v2 layout changed: require migration");
    const char* namespace_;
    bool write_blocked_{false};
#ifndef ARDUINO
    Domain::SmokerConfig mock_config_{};
    bool mock_has_data_{false};
#endif
};
}
