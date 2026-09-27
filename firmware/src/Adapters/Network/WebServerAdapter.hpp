#pragma once

#include "../../Services/SmokerControlService.hpp"
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#endif

namespace SmokerController::Adapters::Network {

class WebServerAdapter {
public:
    explicit WebServerAdapter(Services::SmokerControlService& service, uint16_t port = 80)
        : service_(service),
          port_(port),
          is_initialized_(false)
#ifdef ARDUINO
        , server_(port)
#endif
    {}

    void begin(const char* ap_ssid = "SmokerController", const char* ap_pass = "smoker123") {
        (void)ap_ssid;
        (void)ap_pass;
#ifdef ARDUINO
        // 1. Initialize LittleFS for web assets
        if (!LittleFS.begin(true)) {
            Serial.println("[Web] WARN: LittleFS mount failed.");
        } else {
            Serial.println("[Web] LittleFS mounted successfully.");
        }

        // 2. Set up SoftAP if not connected to STA
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_AP);
            WiFi.softAP(ap_ssid, ap_pass);
            Serial.print("[Web] Started SoftAP: ");
            Serial.println(ap_ssid);
            Serial.print("[Web] AP IP address: ");
            Serial.println(WiFi.softAPIP());
        }

        // 3. Register HTTP Routes
        server_.on("/", HTTP_GET, [this]() {
            if (LittleFS.exists("/index.html")) {
                File file = LittleFS.open("/index.html", "r");
                server_.streamFile(file, "text/html");
                file.close();
            } else {
                server_.send(200, "text/html", "<h1>ESP32 Smoker Controller</h1><p>Upload LittleFS data image.</p>");
            }
        });

        server_.on("/style.css", HTTP_GET, [this]() {
            if (LittleFS.exists("/style.css")) {
                File file = LittleFS.open("/style.css", "r");
                server_.streamFile(file, "text/css");
                file.close();
            } else {
                server_.send(404, "text/plain", "style.css not found");
            }
        });

        server_.on("/app.js", HTTP_GET, [this]() {
            if (LittleFS.exists("/app.js")) {
                File file = LittleFS.open("/app.js", "r");
                server_.streamFile(file, "application/javascript");
                file.close();
            } else {
                server_.send(404, "text/plain", "app.js not found");
            }
        });

        server_.on("/api/telemetry", HTTP_GET, [this]() {
            char json_buf[384];
            formatTelemetryJson(json_buf, sizeof(json_buf));
            server_.send(200, "application/json", json_buf);
        });

        server_.on("/api/setpoint", HTTP_POST, [this]() {
            if (!server_.hasArg("plain")) {
                server_.send(400, "application/json", "{\"error\":\"Missing body\"}");
                return;
            }
            String body = server_.arg("plain");
            // Minimal float extraction: find \"setpoint\": <val>
            int idx = body.indexOf("\"setpoint\":");
            if (idx >= 0) {
                float new_sp = body.substring(idx + 11).toFloat();
                if (new_sp >= 100.0f && new_sp <= 450.0f) {
                    service_.setSetpoint(new_sp);
                    char resp[96];
                    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"setpoint\":%.1f}", new_sp);
                    server_.send(200, "application/json", resp);
                    return;
                }
            }
            server_.send(400, "application/json", "{\"error\":\"Invalid setpoint value\"}");
        });

        server_.on("/api/lid-pause", HTTP_POST, [this]() {
            if (service_.isLidOpen()) {
                service_.cancelLidPause();
            } else {
                service_.triggerLidPause(millis());
            }
            char resp[96];
            snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"lid_open\":%s}", service_.isLidOpen() ? "true" : "false");
            server_.send(200, "application/json", resp);
        });

        server_.begin();
        Serial.printf("[Web] HTTP Server listening on port %u\n", port_);
#endif
        is_initialized_ = true;
    }

    void update() {
#ifdef ARDUINO
        if (is_initialized_) {
            server_.handleClient();
        }
#endif
    }

    void formatTelemetryJson(char* buf, size_t max_len) const noexcept {
        const auto& snap = service_.lastTelemetry();
        snprintf(
            buf, max_len,
            "{\"timestamp_ms\":%u,"
            "\"pit_temp_f\":%.1f,"
            "\"meat_temp_f\":%.1f,"
            "\"setpoint_f\":%.1f,"
            "\"damper_position_pct\":%.1f,"
            "\"blower_speed_pct\":%.1f,"
            "\"demand_pct\":%.1f,"
            "\"is_pit_valid\":%s,"
            "\"is_meat_valid\":%s,"
            "\"lid_open\":%s,"
            "\"status\":\"%s\"}",
            snap.timestamp_ms,
            snap.pit_temp_f,
            snap.meat_temp_f,
            snap.setpoint_f,
            snap.damper_position_pct,
            snap.blower_speed_pct,
            snap.demand_pct,
            snap.is_pit_valid ? "true" : "false",
            snap.is_meat_valid ? "true" : "false",
            snap.lid_open ? "true" : "false",
            snap.status ? snap.status : "OK"
        );
    }

    [[nodiscard]] uint16_t port() const noexcept { return port_; }
    [[nodiscard]] bool isInitialized() const noexcept { return is_initialized_; }

private:
    Services::SmokerControlService& service_;
    uint16_t port_;
    bool is_initialized_;
#ifdef ARDUINO
    WebServer server_;
#endif
};

} // namespace SmokerController::Adapters::Network
