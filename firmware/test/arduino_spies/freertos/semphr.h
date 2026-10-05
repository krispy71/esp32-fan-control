#pragma once
#include "FreeRTOS.h"
#include "../Arduino.h"
using SemaphoreHandle_t = std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t) { mutex->lock(); assert(!Spy::busLocked); Spy::busLocked = true; Spy::event("lock"); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(Spy::activeCs == -1 && !Spy::transaction); Spy::event("unlock"); Spy::busLocked = false; mutex->unlock(); }

inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }
