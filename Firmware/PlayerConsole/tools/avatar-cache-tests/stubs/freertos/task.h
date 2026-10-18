#pragma once
#include "FreeRTOS.h"
// Execute explicit interleavings in tests; never start production's infinite loop.
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t,
                                         void *context, int, TaskHandle_t *task, int) {
    *task = context;
    return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelay(uint32_t) {}
inline void ulTaskNotifyTake(int, uint32_t) {}
