#pragma once
#include <functional>
#include <atomic>
#include <sys/types.h>
#include "lwip/sockets.h"
struct esp_tls_cfg_server_t { const unsigned char* servercert_buf{}; unsigned servercert_bytes{}; const unsigned char* serverkey_buf{}; unsigned serverkey_bytes{}; };
struct esp_tls_t { int fd=-1; };
namespace WebSpy {
inline bool fail_tls_init=false, fail_handshake=false;
inline std::atomic<int> tls_live{0};
inline std::function<void(int)> handshake_hook, read_hook;
inline esp_tls_cfg_server_t tls_config;
}
inline esp_tls_t* esp_tls_init() { if(WebSpy::fail_tls_init) return nullptr; ++WebSpy::tls_live; return new esp_tls_t; }
inline int esp_tls_server_session_create(esp_tls_cfg_server_t* config,int fd,esp_tls_t* tls) {
    WebSpy::tls_config=*config; tls->fd=fd;
    if(WebSpy::handshake_hook) WebSpy::handshake_hook(fd);
    return WebSpy::fail_handshake ? -1 : 0;
}
// IDF 4.4.7 esp_mbedtls_server_session_delete frees TLS state, not the socket.
inline void esp_tls_server_session_delete(esp_tls_t* tls) { delete tls; --WebSpy::tls_live; }
inline int esp_tls_conn_read(esp_tls_t* tls,char* data,size_t size) { if(WebSpy::read_hook) WebSpy::read_hook(tls->fd); if(size) data[0]='x'; return size ? 1 : 0; }
inline int esp_tls_conn_write(esp_tls_t*,const char*,size_t size) { return size; }
inline int esp_tls_get_bytes_avail(esp_tls_t*) { return 0; }
