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
    lv_obj_t *pulseTarget = nullptr;
    int16_t trackRestY = 0;
    int16_t footerRestY = 0;
    int16_t pulseRestY = 0;
    uint32_t pulseRevision = 0;
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
void uiCenterListDestroy(UiCenterList &list);
int8_t uiCenterListSwipeStep(int16_t dx, int16_t dy);
void uiCenterListBoundaryPulse(UiCenterList &list, int8_t direction,
                               uint32_t revision);
