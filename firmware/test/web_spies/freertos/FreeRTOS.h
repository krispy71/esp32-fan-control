#pragma once
#include <cstdint>
#include <atomic>
using TickType_t=uint32_t;
constexpr int pdPASS=1, pdTRUE=1;
constexpr uint32_t portMAX_DELAY=0xffffffff;
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
namespace WebSpy { inline bool fail_task=false, fail_semaphore=false; inline std::atomic<int> tasks{0}; }
