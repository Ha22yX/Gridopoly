#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "app_types.h"
#include "ui_layout.h"

using UiEventSink = void (*)(const UiEvent &event);

void uiSetEventSink(UiEventSink sink);
lv_obj_t *uiBox(lv_obj_t *parent, UiRect rect, uint32_t bg, uint32_t border,
                uint8_t radius);
lv_obj_t *uiLabel(lv_obj_t *parent, const char *text, UiRect rect,
                  const lv_font_t *font, uint32_t color,
                  lv_text_align_t align = LV_TEXT_ALIGN_CENTER);
void uiBindTap(lv_obj_t *object, UiEventKind kind, int16_t value = 0);
void uiBindHold(lv_obj_t *object);
