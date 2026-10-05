#pragma once
#include "lwip/sockets.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, HTTP_GET=0, HTTP_POST=1, HTTPD_RESP_USE_STRLEN=-1, HTTPD_500_INTERNAL_SERVER_ERROR=500, HTTPD_SOCK_ERR_TIMEOUT=-3, HTTPD_SOCK_ERR_FAIL=-1;
using httpd_handle_t = void*;
struct httpd_req_t {
    const char* uri;
    int method;
    void* user_ctx;
    size_t content_len;
    std::string body;
    std::map<std::string,std::string> headers;
    std::map<std::string,std::string> response_headers;
    std::string response, status{"200 OK"};
    size_t position{0};
};
struct httpd_uri_t { const char* uri{}; int method{}; esp_err_t (*handler)(httpd_req_t*){}; void* user_ctx{}; };
inline std::vector<httpd_uri_t> routes;
inline bool httpd_uri_match_wildcard(const char*,const char*,size_t) { return true; }
using httpd_recv_func_t = int(*)(httpd_handle_t,int,char*,size_t,int);
using httpd_send_func_t = int(*)(httpd_handle_t,int,const char*,size_t,int);
using httpd_pending_func_t = int(*)(httpd_handle_t,int);
struct httpd_config_t {
    int core_id{},task_priority{},stack_size{},server_port{},max_open_sockets{},max_uri_handlers{},recv_wait_timeout{},send_wait_timeout{};
    bool lru_purge_enable{};
    bool (*uri_match_fn)(const char*,const char*,size_t){};
    void* global_user_ctx{};
    void (*global_user_ctx_free_fn)(void*){};
    esp_err_t (*open_fn)(httpd_handle_t,int){};
    void (*close_fn)(httpd_handle_t,int){};
};
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
struct SessionSpy { void* context{}; void(*free_context)(void*){}; httpd_recv_func_t recv{}; httpd_send_func_t send{}; httpd_pending_func_t pending{}; };
struct ServerSpy { httpd_config_t config{}; std::map<int,SessionSpy> sessions; };
inline httpd_config_t httpd_settings;
inline bool fail_httpd_start=false;
inline int httpd_start(httpd_handle_t* handle,httpd_config_t* config) {
    if(fail_httpd_start) return ESP_FAIL;
    httpd_settings=*config; *handle=new ServerSpy{*config,{}}; return ESP_OK;
}
inline int httpd_stop(httpd_handle_t handle) {
    auto* server=static_cast<ServerSpy*>(handle);
    for(auto& [fd,session]:server->sessions) { (void)session; server->config.close_fn(handle,fd); }
    server->config.global_user_ctx_free_fn(server->config.global_user_ctx);
    delete server; routes.clear(); return ESP_OK;
}
inline int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t* route) { routes.push_back(*route); return ESP_OK; }
inline void* httpd_get_global_user_ctx(httpd_handle_t handle) { return static_cast<ServerSpy*>(handle)->config.global_user_ctx; }
inline void* httpd_sess_get_transport_ctx(httpd_handle_t handle,int fd) { return static_cast<ServerSpy*>(handle)->sessions.at(fd).context; }
inline void httpd_sess_set_transport_ctx(httpd_handle_t handle,int fd,void* context,void(*free_context)(void*)) {
    auto& session=static_cast<ServerSpy*>(handle)->sessions.at(fd);
    if(session.context && session.context!=context && session.free_context) session.free_context(session.context);
    session.context=context; session.free_context=free_context;
}
inline int httpd_sess_set_recv_override(httpd_handle_t handle,int fd,httpd_recv_func_t callback) { static_cast<ServerSpy*>(handle)->sessions.at(fd).recv=callback; return ESP_OK; }
inline int httpd_sess_set_send_override(httpd_handle_t handle,int fd,httpd_send_func_t callback) { static_cast<ServerSpy*>(handle)->sessions.at(fd).send=callback; return ESP_OK; }
inline int httpd_sess_set_pending_override(httpd_handle_t handle,int fd,httpd_pending_func_t callback) { static_cast<ServerSpy*>(handle)->sessions.at(fd).pending=callback; return ESP_OK; }
// IDF 4.4.7: httpd_sess_new calls close_fn after a failed open hook;
// httpd_accept_conn then closes the still caller-owned socket.
inline int open_session(httpd_handle_t handle,int fd) {
    auto* server=static_cast<ServerSpy*>(handle); server->sessions.emplace(fd,SessionSpy{});
    int result=server->config.open_fn(handle,fd);
    if(result!=ESP_OK) { server->config.close_fn(handle,fd); server->sessions.erase(fd); lwip_close(fd); }
    return result;
}
inline void close_session(httpd_handle_t handle,int fd) {
    auto* server=static_cast<ServerSpy*>(handle); server->config.close_fn(handle,fd); server->sessions.erase(fd);
}
inline size_t httpd_req_get_hdr_value_len(httpd_req_t* request,const char* key) { return request->headers[key].size(); }
inline int httpd_req_get_hdr_value_str(httpd_req_t* request,const char* key,char* target,size_t capacity) {
    const auto& value=request->headers[key]; if (value.size()+1>capacity || value.empty()) return ESP_FAIL;
    std::memcpy(target,value.c_str(),value.size()+1); return ESP_OK;
}
inline int httpd_resp_set_hdr(httpd_req_t* request,const char* key,const char* value) { request->response_headers[key]=value; return ESP_OK; }
inline void httpd_resp_set_status(httpd_req_t* request,const char* value) { request->status=value; }
inline void httpd_resp_set_type(httpd_req_t* request,const char* value) { request->response_headers["Content-Type"]=value; }
inline int httpd_resp_send(httpd_req_t* request,const char* body,int length) { request->response.assign(body,length<0?std::strlen(body):static_cast<size_t>(length)); return ESP_OK; }
inline int httpd_resp_send_err(httpd_req_t* request,int,const char* message) { request->status="500 Internal Server Error"; request->response=message; return ESP_OK; }
inline int httpd_resp_send_chunk(httpd_req_t* request,const char* data,size_t length) { if(data) request->response.append(data,length); return ESP_OK; }
inline int httpd_req_recv(httpd_req_t* request,char* target,size_t length) {
    size_t count=std::min(length,request->body.size()-request->position);
    std::memcpy(target,request->body.data()+request->position,count); request->position+=count; return static_cast<int>(count);
}
inline int httpd_req_get_url_query_str(httpd_req_t* request,char* target,size_t capacity) {
    const char* start=std::strchr(request->uri,'?'); if(!start || std::strlen(start+1)>=capacity) return ESP_FAIL;
    std::strcpy(target,start+1); return ESP_OK;
}
inline int httpd_query_key_value(const char* query,const char* key,char* target,size_t capacity) {
    std::string prefix=std::string(key)+"="; if(std::strncmp(query,prefix.c_str(),prefix.size())!=0 || std::strlen(query+prefix.size())>=capacity) return ESP_FAIL;
    std::strcpy(target,query+prefix.size()); return ESP_OK;
}
