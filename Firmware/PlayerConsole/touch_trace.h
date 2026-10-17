#pragma once
#include "touch_trace_core.h"
// Main-loop owner: start/stop/service; LVGL owner: token/capture. No device I/O here.
void touchTraceStart(uint32_t nowMs);
void touchTraceStop(uint32_t nowMs);
void touchTraceService(uint32_t nowMs);
bool touchTraceEnabled();
uint32_t touchTraceToken(uint32_t nowMs);
void touchTraceRecord(const TouchTraceSample &sample, uint32_t token, uint32_t readStartedUs);
