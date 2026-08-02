#include "ui_center_list.h"

#include <stdio.h>

#include "src/fonts/ui_fonts.h"
#include "ui_layout.h"
#include "ui_motion.h"
#include "ui_primitives.h"

namespace {

constexpr uint32_t kPanel = 0x11191B;
constexpr uint32_t kLine = 0x263234;
constexpr uint32_t kText = 0xEDF3F1;
constexpr uint32_t kMuted = 0x81908C;
constexpr uint32_t kAccent = 0x52DCB7;
constexpr uint32_t kSelected = 0x16302A;
constexpr int16_t kRowHeight = 50;
constexpr int16_t kRowStride = 58;
constexpr int16_t kProgressWidth = 72;
constexpr int16_t kProgressHeight = 4;
constexpr int16_t kSwipeThreshold = 36;
constexpr int16_t kSwipeDominance = 12;
constexpr int16_t kTrackBoundaryPulsePx = 8;
constexpr int16_t kFooterBoundaryPulsePx = 6;
constexpr uint32_t kBoundaryPulseOutMs = 60;
constexpr uint32_t kBoundaryPulseReturnMs = 120;

static_assert(kNormalListViewport.h / kRowStride <= 3,
              "normal center list shows at most three full rows");
static_assert(kNormalListFocus.w < kNormalListViewport.w &&
              kNormalListFocus.h == kRowHeight,
              "center-list row must fit its named viewport and focus frame");

struct UiSwipeContext {
    lv_obj_t *viewport = nullptr;
    lv_obj_t *previousTarget = nullptr;
    lv_obj_t *nextTarget = nullptr;
    lv_point_t start{};
    bool tracking = false;
    lv_obj_t *suppressedClickTarget = nullptr;
};

UiSwipeContext swipeContext{};

UiRect listChildRect(UiRect absolute)
{
    return UiRect{static_cast<int16_t>(absolute.x - kNormalListViewport.x),
                  static_cast<int16_t>(absolute.y - kNormalListViewport.y),
                  absolute.w, absolute.h};
}

void setTrackY(void *object, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value));
}

void setBoundaryPulseY(void *object, int32_t value)
{
    UiCenterList &list = *static_cast<UiCenterList *>(object);
    if (list.pulseTarget != nullptr) {
        lv_obj_set_y(list.pulseTarget, static_cast<lv_coord_t>(value));
    }
}

void finishBoundaryPulse(lv_anim_t *completed)
{
    UiCenterList &list = *static_cast<UiCenterList *>(completed->var);
    list.pulseTarget = nullptr;
}

void startBoundaryReturn(UiCenterList &list)
{
    if (list.pulseTarget == nullptr) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &list);
    lv_anim_set_exec_cb(&animation, setBoundaryPulseY);
    lv_anim_set_values(&animation, lv_obj_get_y(list.pulseTarget), list.pulseRestY);
    lv_anim_set_time(&animation, kBoundaryPulseReturnMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, finishBoundaryPulse);
    lv_anim_start(&animation);
}

void returnBoundaryPulse(lv_anim_t *completed)
{
    UiCenterList &list = *static_cast<UiCenterList *>(completed->var);
    startBoundaryReturn(list);
}

lv_obj_t *cancelBoundaryPulse(UiCenterList &list)
{
    lv_obj_t *target = list.pulseTarget;
    lv_anim_del(&list, setBoundaryPulseY);
    list.pulseTarget = nullptr;
    return target;
}

void animateTrack(lv_obj_t *track, int16_t targetY, bool animate)
{
    if (track == nullptr) return;
    lv_obj_update_layout(track);
    const int16_t currentY = lv_obj_get_y(track);
    lv_anim_del(track, setTrackY);
    if (!animate || currentY == targetY) {
        lv_obj_set_y(track, targetY);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, track);
    lv_anim_set_exec_cb(&animation, setTrackY);
    lv_anim_set_values(&animation, currentY, targetY);
    lv_anim_set_time(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void styleRow(lv_obj_t *row, const UiListItemView &item, bool selected,
              bool footerFocused)
{
    if (row == nullptr) return;
    const bool focused = selected && !footerFocused;
    lv_obj_set_style_bg_color(row, lv_color_hex(focused ? kSelected : kPanel), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(focused ? kAccent : kLine), 0);
    lv_obj_set_style_border_width(row, focused ? 2 : 1, 0);
    if (item.enabled) lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_obj_get_child(row, 0);
    lv_obj_t *meta = lv_obj_get_child(row, 1);
    lv_obj_t *check = lv_obj_get_child(row, 2);
    const char *metaText = !item.enabled && item.disabledReason != nullptr &&
                                   item.disabledReason[0] != '\0'
                               ? item.disabledReason
                               : (item.meta == nullptr ? "" : item.meta);
    if (title != nullptr) {
        lv_label_set_text(title, item.title == nullptr ? "" : item.title);
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(item.enabled ? (focused ? kAccent : kText) : kMuted), 0);
    }
    if (meta != nullptr) {
        lv_label_set_text(meta, metaText);
        lv_obj_set_style_text_color(meta, lv_color_hex(kMuted), 0);
    }
    if (check != nullptr) {
        lv_label_set_text(check, item.checked ? "[x]" : "");
        lv_obj_set_style_text_color(check, lv_color_hex(kAccent), 0);
    }
}

void destroyListObjects(UiCenterList &list)
{
    if (swipeContext.viewport == list.viewport) swipeContext = UiSwipeContext{};
    lv_obj_t *progressTrack = list.progressFill == nullptr
                                  ? nullptr
                                  : lv_obj_get_parent(list.progressFill);
    if (list.track != nullptr) lv_anim_del(list.track, setTrackY);
    lv_anim_del(&list, setBoundaryPulseY);
    if (list.viewport != nullptr) lv_obj_del(list.viewport);
    if (list.focusFrame != nullptr) lv_obj_del(list.focusFrame);
    if (progressTrack != nullptr) lv_obj_del(progressTrack);
    if (list.countLabel != nullptr) lv_obj_del(list.countLabel);
    if (list.footer != nullptr) lv_obj_del(list.footer);
    list = UiCenterList{};
}

lv_indev_t *eventInput(lv_event_t *event)
{
    lv_indev_t *input = event == nullptr ? nullptr : lv_event_get_indev(event);
    return input == nullptr ? lv_indev_get_act() : input;
}

bool readPointer(lv_event_t *event, lv_point_t &point)
{
    lv_indev_t *input = eventInput(event);
    if (input == nullptr) return false;
    lv_indev_get_point(input, &point);
    return true;
}

void swipeCallback(lv_event_t *event)
{
    if (event == nullptr) return;
    const lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);
    if (lv_event_get_current_target(event) != target) return;
    if (code == LV_EVENT_CLICKED && swipeContext.suppressedClickTarget == target) {
        swipeContext.suppressedClickTarget = nullptr;
        lv_event_stop_processing(event);
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        swipeContext.suppressedClickTarget = nullptr;
        swipeContext.tracking = readPointer(event, swipeContext.start);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        swipeContext.tracking = false;
        return;
    }
    if (code == LV_EVENT_RELEASED && swipeContext.tracking) {
        lv_point_t finish{};
        swipeContext.tracking = false;
        if (!readPointer(event, finish)) return;
        const int8_t step = uiCenterListSwipeStep(
            static_cast<int16_t>(finish.x - swipeContext.start.x),
            static_cast<int16_t>(finish.y - swipeContext.start.y)
        );
        lv_indev_t *input = eventInput(event);
        if (step != 0 && input != nullptr) {
            swipeContext.suppressedClickTarget = target;
            lv_indev_reset(input, target);
        }
        if (step < 0 && swipeContext.previousTarget != nullptr) {
            lv_event_send(swipeContext.previousTarget, LV_EVENT_CLICKED, nullptr);
        } else if (step > 0 && swipeContext.nextTarget != nullptr) {
            lv_event_send(swipeContext.nextTarget, LV_EVENT_CLICKED, nullptr);
        }
    }
}

lv_obj_t *createEventTarget(lv_obj_t *parent, UiEventKind kind)
{
    lv_obj_t *target = lv_obj_create(parent);
    if (target == nullptr) return nullptr;
    lv_obj_remove_style_all(target);
    lv_obj_set_size(target, 1, 1);
    lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    uiBindTap(target, kind);
    return target;
}

} // namespace

int8_t uiCenterListSwipeStep(int16_t dx, int16_t dy)
{
    const int32_t absoluteX = dx < 0 ? -static_cast<int32_t>(dx) : dx;
    const int32_t absoluteY = dy < 0 ? -static_cast<int32_t>(dy) : dy;
    if (absoluteY < kSwipeThreshold) return 0;
    if (absoluteY < absoluteX + kSwipeDominance) return 0;
    return dy < 0 ? 1 : -1;
}

void uiCenterListCreate(UiCenterList &list, lv_obj_t *parent,
                        const UiListItemView *items, uint8_t count,
                        uint8_t selected, const char *footerText,
                        bool footerEnabled, bool footerFocused)
{
    destroyListObjects(list);
    if (parent == nullptr || (count != 0 && items == nullptr)) return;

    list.viewport = uiBox(parent, kNormalListViewport, 0x090E10, 0x090E10, 0);
    if (list.viewport == nullptr) return;
    lv_obj_set_style_bg_opa(list.viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list.viewport, 0, 0);
    lv_obj_clear_flag(list.viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(list.viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(list.viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(list.viewport, swipeCallback, LV_EVENT_ALL, nullptr);

    const int16_t trackHeight = count == 0 ? 1 :
        static_cast<int16_t>((count - 1) * kRowStride + kRowHeight);
    list.track = uiBox(list.viewport, UiRect{0, 0, kNormalListViewport.w, trackHeight},
                       0x090E10, 0x090E10, 0);
    if (list.track != nullptr) {
        lv_obj_set_style_bg_opa(list.track, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list.track, 0, 0);
        lv_obj_clear_flag(list.track, LV_OBJ_FLAG_SCROLLABLE);
        for (uint8_t index = 0; index < count; ++index) {
            const UiRect rowRect{static_cast<int16_t>(kNormalListFocus.x - kNormalListViewport.x),
                                 static_cast<int16_t>(index * kRowStride),
                                 kNormalListFocus.w, kRowHeight};
            lv_obj_t *row = uiBox(list.track, rowRect, kPanel, kLine, 6);
            if (row == nullptr) continue;
            lv_obj_add_event_cb(row, swipeCallback, LV_EVENT_ALL, nullptr);
            uiLabel(row, "", UiRect{12, 8, 138, 28}, &ui_font_16, kText,
                    LV_TEXT_ALIGN_LEFT);
            uiLabel(row, "", UiRect{150, 8, 90, 28}, &ui_font_14, kMuted,
                    LV_TEXT_ALIGN_RIGHT);
            uiLabel(row, "", UiRect{242, 8, 22, 28}, &lv_font_montserrat_14,
                    kAccent, LV_TEXT_ALIGN_RIGHT);
            uiBindTap(row, UiEventKind::SelectListItem, index);
            lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }

    swipeContext.viewport = list.viewport;
    swipeContext.previousTarget = createEventTarget(list.viewport, UiEventKind::ListPrevious);
    swipeContext.nextTarget = createEventTarget(list.viewport, UiEventKind::ListNext);

    list.focusFrame = uiBox(parent, kNormalListFocus, 0x090E10, kAccent, 6);
    if (list.focusFrame != nullptr) {
        lv_obj_set_style_bg_opa(list.focusFrame, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(list.focusFrame, LV_OBJ_FLAG_CLICKABLE);
    }

    const UiRect progressRect{
        static_cast<int16_t>(kNormalListProgress.x +
                             (kNormalListProgress.w - kProgressWidth) / 2),
        static_cast<int16_t>(kNormalListProgress.y +
                             (kNormalListProgress.h - kProgressHeight) / 2),
        kProgressWidth, kProgressHeight
    };
    lv_obj_t *progressTrack = uiBox(parent, progressRect, kLine, kLine, 2);
    if (progressTrack != nullptr) {
        lv_obj_set_style_border_width(progressTrack, 0, 0);
        list.progressFill = uiBox(progressTrack, UiRect{0, 0, 0, kProgressHeight},
                                  kAccent, kAccent, 2);
        if (list.progressFill != nullptr) {
            lv_obj_set_style_border_width(list.progressFill, 0, 0);
            lv_obj_clear_flag(list.progressFill, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    list.countLabel = uiLabel(parent, "", kNormalListCount, &lv_font_montserrat_14, kMuted);
    list.footer = uiBox(parent, kNormalFooter, kPanel, kLine, 6);
    if (list.footer != nullptr) {
        uiLabel(list.footer, footerText == nullptr ? "" : footerText,
                UiRect{8, 16, static_cast<int16_t>(kNormalFooter.w - 16), 28},
                &ui_font_16, kText);
        uiBindTap(list.footer, UiEventKind::SelectFooter);
    }

    list.count = count;
    list.footerRestY = kNormalFooter.y;
    uiCenterListUpdate(list, items, count, selected, footerText,
                       footerEnabled, footerFocused, false);
}

void uiCenterListDestroy(UiCenterList &list)
{
    destroyListObjects(list);
}

void uiCenterListBoundaryPulse(UiCenterList &list, int8_t direction,
                               uint32_t revision)
{
    if (direction == 0 || revision == list.pulseRevision) return;
    const bool firstRow = direction < 0;
    lv_obj_t *target = firstRow ? list.track : list.footer;
    if (target == nullptr) return;
    list.pulseRevision = revision;
    lv_obj_update_layout(target);
    cancelBoundaryPulse(list);
    list.pulseTarget = target;
    list.pulseRestY = firstRow ? list.trackRestY : list.footerRestY;
    const int16_t currentY = lv_obj_get_y(target);
    const int16_t offset = firstRow ? kTrackBoundaryPulsePx : kFooterBoundaryPulsePx;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &list);
    lv_anim_set_exec_cb(&animation, setBoundaryPulseY);
    lv_anim_set_values(&animation, currentY, static_cast<int16_t>(list.pulseRestY + offset));
    lv_anim_set_time(&animation, kBoundaryPulseOutMs);
    lv_anim_set_ready_cb(&animation, returnBoundaryPulse);
    lv_anim_start(&animation);
}

void uiCenterListUpdate(UiCenterList &list, const UiListItemView *items,
                        uint8_t count, uint8_t selected,
                        const char *footerText, bool footerEnabled,
                        bool footerFocused, bool animate)
{
    if (list.viewport == nullptr || list.track == nullptr ||
        (count != 0 && items == nullptr)) return;
    if (count != list.count) {
        lv_obj_t *parent = lv_obj_get_parent(list.viewport);
        uiCenterListCreate(list, parent, items, count, selected, footerText,
                           footerEnabled, footerFocused);
        return;
    }
    if (count == 0) selected = 0;
    else if (selected >= count) selected = count - 1;
    list.selected = selected;
    lv_obj_t *const interruptedPulse = cancelBoundaryPulse(list);

    for (uint8_t index = 0; index < count; ++index) {
        styleRow(lv_obj_get_child(list.track, index), items[index], index == selected,
                 footerFocused);
    }
    const int16_t selectedTop = static_cast<int16_t>(kNormalListFocus.y -
                                                      kNormalListViewport.y);
    list.trackRestY = uiCenterListTrackY(selected, selectedTop, kRowStride);
    animateTrack(list.track, list.trackRestY, animate);

    if (interruptedPulse == list.footer && list.footer != nullptr &&
        lv_obj_get_y(list.footer) != list.footerRestY) {
        list.pulseTarget = list.footer;
        list.pulseRestY = list.footerRestY;
        startBoundaryReturn(list);
    }

    if (list.focusFrame != nullptr) {
        lv_obj_set_style_border_color(list.focusFrame,
                                      lv_color_hex(footerFocused ? kLine : kAccent), 0);
        lv_obj_set_style_border_width(list.focusFrame, footerFocused ? 1 : 2, 0);
    }
    const uint16_t progress = uiListProgressPermille(selected, count);
    if (list.progressFill != nullptr) {
        lv_obj_set_width(list.progressFill,
                         static_cast<int16_t>(kProgressWidth * progress / 1000));
    }
    if (list.countLabel != nullptr) {
        char value[16];
        snprintf(value, sizeof(value), "%u / %u", count == 0 ? 0 : selected + 1, count);
        lv_label_set_text(list.countLabel, value);
    }
    if (list.footer != nullptr) {
        lv_obj_set_style_bg_color(list.footer, lv_color_hex(footerFocused ? kSelected : kPanel), 0);
        lv_obj_set_style_border_color(list.footer,
                                      lv_color_hex(footerFocused ? kAccent : kLine), 0);
        lv_obj_set_style_border_width(list.footer, footerFocused ? 2 : 1, 0);
        if (footerEnabled) lv_obj_add_flag(list.footer, LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_clear_flag(list.footer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *footerLabel = lv_obj_get_child(list.footer, 0);
        if (footerLabel != nullptr) {
            lv_label_set_text(footerLabel, footerText == nullptr ? "" : footerText);
            lv_obj_set_style_text_color(
                footerLabel,
                lv_color_hex(footerEnabled ? (footerFocused ? kAccent : kText) : kMuted), 0
            );
        }
    }
}
