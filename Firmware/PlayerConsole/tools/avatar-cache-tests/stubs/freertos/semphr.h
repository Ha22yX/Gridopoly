#pragma once
#include "FreeRTOS.h"
#include <mutex>
using SemaphoreHandle_t = std::mutex *;
inline std::mutex avatarTestMutex;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return &avatarTestMutex; }
inline void xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t) { mutex->lock(); }
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
