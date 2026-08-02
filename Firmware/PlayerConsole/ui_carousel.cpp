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

void setAnimatedX(void *object, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value));
}

void setAnimatedZoom(void *object, int32_t value)
{
    lv_obj_set_style_transform_zoom(static_cast<lv_obj_t *>(object),
                                    static_cast<uint16_t>(value), 0);
}

void setAnimatedOpacity(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object),
                         static_cast<lv_opa_t>(value), 0);
}

void setPulseOpacity(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object),
                         static_cast<lv_opa_t>(value), 0);
}

void animateProperty(lv_obj_t *object, lv_anim_exec_xcb_t callback,
                     int32_t current, int32_t target)
{
    lv_anim_del(object, callback);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_values(&animation, current, target);
    lv_anim_set_time(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void startDicePulse(lv_obj_t *object, uint32_t delayMs)
{
    lv_anim_del(object, setPulseOpacity);
    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, object);
    lv_anim_set_exec_cb(&pulse, setPulseOpacity);
    lv_anim_set_values(&pulse, LV_OPA_COVER, 232);
    lv_anim_set_time(&pulse, 560);
    lv_anim_set_playback_time(&pulse, 560);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&pulse, delayMs);
    lv_anim_set_early_apply(&pulse, false);
    lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
    lv_anim_start(&pulse);
}

void stopItemAnimations(lv_obj_t *item)
{
    if (item == nullptr) return;
    lv_anim_del(item, setAnimatedX);
    lv_anim_del(item, setAnimatedZoom);
    lv_anim_del(item, setAnimatedOpacity);
    lv_anim_del(item, setPulseOpacity);
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

} // namespace

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
        lv_obj_set_style_transform_pivot_x(item, kItemRect.w / 2, 0);
        lv_obj_set_style_transform_pivot_y(item, kItemRect.h / 2, 0);
        if (index >= carousel.count) {
            lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const HomeAction action = actions[index];
        uiLabel(item, actionIcon(action), kIconRect, &lv_font_montserrat_20,
                action == HomeAction::Dice ? kDiceYellow : kSignalCyan);
        carousel.labels[index] = uiLabel(item, actionLabel(action), kLabelRect,
                                         &lv_font_montserrat_14,
                                         action == HomeAction::Dice ? kDiceYellow : kMuted);
        uiBindTap(item, UiEventKind::SelectHomeAction, index);
        styleItem(item, action, index == carousel.selected);
    }
    uiCarouselSetSelection(carousel, carousel.selected, false);
}

void uiCarouselSetSelection(UiCarousel &carousel, uint8_t selected, bool animate)
{
    if (carousel.container == nullptr || carousel.count == 0 || selected >= carousel.count) return;
    carousel.selected = selected;
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr) continue;
        lv_obj_update_layout(item);
        const int16_t currentX = lv_obj_get_x(item);
        const uint16_t currentZoom = lv_obj_get_style_transform_zoom(item, 0);
        const uint8_t currentOpacity = lv_obj_get_style_opa(item, 0);
        const CarouselPose target = uiCarouselPose(relativeSlot(index, selected, carousel.count));
        const int16_t targetX = static_cast<int16_t>(target.centerX - kCarouselRect.x - kItemRect.w / 2);
        const bool dice = carousel.count == 5 && index == 0;
        styleItem(item, dice ? HomeAction::Dice : HomeAction::Assets, index == selected);
        lv_anim_del(item, setPulseOpacity);
        if (animate) {
            animateProperty(item, setAnimatedX, currentX, targetX);
            animateProperty(item, setAnimatedZoom, currentZoom, target.zoom);
            animateProperty(item, setAnimatedOpacity, currentOpacity, target.opacity);
        } else {
            stopItemAnimations(item);
            lv_obj_set_x(item, targetX);
            lv_obj_set_style_transform_zoom(item, target.zoom, 0);
            lv_obj_set_style_opa(item, target.opacity, 0);
        }
        if (dice && index == selected) startDicePulse(item, animate ? 220 : 0);
    }
}

void uiCarouselDestroy(UiCarousel &carousel)
{
    for (lv_obj_t *item : carousel.items) stopItemAnimations(item);
    if (carousel.container != nullptr) lv_obj_del(carousel.container);
    carousel = UiCarousel{};
}
