#pragma once

#include <stdint.h>

#include "app_types.h"

bool hardwareInputBegin();
bool hardwareInputPoll(InputEvent &event);
bool hardwareInputPending();

#if GRIDOPOLY_SELF_TEST == 1
void hardwareInputTestReset();
void hardwareInputTestEnqueue(const InputEvent &event);
#endif

// Bounded callback trace, drained by the main task (never printed in the timer).
struct HardwareInputDiagnostic {
    uint32_t timestampMs = 0, invalidTransitions = 0, droppedTraces = 0;
    uint32_t sampleGapUs = 0, maxSampleGapUs = 0, inputQueueDropped = 0;
    uint8_t rawPhases = 0, stablePhases = 0;
    int8_t emittedStep = 0;
    uint8_t queueEntries = 0;
};
// Saturating boot counters survive trace ON/OFF and USB reconnects. A timing
// gap describes actual software observations, not a count of physical edges.
struct HardwareInputStatistics {
    uint32_t samples = 0, lastSampleGapUs = 0, maxSampleGapUs = 0;
    uint32_t decodedSteps = 0, positiveSteps = 0, negativeSteps = 0;
    uint32_t invalidTransitions = 0, inputQueueDropped = 0;
    uint32_t droppedTraces = 0;
    uint8_t queueEntries = 0;
};
HardwareInputStatistics hardwareInputStatistics();
bool hardwareInputPollDiagnostic(HardwareInputDiagnostic &diagnostic);
void hardwareInputClearDiagnostics();
