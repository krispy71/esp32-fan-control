#pragma once

#ifdef ARDUINO
#include <esp_http_server.h>
#include <esp_tls.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <array>
#include <atomic>
#include <mutex>

namespace SmokerController::Adapters::Network {

// Own the documented HTTPD transport hooks so the deadline starts before TLS
// handshake and HTTP header parsing. TLS itself remains entirely in ESP-TLS.
// Every connection serves one response and has a three-second lifetime.
class BoundedHttpsTransport {
public:
    ~BoundedHttpsTransport() { stop(); }
    BoundedHttpsTransport() = default;
    BoundedHttpsTransport(const BoundedHttpsTransport&) = delete;
    BoundedHttpsTransport& operator=(const BoundedHttpsTransport&) = delete;

    bool start(const char* certificate, size_t certificate_size,
               const char* key, size_t key_size, uint16_t port) {
        if (server_) return true;
        tls_config_ = {};
        tls_config_.servercert_buf = reinterpret_cast<const unsigned char*>(certificate);
        tls_config_.servercert_bytes = certificate_size;
        tls_config_.serverkey_buf = reinterpret_cast<const unsigned char*>(key);
        tls_config_.serverkey_bytes = key_size;
        stopped_ = xSemaphoreCreateBinary();
        if (!stopped_) return false;
        stopping_ = false;
        if (xTaskCreatePinnedToCore(watchdog, "HttpsDeadline", 2048, this, 2, nullptr, 0) != pdPASS) {
            vSemaphoreDelete(stopped_); stopped_ = nullptr;
            return false;
        }
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.core_id = 0;
        config.task_priority = 1;
        config.stack_size = 10240;
        config.server_port = port;
        config.max_open_sockets = sessions_.size();
        config.max_uri_handlers = 2;
        config.recv_wait_timeout = 3;
        config.send_wait_timeout = 3;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        config.global_user_ctx = this;
        config.global_user_ctx_free_fn = borrowed;
        config.open_fn = open;
        config.close_fn = close;
        if (httpd_start(&server_, &config) != ESP_OK) { stop(); return false; }
        return true;
    }

    void stop() {
        // Wake blocked handshakes/reads before waiting for the HTTPD task.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& session : sessions_) if (session.fd >= 0) lwip_shutdown(session.fd, SHUT_RDWR);
        }
        if (server_) { httpd_stop(server_); server_ = nullptr; }
        if (stopped_) {
            stopping_ = true;
            xSemaphoreTake(stopped_, portMAX_DELAY);
            vSemaphoreDelete(stopped_); stopped_ = nullptr;
        }
    }

    httpd_handle_t server() const { return server_; }

private:
    struct Session { int fd{-1}; int64_t deadline{0}; esp_tls_t* tls{nullptr}; };
    static constexpr int64_t lifetime_us = 3000000;
    std::array<Session, 2> sessions_{};
    std::mutex mutex_;
    std::atomic<bool> stopping_{false};
    SemaphoreHandle_t stopped_{nullptr};
    httpd_handle_t server_{nullptr};
    esp_tls_cfg_server_t tls_config_{};

    static void borrowed(void*) {}
    static BoundedHttpsTransport& owner(httpd_handle_t server) {
        return *static_cast<BoundedHttpsTransport*>(httpd_get_global_user_ctx(server));
    }
    static Session* session(httpd_handle_t server, int fd) {
        return static_cast<Session*>(httpd_sess_get_transport_ctx(server, fd));
    }
    static esp_err_t open(httpd_handle_t server, int fd) {
        auto& self = owner(server);
        Session* selected = nullptr;
        {
            std::lock_guard<std::mutex> lock(self.mutex_);
            for (auto& item : self.sessions_) if (item.fd < 0) {
                item.fd = fd;
                item.deadline = esp_timer_get_time() + lifetime_us;
                selected = &item;
                break;
            }
        }
        if (!selected) return ESP_FAIL;
        httpd_sess_set_transport_ctx(server, fd, selected, borrowed);
        selected->tls = esp_tls_init();
        // HTTPD invokes close() after any open-hook failure as well.
        if (!selected->tls || esp_tls_server_session_create(&self.tls_config_, fd, selected->tls) != 0)
            return ESP_FAIL;
        if (esp_timer_get_time() >= selected->deadline) return ESP_FAIL;
        if (httpd_sess_set_recv_override(server, fd, receive) != ESP_OK
            || httpd_sess_set_send_override(server, fd, send) != ESP_OK
            || httpd_sess_set_pending_override(server, fd, pending) != ESP_OK) return ESP_FAIL;
        return ESP_OK;
    }
    static void close(httpd_handle_t server, int fd) {
        auto& self = owner(server);
        Session* current = session(server, fd);
        if (current) {
            // Serialize invalidation with shutdown. The watchdog never retains
            // an fd outside this lock, so it cannot act on a reused descriptor.
            {
                std::lock_guard<std::mutex> lock(self.mutex_);
                current->fd = -1;
            }
            if (current->tls) { esp_tls_server_session_delete(current->tls); current->tls = nullptr; }
            httpd_sess_set_transport_ctx(server, fd, nullptr, nullptr);
        }
        // IDF 4.4 ESP-TLS frees the TLS context, while HTTPD's close hook owns
        // the accepted socket (the default close is skipped when a hook exists).
        lwip_close(fd);
    }
    static int receive(httpd_handle_t server, int fd, char* data, size_t size, int) {
        auto* current = session(server, fd);
        if (!current || esp_timer_get_time() >= current->deadline) return HTTPD_SOCK_ERR_TIMEOUT;
        const int result = esp_tls_conn_read(current->tls, data, size);
        if (esp_timer_get_time() >= current->deadline) return HTTPD_SOCK_ERR_TIMEOUT;
        return result < 0 ? HTTPD_SOCK_ERR_FAIL : result;
    }
    static int send(httpd_handle_t server, int fd, const char* data, size_t size, int) {
        auto* current = session(server, fd);
        if (!current || esp_timer_get_time() >= current->deadline) return HTTPD_SOCK_ERR_TIMEOUT;
        const int result = esp_tls_conn_write(current->tls, data, size);
        return result < 0 ? HTTPD_SOCK_ERR_FAIL : result;
    }
    static int pending(httpd_handle_t server, int fd) {
        auto* current = session(server, fd);
        return current ? esp_tls_get_bytes_avail(current->tls) : 0;
    }
    static void watchdog(void* context) {
        auto& self = *static_cast<BoundedHttpsTransport*>(context);
        while (!self.stopping_) {
            {
                std::lock_guard<std::mutex> lock(self.mutex_);
                const auto now = esp_timer_get_time();
                for (auto& current : self.sessions_) {
                    if (current.fd >= 0 && now >= current.deadline)
                        lwip_shutdown(current.fd, SHUT_RDWR);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        xSemaphoreGive(self.stopped_);
        vTaskDelete(nullptr); // No access to self after signaling completion.
    }
};
} // namespace SmokerController::Adapters::Network
#endif
