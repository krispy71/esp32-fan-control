#pragma once
#include "FreeRTOS.h"
#include <condition_variable>
#include <mutex>
struct SemaphoreSpy { std::mutex mutex; std::condition_variable ready; bool signaled=false; };
using SemaphoreHandle_t=SemaphoreSpy*;
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return WebSpy::fail_semaphore ? nullptr : new SemaphoreSpy; }
inline int xSemaphoreTake(SemaphoreHandle_t value,uint32_t) { std::unique_lock<std::mutex> lock(value->mutex); value->ready.wait(lock,[&] { return value->signaled; }); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t value) { std::lock_guard<std::mutex> lock(value->mutex); value->signaled=true; value->ready.notify_one(); }
inline void vSemaphoreDelete(SemaphoreHandle_t value) { delete value; }
