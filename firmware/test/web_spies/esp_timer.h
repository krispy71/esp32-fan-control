#pragma once
#include <atomic>
#include <cstdint>
namespace WebSpy { inline std::atomic<int64_t> now_us{0}; }
inline int64_t esp_timer_get_time() { return WebSpy::now_us; }
