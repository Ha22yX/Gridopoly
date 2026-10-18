#pragma once
#include <cstdint>
using BaseType_t = int;
using TaskHandle_t = void *;
constexpr int pdPASS = 1, pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }
