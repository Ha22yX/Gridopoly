#include "ui_battery.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

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
    lv_label_set_text(ui.shadow, text);
    ++ui.labelUpdates;
}

lv_point_t polar(const lv_point_t &center, float radius, float degrees)
{
    const float radians = degrees * 0.01745329252f;
    return {static_cast<lv_coord_t>(center.x + lroundf(radius * cosf(radians))),
            static_cast<lv_coord_t>(center.y + lroundf(radius * sinf(radians)))};
}

void arc(lv_draw_ctx_t *ctx, const lv_point_t &center, uint16_t radius, uint16_t width,
         uint16_t start, uint16_t end, uint32_t color, bool rounded = false)
{
    lv_draw_arc_dsc_t style;
    lv_draw_arc_dsc_init(&style);
    style.width = width; style.color = lv_color_hex(color); style.rounded = rounded;
    lv_draw_arc(ctx, &style, &center, radius, start, end);
}

void drawBattery(lv_event_t *event)
{
    auto &ui = *static_cast<UiBattery *>(lv_event_get_user_data(event));
    auto *ctx = lv_event_get_draw_ctx(event);
    lv_area_t area; lv_obj_get_coords(ui.root, &area);
    // The body follows the SAME center as the screen's rings. A tiny terminal
    // on the right keeps this recognisable as a battery, not another progress ring.
    const lv_point_t center{static_cast<lv_coord_t>(area.x1 + 240 - kBatteryBadge.x),
                            static_cast<lv_coord_t>(area.y1 + 240 - kBatteryBadge.y)};
    const uint32_t outline = ui.measurement ? 0xC2D2DD : 0x697B8B;
    arc(ctx, center, 223, 8, 78, 79, outline, true);
    arc(ctx, center, 230, 20, 81, 99, outline);
    arc(ctx, center, 228, 16, 82, 98, 0x152333);
    if (ui.measurement && ui.percent != 0) {
        // LVGL arcs use whole degrees. Retain the sub-degree end with one
        // convex wedge so a 1% change need not wait for a 6% angular step.
        const float start = 98.0f - 16.0f * ui.percent / 100.0f;
        const uint16_t whole = static_cast<uint16_t>(ceilf(start));
        if (whole < 98) arc(ctx, center, 228, 16, whole, 98, ui.previousColor);
        if (start < whole) {
            const lv_point_t wedge[] = {polar(center, 228, start), polar(center, 228, whole),
                                       polar(center, 212, whole), polar(center, 212, start)};
            lv_draw_rect_dsc_t fill; lv_draw_rect_dsc_init(&fill);
            fill.bg_color = lv_color_hex(ui.previousColor); fill.bg_opa = LV_OPA_COVER;
            lv_draw_polygon(ctx, &fill, wedge, 4);
        }
    }
    if (ui.indicator != UiBatteryIndicator::None) {
        const lv_coord_t x = static_cast<lv_coord_t>(area.x1 + 21);
        const lv_coord_t y = static_cast<lv_coord_t>(area.y1 + 7);
        lv_draw_line_dsc_t stroke; lv_draw_line_dsc_init(&stroke);
        stroke.width = 2; stroke.round_start = stroke.round_end = true;
        // Dark underlay keeps a small symbol readable over every charge colour.
        const lv_point_t bolt[] = {{static_cast<lv_coord_t>(x+3),y},
            {x,static_cast<lv_coord_t>(y+5)}, {static_cast<lv_coord_t>(x+5),static_cast<lv_coord_t>(y+5)},
            {static_cast<lv_coord_t>(x+1),static_cast<lv_coord_t>(y+11)}};
        for (uint8_t pass = 0; pass < 2; ++pass) {
            stroke.width = pass == 0 ? 4 : 2;
            stroke.color = lv_color_hex(pass == 0 ? 0x101B28 :
                ui.indicator == UiBatteryIndicator::Warning ? 0xF4C35E : 0xFFFFFF);
            if (ui.indicator == UiBatteryIndicator::Charging) {
                for (uint8_t i = 0; i < 3; ++i) lv_draw_line(ctx, &stroke, &bolt[i], &bolt[i+1]);
            } else {
                const lv_point_t top{static_cast<lv_coord_t>(x+2),y};
                const lv_point_t bottom{static_cast<lv_coord_t>(x+2),static_cast<lv_coord_t>(y+6)};
                const lv_point_t dot{static_cast<lv_coord_t>(x+2),static_cast<lv_coord_t>(y+10)};
                lv_draw_line(ctx, &stroke, &top, &bottom);
                const lv_point_t dotEnd{dot.x,static_cast<lv_coord_t>(dot.y+1)};
                lv_draw_line(ctx, &stroke, &dot, &dotEnd);
            }
        }
    }
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
        lv_obj_add_event_cb(ui.root, drawBattery, LV_EVENT_DRAW_MAIN, &ui);
        ui.shadow = lv_label_create(ui.root);
        ui.label = lv_label_create(ui.root);
        lv_obj_t *labels[] = {ui.shadow, ui.label};
        for (auto *label : labels) {
            passive(label);
            lv_obj_set_size(label, 40, 16);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(label, "");
        }
        lv_obj_set_style_text_color(ui.shadow, lv_color_hex(0x101B28), 0);
        lv_obj_set_style_text_color(ui.label, lv_color_hex(0xFFFFFF), 0);
    }
    const bool measurement = status.measurementValid &&
        static_cast<uint32_t>(nowMs - status.measurementAtMs) <= BatteryTelemetry::kMeasurementMaxAgeMs;
    char percent[5] = "--";
    if (measurement) snprintf(percent, sizeof(percent), "%u%%", status.percent > 100 ? 100 : status.percent);
    const uint8_t clamped = status.percent > 100 ? 100 : status.percent;
    const uint32_t color = !measurement ? 0x697B8B : clamped <= 15 ? 0xFF6878 :
        clamped <= 50 ? 0xF4C35E : 0x48D597;
    const UiBatteryIndicator indicator = ui.indicatorFilter.update(status, nowMs);
    textIfChanged(ui, ui.label, ui.previousText, percent);
    if (color != ui.previousColor || clamped != ui.percent || measurement != ui.measurement || indicator != ui.indicator) {
        ui.previousColor = color; ui.percent = clamped; ui.measurement = measurement; ui.indicator = indicator;
        const lv_coord_t labelX = indicator == UiBatteryIndicator::None ? 24 : 30;
        lv_obj_set_pos(ui.label, labelX, 7);
        lv_obj_set_pos(ui.shadow, labelX+1, 8);
        lv_obj_invalidate(ui.root);
        ++ui.iconUpdates;
    }
}
