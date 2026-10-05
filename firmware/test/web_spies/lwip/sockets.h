#pragma once
#include <map>
#include <mutex>
constexpr int SHUT_RDWR=2;
namespace WebSpy {
inline std::mutex io_mutex;
inline std::map<int,unsigned> shutdowns, closes;
inline unsigned shutdown_count(int fd) { std::lock_guard<std::mutex> lock(io_mutex); return shutdowns[fd]; }
inline unsigned close_count(int fd) { std::lock_guard<std::mutex> lock(io_mutex); return closes[fd]; }
}
inline int lwip_shutdown(int fd,int) { std::lock_guard<std::mutex> lock(WebSpy::io_mutex); ++WebSpy::shutdowns[fd]; return 0; }
inline int lwip_close(int fd) { std::lock_guard<std::mutex> lock(WebSpy::io_mutex); ++WebSpy::closes[fd]; return 0; }
