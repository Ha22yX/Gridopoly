#include "ui_center_list.h"

#include <stdio.h>
#include <string.h>

#include "src/fonts/ui_fonts.h"
#include "ui_layout.h"
#include "ui_motion.h"
#include "ui_primitives.h"

namespace {

constexpr uint32_t kPanel = 0x11191B;
constexpr uint32_t kBg = 0x090E10;
constexpr uint32_t kLine = 0x263234;
constexpr uint32_t kText = 0xEDF3F1;
constexpr uint32_t kMuted = 0x81908C;
constexpr uint32_t kAccent = 0x52DCB7;
constexpr uint32_t kSelected = 0x16302A;
constexpr uint32_t kChecked = 0x102722;
constexpr int16_t kRowHeight = 50;
constexpr int16_t kRowStride = 58;
constexpr int16_t kProgressWidth = 72;
constexpr int16_t kProgressHeight = 4;
constexpr int16_t kSwipeThreshold = 36;
constexpr int16_t kSwipeDominance = 12;
constexpr uint32_t kCenterListRefreshPeriodMs = 16;
constexpr int16_t kTrackBoundaryPulsePx = 8;
constexpr int16_t kFooterBoundaryPulsePx = 6;
constexpr uint32_t kBoundaryPulseOutMs = 60;
constexpr uint32_t kBoundaryPulseReturnMs = 120;
constexpr UiRect kStandardTitle{14, 8, 150, 28};
constexpr UiRect kStandardMeta{174, 8, 84, 28};
constexpr UiRect kFullWidthTitle{14, 8, 244, 28};
constexpr UiRect kTaggedTitle{14, 8, 194, 28};
constexpr UiRect kCheckableTitle{14, 8, 136, 28};
constexpr UiRect kCheckableMeta{158, 8, 66, 28};
constexpr UiRect kCheckIndicator{232, 13, 24, 24};
constexpr UiRect kTagPill{216, 14, 40, 22};
constexpr UiRect kTagText{2, 2, 36, 18};

static_assert(kNormalListViewport.w == kNormalListFocus.w &&
              kNormalListViewport.h == 3 * kRowHeight + 2 * (kRowStride - kRowHeight),
              "normal center list clips exactly three full rows and two gaps");
static_assert(kNormalListFocus.w == kNormalListViewport.w &&
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
    lv_anim_t *running = lv_anim_get(track, setTrackY);
    if (animate && running != nullptr && running->end_value == targetY) return;
    if (running == nullptr && currentY == targetY) return;
    lv_anim_del(track, setTrackY);
    if (!animate || currentY == targetY) {
        if (currentY != targetY) lv_obj_set_y(track, targetY);
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
    lv_disp_t *display = lv_obj_get_disp(track);
    if (display != nullptr && display->refr_timer != nullptr) {
        lv_timer_ready(display->refr_timer);
    }
}

void setLabelTextIfChanged(UiCenterList &list, lv_obj_t *label,
                           const char *text)
{
    if (label == nullptr) return;
    const char *desired = text == nullptr ? "" : text;
    const char *current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, desired) == 0) return;
    lv_label_set_text(label, desired);
#if GRIDOPOLY_SELF_TEST == 1
    ++list.labelTextWrites;
#endif
}

void setClickableIfChanged(UiCenterList &list, lv_obj_t *object, bool enabled)
{
    if (object == nullptr) return;
    const bool clickable = lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE);
    if (clickable == enabled) return;
    if (enabled) lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
#if GRIDOPOLY_SELF_TEST == 1
    ++list.clickableFlagWrites;
#endif
}

void setTextColorIfChanged(lv_obj_t *object, uint32_t color)
{
    if (object == nullptr) return;
    const lv_color_t desired = lv_color_hex(color);
    if (lv_obj_get_style_text_color(object, 0).full == desired.full) return;
    lv_obj_set_style_text_color(object, desired, 0);
}

void setObjectRectIfChanged(lv_obj_t *object, UiRect rect)
{
    if (object == nullptr) return;
    if (lv_obj_get_x(object) != rect.x || lv_obj_get_y(object) != rect.y) {
        lv_obj_set_pos(object, rect.x, rect.y);
    }
    if (lv_obj_get_width(object) != rect.w || lv_obj_get_height(object) != rect.h) {
        lv_obj_set_size(object, rect.w, rect.h);
    }
}

void setFocusedLongMode(lv_obj_t *label, bool focused)
{
    if (label == nullptr) return;
    const lv_label_long_mode_t desired = focused
        ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT;
    if (lv_label_get_long_mode(label) == desired) return;
    lv_obj_set_style_opa(label, LV_OPA_40, 0);
    lv_label_set_long_mode(label, desired);
    lv_obj_fade_in(label, 120, 0);
}

void updateCheckIndicator(lv_obj_t *check, bool checkable, bool checked,
                          uint32_t accent)
{
    if (check == nullptr) return;
    if (!checkable) {
        lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(check, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(check, lv_color_hex(checked ? accent : kPanel), 0);
    lv_obj_set_style_bg_opa(check, checked ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(check, lv_color_hex(checked ? accent : kMuted), 0);
    lv_obj_set_style_border_width(check, 1, 0);
    lv_obj_t *mark = lv_obj_get_child(check, 0);
    if (mark == nullptr) return;
    if (checked) lv_obj_clear_flag(mark, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
}

void updateTag(UiCenterList &list, lv_obj_t *tag, const char *text,
               uint32_t accent)
{
    if (tag == nullptr) return;
    const bool visible = text != nullptr && text[0] != '\0';
    if (!visible) {
        lv_obj_add_flag(tag, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(tag, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tag, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(tag, 1, 0);
    lv_obj_t *label = lv_obj_get_child(tag, 0);
    setLabelTextIfChanged(list, label, text);
    setTextColorIfChanged(label, accent);
}

void applyRowSelectionStyle(UiCenterList &list, lv_obj_t *row,
                            const UiListItemView &item, bool focused)
{
    if (row == nullptr) return;
    const uint32_t accent = item.accent == 0 ? kAccent : item.accent;
    const lv_color_t background = lv_color_hex(
        focused ? kSelected : (item.checked ? kChecked : kPanel)
    );
    if (lv_obj_get_style_bg_color(row, 0).full != background.full) {
        lv_obj_set_style_bg_color(row, background, 0);
    }
    const lv_color_t border = lv_color_hex(focused ? accent : kLine);
    if (lv_obj_get_style_border_color(row, 0).full != border.full) {
        lv_obj_set_style_border_color(row, border, 0);
    }
    const lv_coord_t borderWidth = focused ? 2 : 1;
    if (lv_obj_get_style_border_width(row, 0) != borderWidth) {
        lv_obj_set_style_border_width(row, borderWidth, 0);
    }
    setTextColorIfChanged(lv_obj_get_child(row, 0),
                          item.enabled ? (item.accent == 0 ? (focused ? kAccent : kText)
                                                          : accent)
                                       : kMuted);
    updateCheckIndicator(lv_obj_get_child(row, 2), item.checkable, item.checked, accent);
#if GRIDOPOLY_SELF_TEST == 1
    ++list.rowSelectionUpdates;
#endif
}

void updateRowContent(UiCenterList &list, lv_obj_t *row,
                      const UiListItemView &item, bool focused)
{
    if (row == nullptr) return;
    setClickableIfChanged(list, row, item.enabled);

    lv_obj_t *title = lv_obj_get_child(row, 0);
    lv_obj_t *meta = lv_obj_get_child(row, 1);
    lv_obj_t *check = lv_obj_get_child(row, 2);
    lv_obj_t *tag = lv_obj_get_child(row, 3);
    const uint32_t accent = item.accent == 0 ? kAccent : item.accent;
    const char *metaText = !item.enabled && item.disabledReason != nullptr &&
                                   item.disabledReason[0] != '\0'
                               ? item.disabledReason
                               : (item.meta == nullptr ? "" : item.meta);
    const bool hasMeta = metaText[0] != '\0';
    const bool hasTag = item.tag != nullptr && item.tag[0] != '\0';
    setLabelTextIfChanged(list, title, item.title);
    setLabelTextIfChanged(list, meta, metaText);
    setObjectRectIfChanged(title, item.checkable ? kCheckableTitle
                                                 : (hasTag ? kTaggedTitle
                                                           : hasMeta ? kStandardTitle
                                                            : kFullWidthTitle));
    setObjectRectIfChanged(meta, item.checkable ? kCheckableMeta : kStandardMeta);
    setObjectRectIfChanged(check, kCheckIndicator);
    setObjectRectIfChanged(tag, kTagPill);
    setFocusedLongMode(title, focused);
    setFocusedLongMode(meta, focused);
    updateCheckIndicator(check, item.checkable, item.checked, accent);
    updateTag(list, tag, item.tag, accent);
    setTextColorIfChanged(title, item.enabled ? (item.accent == 0 ? (focused ? kAccent : kText)
                                                                  : accent)
                                               : kMuted);
}

void destroyListObjects(UiCenterList &list)
{
    if (swipeContext.viewport == list.viewport) swipeContext = UiSwipeContext{};
    if (list.viewport != nullptr) {
        lv_disp_t *display = lv_obj_get_disp(list.viewport);
        if (display != nullptr && display->refr_timer != nullptr) {
            lv_timer_set_period(display->refr_timer, LV_DISP_DEF_REFR_PERIOD);
        }
    }
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
        uiCenterListDispatchSwipe(target, eventInput(event), swipeContext.start, finish);
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

bool uiCenterListDispatchSwipe(lv_obj_t *target, lv_indev_t *input,
                               lv_point_t start, lv_point_t finish)
{
    const int8_t step = uiCenterListSwipeStep(
        static_cast<int16_t>(finish.x - start.x), static_cast<int16_t>(finish.y - start.y)
    );
    if (step == 0 || target == nullptr || input == nullptr) return false;
    swipeContext.suppressedClickTarget = target;
    lv_indev_reset(input, target);
    if (step < 0 && swipeContext.previousTarget != nullptr) {
        lv_event_send(swipeContext.previousTarget, LV_EVENT_CLICKED, nullptr);
    } else if (step > 0 && swipeContext.nextTarget != nullptr) {
        lv_event_send(swipeContext.nextTarget, LV_EVENT_CLICKED, nullptr);
    }
    return true;
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
    lv_disp_t *display = lv_obj_get_disp(list.viewport);
    if (display != nullptr && display->refr_timer != nullptr) {
        lv_timer_set_period(display->refr_timer, kCenterListRefreshPeriodMs);
    }
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
            uiLabel(row, "", kStandardTitle, &ui_font_14, kText,
                    LV_TEXT_ALIGN_LEFT);
            uiLabel(row, "", kStandardMeta, &ui_font_14, kMuted,
                    LV_TEXT_ALIGN_RIGHT);
            lv_obj_t *check = uiBox(row, kCheckIndicator, kPanel, kMuted, 12);
            if (check != nullptr) {
                lv_obj_set_style_bg_opa(check, LV_OPA_TRANSP, 0);
                lv_obj_clear_flag(check, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
                lv_obj_t *mark = uiLabel(
                    check, LV_SYMBOL_OK, UiRect{2, 1, 20, 20},
                    &lv_font_montserrat_14, kBg
                );
                if (mark != nullptr) lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_t *tag = uiBox(row, kTagPill, kBg, kMuted, 7);
            if (tag != nullptr) {
                lv_obj_clear_flag(tag, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(tag, LV_OBJ_FLAG_HIDDEN);
                uiLabel(tag, "", kTagText, &lv_font_montserrat_10, kMuted,
                        LV_TEXT_ALIGN_CENTER);
            }
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
    const int16_t currentY = lv_obj_get_y(target);
    if (firstRow && list.track != nullptr) lv_anim_del(list.track, setTrackY);
    list.pulseTarget = target;
    list.pulseRestY = firstRow ? list.trackRestY : list.footerRestY;
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
    const uint8_t previousSelected = list.selected;
    const bool previousFooterFocused = list.footerFocused;
    const bool selectionChanged = !list.selectionInitialized ||
                                  previousSelected != selected ||
                                  previousFooterFocused != footerFocused;
    lv_obj_t *const interruptedPulse = cancelBoundaryPulse(list);

    for (uint8_t index = 0; index < count; ++index) {
        updateRowContent(
            list, lv_obj_get_child(list.track, index), items[index],
            index == selected && !footerFocused
        );
    }
    if (count > 0 && selectionChanged) {
        if (list.selectionInitialized && previousSelected < count &&
            previousSelected != selected) {
            applyRowSelectionStyle(
                list, lv_obj_get_child(list.track, previousSelected),
                items[previousSelected], false
            );
        }
        applyRowSelectionStyle(
            list, lv_obj_get_child(list.track, selected), items[selected], !footerFocused
        );
    }
    list.selected = selected;
    list.footerFocused = footerFocused;
    list.selectionInitialized = true;
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
        const uint32_t selectedAccent = count > 0 && items[selected].accent != 0
            ? items[selected].accent : kAccent;
        const lv_color_t border = lv_color_hex(footerFocused ? kLine : selectedAccent);
        if (lv_obj_get_style_border_color(list.focusFrame, 0).full != border.full) {
            lv_obj_set_style_border_color(list.focusFrame, border, 0);
        }
        const lv_coord_t borderWidth = footerFocused ? 1 : 2;
        if (lv_obj_get_style_border_width(list.focusFrame, 0) != borderWidth) {
            lv_obj_set_style_border_width(list.focusFrame, borderWidth, 0);
        }
    }
    const uint16_t progress = uiListProgressPermille(selected, count);
    if (list.progressFill != nullptr) {
        const int16_t width = static_cast<int16_t>(kProgressWidth * progress / 1000);
        if (lv_obj_get_width(list.progressFill) != width) {
            lv_obj_set_width(list.progressFill, width);
        }
    }
    if (list.countLabel != nullptr) {
        char value[16];
        snprintf(value, sizeof(value), "%u / %u", count == 0 ? 0 : selected + 1, count);
        setLabelTextIfChanged(list, list.countLabel, value);
    }
    if (list.footer != nullptr) {
        const lv_color_t background = lv_color_hex(footerFocused ? kSelected : kPanel);
        if (lv_obj_get_style_bg_color(list.footer, 0).full != background.full) {
            lv_obj_set_style_bg_color(list.footer, background, 0);
        }
        const lv_color_t border = lv_color_hex(footerFocused ? kAccent : kLine);
        if (lv_obj_get_style_border_color(list.footer, 0).full != border.full) {
            lv_obj_set_style_border_color(list.footer, border, 0);
        }
        const lv_coord_t borderWidth = footerFocused ? 2 : 1;
        if (lv_obj_get_style_border_width(list.footer, 0) != borderWidth) {
            lv_obj_set_style_border_width(list.footer, borderWidth, 0);
        }
        setClickableIfChanged(list, list.footer, footerEnabled);
        lv_obj_t *footerLabel = lv_obj_get_child(list.footer, 0);
        if (footerLabel != nullptr) {
            setLabelTextIfChanged(list, footerLabel, footerText);
            setTextColorIfChanged(
                footerLabel, footerEnabled ? (footerFocused ? kAccent : kText) : kMuted
            );
        }
    }
}
