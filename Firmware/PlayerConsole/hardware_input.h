#pragma once

#include <stdint.h>

#include "app_types.h"

bool hardwareInputBegin();
bool hardwareInputPoll(InputEvent &event);

#if GRIDOPOLY_SELF_TEST == 1
void hardwareInputTestReset();
void hardwareInputTestEnqueue(const InputEvent &event);
#endif
