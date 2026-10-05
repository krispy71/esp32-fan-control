#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, HTTP_GET=0, HTTP_POST=1, HTTPD_RESP_USE_STRLEN=-1, HTTPD_500_INTERNAL_SERVER_ERROR=500;
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
struct httpd_config_t {
    int core_id{},task_priority{},max_open_sockets{},max_uri_handlers{},recv_wait_timeout{},send_wait_timeout{};
    bool (*uri_match_fn)(const char*,const char*,size_t){};
};
struct httpd_ssl_config_t {
    httpd_config_t httpd{};
    unsigned port_secure{};
    const unsigned char* cacert_pem{}; size_t cacert_len{};
    const unsigned char* prvtkey_pem{}; size_t prvtkey_len{};
};
#define HTTPD_SSL_CONFIG_DEFAULT() httpd_ssl_config_t{}
inline httpd_ssl_config_t tls_settings;
inline int httpd_ssl_start(httpd_handle_t* handle,httpd_ssl_config_t* config) { tls_settings=*config; *handle=reinterpret_cast<void*>(1); return ESP_OK; }
inline void httpd_ssl_stop(httpd_handle_t) { routes.clear(); }
inline int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t* route) { routes.push_back(*route); return ESP_OK; }
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
