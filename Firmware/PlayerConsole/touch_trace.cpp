#include "touch_trace.h"
#include <Arduino.h>
#include <atomic>
namespace {
portMUX_TYPE traceMux = portMUX_INITIALIZER_UNLOCKED;
TouchTraceCore trace;
TouchTraceOutput output;
std::atomic<bool> enabled{false};
bool outputPending = false; // main-loop owner only
struct Source {
    TouchTraceStats snapshot(uint32_t nowMs) {
        portENTER_CRITICAL(&traceMux);
        const auto copy = trace.snapshot(nowMs);
        enabled.store(copy.active, std::memory_order_relaxed);
        portEXIT_CRITICAL(&traceMux);
        return copy;
    }
    bool pop(TouchTraceSample &sample) {
        portENTER_CRITICAL(&traceMux); const bool found = trace.pop(sample); portEXIT_CRITICAL(&traceMux);
        return found;
    }
} source;
}
bool touchTraceEnabled() { return enabled.load(std::memory_order_relaxed); }
void touchTraceStart(uint32_t nowMs) {
    outputPending = true;
    portENTER_CRITICAL(&traceMux); trace.start(nowMs); enabled.store(true, std::memory_order_relaxed);
    portEXIT_CRITICAL(&traceMux);
}
void touchTraceStop(uint32_t nowMs) {
    portENTER_CRITICAL(&traceMux); trace.stop(nowMs); enabled.store(false, std::memory_order_relaxed);
    portEXIT_CRITICAL(&traceMux);
}
uint32_t touchTraceToken(uint32_t nowMs) {
    if (!enabled.load(std::memory_order_relaxed)) return 0;
    portENTER_CRITICAL(&traceMux); const uint32_t token = trace.token(nowMs);
    if (!token) enabled.store(false, std::memory_order_relaxed);
    portEXIT_CRITICAL(&traceMux); return token;
}
void touchTraceRecord(const TouchTraceSample &sample, uint32_t token, uint32_t readStartedUs) {
    if (!token) return;
    portENTER_CRITICAL(&traceMux); trace.record(sample, token, readStartedUs); portEXIT_CRITICAL(&traceMux);
}
void touchTraceService(uint32_t nowMs) {
    if (!outputPending) return;
    for (unsigned i = 0; i < 4; ++i) output.tick(Serial, source, nowMs);
    if (output.closed()) outputPending = false;
}
