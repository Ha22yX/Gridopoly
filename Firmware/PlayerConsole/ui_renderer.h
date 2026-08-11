#pragma once

#include <stdint.h>

#include "app_types.h"

bool uiRendererBegin();
void uiRendererRender(const AppState &state, uint32_t nowMs);
void uiRendererInvalidateArtwork();
bool uiRendererPollTouch(TouchAction &action);
bool uiRendererPollHandwriting(char &character, uint32_t nowMs);
void uiRendererShowFault(const char *code);

struct UiRendererTestStats {
    uint32_t incrementalRenders = 0;
    uint32_t rebuildRenders = 0;
    uint32_t artworkPrefetches = 0;
};

void uiRendererResetForTest();
void uiRendererResetTestStats();
UiRendererTestStats uiRendererGetTestStats();
