#pragma once

#include <stdint.h>

#include "app_types.h"

bool uiRendererBegin();
void uiRendererRender(const AppState &state, uint32_t nowMs);
bool uiRendererPollTouch(TouchAction &action);
void uiRendererShowFault(const char *code);
