#include "ui_carousel.h"

#include "src/fonts/ui_fonts.h"
#include "ui_motion.h"
#include "ui_primitives.h"

namespace {

constexpr uint32_t kPanel = 0x11191B;
constexpr uint32_t kCyanFill = 0x16302A;
constexpr uint32_t kSignalCyan = 0x52DCB7;
constexpr uint32_t kDiceFill = 0x2B2618;
constexpr uint32_t kDiceYellow = 0xF2C453;
constexpr uint32_t kMuted = 0x81908C;
constexpr UiRect kCarouselRect{48, 300, 384, 74};
constexpr UiRect kItemRect{0, 0, 72, 74};
constexpr UiRect kIconRect{8, 6, 56, 28};
constexpr UiRect kLabelRect{2, 42, 68, 24};
constexpr int16_t kSwipeThreshold = 36;
constexpr int16_t kSwipeDominance = 12;

constexpr bool rectInside(UiRect child, UiRect parent)
{
    return child.x >= parent.x && child.y >= parent.y &&
           child.x + child.w <= parent.x + parent.w &&
           child.y + child.h <= parent.y + parent.h;
}

static_assert(rectInside(kIconRect, kItemRect) && rectInside(kLabelRect, kItemRect),
              "carousel icon and text must stay inside their item box");

const char *actionLabel(HomeAction action)
{
    switch (action) {
        case HomeAction::Dice: return "DICE";
        case HomeAction::Assets: return "ASSETS";
        case HomeAction::Players: return "PLAYERS";
        case HomeAction::Trade: return "TRADE";
        case HomeAction::More: return "MORE";
    }
    return "";
}

const char *actionIcon(HomeAction action)
{
    switch (action) {
        case HomeAction::Dice: return "D6";
        case HomeAction::Assets: return "$";
        case HomeAction::Players: return "P";
        case HomeAction::Trade: return "<>";
        case HomeAction::More: return "...";
    }
    return "";
}

int32_t interpolate(int32_t start, int32_t target, int32_t progress)
{
    return start + (target - start) * progress / 1000;
}

void setCarouselTransitionProgress(void *object, int32_t progress)
{
    UiCarousel &carousel = *static_cast<UiCarousel *>(object);
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr) continue;
        lv_obj_set_x(item, static_cast<lv_coord_t>(
            interpolate(carousel.startX[index], carousel.targetX[index], progress)
        ));
        lv_obj_set_style_transform_zoom(item, static_cast<uint16_t>(
            interpolate(carousel.startZoom[index], carousel.targetZoom[index], progress)
        ), 0);
        lv_obj_set_style_opa(item, static_cast<lv_opa_t>(
            interpolate(carousel.startOpacity[index], carousel.targetOpacity[index], progress)
        ), 0);
    }
}

void startCarouselTransition(UiCarousel &carousel)
{
    lv_anim_del(&carousel, setCarouselTransitionProgress);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &carousel);
    lv_anim_set_exec_cb(&animation, setCarouselTransitionProgress);
    lv_anim_set_values(&animation, 0, 1000);
    lv_anim_set_time(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

int8_t relativeSlot(uint8_t index, uint8_t selected, uint8_t count)
{
    int8_t relative = static_cast<int8_t>(index) - static_cast<int8_t>(selected);
    while (relative > 2) relative = static_cast<int8_t>(relative - count);
    while (relative < -2) relative = static_cast<int8_t>(relative + count);
    if (count == 4 && relative == -2) relative = 2;
    return relative;
}

void styleItem(lv_obj_t *item, HomeAction action, bool selected)
{
    if (item == nullptr) return;
    const bool dice = action == HomeAction::Dice;
    const uint32_t accent = dice ? kDiceYellow : kSignalCyan;
    const uint32_t fill = dice ? kDiceFill : (selected ? kCyanFill : kPanel);
    lv_obj_set_style_bg_color(item, lv_color_hex(fill), 0);
    lv_obj_set_style_border_color(item, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(item, selected ? 2 : 1, 0);
    lv_obj_set_style_outline_color(item, lv_color_hex(accent), 0);
    lv_obj_set_style_outline_width(item, dice ? 1 : 0, 0);
    lv_obj_set_style_outline_pad(item, dice ? 3 : 0, 0);
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

uint8_t tappedAction(const UiCarousel &carousel, const lv_point_t &point)
{
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr || lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t area{};
        lv_obj_get_coords(item, &area);
        if (point.x >= area.x1 && point.x <= area.x2 &&
            point.y >= area.y1 && point.y <= area.y2) return index;
    }
    return carousel.selected;
}

void carouselSwipeCallback(lv_event_t *event)
{
    if (event == nullptr) return;
    UiCarousel *carousel = static_cast<UiCarousel *>(lv_event_get_user_data(event));
    if (carousel == nullptr || lv_event_get_current_target(event) != carousel->container) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) {
        if (carousel->suppressClick) {
            carousel->suppressClick = false;
            lv_event_stop_processing(event);
            return;
        }
        lv_point_t point{};
        const uint8_t action = readPointer(event, point) ? tappedAction(*carousel, point)
                                                          : carousel->selected;
        if (action < carousel->count && carousel->actionTargets[action] != nullptr) {
            lv_event_send(carousel->actionTargets[action], LV_EVENT_CLICKED, nullptr);
        }
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        carousel->suppressClick = false;
        carousel->swipeTracking = readPointer(event, carousel->swipeStart);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        carousel->swipeTracking = false;
        return;
    }
    if (code != LV_EVENT_RELEASED || !carousel->swipeTracking) return;
    carousel->swipeTracking = false;
    lv_point_t finish{};
    if (!readPointer(event, finish)) return;
    const int8_t step = uiCarouselSwipeStep(
        static_cast<int16_t>(finish.x - carousel->swipeStart.x),
        static_cast<int16_t>(finish.y - carousel->swipeStart.y)
    );
    lv_indev_t *input = eventInput(event);
    if (step == 0 || input == nullptr) return;
    carousel->suppressClick = true;
    lv_indev_reset(input, carousel->container);
    if (step < 0 && carousel->nextTarget != nullptr) {
        lv_event_send(carousel->nextTarget, LV_EVENT_CLICKED, nullptr);
    } else if (step > 0 && carousel->previousTarget != nullptr) {
        lv_event_send(carousel->previousTarget, LV_EVENT_CLICKED, nullptr);
    }
}

lv_obj_t *createEventTarget(UiCarousel &carousel, UiEventKind kind, int16_t value = 0)
{
    lv_obj_t *target = lv_obj_create(carousel.container);
    if (target == nullptr) return nullptr;
    lv_obj_remove_style_all(target);
    lv_obj_set_size(target, 1, 1);
    lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    uiBindTap(target, kind, value);
    return target;
}

} // namespace

int8_t uiCarouselSwipeStep(int16_t dx, int16_t dy)
{
    const int32_t absoluteX = dx < 0 ? -static_cast<int32_t>(dx) : dx;
    const int32_t absoluteY = dy < 0 ? -static_cast<int32_t>(dy) : dy;
    if (absoluteX < kSwipeThreshold) return 0;
    if (absoluteX < absoluteY + kSwipeDominance) return 0;
    return dx < 0 ? -1 : 1;
}

uint8_t uiCarouselActions(HomePhase phase, HomeAction (&actions)[5])
{
    if (phase == HomePhase::MyTurn) {
        actions[0] = HomeAction::Dice;
        actions[1] = HomeAction::Assets;
        actions[2] = HomeAction::Players;
        actions[3] = HomeAction::Trade;
        actions[4] = HomeAction::More;
        return 5;
    }
    actions[0] = HomeAction::Assets;
    actions[1] = HomeAction::Players;
    actions[2] = HomeAction::Trade;
    actions[3] = HomeAction::More;
    actions[4] = HomeAction::More;
    return 4;
}

void uiCarouselCreate(UiCarousel &carousel, lv_obj_t *parent,
                      const HomeAction *actions, uint8_t count, uint8_t selected)
{
    uiCarouselDestroy(carousel);
    if (parent == nullptr || actions == nullptr) return;

    carousel.container = uiBox(parent, kCarouselRect, 0x090E10, 0x090E10, 0);
    if (carousel.container == nullptr) return;
    lv_obj_set_style_bg_opa(carousel.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(carousel.container, 0, 0);
    lv_obj_clear_flag(carousel.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(carousel.container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    carousel.count = count > 5 ? 5 : count;
    carousel.selected = selected < carousel.count ? selected : 0;

    for (uint8_t index = 0; index < 5; ++index) {
        lv_obj_t *item = uiBox(carousel.container, kItemRect, kPanel, kSignalCyan, 8);
        carousel.items[index] = item;
        if (item == nullptr) continue;
        // The container owns both gesture recognition and two-tap activation.
        lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_transform_pivot_x(item, kItemRect.w / 2, 0);
        lv_obj_set_style_transform_pivot_y(item, kItemRect.h / 2, 0);
        if (index >= carousel.count) {
            lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const HomeAction action = actions[index];
        carousel.actions[index] = action;
        uiLabel(item, actionIcon(action), kIconRect, &lv_font_montserrat_20,
                action == HomeAction::Dice ? kDiceYellow : kSignalCyan);
        carousel.labels[index] = uiLabel(item, actionLabel(action), kLabelRect,
                                         &lv_font_montserrat_14,
                                         action == HomeAction::Dice ? kDiceYellow : kMuted);
        styleItem(item, action, index == carousel.selected);
    }
    lv_obj_add_flag(carousel.container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(carousel.container, carouselSwipeCallback, LV_EVENT_ALL, &carousel);
    for (uint8_t index = 0; index < carousel.count; ++index) {
        carousel.actionTargets[index] = createEventTarget(
            carousel, UiEventKind::SelectHomeAction, index
        );
    }
    carousel.previousTarget = createEventTarget(carousel, UiEventKind::ListPrevious);
    carousel.nextTarget = createEventTarget(carousel, UiEventKind::ListNext);
    uiCarouselSetSelection(carousel, carousel.selected, false);
}

void uiCarouselSetSelection(UiCarousel &carousel, uint8_t selected, bool animate)
{
    if (carousel.container == nullptr || carousel.count == 0 || selected >= carousel.count) return;
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr) continue;
        lv_obj_update_layout(item);
        carousel.startX[index] = lv_obj_get_x(item);
        carousel.startZoom[index] = lv_obj_get_style_transform_zoom(item, 0);
        carousel.startOpacity[index] = lv_obj_get_style_opa(item, 0);
        const CarouselPose target = uiCarouselPose(relativeSlot(index, selected, carousel.count));
        carousel.targetX[index] = static_cast<int16_t>(
            target.centerX - kCarouselRect.x - kItemRect.w / 2
        );
        carousel.targetZoom[index] = target.zoom;
        carousel.targetOpacity[index] = target.opacity;
        styleItem(item, carousel.actions[index], index == selected);
    }
    carousel.selected = selected;
    if (animate) startCarouselTransition(carousel);
    else {
        lv_anim_del(&carousel, setCarouselTransitionProgress);
        setCarouselTransitionProgress(&carousel, 1000);
    }
}

void uiCarouselDestroy(UiCarousel &carousel)
{
    lv_anim_del(&carousel, setCarouselTransitionProgress);
    if (carousel.container != nullptr) lv_obj_del(carousel.container);
    carousel = UiCarousel{};
}
