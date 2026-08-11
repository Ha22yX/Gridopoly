#pragma once

#include <stdint.h>

#include <lvgl.h>

struct UiListItemView {
    const char *title;
    const char *meta;
    const char *disabledReason;
    bool enabled;
    bool checked;
    uint32_t accent = 0;
    const char *groupCode = "";
    bool checkable = false;
    // Optional compact semantic tag rendered as a fixed pill at row right.
    const char *tag = "";
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
    bool selectionInitialized = false;
    bool footerFocused = false;
#if GRIDOPOLY_SELF_TEST == 1
    uint32_t rowSelectionUpdates = 0;
    uint32_t labelTextWrites = 0;
    uint32_t clickableFlagWrites = 0;
#endif
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
bool uiCenterListDispatchSwipe(lv_obj_t *target, lv_indev_t *input,
                               lv_point_t start, lv_point_t finish);
void uiCenterListBoundaryPulse(UiCenterList &list, int8_t direction,
                               uint32_t revision);
