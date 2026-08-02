#include "ui_primitives.h"

namespace {

UiEventSink eventSink = nullptr;

void baseObject(lv_obj_t *object)
{
    if (object == nullptr) return;
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

intptr_t encodeEvent(UiEventKind kind, int16_t value)
{
    return (static_cast<intptr_t>(static_cast<uint8_t>(kind)) << 16) |
           static_cast<uint16_t>(value);
}

UiEvent decodeEvent(lv_event_t *event)
{
    if (event == nullptr) return UiEvent{};
    const intptr_t encoded = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    return UiEvent{static_cast<UiEventKind>((encoded >> 16) & 0xff),
                   static_cast<int16_t>(encoded & 0xffff)};
}

void tapCallback(lv_event_t *event)
{
    if (event == nullptr) return;
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && eventSink != nullptr) eventSink(decodeEvent(event));
}

void holdCallback(lv_event_t *event)
{
    if (event == nullptr || eventSink == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) eventSink(UiEvent{UiEventKind::HoldDown, 0});
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        eventSink(UiEvent{UiEventKind::HoldUp, 0});
    }
}

}

void uiSetEventSink(UiEventSink sink)
{
    eventSink = sink;
}

lv_obj_t *uiBox(lv_obj_t *parent, UiRect rect, uint32_t bg, uint32_t border, uint8_t radius)
{
    if (parent == nullptr) return nullptr;
    lv_obj_t *object = lv_obj_create(parent);
    if (object == nullptr) return nullptr;
    baseObject(object);
    lv_obj_set_pos(object, rect.x, rect.y);
    lv_obj_set_size(object, rect.w, rect.h);
    lv_obj_set_style_bg_color(object, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, radius, 0);
    return object;
}

lv_obj_t *uiLabel(lv_obj_t *parent, const char *text, UiRect rect, const lv_font_t *font,
                  uint32_t color, lv_text_align_t align)
{
    if (parent == nullptr) return nullptr;
    lv_obj_t *object = lv_label_create(parent);
    if (object == nullptr) return nullptr;
    baseObject(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(object, text);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_size(object, rect.w, rect.h);
    lv_obj_set_pos(object, rect.x, rect.y);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, align, 0);
    return object;
}

void uiBindTap(lv_obj_t *object, UiEventKind kind, int16_t value)
{
    if (object == nullptr) return;
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_event_cb(object, tapCallback);
    lv_obj_add_event_cb(object, tapCallback, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(encodeEvent(kind, value)));
}

void uiBindHold(lv_obj_t *object)
{
    if (object == nullptr) return;
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, holdCallback, LV_EVENT_ALL, nullptr);
}
