#pragma once
#include <lvgl.h>
#include "battery_telemetry.h"
#include "ui_layout.h"

constexpr UiRect kBatteryBadge{196, 447, 92, 26};
static_assert(uiRectInsideCircle(kBatteryBadge, 238), "Battery status must fit the circular display");

enum class UiBatteryIndicator : uint8_t { None, Charging, Warning };

struct UiBattery {
    lv_obj_t *root = nullptr;
    lv_obj_t *label = nullptr;
    lv_obj_t *shadow = nullptr;
    char previousText[24]{};
    uint32_t previousColor = 0;
    uint32_t labelUpdates = 0;
    uint32_t iconUpdates = 0;
    uint8_t percent = 0;
    bool measurement = false;
    UiBatteryIndicator indicator = UiBatteryIndicator::None;
};

// Called only inside the existing LVGL lock. Owns no page, input or focus state.
void uiBatteryRender(UiBattery &ui, const gridopoly::player_console::BatteryStatus &status, uint32_t nowMs);
