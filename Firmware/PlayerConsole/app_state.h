#pragma once

#include "app_types.h"

void appInit(AppState &state, uint32_t nowMs);
void appHandleInput(AppState &state, const InputEvent &event, uint32_t nowMs);
void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs);
void appTick(AppState &state, uint32_t nowMs);
bool appCanNavigateBack(const AppState &state);
uint8_t appPageContentCount(const AppState &state);
uint8_t appFocusCount(const AppState &state);
bool appFocusIsFooter(const AppState &state);
void appHandleUiEvent(AppState &state, const UiEvent &event, uint32_t nowMs);
uint16_t appHoldProgressPermille(const AppState &state, uint32_t nowMs);
uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs);
