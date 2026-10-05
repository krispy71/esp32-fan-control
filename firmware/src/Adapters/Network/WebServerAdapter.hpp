#pragma once

#include "../../Services/Ports/ControlChannelPort.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_https_server.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#endif

namespace SmokerController::Adapters::Network {

// The HTTPS task owns this adapter. The control task owns the service; the only
// shared boundary is a bounded channel carrying owned command/state values.
class WebServerAdapter {
public:
    explicit WebServerAdapter(Services::Ports::IControlChannel& channel, uint16_t port = 443)
        : channel_(channel), port_(port) {}

    bool begin() {
#ifdef ARDUINO
        if (server_) return true;
        if (!LittleFS.begin(false) || !loadAccess()) {
            Serial.println("[Web] HTTPS disabled: valid device access provisioning is required.");
            return false;
        }
        httpd_ssl_config_t settings = HTTPD_SSL_CONFIG_DEFAULT();
        settings.httpd.core_id = 0;
        settings.httpd.task_priority = 1;
        settings.httpd.max_open_sockets = 2;
        settings.httpd.max_uri_handlers = 2;
        settings.httpd.recv_wait_timeout = 3;
        settings.httpd.send_wait_timeout = 3;
        settings.httpd.uri_match_fn = httpd_uri_match_wildcard;
        settings.port_secure = port_;
        settings.cacert_pem = reinterpret_cast<const uint8_t*>(certificate_.c_str());
        settings.cacert_len = certificate_.length() + 1;
        settings.prvtkey_pem = reinterpret_cast<const uint8_t*>(private_key_.c_str());
        settings.prvtkey_len = private_key_.length() + 1;
        if (httpd_ssl_start(&server_, &settings) != ESP_OK) {
            WiFi.softAPdisconnect(true);
            Serial.println("[Web] HTTPS disabled: TLS startup failed.");
            return false;
        }
        httpd_uri_t get{};
        get.uri = "/*"; get.method = HTTP_GET; get.handler = dispatch; get.user_ctx = this;
        httpd_uri_t post = get; post.method = HTTP_POST;
        if (httpd_register_uri_handler(server_, &get) != ESP_OK || httpd_register_uri_handler(server_, &post) != ESP_OK) {
            httpd_ssl_stop(server_); server_ = nullptr;
            WiFi.softAPdisconnect(true);
            return false;
        }
        initialized_ = true;
        Serial.println("[Web] Provisioned HTTPS dashboard ready on port 443.");
        return true;
#else
        return false;
#endif
    }

    void update() noexcept {} // ESP-IDF owns the HTTPS task.
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }
    [[nodiscard]] uint16_t port() const noexcept { return port_; }

private:
    Services::Ports::IControlChannel& channel_;
    uint16_t port_;
    bool initialized_{false};
#ifdef ARDUINO
    static constexpr size_t max_body = 2048;
    struct Json {
        cJSON* value;
        explicit Json(cJSON* v = nullptr) : value(v) {}
        ~Json() { cJSON_Delete(value); }
        Json(const Json&) = delete;
        Json& operator=(const Json&) = delete;
    };
    struct Result {
        uint32_t id{0};
        bool accepted{false};
        Domain::PersistenceStatus persistence{Domain::PersistenceStatus::NotConfigured};
        uint32_t version{0};
    };
    httpd_handle_t server_{nullptr};
    String certificate_, private_key_;
    char password_hash_[65]{};
    char hosts_[8][65]{};
    size_t host_count_{0};
    uint32_t pending_{0}, next_id_{1};
    Result results_[8]{};
    size_t result_cursor_{0};

    static const char* persistenceName(Domain::PersistenceStatus status) {
        switch (status) {
            case Domain::PersistenceStatus::Saved: return "saved";
            case Domain::PersistenceStatus::Failed: return "failed";
            case Domain::PersistenceStatus::Unchanged: return "unchanged";
            default: return "not_configured";
        }
    }
    static bool boundedStructure(const char* data, size_t length) {
        bool quoted = false, escaped = false;
        int depth = 0;
        for (size_t i = 0; i < length; ++i) {
            const char c = data[i];
            if (c == '\0') return false;
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') quoted = true;
            else if (c == '{' || c == '[') { if (++depth > 2) return false; }
            else if (c == '}' || c == ']') { if (--depth < 0) return false; }
        }
        return !quoted && depth == 0;
    }
    static cJSON* parse(const char* body, size_t length) {
        if (!boundedStructure(body, length)) return nullptr;
        cJSON* value = cJSON_ParseWithLengthOpts(body, length + 1, nullptr, true);
        if (!cJSON_IsObject(value)) { cJSON_Delete(value); return nullptr; }
        for (auto* item = value->child; item; item = item->next) {
            for (auto* other = item->next; other; other = other->next) {
                if (std::strcmp(item->string, other->string) == 0) { cJSON_Delete(value); return nullptr; }
            }
        }
        return value;
    }
    static bool textField(const cJSON* object, const char* key, char* target, size_t capacity, bool empty = false) {
        const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
        if (!cJSON_IsString(item) || !item->valuestring) return false;
        size_t length = std::strlen(item->valuestring);
        if (length >= capacity || (!empty && length == 0)) return false;
        for (size_t i = 0; i < length; ++i) if (item->valuestring[i] < 33 || item->valuestring[i] > 126) return false;
        std::memcpy(target, item->valuestring, length + 1);
        return true;
    }
    static bool readFile(const char* name, String& output, size_t maximum) {
        File file = LittleFS.open(name, "r");
        if (!file || file.size() == 0 || file.size() > maximum) return false;
        output = file.readString();
        return output.length() == file.size();
    }
    bool loadAccess() {
        String content;
        if (!readFile("/device-access.json", content, max_body)
            || !readFile("/device-cert.pem", certificate_, 4096)
            || !readFile("/device-key.pem", private_key_, 2048)) return false;
        Json access(parse(content.c_str(), content.length()));
        if (!access.value) return false;
        char ssid[33]{}, ap_password[64]{}, username[16]{};
        if (!textField(access.value, "ap_ssid", ssid, sizeof(ssid))
            || !textField(access.value, "ap_password", ap_password, sizeof(ap_password))
            || std::strlen(ap_password) < 16
            || !textField(access.value, "username", username, sizeof(username)) || std::strcmp(username, "admin") != 0
            || !textField(access.value, "password_sha256", password_hash_, sizeof(password_hash_))
            || std::strlen(password_hash_) != 64) return false;
        for (char c : password_hash_) if (c && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        const auto* hosts = cJSON_GetObjectItemCaseSensitive(access.value, "hosts");
        if (!cJSON_IsArray(hosts) || cJSON_GetArraySize(hosts) < 1 || cJSON_GetArraySize(hosts) > 8) return false;
        host_count_ = 0;
        for (const auto* host = hosts->child; host; host = host->next) {
            if (!cJSON_IsString(host) || !host->valuestring || std::strlen(host->valuestring) == 0 || std::strlen(host->valuestring) >= 65) return false;
            for (const char* p = host->valuestring; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) return false;
            std::strcpy(hosts_[host_count_++], host->valuestring);
        }
        WiFi.mode(WIFI_AP);
        return WiFi.softAP(ssid, ap_password);
    }
    static esp_err_t reply(httpd_req_t* req, const char* status, cJSON* value) {
        char* body = cJSON_PrintUnformatted(value);
        if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response unavailable");
        httpd_resp_set_status(req, status);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
        auto result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
        cJSON_free(body);
        return result;
    }
    static esp_err_t error(httpd_req_t* req, const char* status, const char* message) {
        Json response(cJSON_CreateObject());
        cJSON_AddStringToObject(response.value, "error", message);
        return reply(req, status, response.value);
    }
    static bool header(httpd_req_t* req, const char* name, char* output, size_t length) {
        const size_t needed = httpd_req_get_hdr_value_len(req, name);
        return needed > 0 && needed < length && httpd_req_get_hdr_value_str(req, name, output, length) == ESP_OK;
    }
    bool authorized(httpd_req_t* req) {
        char host[80]{}, expected[80]{};
        bool allowed = false;
        if (header(req, "Host", host, sizeof(host))) {
            for (size_t i = 0; i < host_count_; ++i) {
                std::snprintf(expected, sizeof(expected), "%s:%u", hosts_[i], port_);
                if (std::strcmp(host, expected) == 0 || (port_ == 443 && std::strcmp(host, hosts_[i]) == 0)) allowed = true;
            }
        }
        if (!allowed) { error(req, "403 Forbidden", "Invalid host"); return false; }
        char origin[96]{}, site[32]{};
        if (httpd_req_get_hdr_value_len(req, "Origin") > 0) {
            char expected_origin[96]{};
            std::snprintf(expected_origin, sizeof(expected_origin), "https://%s", host);
            if (!header(req, "Origin", origin, sizeof(origin)) || std::strcmp(origin, expected_origin) != 0) {
                error(req, "403 Forbidden", "Cross-origin requests are not allowed"); return false;
            }
        }
        if (req->method == HTTP_POST && header(req, "Sec-Fetch-Site", site, sizeof(site)) && std::strcmp(site, "cross-site") == 0) {
            error(req, "403 Forbidden", "Cross-origin requests are not allowed"); return false;
        }
        char auth[256]{};
        unsigned char decoded[160]{}, digest[32]{};
        size_t decoded_len = 0;
        bool valid = header(req, "Authorization", auth, sizeof(auth)) && std::strncmp(auth, "Basic ", 6) == 0;
        if (valid) valid = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
            reinterpret_cast<const unsigned char*>(auth + 6), std::strlen(auth + 6)) == 0;
        if (valid) valid = decoded_len > 6 && std::memcmp(decoded, "admin:", 6) == 0 && std::strlen(reinterpret_cast<char*>(decoded)) == decoded_len;
        if (valid) {
            valid = mbedtls_sha256_ret(decoded + 6, decoded_len - 6, digest, 0) == 0;
            const char hex[] = "0123456789abcdef";
            volatile unsigned char different = 0;
            for (size_t i = 0; i < 32; ++i) {
                different |= static_cast<unsigned char>(hex[digest[i] >> 4] ^ password_hash_[2*i]);
                different |= static_cast<unsigned char>(hex[digest[i] & 15] ^ password_hash_[2*i+1]);
            }
            valid = valid && different == 0;
        }
        std::memset(decoded, 0, sizeof(decoded));
        if (!valid) {
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"SmokerController\", charset=\"UTF-8\"");
            error(req, "401 Unauthorized", "Authentication required");
        }
        return valid;
    }
    void collectResult(const Domain::ControlState& state) {
        if (pending_ && state.last_command_id == pending_) {
            results_[result_cursor_] = Result{pending_, state.last_command_accepted, state.persistence, state.config_version};
            result_cursor_ = (result_cursor_ + 1) % 8;
            pending_ = 0;
        }
    }
    static void addConfig(cJSON* json, const Domain::ControlState& state) {
        const auto& c = state.config;
        cJSON_AddNumberToObject(json, "setpoint_f", c.setpoint_f);
        cJSON_AddNumberToObject(json, "pid_kp", c.pid_kp);
        cJSON_AddNumberToObject(json, "pid_ki", c.pid_ki);
        cJSON_AddNumberToObject(json, "pid_kd", c.pid_kd);
        cJSON_AddNumberToObject(json, "airflow_threshold_pct", c.airflow_threshold_pct);
        cJSON_AddNumberToObject(json, "lid_drop_threshold_deg", c.lid_drop_threshold_deg);
        cJSON_AddNumberToObject(json, "lid_pause_duration_s", c.lid_pause_duration_ms / 1000.0);
        cJSON_AddNumberToObject(json, "servo_min_pulse_us", c.servo_min_pulse_us);
        cJSON_AddNumberToObject(json, "servo_max_pulse_us", c.servo_max_pulse_us);
        cJSON_AddBoolToObject(json, "servo_inverted", c.servo_inverted);
        cJSON_AddNumberToObject(json, "meat_probe_mode", static_cast<unsigned>(c.meat_probe_mode));
        cJSON_AddBoolToObject(json, "meater_cloud_token_configured", c.meater_cloud_token[0] != '\0');
        cJSON_AddStringToObject(json, "meater_mac_filter", c.meater_mac_filter);
        cJSON_AddNumberToObject(json, "config_version", state.config_version);
        cJSON_AddStringToObject(json, "persistence", persistenceName(state.persistence));
    }
    static void addTelemetry(cJSON* json, const Domain::ControlState& state) {
        const auto& s = state.telemetry;
        cJSON_AddNumberToObject(json, "timestamp_ms", s.timestamp_ms);
        cJSON_AddNumberToObject(json, "pit_temp_f", s.pit_temp_f);
        cJSON_AddNumberToObject(json, "meat_temp_f", s.meat_temp_f);
        cJSON_AddNumberToObject(json, "setpoint_f", s.setpoint_f);
        cJSON_AddNumberToObject(json, "damper_position_pct", s.damper_position_pct);
        cJSON_AddNumberToObject(json, "blower_speed_pct", s.blower_speed_pct);
        cJSON_AddNumberToObject(json, "demand_pct", s.demand_pct);
        cJSON_AddBoolToObject(json, "is_pit_valid", s.is_pit_valid);
        cJSON_AddBoolToObject(json, "is_meat_valid", s.is_meat_valid);
        cJSON_AddBoolToObject(json, "lid_open", s.lid_open);
        cJSON_AddStringToObject(json, "status", s.status);
        cJSON_AddBoolToObject(json, "is_meat_wireless", s.is_meat_wireless);
        cJSON_AddNumberToObject(json, "meat_battery_pct", s.meat_battery_pct);
        cJSON_AddStringToObject(json, "meat_probe_name", s.meat_probe_name);
        cJSON_AddNumberToObject(json, "config_version", state.config_version);
    }
    static bool number(const cJSON* item, double minimum, double maximum, bool integer = false) {
        return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) && item->valuedouble >= minimum
            && item->valuedouble <= maximum && (!integer || std::floor(item->valuedouble) == item->valuedouble);
    }
    static bool configUpdate(cJSON* json, Domain::SmokerConfig& cfg) {
        for (const auto* p = json->child; p; p = p->next) {
            const char* key = p->string;
            if (std::strcmp(key, "config_version") == 0) continue;
            if (std::strcmp(key, "servo_inverted") == 0) {
                if (!cJSON_IsBool(p)) return false;
                cfg.servo_inverted = cJSON_IsTrue(p); continue;
            }
            if (std::strcmp(key, "meater_cloud_token") == 0) {
                if (!textField(json, key, cfg.meater_cloud_token, sizeof(cfg.meater_cloud_token), true)) return false;
                continue;
            }
            if (std::strcmp(key, "meater_mac_filter") == 0) {
                if (!textField(json, key, cfg.meater_mac_filter, sizeof(cfg.meater_mac_filter), true)) return false;
                if (cfg.meater_mac_filter[0]) {
                    if (std::strlen(cfg.meater_mac_filter) != 17) return false;
                    for (size_t i = 0; i < 17; ++i) {
                        char c = cfg.meater_mac_filter[i];
                        if (i % 3 == 2 ? c != ':' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
                    }
                }
                continue;
            }
            if (!number(p, 0, 0xFFFFFFFFu)) return false;
            double v = p->valuedouble;
            if (std::strcmp(key, "setpoint_f") == 0) cfg.setpoint_f = v;
            else if (std::strcmp(key, "pid_kp") == 0) cfg.pid_kp = v;
            else if (std::strcmp(key, "pid_ki") == 0) cfg.pid_ki = v;
            else if (std::strcmp(key, "pid_kd") == 0) cfg.pid_kd = v;
            else if (std::strcmp(key, "airflow_threshold_pct") == 0) cfg.airflow_threshold_pct = v;
            else if (std::strcmp(key, "lid_drop_threshold_deg") == 0) cfg.lid_drop_threshold_deg = v;
            else if (std::strcmp(key, "lid_pause_duration_s") == 0) {
                if (!number(p, 10, 600)) return false;
                cfg.lid_pause_duration_ms = static_cast<uint32_t>(v * 1000);
            } else if (std::strcmp(key, "servo_min_pulse_us") == 0 || std::strcmp(key, "servo_max_pulse_us") == 0) {
                if (!number(p, 500, 2500, true)) return false;
                if (std::strcmp(key, "servo_min_pulse_us") == 0) cfg.servo_min_pulse_us = v;
                else cfg.servo_max_pulse_us = v;
            } else if (std::strcmp(key, "meat_probe_mode") == 0) {
                if (!number(p, 0, 3, true)) return false;
                cfg.meat_probe_mode = static_cast<Domain::MeatProbeMode>(static_cast<uint8_t>(v));
            } else return false;
        }
        return cfg.isValid();
    }
    esp_err_t post(httpd_req_t* req, const Domain::ControlState& state) {
        if (std::strcmp(req->uri, "/api/config") != 0 && std::strcmp(req->uri, "/api/setpoint") != 0 && std::strcmp(req->uri, "/api/lid-pause") != 0)
            return error(req, "404 Not Found", "Not found");
        if (pending_) return error(req, "409 Conflict", "A command is still pending");
        char type[64]{};
        if (!header(req, "Content-Type", type, sizeof(type)) || std::strcmp(type, "application/json") != 0)
            return error(req, "415 Unsupported Media Type", "Use application/json");
        if (httpd_req_get_hdr_value_len(req, "Transfer-Encoding") || req->content_len == 0 || req->content_len > max_body)
            return error(req, "413 Payload Too Large", "Request body too large or empty");
        char body[max_body + 1]{};
        size_t received = 0;
        while (received < req->content_len) {
            int count = httpd_req_recv(req, body + received, req->content_len - received);
            if (count <= 0) return error(req, "400 Bad Request", "Incomplete request");
            received += static_cast<size_t>(count);
        }
        Json json(parse(body, received));
        if (!json.value) return error(req, "400 Bad Request", "Invalid JSON object");
        const auto* version = cJSON_GetObjectItemCaseSensitive(json.value, "config_version");
        if (!number(version, 0, 0xFFFFFFFFu, true)) return error(req, "400 Bad Request", "config_version is required");
        if (static_cast<uint32_t>(version->valuedouble) != state.config_version)
            return error(req, "409 Conflict", "Configuration changed; reload settings before saving");
        Domain::ControlCommand command{};
        command.request_id = next_id_;
        command.expected_config_version = state.config_version;
        command.require_config_version = true;
        if (std::strcmp(req->uri, "/api/setpoint") == 0) {
            const auto* sp = cJSON_GetObjectItemCaseSensitive(json.value, "setpoint");
            if (cJSON_GetArraySize(json.value) != 2 || !number(sp, 100, 450)) return error(req, "400 Bad Request", "Invalid setpoint");
            command.kind = Domain::ControlCommandKind::SetSetpoint;
            command.setpoint_f = sp->valuedouble;
        } else if (std::strcmp(req->uri, "/api/lid-pause") == 0) {
            char action[8]{};
            if (cJSON_GetArraySize(json.value) != 2 || !textField(json.value, "action", action, sizeof(action)))
                return error(req, "400 Bad Request", "Expected pause or resume");
            if (std::strcmp(action, "pause") == 0) command.kind = Domain::ControlCommandKind::TriggerLidPause;
            else if (std::strcmp(action, "resume") == 0) command.kind = Domain::ControlCommandKind::CancelLidPause;
            else return error(req, "400 Bad Request", "Expected pause or resume");
        } else {
            command.kind = Domain::ControlCommandKind::UpdateConfig;
            command.config = state.config;
            if (cJSON_GetArraySize(json.value) < 2 || !configUpdate(json.value, command.config))
                return error(req, "400 Bad Request", "Invalid tuning or calibration limits");
        }
        if (!channel_.submit(command)) return error(req, "503 Service Unavailable", "Controller command queue is full");
        pending_ = next_id_++;
        if (next_id_ == 0) next_id_ = 1;
        Json response(cJSON_CreateObject());
        cJSON_AddStringToObject(response.value, "status", "queued");
        cJSON_AddNumberToObject(response.value, "request_id", pending_);
        return reply(req, "202 Accepted", response.value);
    }
    esp_err_t handle(httpd_req_t* req) {
        if (!authorized(req)) return ESP_OK;
        Domain::ControlState state{};
        if (!channel_.snapshot(state)) return error(req, "503 Service Unavailable", "Controller initializing");
        collectResult(state);
        if (req->method == HTTP_POST) return post(req, state);
        Json response(cJSON_CreateObject());
        if (std::strcmp(req->uri, "/api/config") == 0) {
            addConfig(response.value, state); return reply(req, "200 OK", response.value);
        }
        if (std::strcmp(req->uri, "/api/telemetry") == 0) {
            addTelemetry(response.value, state); return reply(req, "200 OK", response.value);
        }
        if (std::strncmp(req->uri, "/api/command?", 13) == 0) {
            char query[32]{}, id_text[16]{};
            if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "id", id_text, sizeof(id_text)) != ESP_OK)
                return error(req, "400 Bad Request", "Invalid command id");
            char* end = nullptr;
            unsigned long value = std::strtoul(id_text, &end, 10);
            if (!id_text[0] || *end || !value || value > 0xFFFFFFFFul) return error(req, "400 Bad Request", "Invalid command id");
            uint32_t id = static_cast<uint32_t>(value);
            cJSON_AddNumberToObject(response.value, "request_id", id);
            if (pending_ == id) {
                cJSON_AddStringToObject(response.value, "status", "queued"); return reply(req, "202 Accepted", response.value);
            }
            for (const auto& result : results_) if (result.id == id) {
                cJSON_AddStringToObject(response.value, "status", result.accepted ? "applied" : "rejected");
                cJSON_AddStringToObject(response.value, "persistence", persistenceName(result.persistence));
                cJSON_AddNumberToObject(response.value, "config_version", result.version);
                return reply(req, "200 OK", response.value);
            }
            return error(req, "404 Not Found", "Command result expired or unknown");
        }
        const char* file_name = nullptr;
        const char* mime = nullptr;
        if (std::strcmp(req->uri, "/") == 0) { file_name = "/index.html"; mime = "text/html"; }
        else if (std::strcmp(req->uri, "/style.css") == 0) { file_name = "/style.css"; mime = "text/css"; }
        else if (std::strcmp(req->uri, "/app.js") == 0) { file_name = "/app.js"; mime = "application/javascript"; }
        if (!file_name) return error(req, "404 Not Found", "Not found");
        File file = LittleFS.open(file_name, "r");
        if (!file) return error(req, "503 Service Unavailable", "Dashboard unavailable");
        httpd_resp_set_type(req, mime);
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        char chunk[1024];
        while (file.available()) {
            const size_t read = file.readBytes(chunk, sizeof(chunk));
            if (httpd_resp_send_chunk(req, chunk, read) != ESP_OK) return ESP_FAIL;
        }
        return httpd_resp_send_chunk(req, nullptr, 0);
    }
    static esp_err_t dispatch(httpd_req_t* req) {
        return static_cast<WebServerAdapter*>(req->user_ctx)->handle(req);
    }
#endif
};

} // namespace SmokerController::Adapters::Network
