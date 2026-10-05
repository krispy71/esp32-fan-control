#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <chrono>
inline int xTaskCreatePinnedToCore(void(*entry)(void*),const char*,uint32_t,void* context,int,void*,int) {
    if(WebSpy::fail_task) return 0;
    ++WebSpy::tasks;
    std::thread([entry,context] { entry(context); --WebSpy::tasks; }).detach(); return pdPASS;
}
inline void vTaskDelay(uint32_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
inline void vTaskDelete(void*) {}
