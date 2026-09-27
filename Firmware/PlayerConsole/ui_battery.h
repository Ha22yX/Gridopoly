#pragma once
#include <lvgl.h>
#include "battery_telemetry.h"
#include "ui_layout.h"

constexpr UiRect kBatteryBadge{172, 450, 136, 16};
static_assert(uiRectInsideCircle(kBatteryBadge, 238), "Battery status must fit the circular display");

struct UiBattery {
    lv_obj_t *root = nullptr;
    lv_obj_t *label = nullptr;
    char previousText[24]{};
    uint32_t previousColor = 0;
    uint32_t labelUpdates = 0;
};

// Called only inside the existing LVGL lock. Owns no page, input or focus state.
void uiBatteryRender(UiBattery &ui, const gridopoly::player_console::BatteryStatus &status, uint32_t nowMs);
