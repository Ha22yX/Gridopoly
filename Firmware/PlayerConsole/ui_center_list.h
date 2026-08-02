#pragma once

#include <stdint.h>

#include <lvgl.h>

struct UiListItemView {
    const char *title;
    const char *meta;
    const char *disabledReason;
    bool enabled;
    bool checked;
};

struct UiCenterList {
    lv_obj_t *viewport = nullptr;
    lv_obj_t *track = nullptr;
    lv_obj_t *focusFrame = nullptr;
    lv_obj_t *progressFill = nullptr;
    lv_obj_t *countLabel = nullptr;
    lv_obj_t *footer = nullptr;
    uint8_t selected = 0;
    uint8_t count = 0;
};

void uiCenterListCreate(UiCenterList &list, lv_obj_t *parent,
                        const UiListItemView *items, uint8_t count,
                        uint8_t selected, const char *footerText,
                        bool footerEnabled, bool footerFocused);
void uiCenterListUpdate(UiCenterList &list, const UiListItemView *items,
                        uint8_t count, uint8_t selected,
                        const char *footerText, bool footerEnabled,
                        bool footerFocused, bool animate);
int8_t uiCenterListSwipeStep(int16_t dx, int16_t dy);
