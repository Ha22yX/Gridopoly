#include "ui_battery.h"
#include <stdio.h>
#include <string.h>

namespace {
void passive(lv_obj_t *object)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
        LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
}
void textIfChanged(UiBattery &ui, lv_obj_t *label, char *previous, const char *text)
{
    if (strcmp(previous, text) == 0) return;
    strcpy(previous, text);
    lv_label_set_text(label, text);
    ++ui.labelUpdates;
}
}

void uiBatteryRender(UiBattery &ui, const gridopoly::player_console::BatteryStatus &status, uint32_t nowMs)
{
    using namespace gridopoly::player_console;
    if (!status.enabled) {
        if (ui.root != nullptr) lv_obj_del(ui.root);
        ui = UiBattery{};
        return;
    }
    if (ui.root == nullptr) {
        ui.root = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(ui.root);
        lv_obj_set_pos(ui.root, kBatteryBadge.x, kBatteryBadge.y);
        lv_obj_set_size(ui.root, kBatteryBadge.w, kBatteryBadge.h);
        passive(ui.root);
        ui.label = lv_label_create(ui.root);
        passive(ui.label);
        lv_obj_set_pos(ui.label, 0, 0); lv_obj_set_size(ui.label, kBatteryBadge.w, kBatteryBadge.h);
        lv_obj_set_style_text_font(ui.label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(ui.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(ui.label, "");
    }
    const bool measurement = status.measurementValid &&
        static_cast<uint32_t>(nowMs - status.measurementAtMs) <= BatteryTelemetry::kMeasurementMaxAgeMs;
    const bool gpio = status.gpioValid &&
        static_cast<uint32_t>(nowMs - status.gpioAtMs) <= BatteryTelemetry::kGpioMaxAgeMs;
    char percent[5] = "--%";
    if (measurement) snprintf(percent, sizeof(percent), "%u%%", status.percent > 100 ? 100 : status.percent);
    const char *power = "POWER --";
    uint32_t color = measurement ? (status.percent <= 15 ? 0xF2C453 : 0x81908C) : 0x81908C;
    if (gpio) {
        if (status.charge == ChargeIndicator::FaultCandidate) { power = "CHECK CHG"; color = 0xF2C453; }
        else if (status.charge == ChargeIndicator::Inconsistent) { power = "CHECK PWR"; color = 0xF2C453; }
        else if (status.external == ExternalPower::Present)
            power = status.charge == ChargeIndicator::Active ? "EXT / CHG" : "EXT";
        else if (status.external == ExternalPower::Absent) power = "NO EXT";
    }
    char text[24];
    snprintf(text, sizeof(text), "%s  %s", percent, power);
    textIfChanged(ui, ui.label, ui.previousText, text);
    if (color != ui.previousColor) {
        lv_obj_set_style_text_color(ui.label, lv_color_hex(color), 0);
        ui.previousColor = color;
    }
}
