#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t stack, void *,
                                 UBaseType_t priority, TaskHandle_t *, BaseType_t core);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t);
