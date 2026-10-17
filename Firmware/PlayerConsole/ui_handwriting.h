#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "ui_layout.h"

// Creates a touch canvas which records only physical samples inside the canvas.
// Adjacent samples are joined during one observed physical contact. Release,
// leaving the canvas and unavailable controller samples break that contact;
// a scheduler delay alone does not.
// Recognition runs locally after the canvas has received no touch for 1.2 seconds.
lv_obj_t *uiHandwritingCreate(lv_obj_t *parent, UiRect rect, uint32_t background,
                              uint32_t border, uint32_t ink);
// Called for every controller sample under the LVGL lock, including releases.
// Sampling does not depend on how often the application loop can render.
void uiHandwritingRecordTouch(bool pressed, int16_t screenX, int16_t screenY,
                              uint32_t sampledAtMs);
void uiHandwritingTouchUnavailable(uint32_t nowMs);
bool uiHandwritingPoll(char &character, uint32_t nowMs, bool finishLetter = false);
bool uiHandwritingHasInk();
bool uiHandwritingNeedsCorrection();
bool uiHandwritingClearInk();
const char *uiHandwritingStatusText();
void uiHandwritingReset();

constexpr uint32_t kHandwritingRecognitionIdleMs = 1200;
constexpr uint32_t kHandwritingStrokeJoinMs = 100;

struct UiHandwritingSample {
    int16_t x;
    int16_t y;
    uint32_t sampledAtMs;
    bool connectsPrevious;
};

// Legacy time-only diagnostic; production capture uses the contact overload.
bool uiHandwritingSamplesShouldConnect(uint32_t previousSampleMs,
                                       uint32_t currentSampleMs);
// Stroke-boundary contract shared by drawing, recognition, and self-tests.
bool uiHandwritingSamplesShouldConnect(bool continuingPhysicalContact,
                                       uint32_t previousSampleMs,
                                       uint32_t currentSampleMs);

// Pure recognizer entry point used by the hardware path and regressions.
// Clear geometric matches use stroke-aware templates; remaining shapes use
// the quantized EMNIST network, with templates for ambiguous logits.
char uiHandwritingRecognizeSamples(const UiHandwritingSample *samples,
                                   uint16_t sampleCount);
float uiHandwritingNeuralTestAccuracy();

// Pure timing contract used by the hardware implementation and self-tests.
// Outside-screen touches do not reset the canvas timer; touchingCanvas does.
bool uiHandwritingRecognitionDue(bool touchingCanvas, uint16_t sampledPointCount,
                                 uint32_t lastCanvasTouchMs, uint32_t nowMs);

// LVGL-lock owner only. Read-only diagnostics; enabling never changes captured ink.
struct UiHandwritingTraceState {
    uint32_t generation = 0, pendingAgeMs = 0, drawUs = 0, drawLagMs = 0, drawAtMs = 0, fallbackUs = 0;
    uint16_t points = 0, drawn = 0, fallbackCount = 0;
    int16_t originX = 0, originY = 0;
    bool canvas = false, contact = false, connects = false, recognizerReady = false;
};
void uiHandwritingTraceSession(uint32_t session);
UiHandwritingTraceState uiHandwritingTraceState(uint32_t nowMs);
