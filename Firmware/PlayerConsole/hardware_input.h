#pragma once

#include <stdint.h>

#include "app_types.h"

bool hardwareInputBegin();
bool hardwareInputPoll(InputEvent &event);

#if GRIDOPOLY_SELF_TEST == 1
void hardwareInputTestReset();
void hardwareInputTestEnqueue(const InputEvent &event);
#endif

// Bounded callback trace, drained by the main task (never printed in the timer).
struct HardwareInputDiagnostic {
    uint32_t timestampMs = 0, invalidTransitions = 0, droppedTraces = 0;
    uint8_t rawPhases = 0, stablePhases = 0;
    int8_t emittedStep = 0;
    uint8_t queueEntries = 0;
};
bool hardwareInputPollDiagnostic(HardwareInputDiagnostic &diagnostic);
void hardwareInputClearDiagnostics();
