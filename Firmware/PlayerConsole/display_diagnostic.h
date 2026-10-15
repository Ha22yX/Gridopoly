#pragma once

#include "display_config.h"

#ifndef GRIDOPOLY_DISPLAY_DIAGNOSTIC
#define GRIDOPOLY_DISPLAY_DIAGNOSTIC 0
#endif
#if GRIDOPOLY_DISPLAY_DIAGNOSTIC != 0 && GRIDOPOLY_DISPLAY_DIAGNOSTIC != 1
#error "Unsupported display diagnostic profile"
#endif

// A single bounded observation window, started when the normal loop is ready.
// Subtraction keeps the deadline correct across millis() wrap. No catch-up burst.
struct DisplayDiagnosticWindow {
    bool started = false;
    bool finished = false;
    uint32_t startedMs = 0;
    uint32_t lastSampleMs = 0;

    bool sampleDue(uint32_t nowMs) {
        if (finished) return false;
        if (!started) {
            started = true;
            startedMs = lastSampleMs = nowMs;
            return false;
        }
        if (nowMs - startedMs >= 120000u) {
            finished = true;
            return true;
        }
        if (nowMs - lastSampleMs < 5000u) return false;
        lastSampleMs = nowMs;
        return true;
    }
};
