#pragma once
#include "../../host-stubs/Arduino.h"
#include <algorithm>
#include <cstdlib>
using std::min;
using std::max;
using SemaphoreHandle_t = void *;
using TaskHandle_t = void *;
constexpr int pdTRUE = 1, pdPASS = 1;
constexpr unsigned portMAX_DELAY = ~0u;
inline uint32_t hostMillis = 0;
inline uint32_t millis() { return hostMillis; }
inline uint32_t micros() { return hostMillis * 1000u; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline unsigned ulTaskNotifyTake(int, unsigned) { return 0; }
inline void xTaskNotifyGive(TaskHandle_t) {}
// Exercise the same production classifier through its allocation-fallback path.
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *,
                                  unsigned, TaskHandle_t *, int) { return 0; }
