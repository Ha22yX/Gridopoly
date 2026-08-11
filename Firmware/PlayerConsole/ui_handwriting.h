#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "ui_layout.h"

// Creates a touch canvas which records only physical samples inside the canvas.
// Adjacent samples are joined only during one physical contact and while their
// gap is at most 100 ms.
// Recognition runs locally after the canvas has received no touch for 1.2 seconds.
lv_obj_t *uiHandwritingCreate(lv_obj_t *parent, UiRect rect, uint32_t background,
                              uint32_t border, uint32_t ink);
bool uiHandwritingPoll(char &character, uint32_t nowMs);
void uiHandwritingReset();

constexpr uint32_t kHandwritingRecognitionIdleMs = 1200;
constexpr uint32_t kHandwritingStrokeJoinMs = 100;

struct UiHandwritingSample {
    int16_t x;
    int16_t y;
    uint32_t sampledAtMs;
    bool connectsPrevious;
};

// Pure stroke-boundary contract shared by drawing, recognition, and self-tests.
bool uiHandwritingSamplesShouldConnect(uint32_t previousSampleMs,
                                       uint32_t currentSampleMs);
bool uiHandwritingSamplesShouldConnect(bool continuingPhysicalContact,
                                       uint32_t previousSampleMs,
                                       uint32_t currentSampleMs);

// Pure recognizer entry point used by the hardware path and deterministic
// regressions. A quantized EMNIST neural network is the primary classifier;
// ambiguous logits fall back to stroke-aware template matching.
char uiHandwritingRecognizeSamples(const UiHandwritingSample *samples,
                                   uint16_t sampleCount);
float uiHandwritingNeuralTestAccuracy();

// Pure timing contract used by the hardware implementation and self-tests.
// Outside-screen touches do not reset the canvas timer; touchingCanvas does.
bool uiHandwritingRecognitionDue(bool touchingCanvas, uint16_t sampledPointCount,
                                 uint32_t lastCanvasTouchMs, uint32_t nowMs);
