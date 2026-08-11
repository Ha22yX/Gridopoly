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
constexpr uint32_t kCarouselBackground = 0x090E10;
constexpr UiRect kCarouselRect{48, 300, 384, 74};
constexpr UiRect kItemRect{0, 0, 72, 74};
constexpr int16_t kSwipeThreshold = 36;
constexpr int16_t kSwipeDominance = 12;
constexpr uint8_t kItemCapacity = 5;
constexpr uint32_t kCarouselRefreshPeriodMs = 16;
constexpr uint16_t kFarZoom = 179;
constexpr int32_t kWrapSwapProgress = 500;
constexpr uint8_t kTextVisibilityFloor = 15;

struct FontBlend {
    const lv_font_t *lower;
    const lv_font_t *upper;
    uint8_t lowerWeight;
    uint8_t upperWeight;
};

struct CarouselVisual {
    int16_t x;
    uint16_t zoom;
    uint8_t opacity;
};

const char *actionLabel(HomeAction action)
{
    switch (action) {
        case HomeAction::Dice: return "DICE";
        case HomeAction::ExtraRoll: return "ROLL AGAIN";
        case HomeAction::EndTurn: return "END TURN";
        case HomeAction::Assets: return "ASSETS";
        case HomeAction::Players: return "PLAYERS";
        case HomeAction::Trade: return "TRADE";
    }
    return "";
}

const char *actionIcon(HomeAction action)
{
    switch (action) {
        case HomeAction::Dice: return "D6";
        case HomeAction::ExtraRoll: return "D6+";
        case HomeAction::EndTurn: return "OK";
        case HomeAction::Assets: return "$";
        case HomeAction::Players: return "P";
        case HomeAction::Trade: return "<>";
    }
    return "";
}

int32_t interpolate(int32_t start, int32_t target, int32_t progress)
{
    return start + (target - start) * progress / 1000;
}

lv_coord_t scaledDimension(lv_coord_t base, uint16_t zoom)
{
    return static_cast<lv_coord_t>((static_cast<int32_t>(base) * zoom + 128) / 256);
}

FontBlend fontBlend(uint16_t zoom, const lv_font_t *small,
                    const lv_font_t *medium, const lv_font_t *large)
{
    if (zoom <= kFarZoom) return FontBlend{small, medium, 255, 0};
    if (zoom <= 213) {
        const uint8_t upper = static_cast<uint8_t>(
            (static_cast<uint32_t>(zoom - kFarZoom) * 255U + 17U) / 34U
        );
        return FontBlend{small, medium, static_cast<uint8_t>(255U - upper), upper};
    }
    if (zoom >= 256) return FontBlend{medium, large, 0, 255};
    const uint8_t upper = static_cast<uint8_t>(
        (static_cast<uint32_t>(zoom - 213U) * 255U + 21U) / 43U
    );
    return FontBlend{medium, large, static_cast<uint8_t>(255U - upper), upper};
}

lv_opa_t weightedOpacity(lv_opa_t opacity, uint8_t weight)
{
    return static_cast<lv_opa_t>(
        (static_cast<uint16_t>(opacity) * weight + 127U) / 255U
    );
}

void drawCrossfadedLabel(lv_draw_ctx_t *drawContext,
                         const lv_draw_label_dsc_t &base,
                         const lv_area_t &area, const char *text,
                         uint16_t zoom, const lv_font_t *small,
                         const lv_font_t *medium, const lv_font_t *large)
{
    const FontBlend blend = fontBlend(zoom, small, medium, large);
    lv_draw_label_dsc_t layer = base;
    layer.font = blend.lower;
    layer.opa = weightedOpacity(base.opa, blend.lowerWeight);
    if (layer.opa > LV_OPA_TRANSP) {
        lv_draw_label(drawContext, &layer, &area, text, nullptr);
    }
    layer.font = blend.upper;
    layer.opa = weightedOpacity(base.opa, blend.upperWeight);
    if (layer.opa > LV_OPA_TRANSP) {
        lv_draw_label(drawContext, &layer, &area, text, nullptr);
    }
}

uint8_t carouselItemIndex(const UiCarousel &carousel, const lv_obj_t *item)
{
    for (uint8_t index = 0; index < carousel.count; ++index) {
        if (carousel.items[index] == item) return index;
    }
    return kItemCapacity;
}

void drawCarouselItem(lv_event_t *event)
{
    if (event == nullptr) return;
    UiCarousel *carousel = static_cast<UiCarousel *>(lv_event_get_user_data(event));
    lv_obj_t *item = lv_event_get_current_target(event);
    if (carousel == nullptr || item == nullptr) return;
    const uint8_t index = carouselItemIndex(*carousel, item);
    if (index >= carousel->count) return;

    lv_draw_ctx_t *drawContext = lv_event_get_draw_ctx(event);
    if (drawContext == nullptr) return;
    lv_area_t itemArea{};
    lv_obj_get_coords(item, &itemArea);
    const lv_coord_t height = lv_area_get_height(&itemArea);
    const uint16_t zoom = carousel->currentZoom[index];
    const lv_opa_t opacity = carousel->currentOpacity[index];
    const bool primary = carousel->actions[index] == HomeAction::Dice ||
                         carousel->actions[index] == HomeAction::ExtraRoll ||
                         carousel->actions[index] == HomeAction::EndTurn;
    const lv_color_t accent = lv_color_hex(primary ? kDiceYellow : kSignalCyan);
    if (opacity <= kTextVisibilityFloor) return;

    lv_draw_label_dsc_t icon{};
    lv_draw_label_dsc_init(&icon);
    icon.color = accent;
    icon.opa = opacity;
    icon.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t iconArea{
        itemArea.x1,
        static_cast<lv_coord_t>(itemArea.y1 + height * 6 / kItemRect.h),
        itemArea.x2,
        static_cast<lv_coord_t>(itemArea.y1 + height * 34 / kItemRect.h)
    };
    drawCrossfadedLabel(
        drawContext, icon, iconArea, actionIcon(carousel->actions[index]), zoom,
        &lv_font_montserrat_16, &lv_font_montserrat_18, &lv_font_montserrat_20
    );

    lv_draw_label_dsc_t label{};
    lv_draw_label_dsc_init(&label);
    label.color = lv_color_hex(primary ? kDiceYellow : kMuted);
    label.opa = opacity;
    label.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t labelArea{
        itemArea.x1,
        static_cast<lv_coord_t>(itemArea.y1 + height * 42 / kItemRect.h),
        itemArea.x2,
        itemArea.y2
    };
    drawCrossfadedLabel(
        drawContext, label, labelArea, actionLabel(carousel->actions[index]), zoom,
        &lv_font_montserrat_10, &lv_font_montserrat_12, &lv_font_montserrat_14
    );
}

CarouselVisual transitionVisual(const UiCarousel &carousel, uint8_t index,
                                int32_t progress)
{
    if (progress < 0) progress = 0;
    else if (progress > 1000) progress = 1000;
    if (carousel.paths[index] == CarouselPath::Linear) {
        return CarouselVisual{
            static_cast<int16_t>(interpolate(
                carousel.startX[index], carousel.targetX[index], progress
            )),
            static_cast<uint16_t>(interpolate(
                carousel.startZoom[index], carousel.targetZoom[index], progress
            )),
            static_cast<uint8_t>(interpolate(
                carousel.startOpacity[index], carousel.targetOpacity[index], progress
            )),
        };
    }

    const int16_t leftExit = static_cast<int16_t>(-scaledDimension(kItemRect.w, kFarZoom));
    const int16_t rightExit = kCarouselRect.w;
    const bool leaving = progress < kWrapSwapProgress;
    const int32_t segmentProgress = leaving
        ? progress * 2
        : (progress - kWrapSwapProgress) * 2;
    const bool leftToRight = carousel.paths[index] == CarouselPath::WrapLeftToRight;
    const int16_t exitX = leftToRight ? leftExit : rightExit;
    const int16_t entryX = leftToRight ? rightExit : leftExit;
    return CarouselVisual{
        static_cast<int16_t>(leaving
            ? interpolate(carousel.startX[index], exitX, segmentProgress)
            : interpolate(entryX, carousel.targetX[index], segmentProgress)),
        static_cast<uint16_t>(leaving
            ? interpolate(carousel.startZoom[index], kFarZoom, segmentProgress)
            : interpolate(kFarZoom, carousel.targetZoom[index], segmentProgress)),
        static_cast<uint8_t>(leaving
            ? interpolate(carousel.startOpacity[index], 0, segmentProgress)
            : interpolate(0, carousel.targetOpacity[index], segmentProgress)),
    };
}

void applyCarouselVisual(UiCarousel &carousel, uint8_t index, int16_t x,
                         uint16_t zoom, uint8_t opacity)
{
    if (index >= carousel.count || carousel.items[index] == nullptr) return;
    lv_obj_t *item = carousel.items[index];
    const lv_coord_t width = scaledDimension(kItemRect.w, zoom);
    const lv_coord_t height = scaledDimension(kItemRect.h, zoom);
    lv_obj_set_pos(item, x, static_cast<lv_coord_t>((kItemRect.h - height) / 2));
    lv_obj_set_size(item, width, height);
    lv_obj_set_style_bg_opa(item, opacity, 0);
    lv_obj_set_style_border_opa(item, opacity, 0);
    lv_obj_set_style_outline_opa(item, opacity, 0);
    lv_obj_set_style_text_opa(item, opacity, 0);
    carousel.currentZoom[index] = zoom;
    carousel.currentOpacity[index] = opacity;
}

void setCarouselTransitionProgress(void *object, int32_t progress)
{
    UiCarousel &carousel = *static_cast<UiCarousel *>(object);
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr) continue;
        const CarouselVisual visual = transitionVisual(carousel, index, progress);
        applyCarouselVisual(carousel, index, visual.x, visual.zoom, visual.opacity);
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
    lv_anim_set_early_apply(&animation, false);
    lv_anim_start(&animation);
    lv_disp_t *display = lv_obj_get_disp(carousel.container);
    if (display != nullptr && display->refr_timer != nullptr) {
        lv_timer_ready(display->refr_timer);
    }
}

int8_t relativeSlot(uint8_t index, uint8_t selected, uint8_t count)
{
    int8_t relative = static_cast<int8_t>(index) - static_cast<int8_t>(selected);
    const int8_t half = static_cast<int8_t>(count / 2);
    while (relative > half) relative = static_cast<int8_t>(relative - count);
    while (relative < -half) relative = static_cast<int8_t>(relative + count);
    if ((count & 1u) == 0 && relative == -half) relative = half;
    return relative;
}

int8_t selectionDirection(uint8_t previous, uint8_t selected, uint8_t count)
{
    if (count < 2 || previous == selected) return 0;
    const uint8_t forward = static_cast<uint8_t>((selected + count - previous) % count);
    const uint8_t reverse = static_cast<uint8_t>((previous + count - selected) % count);
    return forward <= reverse ? 1 : -1;
}

CarouselPath carouselPath(int16_t startX, int16_t targetX, int8_t direction)
{
    if (direction > 0 && targetX > startX) return CarouselPath::WrapLeftToRight;
    if (direction < 0 && targetX < startX) return CarouselPath::WrapRightToLeft;
    return CarouselPath::Linear;
}

void configureItemStaticStyle(UiCarousel &carousel, lv_obj_t *item,
                              HomeAction action)
{
    if (item == nullptr) return;
    const bool primary = action == HomeAction::Dice ||
                         action == HomeAction::ExtraRoll ||
                         action == HomeAction::EndTurn;
    const uint32_t accent = primary ? kDiceYellow : kSignalCyan;
    lv_obj_set_style_bg_color(item, lv_color_hex(primary ? kDiceFill : kPanel), 0);
    lv_obj_set_style_border_color(item, lv_color_hex(accent), 0);
    lv_obj_set_style_radius(item, 8, 0);
    lv_obj_set_style_outline_color(item, lv_color_hex(accent), 0);
    lv_obj_set_style_outline_width(item, primary ? 1 : 0, 0);
    lv_obj_set_style_outline_pad(item, primary ? 3 : 0, 0);
#if GRIDOPOLY_SELF_TEST == 1
    ++carousel.staticStyleConfigurations;
#endif
}

void applyItemSelectionStyle(UiCarousel &carousel, lv_obj_t *item,
                             HomeAction action, bool selected)
{
    if (item == nullptr) return;
    const lv_color_t background = lv_color_hex(
        (action == HomeAction::Dice || action == HomeAction::ExtraRoll ||
         action == HomeAction::EndTurn)
            ? kDiceFill : (selected ? kCyanFill : kPanel)
    );
    if (lv_obj_get_style_bg_color(item, 0).full != background.full) {
        lv_obj_set_style_bg_color(item, background, 0);
    }
    const lv_coord_t borderWidth = selected ? 2 : 1;
    if (lv_obj_get_style_border_width(item, 0) != borderWidth) {
        lv_obj_set_style_border_width(item, borderWidth, 0);
    }
#if GRIDOPOLY_SELF_TEST == 1
    ++carousel.selectionStyleUpdates;
#endif
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
    if (phase == HomePhase::MyTurn || phase == HomePhase::MyTurnEnd) {
        actions[0] = phase == HomePhase::MyTurn ? HomeAction::Dice : HomeAction::EndTurn;
        actions[1] = HomeAction::Assets;
        actions[2] = HomeAction::Players;
        actions[3] = HomeAction::Trade;
        return 4;
    }
    actions[0] = HomeAction::Assets;
    actions[1] = HomeAction::Players;
    actions[2] = HomeAction::Trade;
    return 3;
}

void uiCarouselCreate(UiCarousel &carousel, lv_obj_t *parent,
                      const HomeAction *actions, uint8_t count, uint8_t selected)
{
    uiCarouselDestroy(carousel);
    if (parent == nullptr || actions == nullptr) return;

    carousel.container = uiBox(parent, kCarouselRect, kCarouselBackground, kCarouselBackground, 0);
    if (carousel.container == nullptr) return;
    lv_disp_t *display = lv_obj_get_disp(carousel.container);
    if (display != nullptr && display->refr_timer != nullptr) {
        lv_timer_set_period(display->refr_timer, kCarouselRefreshPeriodMs);
    }
    lv_obj_set_style_bg_opa(carousel.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(carousel.container, 0, 0);
    lv_obj_clear_flag(carousel.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(carousel.container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    carousel.count = count > 5 ? 5 : count;
    carousel.selected = selected < carousel.count ? selected : 0;

    for (uint8_t index = 0; index < 5; ++index) {
        lv_obj_t *item = lv_obj_create(carousel.container);
        carousel.items[index] = item;
        if (item == nullptr) continue;
        lv_obj_remove_style_all(item);
        lv_obj_set_style_pad_all(item, 0, 0);
        // The container owns both gesture recognition and two-tap activation.
        lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(item, drawCarouselItem, LV_EVENT_DRAW_POST, &carousel);
        if (index >= carousel.count) {
            lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const HomeAction action = actions[index];
        carousel.actions[index] = action;
        configureItemStaticStyle(carousel, item, action);
        applyItemSelectionStyle(carousel, item, action, index == carousel.selected);
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
    const uint8_t previousSelected = carousel.selected;
    const int8_t direction = selectionDirection(previousSelected, selected, carousel.count);
    for (uint8_t index = 0; index < carousel.count; ++index) {
        lv_obj_t *item = carousel.items[index];
        if (item == nullptr) continue;
        lv_obj_update_layout(item);
        carousel.startX[index] = lv_obj_get_x(item);
        carousel.startZoom[index] = carousel.currentZoom[index];
        carousel.startOpacity[index] = carousel.currentOpacity[index];
        const CarouselPose target = uiCarouselPose(relativeSlot(index, selected, carousel.count));
        const lv_coord_t targetWidth = scaledDimension(kItemRect.w, target.zoom);
        const int16_t targetX = static_cast<int16_t>(
            target.centerX - kCarouselRect.x - targetWidth / 2
        );
        carousel.targetX[index] = targetX;
        carousel.targetZoom[index] = target.zoom;
        carousel.targetOpacity[index] = target.opacity;
        carousel.paths[index] = carouselPath(carousel.startX[index], targetX, direction);
        if (previousSelected != selected &&
            (index == previousSelected || index == selected)) {
            applyItemSelectionStyle(
                carousel, item, carousel.actions[index], index == selected
            );
        }
    }
    carousel.selected = selected;
    if (animate) startCarouselTransition(carousel);
    else {
        lv_anim_del(&carousel, setCarouselTransitionProgress);
        setCarouselTransitionProgress(&carousel, 1000);
    }
}

void uiCarouselSetEndTurnExitProgress(UiCarousel &carousel, uint16_t progressPermille)
{
    if (carousel.container == nullptr || carousel.count != 4 ||
        carousel.actions[0] != HomeAction::EndTurn) return;
    const int32_t progress = progressPermille > 1000 ? 1000 : progressPermille;
    const int32_t eased = progress * (2000 - progress) / 1000;
    for (uint8_t index = 0; index < carousel.count; ++index) {
        const CarouselPose start = uiCarouselPose(relativeSlot(index, 0, 4));
        CarouselPose target{};
        if (index == 0) {
            target = CarouselPose{240, 62, 0};
        } else {
            const uint8_t waitingIndex = static_cast<uint8_t>(index - 1);
            target = uiCarouselPose(relativeSlot(waitingIndex, 0, 3));
        }
        const uint16_t zoom = static_cast<uint16_t>(interpolate(start.zoom, target.zoom, eased));
        const lv_coord_t width = scaledDimension(kItemRect.w, zoom);
        const int16_t centerX = static_cast<int16_t>(interpolate(start.centerX, target.centerX, eased));
        const int16_t x = static_cast<int16_t>(centerX - kCarouselRect.x - width / 2);
        applyCarouselVisual(
            carousel,
            index,
            x,
            zoom,
            static_cast<uint8_t>(interpolate(start.opacity, target.opacity, eased))
        );
    }
}

void uiCarouselDestroy(UiCarousel &carousel)
{
    lv_anim_del(&carousel, setCarouselTransitionProgress);
    if (carousel.container != nullptr) {
        lv_disp_t *display = lv_obj_get_disp(carousel.container);
        if (display != nullptr && display->refr_timer != nullptr) {
            lv_timer_set_period(display->refr_timer, LV_DISP_DEF_REFR_PERIOD);
        }
        lv_obj_del(carousel.container);
    }
    carousel = UiCarousel{};
}
