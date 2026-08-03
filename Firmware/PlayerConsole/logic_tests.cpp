#include "logic_tests.h"

#include "app_config.h"
#include "app_state.h"
#include "authority_snapshot_reducer.h"
#include "demo_data.h"
#include "demo_transport.h"
#include "ui_layout.h"
#include "ui_carousel.h"
#include "ui_center_list.h"
#include "ui_modal.h"
#include "ui_motion.h"
#include "ui_primitives.h"
#include "ui_renderer.h"

#include <limits.h>
#include <string.h>

namespace {

bool expect(Stream &out, bool condition, const char *name)
{
    out.printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

void shortPress(AppState &state, uint32_t downMs, uint32_t upMs)
{
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, downMs}, downMs);
    appTick(state, upMs);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, upMs}, upMs);
}

UiEvent centerListEvents[8]{};
uint8_t centerListEventCount = 0;

void captureCenterListEvent(const UiEvent &event)
{
    if (centerListEventCount < sizeof(centerListEvents) / sizeof(centerListEvents[0])) {
        centerListEvents[centerListEventCount++] = event;
    }
}

void centerListFlush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *)
{
    lv_disp_flush_ready(driver);
}

void centerListRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->point = lv_point_t{0, 0};
}

bool centerListTreeMatches(const UiCenterList &list, lv_obj_t *parent, uint8_t count)
{
    if (list.viewport == nullptr || list.track == nullptr || list.focusFrame == nullptr ||
        list.progressFill == nullptr || list.countLabel == nullptr || list.footer == nullptr) {
        return false;
    }
    lv_obj_t *progressTrack = lv_obj_get_parent(list.progressFill);
    return list.count == count && lv_obj_get_parent(list.viewport) == parent &&
           lv_obj_get_parent(list.track) == list.viewport &&
           lv_obj_get_parent(list.focusFrame) == parent &&
           progressTrack != nullptr && lv_obj_get_parent(progressTrack) == parent &&
           lv_obj_get_parent(list.countLabel) == parent &&
           lv_obj_get_parent(list.footer) == parent &&
           lv_obj_get_child_cnt(list.track) == count &&
           !lv_obj_has_flag(list.viewport, LV_OBJ_FLAG_SCROLLABLE) &&
           !lv_obj_has_flag(list.viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

UiEvent carouselEvents[8]{};
uint8_t carouselEventCount = 0;

void captureCarouselEvent(const UiEvent &event)
{
    if (carouselEventCount < sizeof(carouselEvents) / sizeof(carouselEvents[0])) {
        carouselEvents[carouselEventCount++] = event;
    }
}

bool runCarouselComponentTests(Stream &out)
{
    bool ok = true;
    if (!lv_is_initialized()) lv_init();
    lv_disp_t *const previousDisplay = lv_disp_get_default();

    static lv_color_t pixels[480]{};
    static lv_disp_draw_buf_t drawBuffer{};
    static lv_disp_drv_t displayDriver{};
    static lv_indev_drv_t inputDriver{};
    static HomeAction actions[5]{};
    static UiCarousel carousel{};
    static AppState rotary{};
    static AppState swipe{};
    drawBuffer = lv_disp_draw_buf_t{};
    displayDriver = lv_disp_drv_t{};
    inputDriver = lv_indev_drv_t{};
    actions[0] = HomeAction::Dice;
    carousel = UiCarousel{};
    rotary = AppState{};
    swipe = AppState{};
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, 480);
    lv_disp_drv_init(&displayDriver);
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = centerListFlush;
    displayDriver.hor_res = 480;
    displayDriver.ver_res = 480;
    lv_disp_t *display = lv_disp_drv_register(&displayDriver);
    if (display != nullptr) lv_disp_set_default(display);

    lv_indev_drv_init(&inputDriver);
    inputDriver.type = LV_INDEV_TYPE_POINTER;
    inputDriver.disp = display;
    inputDriver.read_cb = centerListRead;
    lv_indev_t *input = lv_indev_drv_register(&inputDriver);
    lv_obj_t *parent = display == nullptr ? nullptr : lv_obj_create(nullptr);
    const uint8_t count = uiCarouselActions(HomePhase::Waiting, actions);
    uiSetEventSink(captureCarouselEvent);
    uiCarouselCreate(carousel, parent, actions, count, 0);

    bool flattenedItems = true;
    for (uint8_t index = 0; index < count; ++index) {
        flattenedItems &= carousel.items[index] != nullptr &&
                          lv_obj_check_type(carousel.items[index], &lv_obj_class) &&
                          lv_obj_get_child_cnt(carousel.items[index]) == 0;
    }
    ok &= expect(out, flattenedItems,
                 "carousel draws each action in one retained childless geometry object");

    lv_obj_t *const retainedContainer = carousel.container;
    lv_obj_t *const retainedFirst = carousel.items[0];
    const int16_t firstX = lv_obj_get_x(carousel.items[0]);
    carouselEventCount = 0;
    input->proc.types.pointer.act_point = lv_point_t{300, 332};
    lv_event_send(carousel.items[0], LV_EVENT_PRESSED, input);
    input->proc.types.pointer.act_point = lv_point_t{252, 336};
    lv_event_send(carousel.items[0], LV_EVENT_RELEASED, input);
    lv_event_send(carousel.items[0], LV_EVENT_CLICKED, input);
    appInit(rotary, 0);
    appInit(swipe, 0);
    appHandleInput(rotary, InputEvent{InputKind::Rotate, 1, 1}, 1);
    if (carouselEventCount == 1) appHandleUiEvent(swipe, carouselEvents[0], 1);
    ok &= expect(out, carouselEventCount == 1 && carouselEvents[0].kind == UiEventKind::ListNext &&
                      swipe.nav.current.focus == rotary.nav.current.focus,
                 "Home item-hit left swipe emits one forward focus step and suppresses its click");

    carouselEventCount = 0;
    input->proc.types.pointer.act_point = lv_point_t{252, 332};
    lv_event_send(carousel.items[0], LV_EVENT_PRESSED, input);
    input->proc.types.pointer.act_point = lv_point_t{300, 336};
    lv_event_send(carousel.items[0], LV_EVENT_RELEASED, input);
    lv_event_send(carousel.items[0], LV_EVENT_CLICKED, input);
    appHandleInput(rotary, InputEvent{InputKind::Rotate, -1, 2}, 2);
    if (carouselEventCount == 1) appHandleUiEvent(swipe, carouselEvents[0], 2);
    ok &= expect(out, carouselEventCount == 1 && carouselEvents[0].kind == UiEventKind::ListPrevious &&
                      swipe.nav.current.focus == rotary.nav.current.focus,
                 "Home item-hit right swipe matches reverse rotary focus including wrap");
    ok &= expect(out, uiCarouselSwipeStep(-35, 0) == 0 && uiCarouselSwipeStep(35, 0) == 0 &&
                      uiCarouselSwipeStep(-36, 0) == -1 && uiCarouselSwipeStep(36, 0) == 1 &&
                      uiCarouselSwipeStep(-36, 24) == -1 && uiCarouselSwipeStep(36, -24) == 1 &&
                      uiCarouselSwipeStep(-36, 25) == 0 && uiCarouselSwipeStep(36, -25) == 0,
                 "Home swipe accepts exact 36px and 12px dominance edges in both directions");

    int16_t initialWidths[5]{};
    uint8_t initialOpacities[5]{};
    for (uint8_t index = 0; index < count; ++index) {
        initialWidths[index] = lv_obj_get_width(carousel.items[index]);
        initialOpacities[index] = carousel.currentOpacity[index];
    }
    uiCarouselSetSelection(carousel, 1, true);
    lv_anim_t *master = lv_anim_get(&carousel, nullptr);
    ok &= expect(out, master != nullptr && master->start_value == 0 && master->end_value == 1000 &&
                      master->time == 220 && lv_anim_get(carousel.items[0], nullptr) == nullptr,
                 "carousel runs one master progress timeline instead of per-item property animations");
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 250);
    }
    lv_obj_update_layout(carousel.container);
    const int16_t interruptedX = lv_obj_get_x(carousel.items[0]);
    const int16_t interruptedWrapX = lv_obj_get_x(carousel.items[3]);
    const uint8_t interruptedWrapOpacity = carousel.currentOpacity[3];
    bool completePoseChanged = true;
    for (uint8_t index = 0; index < count; ++index) {
        completePoseChanged &= lv_obj_get_width(carousel.items[index]) != initialWidths[index] &&
                               carousel.currentOpacity[index] != initialOpacities[index];
    }
    ok &= expect(out, completePoseChanged,
                 "one master callback updates every action scale and opacity together");
    uiCarouselSetSelection(carousel, 2, true);
    master = lv_anim_get(&carousel, nullptr);
    ok &= expect(out, master != nullptr && carousel.container == retainedContainer &&
                      carousel.items[0] == retainedFirst && interruptedX != firstX,
                 "carousel retargets its retained action sequence from current visual poses");
    const int16_t retargetStartX = lv_obj_get_x(carousel.items[3]);
    const uint8_t retargetStartOpacity = carousel.currentOpacity[3];
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 250);
    }
    lv_obj_update_layout(carousel.container);
    out.printf(
        "CAROUSEL TEST DIAG retarget path=%u start_x=%d target_x=%d before_x=%d after_x=%d "
        "before_opa=%u after_opa=%u\n",
        static_cast<unsigned>(carousel.paths[3]),
        static_cast<int>(carousel.startX[3]), static_cast<int>(carousel.targetX[3]),
        static_cast<int>(retargetStartX), static_cast<int>(lv_obj_get_x(carousel.items[3])),
        static_cast<unsigned>(retargetStartOpacity),
        static_cast<unsigned>(carousel.currentOpacity[3])
    );
    ok &= expect(out, retargetStartX == interruptedWrapX &&
                      retargetStartOpacity == interruptedWrapOpacity &&
                      lv_obj_get_x(carousel.items[3]) <= retargetStartX &&
                      carousel.currentOpacity[3] <= retargetStartOpacity,
                 "carousel mid-transition retarget continues the current wrap exit without flashing");

    uiCarouselDestroy(carousel);
    uiCarouselCreate(carousel, parent, actions, 4, 0);
    uiCarouselSetSelection(carousel, 1, true);
    master = lv_anim_get(&carousel, nullptr);
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 500);
    }
    lv_obj_update_layout(carousel.container);
    const int16_t forwardWrapX = lv_obj_get_x(carousel.items[3]);
    const int16_t forwardWrapWidth = lv_obj_get_width(carousel.items[3]);
    const int16_t carouselWidth = lv_obj_get_width(carousel.container);
    out.printf(
        "CAROUSEL TEST DIAG four_fwd path=%u start_x=%d target_x=%d x=%d w=%d cw=%d opa=%u\n",
        static_cast<unsigned>(carousel.paths[3]),
        static_cast<int>(carousel.startX[3]), static_cast<int>(carousel.targetX[3]),
        static_cast<int>(forwardWrapX), static_cast<int>(forwardWrapWidth),
        static_cast<int>(carouselWidth), static_cast<unsigned>(carousel.currentOpacity[3])
    );
    ok &= expect(out, carousel.currentOpacity[3] == 0 &&
                      (forwardWrapX >= carouselWidth ||
                       forwardWrapX + forwardWrapWidth <= 0),
                 "four-item forward wrap becomes invisible before changing sides");
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 1000);
    }
    uiCarouselSetSelection(carousel, 0, true);
    master = lv_anim_get(&carousel, nullptr);
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 500);
    }
    lv_obj_update_layout(carousel.container);
    const int16_t reverseWrapX = lv_obj_get_x(carousel.items[3]);
    const int16_t reverseWrapWidth = lv_obj_get_width(carousel.items[3]);
    out.printf(
        "CAROUSEL TEST DIAG four_rev path=%u start_x=%d target_x=%d x=%d w=%d cw=%d opa=%u\n",
        static_cast<unsigned>(carousel.paths[3]),
        static_cast<int>(carousel.startX[3]), static_cast<int>(carousel.targetX[3]),
        static_cast<int>(reverseWrapX), static_cast<int>(reverseWrapWidth),
        static_cast<int>(carouselWidth), static_cast<unsigned>(carousel.currentOpacity[3])
    );
    ok &= expect(out, carousel.currentOpacity[3] == 0 &&
                      (reverseWrapX >= carouselWidth ||
                       reverseWrapX + reverseWrapWidth <= 0),
                 "four-item reverse wrap becomes invisible before changing sides");

    uiCarouselDestroy(carousel);
    const uint8_t turnCount = uiCarouselActions(HomePhase::MyTurn, actions);
    uiCarouselCreate(carousel, parent, actions, turnCount, 0);
#if GRIDOPOLY_SELF_TEST == 1
    const uint32_t staticStylesBeforeSelection = carousel.staticStyleConfigurations;
    const uint32_t selectionStylesBeforeSelection = carousel.selectionStyleUpdates;
#endif
    uiCarouselSetSelection(carousel, 1, true);
#if GRIDOPOLY_SELF_TEST == 1
    ok &= expect(out,
                 carousel.staticStyleConfigurations == staticStylesBeforeSelection &&
                 carousel.selectionStyleUpdates == selectionStylesBeforeSelection + 2,
                 "My Turn Dice-to-Assets selection updates two dynamic cards without static restyle");
#endif
    master = lv_anim_get(&carousel, nullptr);
    if (master != nullptr && master->exec_cb != nullptr) {
        master->exec_cb(master->var, 500);
    }
    lv_obj_update_layout(carousel.container);
    const int16_t fiveItemWrapX = lv_obj_get_x(carousel.items[3]);
    const int16_t fiveItemWrapWidth = lv_obj_get_width(carousel.items[3]);
    out.printf(
        "CAROUSEL TEST DIAG five_fwd path=%u start_x=%d target_x=%d x=%d w=%d cw=%d opa=%u\n",
        static_cast<unsigned>(carousel.paths[3]),
        static_cast<int>(carousel.startX[3]), static_cast<int>(carousel.targetX[3]),
        static_cast<int>(fiveItemWrapX), static_cast<int>(fiveItemWrapWidth),
        static_cast<int>(carouselWidth), static_cast<unsigned>(carousel.currentOpacity[3])
    );
    ok &= expect(out, carousel.currentOpacity[3] == 0 &&
                      (fiveItemWrapX >= carouselWidth ||
                       fiveItemWrapX + fiveItemWrapWidth <= 0),
                 "five-item forward wrap becomes invisible before changing sides");

    uiCarouselDestroy(carousel);
    uiSetEventSink(nullptr);
    if (parent != nullptr) lv_obj_del(parent);
    if (input != nullptr) lv_indev_delete(input);
    lv_disp_set_default(previousDisplay);
    if (display != nullptr) {
        display->act_scr = nullptr;
        lv_disp_remove(display);
    }
    return ok;
}

bool runCenterListComponentTests(Stream &out)
{
    bool ok = true;
    if (!lv_is_initialized()) lv_init();
    lv_disp_t *const previousDisplay = lv_disp_get_default();

    static lv_color_t pixels[480]{};
    static lv_disp_draw_buf_t drawBuffer{};
    static lv_disp_drv_t displayDriver{};
    static lv_indev_drv_t inputDriver{};
    static UiListItemView items[8]{};
    static UiCenterList list{};
    drawBuffer = lv_disp_draw_buf_t{};
    displayDriver = lv_disp_drv_t{};
    inputDriver = lv_indev_drv_t{};
    list = UiCenterList{};
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, 480);
    lv_disp_drv_init(&displayDriver);
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = centerListFlush;
    displayDriver.hor_res = 480;
    displayDriver.ver_res = 480;
    lv_disp_t *display = lv_disp_drv_register(&displayDriver);
    if (display != nullptr) lv_disp_set_default(display);

    lv_indev_drv_init(&inputDriver);
    inputDriver.type = LV_INDEV_TYPE_POINTER;
    inputDriver.disp = display;
    inputDriver.read_cb = centerListRead;
    lv_indev_t *input = lv_indev_drv_register(&inputDriver);
    lv_obj_t *parent = display == nullptr ? nullptr : lv_obj_create(nullptr);

    for (uint8_t index = 0; index < 8; ++index) {
        items[index] = UiListItemView{"Item", "Meta", "", true, false};
    }
    uiSetEventSink(captureCenterListEvent);

    uiCenterListCreate(list, parent, nullptr, 0, 0, "Back", true, false);
    ok &= expect(out, centerListTreeMatches(list, parent, 0),
                 "center list creates a clipped runtime tree for zero rows");
    ok &= expect(out, display != nullptr && display->refr_timer != nullptr &&
                      display->refr_timer->period == 16,
                 "center list prepares a 16ms display refresh cadence");
    uiCenterListDestroy(list);
    ok &= expect(out, list.viewport == nullptr && parent != nullptr &&
                      lv_obj_get_child_cnt(parent) == 0 &&
                      display != nullptr && display->refr_timer != nullptr &&
                      display->refr_timer->period == LV_DISP_DEF_REFR_PERIOD,
                 "center list destroys every retained object for zero rows");

    uiCenterListCreate(list, parent, items, 1, 0, "Back", true, false);
    ok &= expect(out, centerListTreeMatches(list, parent, 1),
                 "center list creates one runtime row under its track");
    uiCenterListUpdate(list, items, 8, 0, "Back", true, false, false);
    ok &= expect(out, centerListTreeMatches(list, parent, 8),
                 "center list rebuilds its runtime tree when count grows to eight");

#if GRIDOPOLY_SELF_TEST == 1
    const uint32_t labelsBeforeFocus = list.labelTextWrites;
    const uint32_t flagsBeforeFocus = list.clickableFlagWrites;
    const uint32_t stylesBeforeFocus = list.rowSelectionUpdates;
#endif
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, true);
#if GRIDOPOLY_SELF_TEST == 1
    ok &= expect(out, list.labelTextWrites == labelsBeforeFocus + 1 &&
                      list.clickableFlagWrites == flagsBeforeFocus &&
                      list.rowSelectionUpdates == stylesBeforeFocus + 2 &&
                      list.countLabel != nullptr &&
                      strcmp(lv_label_get_text(list.countLabel), "2 / 8") == 0,
                 "center-list focus updates only previous/new rows without duplicate content writes");
    const uint32_t labelsBeforeAssetChange = list.labelTextWrites;
    const uint32_t flagsBeforeAssetChange = list.clickableFlagWrites;
    items[3] = UiListItemView{"Changed", "Meta", "Disabled", false, true};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    ok &= expect(out, list.labelTextWrites == labelsBeforeAssetChange + 3 &&
                      list.clickableFlagWrites == flagsBeforeAssetChange + 1,
                 "center-list still refreshes changed asset text, check state, and enabled flag");
    items[3] = UiListItemView{"Item", "Meta", "", true, false};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
#endif

    uiCenterListUpdate(list, items, 8, 7, "Back", true, false, true);
    lv_tick_inc(80);
    lv_anim_refr_now();
    lv_obj_update_layout(list.track);
    const int16_t interruptedY = lv_obj_get_y(list.track);
    uiCenterListUpdate(list, items, 8, 2, "Back", true, false, true);
    lv_anim_t *retargeted = lv_anim_get(list.track, nullptr);
    const bool retargetMatches = retargeted != nullptr &&
        retargeted->start_value == interruptedY &&
        retargeted->end_value == uiCenterListTrackY(2, 67, 58) &&
        retargeted->time == 200 && retargeted->path_cb == lv_anim_path_ease_out;
    if (!retargetMatches && retargeted != nullptr) {
        out.printf("[INFO] retarget visual=%d start=%ld end=%ld time=%lu path=%d\n",
                   interruptedY, static_cast<long>(retargeted->start_value),
                   static_cast<long>(retargeted->end_value),
                   static_cast<unsigned long>(retargeted->time),
                   retargeted->path_cb == lv_anim_path_ease_out);
    }
    ok &= expect(out, retargetMatches,
                 "center list retargets interrupted travel from the current visual y");

    uiCenterListUpdate(list, items, 1, 0, "Back", true, false, true);
    ok &= expect(out, centerListTreeMatches(list, parent, 1) &&
                      lv_anim_get(list.track, nullptr) == nullptr,
                 "center list count change cancels travel and leaves one valid row");
    uiCenterListUpdate(list, items, 8, 0, "Back", true, false, false);

    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    uiCenterListUpdate(list, items, 8, 0, "Back", true, false, true);
    const bool hadTravelToFirstRow = lv_anim_get(list.track, nullptr) != nullptr;
    lv_tick_inc(40);
    lv_anim_refr_now();
    lv_obj_update_layout(list.track);
    const int16_t pulseTakeoverY = lv_obj_get_y(list.track);
    uiCenterListBoundaryPulse(list, -1, 100);
    lv_anim_t *takeoverPulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, hadTravelToFirstRow && takeoverPulse != nullptr &&
                      takeoverPulse->start_value == pulseTakeoverY &&
                      lv_anim_get(list.track, nullptr) == nullptr,
                 "first-row boundary pulse cancels active track travel and owns track y alone");
    lv_tick_inc(60);
    lv_anim_refr_now();
    lv_tick_inc(120);
    lv_anim_refr_now();

    lv_obj_update_layout(list.track);
    const int16_t firstRowRestY = lv_obj_get_y(list.track);
    uiCenterListBoundaryPulse(list, -1, 1);
    lv_anim_t *pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == firstRowRestY &&
                      pulse->end_value == firstRowRestY + 8 && pulse->time == 60,
                 "first-row boundary feedback starts a +8px 60ms track pulse");
    lv_tick_inc(60);
    lv_anim_refr_now();
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->end_value == firstRowRestY &&
                      pulse->time == 120 && pulse->path_cb == lv_anim_path_ease_out,
                 "first-row boundary feedback returns over 120ms ease-out");
    lv_tick_inc(20);
    lv_anim_refr_now();
    const int16_t restartY = lv_obj_get_y(list.track);
    uiCenterListBoundaryPulse(list, -1, 2);
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == restartY &&
                      pulse->end_value == firstRowRestY + 8 && pulse->time == 60,
                 "a second first-row pulse restarts from current y toward the fixed +8px bound");
    lv_tick_inc(30);
    lv_anim_refr_now();
    lv_obj_update_layout(list.track);
    const int16_t outwardRestartY = lv_obj_get_y(list.track);
    uiCenterListBoundaryPulse(list, -1, 3);
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == outwardRestartY &&
                      pulse->end_value == firstRowRestY + 8 &&
                      pulse->end_value <= firstRowRestY + 8,
                 "outward first-row interruption never accumulates beyond the +8px bound");

    uiCenterListUpdate(list, items, 8, 7, "Back", true, true, false);
    const int16_t footerRestY = lv_obj_get_y(list.footer);
    uiCenterListBoundaryPulse(list, 1, 4);
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == footerRestY &&
                      pulse->end_value == footerRestY + 6 && pulse->time == 60,
                 "footer boundary feedback starts a +6px 60ms local pulse");
    lv_tick_inc(30);
    lv_anim_refr_now();
    lv_obj_update_layout(list.footer);
    const int16_t footerOutwardRestartY = lv_obj_get_y(list.footer);
    uiCenterListBoundaryPulse(list, 1, 5);
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == footerOutwardRestartY &&
                      pulse->end_value == footerRestY + 6 &&
                      pulse->end_value <= footerRestY + 6,
                 "outward footer interruption never accumulates beyond the +6px bound");
    lv_tick_inc(60);
    lv_anim_refr_now();
    lv_tick_inc(20);
    lv_anim_refr_now();
    lv_obj_update_layout(list.footer);
    const int16_t footerReturnRestartY = lv_obj_get_y(list.footer);
    uiCenterListBoundaryPulse(list, 1, 6);
    pulse = lv_anim_get(&list, nullptr);
    ok &= expect(out, pulse != nullptr && pulse->start_value == footerReturnRestartY &&
                      pulse->end_value == footerRestY + 6 &&
                      pulse->end_value <= footerRestY + 6,
                 "returning footer interruption retargets from current y without overshoot");
    lv_tick_inc(60);
    lv_anim_refr_now();
    lv_tick_inc(120);
    lv_anim_refr_now();
    lv_obj_update_layout(list.footer);
    ok &= expect(out, lv_anim_get(&list, nullptr) == nullptr &&
                      lv_obj_get_y(list.footer) == footerRestY,
                 "boundary feedback returns to rest without queued travel");

    centerListEventCount = 0;
    lv_obj_t *row = lv_obj_get_child(list.track, 0);
    input->proc.types.pointer.act_point = lv_point_t{240, 220};
    lv_event_send(row, LV_EVENT_PRESSED, input);
    input->proc.types.pointer.act_point = lv_point_t{240, 170};
    lv_event_send(row, LV_EVENT_RELEASED, input);
    lv_event_send(row, LV_EVENT_CLICKED, input);
    ok &= expect(out, centerListEventCount == 1 &&
                      centerListEvents[0].kind == UiEventKind::ListNext,
                 "center list swipe emits one row step and suppresses follow-up activation");

    static AppState firstBoundary{};
    static AppState footerBoundary{};
    appInit(firstBoundary, 0);
    firstBoundary.nav.current = NavigationEntry{ScreenPage::Assets, 0, 0};
    centerListEventCount = 0;
    const uint32_t firstRevision = firstBoundary.boundaryPulseRevision;
    const uint8_t firstFocus = firstBoundary.nav.current.focus;
    const uint8_t firstAnchor = firstBoundary.nav.current.listAnchor;
    const uint8_t firstIndex = firstBoundary.assetListIndex;
    const uint16_t firstProgress = uiListProgressPermille(firstIndex, kAssetCount);
    const uint8_t firstCount = appFocusCount(firstBoundary);
    const uint32_t firstActivation = firstBoundary.commandCount;
    const bool firstDispatched = uiCenterListDispatchSwipe(
        list.viewport, input, lv_point_t{240, 170}, lv_point_t{240, 206}
    );
    if (centerListEventCount == 1) appHandleUiEvent(firstBoundary, centerListEvents[0], 1);
    ok &= expect(out, firstDispatched && centerListEventCount == 1 &&
                      centerListEvents[0].kind == UiEventKind::ListPrevious &&
                      firstBoundary.nav.current.page == ScreenPage::Assets &&
                      firstBoundary.nav.current.focus == firstFocus &&
                      firstBoundary.nav.current.listAnchor == firstAnchor &&
                      firstBoundary.assetListIndex == firstIndex &&
                      uiListProgressPermille(firstBoundary.assetListIndex, kAssetCount) == firstProgress &&
                      appFocusCount(firstBoundary) == firstCount &&
                      firstBoundary.commandCount == firstActivation &&
                      firstBoundary.boundaryPulseDirection == -1 &&
                      firstBoundary.boundaryPulseRevision == firstRevision + 1,
                 "qualifying LVGL first-row swipe dispatches ListPrevious and preserves boundaries");

    appInit(footerBoundary, 0);
    footerBoundary.nav.current = NavigationEntry{ScreenPage::Assets, kAssetCount, 0};
    footerBoundary.assetListIndex = kAssetCount - 1;
    centerListEventCount = 0;
    const uint32_t footerRevision = footerBoundary.boundaryPulseRevision;
    const uint32_t footerActivation = footerBoundary.commandCount;
    const uint8_t footerAnchor = footerBoundary.nav.current.listAnchor;
    const bool footerDispatched = uiCenterListDispatchSwipe(
        list.viewport, input, lv_point_t{240, 220}, lv_point_t{240, 184}
    );
    if (centerListEventCount == 1) appHandleUiEvent(footerBoundary, centerListEvents[0], 2);
    ok &= expect(out, footerDispatched && centerListEventCount == 1 &&
                      centerListEvents[0].kind == UiEventKind::ListNext &&
                      footerBoundary.nav.current.page == ScreenPage::Assets &&
                      appFocusIsFooter(footerBoundary) &&
                      footerBoundary.nav.current.listAnchor == footerAnchor &&
                      footerBoundary.assetListIndex == kAssetCount - 1 &&
                      uiListProgressPermille(footerBoundary.assetListIndex, kAssetCount) == 1000 &&
                      appFocusCount(footerBoundary) == kAssetCount + 1 &&
                      footerBoundary.commandCount == footerActivation &&
                      footerBoundary.boundaryPulseDirection == 1 &&
                      footerBoundary.boundaryPulseRevision == footerRevision + 1,
                 "qualifying LVGL footer swipe dispatches ListNext and preserves boundaries");

    centerListEventCount = 0;
    lv_event_send(list.footer, LV_EVENT_CLICKED, input);
    ok &= expect(out, centerListEventCount == 1 &&
                      centerListEvents[0].kind == UiEventKind::SelectFooter,
                 "center list footer emits semantic focused-footer selection");

    uiCenterListDestroy(list);
    ok &= expect(out, list.viewport == nullptr && lv_obj_get_child_cnt(parent) == 0,
                 "center list destroys the eight-row runtime tree without stale ownership");
    uiSetEventSink(nullptr);
    if (parent != nullptr) lv_obj_del(parent);
    if (input != nullptr) lv_indev_delete(input);
    lv_disp_set_default(previousDisplay);
    if (display != nullptr) {
        display->act_scr = nullptr;
        lv_disp_remove(display);
    }
    return ok;
}

UiEvent modalEvents[8]{};
uint8_t modalEventCount = 0;

void captureModalEvent(const UiEvent &event)
{
    if (modalEventCount < sizeof(modalEvents) / sizeof(modalEvents[0])) {
        modalEvents[modalEventCount++] = event;
    }
}

bool runModalComponentTests(Stream &out)
{
    bool ok = true;
    if (!lv_is_initialized()) lv_init();
    lv_disp_t *const previousDisplay = lv_disp_get_default();
    static lv_color_t pixels[480]{};
    static lv_disp_draw_buf_t drawBuffer{};
    static lv_disp_drv_t displayDriver{};
    static UiModal modal{};
    drawBuffer = lv_disp_draw_buf_t{};
    displayDriver = lv_disp_drv_t{};
    modal = UiModal{};
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, 480);
    lv_disp_drv_init(&displayDriver);
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = centerListFlush;
    displayDriver.hor_res = 480;
    displayDriver.ver_res = 480;
    lv_disp_t *display = lv_disp_drv_register(&displayDriver);
    if (display != nullptr) lv_disp_set_default(display);
    lv_obj_t *parent = display == nullptr ? nullptr : lv_obj_create(nullptr);
    UiModalView view{
        "Confirm trade", "$ 300", "Player 2 / Cash transfer", "9.8s remaining",
        "HOLD 1.2S TO CONFIRM", 0, true, true, false, false, false,
    };
    uiSetEventSink(captureModalEvent);
    uiModalCreate(modal, parent, view);
    if (parent != nullptr) lv_obj_update_layout(parent);

    const bool completeTree = modal.shade != nullptr && modal.panel != nullptr &&
        modal.confirm != nullptr && modal.holdTrack != nullptr &&
        modal.confirmFill != nullptr && modal.holdLabel != nullptr &&
        modal.back != nullptr && modal.countdown != nullptr;
    ok &= expect(out, completeTree && lv_obj_get_parent(modal.shade) == parent &&
                      lv_obj_get_parent(modal.panel) == modal.shade &&
                      lv_obj_get_parent(modal.confirm) == modal.panel &&
                      lv_obj_get_parent(modal.holdTrack) == modal.confirm &&
                      lv_obj_get_parent(modal.confirmFill) == modal.holdTrack &&
                      lv_obj_get_parent(modal.holdLabel) == modal.confirm &&
                      lv_obj_get_parent(modal.back) == modal.panel,
                 "modal retains the approved shade panel confirm track fill and Back tree");
    ok &= expect(out, completeTree &&
                      lv_obj_get_style_bg_opa(modal.shade, 0) == 184 &&
                      lv_obj_has_flag(modal.shade, LV_OBJ_FLAG_CLICKABLE) &&
                      !lv_obj_has_flag(modal.confirm, LV_OBJ_FLAG_OVERFLOW_VISIBLE) &&
                      !lv_obj_has_flag(modal.holdTrack, LV_OBJ_FLAG_OVERFLOW_VISIBLE) &&
                      !lv_obj_has_flag(modal.confirmFill, LV_OBJ_FLAG_OVERFLOW_VISIBLE),
                 "modal shade blocks background and all progress parents clip overflow");
    const bool trackGeometry = completeTree && lv_obj_get_x(modal.holdTrack) == 12 &&
        lv_obj_get_y(modal.holdTrack) == 46 &&
        lv_obj_get_width(modal.holdTrack) == 200 &&
        lv_obj_get_height(modal.holdTrack) == 8 &&
        lv_obj_get_height(modal.confirm) -
            (lv_obj_get_y(modal.holdTrack) + lv_obj_get_height(modal.holdTrack)) >= 10 &&
        lv_obj_get_y(modal.holdTrack) -
            (lv_obj_get_y(modal.holdLabel) + lv_obj_get_height(modal.holdLabel)) >= 6 &&
        lv_obj_get_index(modal.holdTrack) < lv_obj_get_index(modal.holdLabel);
    if (!trackGeometry && completeTree) {
        out.printf("MODAL TEST DIAG track=%d,%d,%d,%d confirm_h=%d label=%d,%d indices=%d,%d\n",
                   static_cast<int>(lv_obj_get_x(modal.holdTrack)),
                   static_cast<int>(lv_obj_get_y(modal.holdTrack)),
                   static_cast<int>(lv_obj_get_width(modal.holdTrack)),
                   static_cast<int>(lv_obj_get_height(modal.holdTrack)),
                   static_cast<int>(lv_obj_get_height(modal.confirm)),
                   static_cast<int>(lv_obj_get_y(modal.holdLabel)),
                   static_cast<int>(lv_obj_get_height(modal.holdLabel)),
                   static_cast<int>(lv_obj_get_index(modal.holdTrack)),
                   static_cast<int>(lv_obj_get_index(modal.holdLabel)));
    }
    ok &= expect(out, trackGeometry,
                 "modal hold track remains inset below the label and fill layer");
    ok &= expect(out, uiModalHoldFillWidth(0) == 0 &&
                      uiModalHoldFillWidth(500) == 100 &&
                      uiModalHoldFillWidth(1000) == 200 &&
                      uiModalHoldFillWidth(1500) == 200 &&
                      lv_obj_has_flag(modal.confirmFill, LV_OBJ_FLAG_HIDDEN),
                 "modal progress helper clamps exact 0 500 and 1000 permille widths");

    view.holdPermille = 500;
    uiModalUpdate(modal, view);
    if (parent != nullptr) lv_obj_update_layout(parent);
    ok &= expect(out, lv_obj_get_width(modal.confirmFill) == 100 &&
                      !lv_obj_has_flag(modal.confirmFill, LV_OBJ_FLAG_HIDDEN),
                 "modal renders half hold progress fully inside its track");
    view.holdPermille = 1000;
    uiModalUpdate(modal, view);
    if (parent != nullptr) lv_obj_update_layout(parent);
    ok &= expect(out, lv_obj_get_width(modal.confirmFill) == 200,
                 "modal renders completed hold progress at the track inner width");

    modalEventCount = 0;
    lv_event_send(modal.back, LV_EVENT_CLICKED, nullptr);
    lv_event_send(modal.confirm, LV_EVENT_PRESSED, nullptr);
    lv_event_send(modal.confirm, LV_EVENT_RELEASED, nullptr);
    ok &= expect(out, modalEventCount == 3 &&
                      modalEvents[0].kind == UiEventKind::Back &&
                      modalEvents[1].kind == UiEventKind::HoldDown &&
                      modalEvents[2].kind == UiEventKind::HoldUp,
                 "modal Back and confirm expose independent touch semantics");

    view.submitting = true;
    uiModalUpdate(modal, view);
    ok &= expect(out, modal.back == nullptr &&
                      !lv_obj_has_flag(modal.confirm, LV_OBJ_FLAG_CLICKABLE),
                 "submitted modal removes Back and disables repeated confirm touch");
    view.submitting = false;
    view.showBack = false;
    uiModalUpdate(modal, view);
    ok &= expect(out, modal.back == nullptr,
                 "forced modal does not create a Back object");

    uiModalDestroy(modal);
    ok &= expect(out, modal.shade == nullptr && parent != nullptr &&
                      lv_obj_get_child_cnt(parent) == 0,
                 "modal destroys its retained top-level tree without stale ownership");
    uiSetEventSink(nullptr);
    if (parent != nullptr) lv_obj_del(parent);
    lv_disp_set_default(previousDisplay);
    if (display != nullptr) {
        display->act_scr = nullptr;
        lv_disp_remove(display);
    }
    return ok;
}

void openVoluntaryMortgageFixture(AppState &state, uint32_t nowMs)
{
    appInit(state, nowMs);
    state.selectedAsset = 0;
    state.nav.current = NavigationEntry{ScreenPage::AssetDetail, 0, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}

void openTradeFixture(AppState &state, uint32_t nowMs)
{
    appInit(state, nowMs);
    state.nav.current = NavigationEntry{ScreenPage::Trade, 2, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}

void openDebtFixture(AppState &state, int32_t amountDue, int32_t cash,
                     uint32_t eligibleMask)
{
    appInit(state, 0);
    TransportEvent event{};
    event.kind = TransportEventKind::DebtResolutionRequired;
    event.transactionId = 90;
    event.stateVersion = 2;
    event.amount = amountDue;
    event.cash = cash;
    event.assetMask = eligibleMask;
    appHandleTransportEvent(state, event, 0);
}

void focusDebtAsset(AppState &state, uint8_t index)
{
    state.nav.current.focus = index;
    state.debtListIndex = index;
}

bool sameNavigationEntry(const NavigationEntry &left, const NavigationEntry &right)
{
    return left.page == right.page && left.focus == right.focus &&
           left.listAnchor == right.listAnchor;
}

bool runModalAuthorityTests(Stream &out)
{
    bool ok = true;
    static AppState state{};
    TransportCommand command{};

    openTradeFixture(state, 100);
    state.nav.current.listAnchor = 4;
    const NavigationEntry tradeSource = state.nav.current;
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 210}, 210);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 220}, 220);
    appTick(state, 819);
    ok &= expect(out, state.modal.focus == ModalFocus::Cancel &&
                      appHoldProgressPermille(state, 819) == 0 &&
                      state.commandCount == 0,
                 "voluntary Back never accumulates hold progress");
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 819}, 819);
    shortPress(state, 900, 980);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      sameNavigationEntry(state.nav.current, tradeSource) &&
                      state.commandCount == 0,
                 "rotary voluntary Back restores exact source without a command");

    appInit(state, 1000);
    state.selectedAsset = 0;
    state.nav.current = NavigationEntry{ScreenPage::AssetDetail, 0, 3};
    shortPress(state, 1010, 1100);
    const NavigationEntry mortgageSource = state.nav.current;
    appHandleTouch(state, TouchAction::Back, 1110);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      sameNavigationEntry(state.nav.current, mortgageSource) &&
                      state.commandCount == 0,
                 "touch voluntary Back restores exact source without a command");

    openTradeFixture(state, 1200);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 1210}, 1210);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1220}, 1220);
    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 1230}, 1230);
    appTick(state, 2420);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2420}, 2420);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      state.modal.focus == ModalFocus::Confirm &&
                      !state.modal.submitting && !state.modal.holding &&
                      state.commandCount == 0 && appHoldProgressPermille(state, 2420) == 0,
                 "hold started on Back cannot submit after rotating to Confirm");

    openTradeFixture(state, 2000);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 2200}, 2200);
    appTick(state, 3399);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 3399}, 3399);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      !state.modal.submitting && state.commandCount == 0 &&
                      appHoldProgressPermille(state, 3399) == 0,
                 "1199ms release resets progress and sends no command");

    const NavigationEntry submittedSource = state.nav.current;
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 4000}, 4000);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 5200}, 5200);
    appTick(state, 5400);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 5500}, 5500);
    appTick(state, 7000);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 7000}, 7000);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      state.modal.submitting && state.commandCount == 1,
                 "1200ms release queues one command and repeated hold stays one-shot");
    appHandleTouch(state, TouchAction::Back, 7010);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      state.modal.submitting,
                 "submitted voluntary modal removes its Back path");
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::TradeCreate &&
                      command.requestId != 0 && !appPollCommand(state, command),
                 "trade confirmation exposes one nonzero request id");

    const uint32_t tradeRequestId = command.requestId;
    const uint32_t unrelatedRequestId = tradeRequestId + 100;
    state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::RollRequest)] =
        unrelatedRequestId;
    TransportEvent unrelatedRejected{};
    unrelatedRejected.kind = TransportEventKind::CommandRejected;
    unrelatedRejected.requestId = unrelatedRequestId;
    unrelatedRejected.error = TransportError::ActionNotAllowed;
    appHandleTransportEvent(state, unrelatedRejected, 7015);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      state.modal.submitting &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::TradeCreate)] ==
                          tradeRequestId &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::RollRequest)] == 0,
                 "unrelated rejection cannot unlock the active modal submission");

    TransportEvent wrongKind{};
    wrongKind.kind = TransportEventKind::PaymentCompleted;
    wrongKind.requestId = tradeRequestId;
    wrongKind.cash = 1;
    appHandleTransportEvent(state, wrongKind, 7020);
    TransportEvent zeroCompletion{};
    zeroCompletion.kind = TransportEventKind::CommandCompleted;
    appHandleTransportEvent(state, zeroCompletion, 7030);
    TransportEvent wrongCompletion = zeroCompletion;
    wrongCompletion.requestId = tradeRequestId + 1;
    appHandleTransportEvent(state, wrongCompletion, 7040);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      state.modal.submitting &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::TradeCreate)] ==
                          tradeRequestId,
                 "wrong-kind zero and mismatched completions preserve submission");

    TransportEvent tradeCompleted = zeroCompletion;
    tradeCompleted.requestId = tradeRequestId;
    appHandleTransportEvent(state, tradeCompleted, 7050);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      sameNavigationEntry(state.nav.current, submittedSource) &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::TradeCreate)] == 0 &&
                      state.toastUntilMs == 8850,
                 "matching trade completion dismisses once and restores source");
    const uint32_t acceptedRevision = state.revision;
    appHandleTransportEvent(state, tradeCompleted, 7060);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      state.revision == acceptedRevision,
                 "duplicate generic completion is inert");

    appInit(state, 0);
    TransportEvent paymentRequired{};
    paymentRequired.kind = TransportEventKind::PaymentRequired;
    paymentRequired.transactionId = 90;
    paymentRequired.amount = 175;
    paymentRequired.cash = state.money;
    paymentRequired.deadlineMs = 10000;
    appHandleTransportEvent(state, paymentRequired, 0);
    appHandleTouch(state, TouchAction::Back, 10);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 20}, 20);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                      !state.modal.cancelAllowed && state.modal.focus == ModalFocus::Confirm,
                 "forced payment has no touch or rotary Back path");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 100}, 100);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1300}, 1300);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::PayNow && command.requestId != 0,
                 "forced payment submits one correlated request");
    const int32_t cashBeforeCompletion = state.money;
    TransportEvent wrongPayment{};
    wrongPayment.kind = TransportEventKind::PaymentCompleted;
    wrongPayment.requestId = command.requestId + 1;
    wrongPayment.transactionId = 90;
    wrongPayment.cash = 1685;
    appHandleTransportEvent(state, wrongPayment, 1310);
    wrongPayment.requestId = 0;
    appHandleTransportEvent(state, wrongPayment, 1320);
    wrongPayment.requestId = command.requestId;
    wrongPayment.transactionId = 91;
    appHandleTransportEvent(state, wrongPayment, 1330);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                      state.modal.submitting && state.money == cashBeforeCompletion,
                 "forced payment ignores zero mismatched and wrong-transaction completion");
    wrongPayment.transactionId = 90;
    appHandleTransportEvent(state, wrongPayment, 1340);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == 1685,
                 "forced payment exits only on its matching completion");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    state.debt.selectedMask = 0x55;
    focusDebtAsset(state, kAssetCount);
    shortPress(state, 1400, 1490);
    appHandleTouch(state, TouchAction::Back, 1500);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 1510}, 1510);
    ok &= expect(out, state.modal.kind == ModalKind::DebtMortgageConfirm &&
                      !state.modal.cancelAllowed && state.modal.focus == ModalFocus::Confirm,
                 "forced debt confirmation has no Back path");

    static DemoTransport transport;
    TransportEvent event{};
    transport.setScenario(DemoScenario::RentClaim);
    transport.begin(0);
    TransportCommand rent{TransportCommandKind::ClaimRent, 601, 41, 0, 0};
    ok &= expect(out, transport.send(rent, 10), "demo accepts rent claim confirmation");
    transport.tick(129);
    ok &= expect(out, !transport.poll(event), "rent completion is not emitted before 120ms");
    transport.tick(130);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::CommandCompleted &&
                      event.requestId == 601 && !transport.poll(event),
                 "rent completion returns matching request within 150ms");

    transport.setScenario(DemoScenario::Waiting);
    transport.begin(0);
    TransportCommand trade{TransportCommandKind::TradeCreate, 602, 41, 0, 0};
    ok &= expect(out, transport.send(trade, 20), "demo accepts trade confirmation");
    transport.tick(139);
    ok &= expect(out, !transport.poll(event), "trade completion is not emitted before 120ms");
    transport.tick(140);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::CommandCompleted &&
                      event.requestId == 602 && !transport.poll(event),
                 "trade completion returns matching request within 150ms");

    return ok;
}

uint8_t renderedTurnDots(lv_obj_t *screen, uint8_t expectedFilled)
{
    uint8_t matches = 0;
    for (uint8_t index = 0; index < 5; ++index) {
        const int16_t x = static_cast<int16_t>(200 + index * 18);
        for (lv_obj_t *child = lv_obj_get_child(screen, 0); child != nullptr;
             child = lv_obj_get_child(screen, lv_obj_get_index(child) + 1)) {
            if (lv_obj_get_x(child) != x || lv_obj_get_y(child) != 114) continue;
            const bool filled = lv_obj_get_style_bg_opa(child, 0) != LV_OPA_TRANSP;
            if (filled == (index < expectedFilled)) ++matches;
            break;
        }
    }
    return matches;
}

bool treeContainsLabelText(lv_obj_t *object, const char *expected)
{
    if (object == nullptr || expected == nullptr) return false;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *text = lv_label_get_text(object);
        if (text != nullptr && strcmp(text, expected) == 0) return true;
    }
    const uint32_t childCount = lv_obj_get_child_cnt(object);
    for (uint32_t index = 0; index < childCount; ++index) {
        if (treeContainsLabelText(lv_obj_get_child(object, index), expected)) return true;
    }
    return false;
}

lv_obj_t *findDirectChildAt(lv_obj_t *parent, UiRect rect)
{
    if (parent == nullptr) return nullptr;
    const uint32_t childCount = lv_obj_get_child_cnt(parent);
    for (uint32_t index = 0; index < childCount; ++index) {
        lv_obj_t *const child = lv_obj_get_child(parent, index);
        if (lv_obj_get_style_x(child, 0) == rect.x &&
            lv_obj_get_style_y(child, 0) == rect.y &&
            lv_obj_get_style_width(child, 0) == rect.w &&
            lv_obj_get_style_height(child, 0) == rect.h) {
            return child;
        }
    }
    return nullptr;
}

bool runHomeRendererTests(Stream &out)
{
    bool ok = true;
    lv_disp_t *const previousDisplay = lv_disp_get_default();
    static lv_color_t pixels[480]{};
    static lv_disp_draw_buf_t drawBuffer{};
    static lv_disp_drv_t driver{};
    static AppState state{};
    drawBuffer = lv_disp_draw_buf_t{};
    driver = lv_disp_drv_t{};
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, 480);
    lv_disp_drv_init(&driver);
    driver.draw_buf = &drawBuffer;
    driver.flush_cb = centerListFlush;
    driver.hor_res = 480;
    driver.ver_res = 480;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    if (display != nullptr) lv_disp_set_default(display);
    appInit(state, 0);
    ok &= expect(out, display != nullptr && uiRendererBegin(),
                 "Home render fixture initializes a real LVGL screen");
    const uint8_t dotCases[] = {0, 1, 3, 5};
    for (uint8_t filled : dotCases) {
        state.homePhase = HomePhase::Waiting;
        state.turnsUntilYou = filled;
        ++state.revision;
        uiRendererRender(state, filled);
        ok &= expect(out, renderedTurnDots(lv_scr_act(), filled) == 5,
                     "Home renders the exact filled and hollow dot set");
    }
    state.homePhase = HomePhase::MyTurn;
    ++state.revision;
    uiRendererRender(state, 6);
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *ring = nullptr;
    lv_obj_t *carousel = nullptr;
    for (lv_obj_t *child = lv_obj_get_child(screen, 0); child != nullptr;
         child = lv_obj_get_child(screen, lv_obj_get_index(child) + 1)) {
        if (lv_obj_get_x(child) == 31 && lv_obj_get_y(child) == 31) ring = child;
        if (lv_obj_get_x(child) == 48 && lv_obj_get_y(child) == 300) carousel = child;
    }
    lv_obj_t *dice = carousel == nullptr ? nullptr : lv_obj_get_child(carousel, 0);
    ok &= expect(out, ring != nullptr &&
                      lv_obj_get_style_border_color(ring, 0).full == lv_color_hex(0x52DCB7).full &&
                      dice != nullptr &&
                      lv_obj_get_style_border_color(dice, 0).full == lv_color_hex(0xF2C453).full,
                 "My Turn uses the green ring while Dice retains yellow priority");
#if GRIDOPOLY_SELF_TEST == 1
    uiRendererResetTestStats();
    state.focus = 1;
    ++state.revision;
    uiRendererRender(state, 7);
    const UiRendererTestStats rendererStats = uiRendererGetTestStats();
    ok &= expect(out, rendererStats.incrementalRenders == 1 &&
                      rendererStats.rebuildRenders == 0,
                 "My Turn focus change uses retained incremental renderer path");

    uiRendererResetTestStats();
    state.focus = 2;
    state.modal = ModalState{};
    state.modal.kind = ModalKind::ForcedPayment;
    state.modal.title = "FORCED PAYMENT REVIEW";
    state.modal.amount = 120;
    ++state.revision;
    uiRendererRender(state, 8);
    UiRendererTestStats mergedStats = uiRendererGetTestStats();
    lv_obj_t *const modalShade = findDirectChildAt(lv_scr_act(), UiRect{0, 0, 480, 480});
    lv_obj_t *const modalPanel = findDirectChildAt(modalShade, kModalRect);
    const bool modalVisible = modalShade != nullptr && modalPanel != nullptr &&
                              lv_obj_get_style_bg_opa(modalShade, 0) == 184;
    ok &= expect(out, mergedStats.incrementalRenders == 0 &&
                      mergedStats.rebuildRenders == 1 && modalVisible,
                 "focus plus forced-payment modal rebuilds instead of swallowing the overlay");

    state.modal = ModalState{};
    ++state.revision;
    uiRendererRender(state, 9);
    uiRendererResetTestStats();
    state.focus = 3;
    state.money = 2345;
    state.toast = "BALANCE UPDATED";
    state.toastUntilMs = 1000;
    ++state.revision;
    uiRendererRender(state, 10);
    mergedStats = uiRendererGetTestStats();
    const bool moneyVisible = treeContainsLabelText(lv_scr_act(), "$ 2345");
    lv_obj_t *const toast = findDirectChildAt(lv_scr_act(), UiRect{112, 397, 256, 36});
    const bool toastVisible = toast != nullptr && lv_obj_get_child_cnt(toast) != 0 &&
                              lv_obj_get_style_border_color(toast, 0).full ==
                                  lv_color_hex(0x52DCB7).full;
    ok &= expect(out, mergedStats.incrementalRenders == 0 &&
                      mergedStats.rebuildRenders == 1 &&
                      moneyVisible && toastVisible,
                 "focus plus money and toast changes rebuilds every visible field");
    uiRendererResetForTest();
#endif
    lv_disp_set_default(previousDisplay);
    if (display != nullptr) {
        display->act_scr = nullptr;
        lv_disp_remove(display);
    }
    return ok;
}

bool debtAuthorityStateUnchanged(const AppState &state, const AppState &before)
{
    const uint8_t mortgageIndex = static_cast<uint8_t>(TransportCommandKind::MortgageBatchRequest);
    return state.money == before.money && state.position == before.position &&
           state.stateVersion == before.stateVersion && state.pendingCommandMask == before.pendingCommandMask &&
           state.pendingRequestIds[mortgageIndex] == before.pendingRequestIds[mortgageIndex] &&
           state.nav.current.page == before.nav.current.page &&
           state.nav.current.focus == before.nav.current.focus &&
           state.nav.current.listAnchor == before.nav.current.listAnchor &&
           state.modal.kind == before.modal.kind && state.modal.focus == before.modal.focus &&
           state.modal.submitting == before.modal.submitting &&
           state.modal.deadlineMs == before.modal.deadlineMs &&
           state.modal.transactionId == before.modal.transactionId &&
           state.debt.transactionId == before.debt.transactionId &&
           state.debt.amountDue == before.debt.amountDue &&
           state.debt.cashBefore == before.debt.cashBefore &&
           state.debt.selectedMask == before.debt.selectedMask &&
           state.debt.eligibleMask == before.debt.eligibleMask &&
           state.toast == before.toast && state.toastUntilMs == before.toastUntilMs;
}

struct BoundaryInvariant {
    ScreenPage page;
    uint8_t focus;
    uint8_t anchor;
    uint8_t selected;
    uint16_t progress;
    uint8_t count;
    uint32_t activationCount;
    uint32_t pulseRevision;
};

BoundaryInvariant captureAssetBoundaryInvariant(const AppState &state)
{
    return BoundaryInvariant{
        state.nav.current.page,
        state.nav.current.focus,
        state.nav.current.listAnchor,
        state.assetListIndex,
        uiListProgressPermille(state.assetListIndex, kAssetCount),
        appFocusCount(state),
        state.commandCount,
        state.boundaryPulseRevision,
    };
}

bool boundaryInvariantMatches(const AppState &state, const BoundaryInvariant &before)
{
    const BoundaryInvariant after = captureAssetBoundaryInvariant(state);
    return after.page == before.page && after.focus == before.focus &&
           after.anchor == before.anchor && after.selected == before.selected &&
           after.progress == before.progress && after.count == before.count &&
           after.activationCount == before.activationCount;
}

} // namespace

bool runStateLogicTests(Stream &out)
{
    bool ok = true;
    ok &= expect(out, uiBox(nullptr, kModalRect, 0, 0, 0) == nullptr &&
                      uiLabel(nullptr, "", kModalRect, nullptr, 0) == nullptr,
                 "ui primitives reject null parents without allocation");
    uiBindTap(nullptr, UiEventKind::ActivateFocused);
    uiBindHold(nullptr);
    ok &= expect(out, true, "ui primitive bindings ignore null objects");
    ok &= expect(out, uiRectInsideCircle(kNormalFooter),
                 "footer stays inside round safe area");
    ok &= expect(out, kNormalListCount.x == 202 && kNormalListCount.y == 328 &&
                      kNormalListCount.w == 76 && kNormalListCount.h == 16,
                 "list count uses the approved below-progress rectangle");
    ok &= expect(out, uiRectInsideCircle(kModalRect),
                 "modal corners stay inside round safe area");
    ok &= expect(out, !uiRectInsideCircle(UiRect{0, 0, 32, 32}),
                 "corner geometry rejects rectangles outside round safe area");
    ok &= expect(out, uiCarouselPose(0).centerX == 240 &&
                      uiCarouselPose(0).zoom == 256 &&
                      uiCarouselPose(1).zoom == 213 &&
                      uiCarouselPose(2).zoom == 179 &&
                      uiCarouselPose(0).opacity == 255 &&
                      uiCarouselPose(1).opacity == 87 &&
                      uiCarouselPose(2).opacity == 15,
                 "carousel uses approved 100 83 70 percent zoom and opacity hierarchy");
    ok &= expect(out, uiCarouselPose(-1).centerX == 152 &&
                      uiCarouselPose(1).centerX == 328 &&
                      uiCarouselPose(3).opacity == 0,
                 "carousel keeps neighboring slots symmetric and hides distant slots");
    HomeAction waitingActions[5]{};
    HomeAction nextActions[5]{};
    HomeAction turnActions[5]{};
    const uint8_t waitingActionCount = uiCarouselActions(HomePhase::Waiting, waitingActions);
    const uint8_t nextActionCount = uiCarouselActions(HomePhase::NextPlayer, nextActions);
    const uint8_t turnActionCount = uiCarouselActions(HomePhase::MyTurn, turnActions);
    ok &= expect(out, waitingActionCount == 4 && nextActionCount == 4 &&
                      waitingActions[0] == HomeAction::Assets &&
                      waitingActions[1] == HomeAction::Players &&
                      waitingActions[2] == HomeAction::Trade &&
                      waitingActions[3] == HomeAction::More &&
                      nextActions[0] == HomeAction::Assets &&
                      nextActions[1] == HomeAction::Players &&
                      nextActions[2] == HomeAction::Trade &&
                      nextActions[3] == HomeAction::More,
                 "waiting and next expose Assets Players Trade More in order");
    ok &= expect(out, turnActionCount == 5 &&
                      turnActions[0] == HomeAction::Dice &&
                      turnActions[1] == HomeAction::Assets &&
                      turnActions[2] == HomeAction::Players &&
                      turnActions[3] == HomeAction::Trade &&
                      turnActions[4] == HomeAction::More,
                 "my turn exposes Dice Assets Players Trade More in order");
    ok &= expect(out, uiCenterListTrackY(3, 220, 58) == 46,
                 "center list track locks selected row");
    ok &= expect(out, uiListProgressPermille(0, 8) == 0 &&
                      uiListProgressPermille(7, 8) == 1000 &&
                      uiListProgressPermille(2, 1) == 0,
                 "list progress spans complete item range");
    ok &= expect(out, uiCenterListSwipeStep(0, 35) == 0 &&
                      uiCenterListSwipeStep(0, 36) == -1 &&
                      uiCenterListSwipeStep(0, -36) == 1,
                 "center list swipe uses the exact 36px threshold");
    ok &= expect(out, uiCenterListSwipeStep(24, 36) == -1 &&
                      uiCenterListSwipeStep(25, 36) == 0 &&
                      uiCenterListSwipeStep(-24, -36) == 1 &&
                      uiCenterListSwipeStep(80, 120) == -1,
                 "center list swipe requires 12px vertical dominance and emits one step");
    ok &= expect(out, uiMoneyFontPx(1860) == 40 &&
                      uiMoneyFontPx(12345678) == 24 &&
                      uiMoneyFontPx(-123456) == 32,
                 "large balances select a round-safe font size");
    ok &= expect(out, uiMoneyFontPx(99999) == 40 &&
                      uiMoneyFontPx(100000) == 32 &&
                      uiMoneyFontPx(10000000) == 24,
                 "money font counts separators at exact display thresholds");
    const DicePose dieZeroStart = uiDicePose(0, 0, 4);
    const DicePose dieZeroLift = uiDicePose(150, 0, 4);
    const DicePose dieZeroSpin = uiDicePose(1050, 0, 4);
    const DicePose dieZeroLand = uiDicePose(1500, 0, 4);
    const DicePose dieZeroPreSettle = uiDicePose(1899, 0, 4);
    const DicePose dieZeroFinal = uiDicePose(1900, 0, 4);
    const DicePose dieOneFinal = uiDicePose(1900, 1, 6);
    ok &= expect(out, dieZeroStart.x != dieZeroLift.x &&
                      dieZeroLift.x != dieZeroSpin.x &&
                      dieZeroSpin.x != dieZeroLand.x,
                 "dice pose traverses all motion segments");
    ok &= expect(out, dieZeroStart.face != 4 && dieZeroLift.face != 4 &&
                      dieZeroSpin.face != 4 && dieZeroLand.face != 4 &&
                      dieZeroPreSettle.face != 4,
                 "dice never selects its final face before settling");
    ok &= expect(out, dieZeroFinal.face == 4 && dieZeroFinal.angleTenths == 0 &&
                      dieZeroFinal.zoom == 256 && dieOneFinal.face == 6 &&
                      dieOneFinal.angleTenths == 0 && dieOneFinal.zoom == 256,
                 "dice settles on its final face for both dice at 1900ms");
    ok &= expect(out, static_cast<uint8_t>(ModalKind::None) == 0 &&
                      static_cast<uint8_t>(ModalKind::CollectRent) == 1 &&
                      static_cast<uint8_t>(ModalKind::ForcedPayment) == 2 &&
                      static_cast<uint8_t>(ModalKind::VoluntaryMortgage) == 3 &&
                      static_cast<uint8_t>(ModalKind::TradeCreate) == 4 &&
                      static_cast<uint8_t>(ModalKind::DebtMortgageConfirm) == 5,
                 "exact modal kind names compile in contract order");
    static AppState state{};
    appInit(state, 0);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 0,
                 "starts on home");

    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 10}, 10);
    ok &= expect(out, state.nav.current.focus == 3, "home focus wraps left");
    appHandleInput(state, InputEvent{InputKind::Rotate, 2, 20}, 20);
    ok &= expect(out, state.nav.current.focus == 1, "home focus advances by delta");

    shortPress(state, 100, 300);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players, "short press activates focus");
    shortPress(state, 400, 1000);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players, "500-799ms dead band does nothing");
    shortPress(state, 1100, 1950);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home, "800ms hold returns home");

    state.nav.current = NavigationEntry{ScreenPage::Home, 0, 0};
    shortPress(state, 2000, 5100);
    ok &= expect(out, state.nav.current.page == ScreenPage::DemoLab, "3s hold opens demo lab");

    state.nav.current.focus = 3;
    shortPress(state, 5200, 5400);
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent, "demo opens rent modal");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 5500}, 5500);
    appTick(state, 6699);
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent, "hold is not early");
    appTick(state, 6700);
    TransportCommand command{};
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent && state.money == 1860 &&
                      appPollCommand(state, command) && command.kind == TransportCommandKind::ClaimRent,
                 "1.2s queues a rent claim without local cash mutation");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::DemoLab, 4, 0};
    shortPress(state, 7000, 7200);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment, "demo opens payment modal");
    appTick(state, 17200);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment && state.modal.submitting &&
                      state.money == 1860 && appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::PayNow,
                 "payment deadline queues PayNow without local cash mutation");

    appInit(state, 0);
    state.homePhase = HomePhase::Waiting;
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 10}, 10);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 100}, 100);
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets,
                 "home asset action opens assets");
    ok &= expect(out, appFocusCount(state) == kAssetCount + 1,
                 "assets include fixed back footer");

    state.nav.current.focus = kAssetCount;
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 200}, 200);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 300}, 300);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      state.nav.current.focus == 0,
                 "footer back restores home asset focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 310);
    state.nav.current.focus = 2;
    state.assetListIndex = 2;
    shortPress(state, 320, 400);
    ok &= expect(out, state.nav.current.page == ScreenPage::AssetDetail,
                 "asset row opens detail");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 410);
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets &&
                      state.nav.current.focus == 2 && state.assetListIndex == 2,
                 "asset detail back restores assets focus and anchor");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 420);

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 430);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 431);
    state.nav.current.focus = 3;
    state.playerListIndex = 3;
    shortPress(state, 440, 520);
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerDetail,
                 "player row opens detail");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 530);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players &&
                      state.nav.current.focus == 3 && state.playerListIndex == 3,
                 "player detail back restores players focus and anchor");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 540);

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 550);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 551);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 560);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 2,
                 "trade back restores home trade focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 570);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 571);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 580);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 3,
                 "more back restores home more focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 590);
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 600);
    ok &= expect(out, state.nav.current.page == ScreenPage::DemoLab, "more opens demo lab");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 610);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 3,
                 "demo lab back restores home more focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 620);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 621);
    const BoundaryInvariant firstBoundaryBefore = captureAssetBoundaryInvariant(state);
    appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, 630);
    ok &= expect(out, boundaryInvariantMatches(state, firstBoundaryBefore) &&
                      state.boundaryPulseDirection == -1 &&
                      state.boundaryPulseRevision == firstBoundaryBefore.pulseRevision + 1,
                 "first-row rotary boundary changes only its local pulse revision");
    const BoundaryInvariant firstSwipeBoundaryBefore = captureAssetBoundaryInvariant(state);
    appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, 631);
    ok &= expect(out, boundaryInvariantMatches(state, firstSwipeBoundaryBefore) &&
                      state.boundaryPulseDirection == -1 &&
                      state.boundaryPulseRevision == firstSwipeBoundaryBefore.pulseRevision + 1,
                 "qualifying first-row swipe boundary preserves navigation invariants");
    for (uint8_t i = 0; i < kAssetCount + 2; ++i) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 640 + i);
    }
    ok &= expect(out, appFocusIsFooter(state) && state.assetListIndex == kAssetCount - 1,
                 "list next clamps at footer without moving list anchor");
    const BoundaryInvariant footerBoundaryBefore = captureAssetBoundaryInvariant(state);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 649}, 649);
    ok &= expect(out, boundaryInvariantMatches(state, footerBoundaryBefore) &&
                      state.boundaryPulseDirection == 1 &&
                      state.boundaryPulseRevision == footerBoundaryBefore.pulseRevision + 1,
                 "bottom rotary boundary preserves page focus anchor progress count and activation");
    const BoundaryInvariant footerSwipeBoundaryBefore = captureAssetBoundaryInvariant(state);
    appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 650);
    ok &= expect(out, boundaryInvariantMatches(state, footerSwipeBoundaryBefore) &&
                      state.boundaryPulseDirection == 1 &&
                      state.boundaryPulseRevision == footerSwipeBoundaryBefore.pulseRevision + 1,
                 "qualifying footer swipe preserves page focus anchor progress count and activation");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 1}, 650);
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets && state.nav.current.focus == 1,
                 "touching an unfocused list row selects it");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 1}, 660);
    ok &= expect(out, state.nav.current.page == ScreenPage::AssetDetail,
                 "touching the selected list row activates it");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::Assets, 0, 0};
    appHandleInput(state, InputEvent{InputKind::Rotate, 20, 10}, 10);
    ok &= expect(out, state.nav.current.focus == kAssetCount,
                 "long asset delta clamps at fixed back footer");
    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 20}, 20);
    ok &= expect(out, state.nav.current.focus == kAssetCount - 1 &&
                      state.assetListIndex == kAssetCount - 1,
                 "reverse from footer restores last asset row");
    appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 30);
    appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 31);
    ok &= expect(out, state.nav.current.focus == kAssetCount &&
                      state.assetListIndex == kAssetCount - 1,
                 "discrete list next clamps at footer and preserves selected row");
    appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, 40);
    ok &= expect(out, state.nav.current.focus == kAssetCount - 1 &&
                      state.assetListIndex == kAssetCount - 1,
                 "discrete list previous returns from footer by one row");
    appHandleTouch(
        state,
        static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::ListItem0) + 2),
        50
    );
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets &&
                      state.nav.current.focus == 2 && state.assetListIndex == 2,
                 "center list row touch synchronizes focus without activation");
    appHandleTouch(
        state,
        static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::ListItem0) + 2),
        60
    );
    ok &= expect(out, state.nav.current.page == ScreenPage::AssetDetail &&
                      state.selectedAsset == 2,
                 "second center list row touch activates the focused row");

    appInit(state, 0);
    state.nav.stack[0] = NavigationEntry{ScreenPage::Home, 0, 0};
    state.nav.depth = 1;
    state.nav.current = NavigationEntry{ScreenPage::Assets, 0, 0};
    appHandleTouch(state, TouchAction::Footer, 70);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home,
                 "ordinary fixed Back footer returns on one touch");

    appInit(state, 0);
    state.nav.stack[0] = NavigationEntry{ScreenPage::Home, 3, 3};
    state.nav.depth = 1;
    state.nav.current = NavigationEntry{ScreenPage::Debt, 0, 0};
    ok &= expect(out, appFocusCount(state) == 2 && !appFocusIsFooter(state),
                 "ordinary Debt has one action followed by fixed Back");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectFooter, 0}, 90);
    ok &= expect(out, state.nav.current.page == ScreenPage::Debt && appFocusIsFooter(state),
                 "first Debt footer touch focuses fixed Back without activation");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectFooter, 0}, 100);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 3,
                 "second Debt footer touch activates fixed Back");

    appInit(state, 0);
    const uint8_t initialTradeReceiver = state.tradeReceiver;
    const int32_t initialTradeAmount = state.tradeAmount;
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 100);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 110);
    ok &= expect(out, state.nav.current.page == ScreenPage::Trade &&
                      state.tradeEntryMode == TradeEntryMode::HomeEditable &&
                      state.nav.current.focus == 0 &&
                      state.inlineEditField == InlineEditField::None &&
                      !appTradeReceiverLocked(state),
                 "Home Trade opens with editable Receiver in Browse");
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 120}, 120);
    ok &= expect(out, state.nav.current.focus == 1 &&
                      state.tradeReceiver == initialTradeReceiver &&
                      state.tradeAmount == initialTradeAmount &&
                      state.inlineEditField == InlineEditField::None,
                 "Trade Browse rotation moves focus without mutating values");
    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 130}, 130);
    shortPress(state, 140, 200);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      appInlineFieldEditing(state, InlineEditField::TradeReceiver) &&
                      state.commandCount == 0,
                 "short press enters Receiver Editing without transport");
    const int32_t receiverEditAmount = state.tradeAmount;
    appHandleInput(state, InputEvent{InputKind::Rotate, 2, 210}, 210);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      state.tradeReceiver == (initialTradeReceiver + 2) % kPlayerCount &&
                      state.tradeAmount == receiverEditAmount &&
                      appInlineFieldEditing(state, InlineEditField::TradeReceiver) &&
                      state.commandCount == 0,
                 "Receiver Editing changes only Receiver and keeps focus fixed");
    const uint8_t committedReceiver = state.tradeReceiver;
    shortPress(state, 220, 280);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.tradeReceiver == committedReceiver && state.commandCount == 0,
                 "second short press commits Receiver draft and returns to Browse");

    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 290}, 290);
    appHandleTouch(state, TouchAction::TradeAmount, 300);
    ok &= expect(out, appInlineFieldEditing(state, InlineEditField::TradeAmount),
                 "tap on focused Amount enters Editing");
    const uint8_t amountEditReceiver = state.tradeReceiver;
    appHandleInput(state, InputEvent{InputKind::Rotate, 20, 310}, 310);
    ok &= expect(out, state.nav.current.focus == 1 && state.tradeAmount == 500 &&
                      state.tradeReceiver == amountEditReceiver &&
                      appInlineFieldEditing(state, InlineEditField::TradeAmount),
                 "Amount Editing applies 50 steps, clamps maximum, and keeps focus fixed");
    appHandleInput(state, InputEvent{InputKind::Rotate, -20, 320}, 320);
    ok &= expect(out, state.tradeAmount == 50 && state.tradeReceiver == amountEditReceiver,
                 "Amount Editing clamps the existing minimum");
    appHandleTouch(state, TouchAction::TradeAmount, 330);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.tradeAmount == 50 && state.commandCount == 0,
                 "second Amount tap commits locally without transport");

    appHandleTouch(state, TouchAction::TradeAmount, 340);
    const uint8_t beforeDifferentFieldReceiver = state.tradeReceiver;
    const int32_t beforeDifferentFieldAmount = state.tradeAmount;
    appHandleTouch(state, TouchAction::TradeReceiver, 350);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      state.inlineEditField == InlineEditField::None &&
                      state.tradeReceiver == beforeDifferentFieldReceiver &&
                      state.tradeAmount == beforeDifferentFieldAmount,
                 "different-field tap exits old editor and only focuses target");
    appHandleTouch(state, TouchAction::TradeReceiver, 360);
    ok &= expect(out, appInlineFieldEditing(state, InlineEditField::TradeReceiver),
                 "second tap on newly focused Receiver enters Editing");
    appHandleTouch(state, TouchAction::TradeConfirm, 370);
    ok &= expect(out, state.nav.current.focus == 2 &&
                      state.inlineEditField == InlineEditField::None &&
                      state.modal.kind == ModalKind::None && state.commandCount == 0,
                 "Submit tap while editing only exits editor and focuses Submit");
    appHandleTouch(state, TouchAction::TradeConfirm, 380);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate && state.commandCount == 0,
                 "separate Submit activation opens TradeCreate without queuing transport");

    appInit(state, 0);
    state.selectedPlayer = 3;
    state.nav.current = NavigationEntry{ScreenPage::PlayerDetail, 0, 0};
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 400);
    ok &= expect(out, state.nav.current.page == ScreenPage::Trade &&
                      state.tradeEntryMode == TradeEntryMode::PlayerLocked &&
                      appTradeReceiverLocked(state) && state.tradeReceiver == 3 &&
                      state.nav.current.focus == 0 &&
                      state.inlineEditField == InlineEditField::None &&
                      appPageContentCount(state) == 2,
                 "Player Detail Trade locks selected Receiver and starts at Amount");
    const uint8_t lockedReceiver = state.tradeReceiver;
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 410}, 410);
    ok &= expect(out, state.nav.current.focus == 1 && state.tradeReceiver == lockedReceiver &&
                      state.inlineEditField == InlineEditField::None,
                 "locked Trade rotary browses from Amount to Submit without Receiver mutation");
    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 420}, 420);
    shortPress(state, 430, 480);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 490}, 490);
    const int32_t lockedEditedAmount = state.tradeAmount;
    appHandleTouch(state, TouchAction::TradeReceiver, 500);
    ok &= expect(out, state.nav.current.focus == 0 && state.tradeReceiver == lockedReceiver &&
                      state.tradeAmount == lockedEditedAmount &&
                      appInlineFieldEditing(state, InlineEditField::TradeAmount),
                 "locked Receiver ignores touch while Amount editor remains active");
    shortPress(state, 510, 560);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.tradeReceiver == lockedReceiver,
                 "locked Amount commits without changing Receiver");

    appHandleTouch(state, TouchAction::TradeAmount, 570);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 580);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.nav.current.page == ScreenPage::PlayerDetail,
                 "page transition clears inline Editing");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::Trade, 1, 0};
    appHandleTouch(state, TouchAction::TradeAmount, 600);
    TransportEvent editingSnapshot{TransportEventKind::StateSnapshotApplied};
    editingSnapshot.stateVersion = 2;
    editingSnapshot.cash = state.money;
    editingSnapshot.playerPosition = state.position;
    appHandleTransportEvent(state, editingSnapshot, 610);
    ok &= expect(out, state.inlineEditField == InlineEditField::None,
                 "authoritative snapshot clears inline Editing");
    appHandleTouch(state, TouchAction::TradeAmount, 620);
    TransportEvent editingAuthorityEvent{TransportEventKind::RollResult};
    editingAuthorityEvent.stateVersion = 3;
    appHandleTransportEvent(state, editingAuthorityEvent, 630);
    ok &= expect(out, state.inlineEditField == InlineEditField::None,
                 "authoritative event clears inline Editing");

    appHandleTouch(state, TouchAction::TradeAmount, 640);
    TransportEvent forcedReplacement{TransportEventKind::DebtResolutionRequired};
    forcedReplacement.transactionId = 91;
    forcedReplacement.stateVersion = 4;
    forcedReplacement.amount = 500;
    forcedReplacement.cash = 240;
    forcedReplacement.assetMask = 0xFF;
    appHandleTransportEvent(state, forcedReplacement, 650);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.nav.current.page == ScreenPage::DebtAssets,
                 "forced-flow replacement clears inline Editing");

    const ScreenPage forcedPages[] = {
        ScreenPage::DiceStage,
        ScreenPage::MoveGuide,
        ScreenPage::DebtAssets,
        ScreenPage::Bankruptcy,
    };
    for (uint8_t i = 0; i < sizeof(forcedPages) / sizeof(forcedPages[0]); ++i) {
        appInit(state, 0);
        state.nav.current = NavigationEntry{forcedPages[i], 0, 0};
        appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 10}, 10);
        appTick(state, 810);
        appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 810}, 810);
        ok &= expect(out, !appCanNavigateBack(state) && state.nav.current.page == forcedPages[i],
                     "forced page ignores 800ms exit");

        appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1000}, 1000);
        appTick(state, 4000);
        appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 4000}, 4000);
        ok &= expect(out, !appCanNavigateBack(state) && state.nav.current.page == forcedPages[i],
                     "forced page ignores 3s exit");
    }

    appInit(state, 0);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 10);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 1 &&
                      state.nav.depth == 0,
                 "home touch selects a nonfocused action first");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 20);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players && state.nav.depth == 1,
                 "home touch activates the focused action on second tap");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    state.nav.current.focus = 4;
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 30);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      state.nav.current.focus == 1 && state.nav.depth == 0,
                 "my turn side tap centers Assets without activating it");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 1}, 40);
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets && state.nav.depth == 1,
                 "my turn centered Assets tap activates it");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 50);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::RollRequest,
                 "my turn centered Dice tap activates the roll action");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    state.nav.current.focus = 2;
    shortPress(state, 60, 140);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players,
                 "knob short press activates the centered my turn action");

    appInit(state, 0);
    state.nav.stack[0] = NavigationEntry{ScreenPage::Home, 0, 0};
    state.nav.stack[1] = NavigationEntry{ScreenPage::Assets, 1, 1};
    state.nav.stack[2] = NavigationEntry{ScreenPage::AssetDetail, 0, 0};
    state.nav.stack[3] = NavigationEntry{ScreenPage::Players, 2, 2};
    state.nav.depth = 4;
    state.nav.current = NavigationEntry{ScreenPage::PlayerDetail, 0, 0};
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 30);
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerDetail && state.nav.depth == 4 &&
                      state.nav.stack[0].page == ScreenPage::Home &&
                      state.nav.stack[1].page == ScreenPage::Assets && state.nav.stack[1].focus == 1 &&
                      state.nav.stack[1].listAnchor == 1 &&
                      state.nav.stack[2].page == ScreenPage::AssetDetail &&
                      state.nav.stack[2].focus == 0 && state.nav.stack[2].listAnchor == 0 &&
                      state.nav.stack[3].page == ScreenPage::Players && state.nav.stack[3].focus == 2 &&
                      state.nav.stack[3].listAnchor == 2,
                 "full navigation stack rejects an additional ordinary push");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 40);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players && state.nav.depth == 3,
                 "back after rejected push restores the immediate parent");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 50);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 60);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 70);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 80);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.depth == 0,
                 "navigation stack underflow leaves root unchanged");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    state.nav.current = NavigationEntry{ScreenPage::Home, 4, 0};
    appHandleTouch(state, TouchAction::DetailPrimary, 90);
    ok &= expect(out, state.position == 17 && state.nav.current.focus == 0 &&
                      appPollCommand(state, command) && command.kind == TransportCommandKind::RollRequest,
                 "home My Turn dice touch queues the roll action");

    appInit(state, 0);
    state.selectedAsset = 0;
    state.nav.current = NavigationEntry{ScreenPage::AssetDetail, 1, 0};
    appHandleTouch(state, TouchAction::DetailPrimary, 100);
    ok &= expect(out, state.modal.kind == ModalKind::VoluntaryMortgage,
                 "detail primary remains an immediate detail action");

    appInit(state, 0);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 110);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 100}, 1000);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1000}, 1000);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home,
                 "queued button events use physical timestamps for hold duration");

    const uint32_t deadlineWrapStartMs = 0xFFFFFF00u;
    appInit(state, deadlineWrapStartMs);
    ok &= expect(out, state.toastUntilMs == deadlineWrapStartMs + 1500u,
                 "first-use toast has an exact 1500ms deadline");
    appTick(state, deadlineWrapStartMs + 1499u);
    ok &= expect(out, state.toastUntilMs != 0,
                 "first-use toast remains visible through 1499ms across millis wrap");
    appTick(state, deadlineWrapStartMs + 1500u);
    ok &= expect(out, state.toastUntilMs == 0,
                 "first-use toast expires at exactly 1500ms across millis wrap");

    appInit(state, deadlineWrapStartMs);
    state.nav.current = NavigationEntry{ScreenPage::DemoLab, 4, 0};
    shortPress(state, deadlineWrapStartMs + 10u, deadlineWrapStartMs + 100u);
    appTick(state, deadlineWrapStartMs + 200u);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                      appModalRemainingMs(state, deadlineWrapStartMs + 200u) == 9900,
                 "modal deadline remains pending before millis wrap");
    appTick(state, deadlineWrapStartMs + 10100u);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment && state.modal.submitting &&
                      appPollCommand(state, command) && command.kind == TransportCommandKind::PayNow,
                 "modal deadline queues authority payment after millis wrap");

    openVoluntaryMortgageFixture(state, 1000);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 1110}, 1110);
    shortPress(state, 1120, 1200);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      state.nav.current.page == ScreenPage::AssetDetail,
                 "voluntary modal cancels to source");

    openTradeFixture(state, 2000);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 2200}, 2200);
    appTick(state, 2799);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2799}, 2799);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      appHoldProgressPermille(state, 2799) == 0,
                 "early modal release resets without closing");

    appInit(state, 0);
    const int32_t initialCash = state.money;
    TransportEvent payment{TransportEventKind::PaymentRequired};
    payment.transactionId = 90;
    payment.amount = 680;
    payment.cash = 240;
    payment.deadlineMs = 13000;
    appHandleTransportEvent(state, payment, 3000);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                      !state.modal.cancelAllowed && state.money == initialCash,
                 "forced payment is not cancelable");
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 3010}, 3010);
    shortPress(state, 3020, 3100);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment,
                 "forced payment cannot cancel through modal focus");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 3200}, 3200);
    appTick(state, 4400);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 4400}, 4400);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::PayNow && command.transactionId == 90,
                 "forced payment queues one PayNow command");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 4500}, 4500);
    appTick(state, 5700);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 5700}, 5700);
    ok &= expect(out, !appPollCommand(state, command),
                 "repeated PayNow activation is ignored while submitting");
    TransportEvent insufficient{TransportEventKind::CommandRejected};
    insufficient.requestId = command.requestId;
    insufficient.transactionId = 90;
    insufficient.error = TransportError::InsufficientCash;
    appHandleTransportEvent(state, insufficient, 5800);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment && state.modal.insufficient &&
                      state.modal.focus == ModalFocus::ResolveAssets && !state.modal.submitting,
                 "insufficient cash exposes resolve assets");
    appTick(state, 13000);
    ok &= expect(out, state.money == initialCash,
                 "payment countdown never mutates cash locally");
    TransportEvent completed{TransportEventKind::PaymentCompleted};
    completed.requestId = command.requestId;
    completed.transactionId = 90;
    completed.cash = 240;
    appHandleTransportEvent(state, completed, 13100);
    ok &= expect(out, state.money == initialCash &&
                      state.modal.kind == ModalKind::ForcedPayment &&
                      state.modal.insufficient,
                 "completion after rejected payment cannot bypass debt resolution");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    appHandleTransportEvent(state, TransportEvent{TransportEventKind::ConnectionLost}, 10);
    shortPress(state, 20, 100);
    ok &= expect(out, !state.authorityOnline && !appPollCommand(state, command),
                 "offline authority blocks dangerous commands");
    state.modal.kind = ModalKind::TradeCreate;
    state.modal.submitting = true;
    TransportEvent snapshot{TransportEventKind::StateSnapshotApplied};
    snapshot.stateVersion = 9;
    snapshot.cash = 1775;
    snapshot.playerPosition = 17;
    snapshot.resync = true;
    appHandleTransportEvent(state, snapshot, 110);
    shortPress(state, 120, 200);
    ok &= expect(out, state.authorityOnline && state.money == 1775 && state.position == 17 &&
                      state.stateVersion == 9 && !state.modal.submitting &&
                      appPollCommand(state, command) && command.kind == TransportCommandKind::RollRequest,
                 "snapshot restores authority fields before commands unlock");

    TransportEvent stale{TransportEventKind::PaymentCompleted};
    stale.stateVersion = 8;
    stale.cash = 1;
    appHandleTransportEvent(state, stale, 210);
    ok &= expect(out, state.money == 1775 && state.stateVersion == 9,
                 "lower state version is ignored");
    TransportEvent newer{TransportEventKind::PaymentCompleted};
    newer.stateVersion = 10;
    newer.cash = 1600;
    appHandleTransportEvent(state, newer, 220);
    appHandleTransportEvent(state, newer, 230);
    ok &= expect(out, state.money == 1775 && state.stateVersion == 9,
                 "uncorrelated payment completion cannot mutate authoritative cash");

    appInit(state, 0);
    openTradeFixture(state, 100);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 300}, 300);
    appTick(state, 1500);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1500}, 1500);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1600}, 1600);
    appTick(state, 2800);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2800}, 2800);
    ok &= expect(out, state.commandCount == 1 && appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::TradeCreate && !appPollCommand(state, command),
                 "command outbox is fixed and modal requests are idempotent");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::DemoLab, 3, 0};
    shortPress(state, 10, 100);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 200}, 200);
    appTick(state, 1400);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1400}, 1400);
    ok &= expect(out, appPollCommand(state, command) && command.kind == TransportCommandKind::ClaimRent,
                 "rent claim is transport-driven");
    const int32_t rentCash = state.money;
    appTick(state, 20100);
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent &&
                      state.modal.submitting && state.money == rentCash,
                 "submitted rent waits for authority beyond the offer deadline");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::DemoLab, 3, 0};
    shortPress(state, 10, 100);
    appTick(state, 20100);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == rentCash,
                 "unclaimed rent expires after twenty seconds without cash mutation");

    static DemoTransport transport;
    transport.begin(0);
    TransportCommand roll{TransportCommandKind::RollRequest, 7, 41, 0, 0};
    ok &= expect(out, transport.send(roll, 10), "demo accepts roll request");
    TransportEvent event{};
    transport.tick(129);
    ok &= expect(out, !transport.poll(event), "roll result is not emitted early");
    transport.tick(130);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult &&
                      event.requestId == 7 && event.dieA == 3 && event.dieB == 4,
                 "demo emits deterministic authoritative dice");
    transport.tick(160);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::MoveGuidanceStarted &&
                      event.targetPosition == 24,
                 "demo starts physical movement guidance");

    transport.begin(0);
    TransportCommand duplicateRoll{TransportCommandKind::RollRequest, 8, 41, 0, 0};
    ok &= expect(out, transport.send(duplicateRoll, 10), "demo accepts first duplicate-id roll");
    ok &= expect(out, transport.send(duplicateRoll, 11), "demo accepts duplicate-id roll");
    transport.tick(130);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult &&
                      event.requestId == 8 && event.dieA == 3 && event.dieB == 4,
                 "duplicate-id roll preserves deterministic dice");
    transport.tick(160);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::MoveGuidanceStarted &&
                      event.targetPosition == 24 && !transport.poll(event),
                 "duplicate-id roll creates no second transaction");

    ok &= expect(out, kAssets[6].mortgageValue == 120 &&
                      strcmp(kAssets[6].name, "\xE6\x97\xA7\xE5\x9F\x8E\xE8\x8A\xAF\xE5\xBB\x8A") == 0,
                 "asset seven is the exact mortgage fixture");

    appInit(state, 0);
    state.debt.amountDue = INT32_MAX;
    state.debt.cashBefore = INT32_MIN;
    ok &= expect(out, appDebtShortfall(state) == INT32_MAX,
                 "debt shortfall saturates at int32 maximum");
    state.debt.amountDue = INT32_MIN;
    state.debt.cashBefore = INT32_MAX;
    ok &= expect(out, appDebtShortfall(state) == 0,
                 "debt shortfall clamps negative extreme to zero");
    state.debt.cashBefore = INT32_MAX;
    state.debt.eligibleMask = 0x01;
    state.debt.selectedMask = 0x01;
    ok &= expect(out, appDebtSelectedProceeds(state) == 130 &&
                      appDebtPostMortgageBalance(state) == INT32_MAX,
                 "debt post-mortgage balance saturates at int32 maximum");
    state.debt.cashBefore = INT32_MIN;
    state.debt.selectedMask = 0;
    ok &= expect(out, appDebtPostMortgageBalance(state) == INT32_MIN,
                 "debt post-mortgage balance preserves int32 minimum");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    static AppState ordinaryDebt{};
    ordinaryDebt = state;
    TransportEvent unsolicitedBankruptcy{};
    unsolicitedBankruptcy.kind = TransportEventKind::BankruptcyResolved;
    unsolicitedBankruptcy.transactionId = 90;
    unsolicitedBankruptcy.stateVersion = 3;
    unsolicitedBankruptcy.cash = 0;
    appHandleTransportEvent(state, unsolicitedBankruptcy, 1);
    ok &= expect(out, debtAuthorityStateUnchanged(state, ordinaryDebt),
                 "matching bankruptcy resolution is inert outside pending bankruptcy");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    ok &= expect(out, appDebtShortfall(state) == 440,
                 "debt computes exact shortfall");
    state.debt.selectedMask = 0x55;
    state.nav.current.focus = 0;
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectFooter, 0}, 2);
    ok &= expect(out, appFocusIsFooter(state) && state.modal.kind == ModalKind::None,
                 "first forced Confirm footer touch only synchronizes focus");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectFooter, 0}, 3);
    ok &= expect(out, state.modal.kind == ModalKind::DebtMortgageConfirm,
                 "second forced Confirm footer touch activates focused confirmation");
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 4}, 4);
    shortPress(state, 5, 50);
    ok &= expect(out, state.modal.kind == ModalKind::DebtMortgageConfirm &&
                      !state.modal.cancelAllowed &&
                      state.nav.current.page == ScreenPage::DebtAssets &&
                      !appCanNavigateBack(state),
                 "debt confirmation ignores rotary cancellation");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    state.debt.selectedMask = 0;
    state.nav.current.focus = 0;
    focusDebtAsset(state, 0);
    shortPress(state, 10, 100);
    focusDebtAsset(state, 2);
    shortPress(state, 110, 200);
    ok &= expect(out, appDebtSelectedProceeds(state) == 300 &&
                      !appDebtCanConfirm(state),
                 "debt sums multiple mortgages below threshold");
    focusDebtAsset(state, 4);
    shortPress(state, 210, 300);
    ok &= expect(out, appDebtSelectedProceeds(state) == 375 &&
                      appDebtPostMortgageBalance(state) == 615,
                 "debt preview updates after every toggle");

    focusDebtAsset(state, 3);
    shortPress(state, 310, 400);
    focusDebtAsset(state, 5);
    shortPress(state, 410, 500);
    focusDebtAsset(state, 7);
    shortPress(state, 510, 600);
    focusDebtAsset(state, 1);
    shortPress(state, 610, 700);
    ok &= expect(out, !appDebtAssetEligible(state, 3) && !appDebtAssetSelected(state, 3) &&
                      !appDebtAssetEligible(state, 5) && !appDebtAssetSelected(state, 5) &&
                      !appDebtAssetEligible(state, 7) && !appDebtAssetSelected(state, 7) &&
                      !appDebtAssetEligible(state, 1) && !appDebtAssetSelected(state, 1) &&
                      appDebtSelectedProceeds(state) == 375,
                 "debt rejects mortgaged building locked and absent assets");

    focusDebtAsset(state, kAssetCount);
    shortPress(state, 710, 800);
    ok &= expect(out, state.modal.kind == ModalKind::None,
                 "debt footer does nothing below shortfall");
    focusDebtAsset(state, 6);
    shortPress(state, 810, 900);
    ok &= expect(out, appDebtSelectedProceeds(state) == 495 && appDebtCanConfirm(state),
                 "debt reaches confirmation threshold with exact proceeds");
    focusDebtAsset(state, kAssetCount);
    shortPress(state, 910, 1000);
    ok &= expect(out, state.modal.kind == ModalKind::DebtMortgageConfirm,
                 "debt footer opens mortgage confirmation at threshold");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1210}, 1210);
    appTick(state, 2410);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2410}, 2410);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::MortgageBatchRequest &&
                      command.transactionId == 90 && command.assetMask == 0x55 && !appPollCommand(state, command),
                 "debt confirmation hold queues exactly one selected mortgage batch");
    TransportEvent mortgageCompleted{};
    mortgageCompleted.kind = TransportEventKind::MortgageBatchCompleted;
    mortgageCompleted.requestId = command.requestId;
    mortgageCompleted.transactionId = 90;
    mortgageCompleted.stateVersion = 3;
    mortgageCompleted.cash = 735;
    mortgageCompleted.assetMask = 0x55;
    static AppState submittedDebt{};
    submittedDebt = state;
    TransportEvent wrongMortgageRequest = mortgageCompleted;
    wrongMortgageRequest.requestId += 1;
    wrongMortgageRequest.stateVersion = 99;
    appHandleTransportEvent(state, wrongMortgageRequest, 2450);
    ok &= expect(out, debtAuthorityStateUnchanged(state, submittedDebt),
                 "wrong mortgage completion request leaves debt submission unchanged");
    TransportEvent wrongMortgageTransaction = mortgageCompleted;
    wrongMortgageTransaction.transactionId += 1;
    appHandleTransportEvent(state, wrongMortgageTransaction, 2460);
    ok &= expect(out, debtAuthorityStateUnchanged(state, submittedDebt),
                 "wrong mortgage completion transaction leaves debt submission unchanged");
    TransportEvent wrongMortgageMask = mortgageCompleted;
    wrongMortgageMask.assetMask = 0x15;
    appHandleTransportEvent(state, wrongMortgageMask, 2470);
    ok &= expect(out, debtAuthorityStateUnchanged(state, submittedDebt),
                 "wrong mortgage completion mask leaves debt submission unchanged");
    appHandleTransportEvent(state, mortgageCompleted, 2500);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment && state.modal.transactionId == 90 &&
                      appModalRemainingMs(state, 2500) == 10000 && state.money == 735,
                 "mortgage completion restores forced payment with a fresh deadline");
    static AppState acceptedMortgage{};
    acceptedMortgage = state;
    appHandleTransportEvent(state, mortgageCompleted, 2600);
    ok &= expect(out, debtAuthorityStateUnchanged(state, acceptedMortgage),
                 "duplicate mortgage completion leaves accepted debt unchanged");

    openDebtFixture(state, /*amountDue=*/500, /*cash=*/240, /*eligibleMask=*/0x01);
    ok &= expect(out, state.nav.current.page == ScreenPage::Bankruptcy && !appCanNavigateBack(state) &&
                      appFocusCount(state) == 1,
                 "unresolvable debt enters terminal bankruptcy without back");
    static AppState pendingBankruptcy{};
    pendingBankruptcy = state;
    TransportEvent wrongBankruptcy{};
    wrongBankruptcy.kind = TransportEventKind::BankruptcyResolved;
    wrongBankruptcy.transactionId = 91;
    wrongBankruptcy.stateVersion = 3;
    wrongBankruptcy.cash = 1;
    appHandleTransportEvent(state, wrongBankruptcy, 2700);
    ok &= expect(out, debtAuthorityStateUnchanged(state, pendingBankruptcy),
                 "wrong bankruptcy resolution leaves forced bankruptcy unchanged");
    TransportEvent staleBankruptcy = wrongBankruptcy;
    staleBankruptcy.transactionId = 90;
    staleBankruptcy.stateVersion = 1;
    appHandleTransportEvent(state, staleBankruptcy, 2710);
    ok &= expect(out, debtAuthorityStateUnchanged(state, pendingBankruptcy),
                 "stale bankruptcy resolution leaves forced bankruptcy unchanged");
    TransportEvent unrelatedBankruptcyEvent{};
    unrelatedBankruptcyEvent.kind = TransportEventKind::StateSnapshotApplied;
    unrelatedBankruptcyEvent.transactionId = 90;
    unrelatedBankruptcyEvent.stateVersion = 3;
    unrelatedBankruptcyEvent.cash = 1;
    unrelatedBankruptcyEvent.playerPosition = 1;
    appHandleTransportEvent(state, unrelatedBankruptcyEvent, 2720);
    ok &= expect(out, debtAuthorityStateUnchanged(state, pendingBankruptcy),
                 "unrelated event cannot alter forced bankruptcy while awaiting resolution");
    TransportEvent acceptedBankruptcy = wrongBankruptcy;
    acceptedBankruptcy.transactionId = 90;
    acceptedBankruptcy.cash = 0;
    appHandleTransportEvent(state, acceptedBankruptcy, 2730);
    ok &= expect(out, state.nav.current.page == ScreenPage::Bankruptcy && state.money == 0,
                 "matching bankruptcy resolution is accepted once");
    static AppState terminalBankruptcy{};
    terminalBankruptcy = state;
    const TransportEventKind terminalEventKinds[] = {
        TransportEventKind::None,
        TransportEventKind::ConnectionLost,
        TransportEventKind::StateSnapshotApplied,
        TransportEventKind::RollResult,
        TransportEventKind::MoveGuidanceStarted,
        TransportEventKind::RfidPositionConfirmed,
        TransportEventKind::RfidPositionRejected,
        TransportEventKind::PaymentRequired,
        TransportEventKind::PaymentCompleted,
        TransportEventKind::DebtResolutionRequired,
        TransportEventKind::MortgageBatchCompleted,
        TransportEventKind::BankruptcyResolved,
        TransportEventKind::CommandCompleted,
        TransportEventKind::CommandRejected,
    };
    bool terminalEventsIgnored = true;
    for (TransportEventKind kind : terminalEventKinds) {
        TransportEvent eventAfterBankruptcy{};
        eventAfterBankruptcy.kind = kind;
        eventAfterBankruptcy.requestId = command.requestId;
        eventAfterBankruptcy.transactionId = 90;
        eventAfterBankruptcy.stateVersion = 4;
        eventAfterBankruptcy.cash = 999;
        eventAfterBankruptcy.amount = 999;
        eventAfterBankruptcy.assetMask = 0x55;
        eventAfterBankruptcy.playerPosition = 1;
        appHandleTransportEvent(state, eventAfterBankruptcy, 2740);
        terminalEventsIgnored &= debtAuthorityStateUnchanged(state, terminalBankruptcy);
    }
    ok &= expect(out, terminalEventsIgnored,
                 "accepted bankruptcy ignores every later transport event kind");

    openVoluntaryMortgageFixture(state, 2800);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 2810}, 2810);
    appTick(state, 4010);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 4010}, 4010);
    TransportCommand voluntaryMortgage{};
    ok &= expect(out, appPollCommand(state, voluntaryMortgage) &&
                      voluntaryMortgage.kind == TransportCommandKind::MortgageBatchRequest &&
                      voluntaryMortgage.transactionId == 0 && voluntaryMortgage.assetMask == 0x01,
                 "voluntary mortgage submits zero transaction with selected mask");
    static DemoTransport voluntaryTransport;
    voluntaryTransport.setScenario(DemoScenario::DebtMortgage);
    voluntaryTransport.begin(0);
    ok &= expect(out, voluntaryTransport.send(voluntaryMortgage, 0),
                 "demo accepts voluntary mortgage command");
    voluntaryTransport.tick(150);
    TransportEvent voluntaryCompleted{};
    ok &= expect(out, voluntaryTransport.poll(voluntaryCompleted) &&
                      voluntaryCompleted.kind == TransportEventKind::MortgageBatchCompleted &&
                      voluntaryCompleted.requestId == voluntaryMortgage.requestId &&
                      voluntaryCompleted.transactionId != 0 && voluntaryCompleted.assetMask == 0x01,
                 "demo assigns transaction to voluntary mortgage completion");
    static AppState submittedVoluntaryMortgage{};
    submittedVoluntaryMortgage = state;
    TransportEvent wrongVoluntaryMask = voluntaryCompleted;
    wrongVoluntaryMask.assetMask = 0x02;
    appHandleTransportEvent(state, wrongVoluntaryMask, 4020);
    ok &= expect(out, debtAuthorityStateUnchanged(state, submittedVoluntaryMortgage),
                 "wrong voluntary mortgage mask leaves submission unchanged");
    appHandleTransportEvent(state, voluntaryCompleted, 4030);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == voluntaryCompleted.cash,
                 "assigned voluntary mortgage completion applies cash and dismisses once");
    static AppState acceptedVoluntaryMortgage{};
    acceptedVoluntaryMortgage = state;
    appHandleTransportEvent(state, voluntaryCompleted, 4040);
    ok &= expect(out, debtAuthorityStateUnchanged(state, acceptedVoluntaryMortgage),
                 "duplicate voluntary mortgage completion is inert");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    focusDebtAsset(state, 0);
    shortPress(state, 2600, 2700);
    TransportEvent refreshedDebt{};
    refreshedDebt.kind = TransportEventKind::DebtResolutionRequired;
    refreshedDebt.transactionId = 90;
    refreshedDebt.stateVersion = 3;
    refreshedDebt.amount = 680;
    refreshedDebt.cash = 240;
    refreshedDebt.assetMask = 0x54;
    appHandleTransportEvent(state, refreshedDebt, 2800);
    ok &= expect(out, !appDebtAssetSelected(state, 0) &&
                      strcmp(state.toast, "\xE8\xB5\x84\xE4\xBA\xA7\xE7\x8A\xB6\xE6\x80\x81\xE5\xB7\xB2\xE5\x8F\x98\xE5\x8C\x96\xEF\xBC\x8C\xE8\xAF\xB7\xE9\x87\x8D\xE6\x96\xB0\xE9\x80\x89\xE6\x8B\xA9") == 0,
                 "refreshed debt eligibility clears invalid selection with toast");

    transport.setScenario(DemoScenario::DebtMortgage);
    transport.begin(0);
    TransportCommand debtBatch{TransportCommandKind::MortgageBatchRequest, 700, 42, 90, 0, 0x55};
    ok &= expect(out, transport.send(debtBatch, 0), "demo accepts selected debt mortgage batch");
    transport.tick(149);
    ok &= expect(out, !transport.poll(event), "demo debt batch waits 150ms");
    transport.tick(150);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::MortgageBatchCompleted &&
                      event.transactionId == 90 && event.assetMask == 0x55,
                 "demo debt batch completes with selected mask and transaction");

    transport.setScenario(DemoScenario::DebtBankruptcy);
    transport.begin(0);
    ok &= expect(out, transport.send(debtBatch, 0), "demo accepts bankruptcy debt batch");
    transport.tick(150);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::BankruptcyResolved &&
                      event.transactionId == 90,
                 "demo bankruptcy branch resolves terminally");

    transport.setScenario(DemoScenario::Waiting);
    transport.begin(0);
    TransportCommand zeroRequestRoll{TransportCommandKind::RollRequest, 0, 41, 0, 0};
    ok &= expect(out, transport.send(zeroRequestRoll, 10), "zero request-id roll is accepted");
    transport.tick(130);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult && event.requestId == 0 &&
                      event.dieA == 3 && event.dieB == 4,
                 "zero request-id roll gets authoritative dice");

    transport.begin(0);
    TransportCommand replayRoll{TransportCommandKind::RollRequest, 12, 41, 0, 0};
    ok &= expect(out, transport.send(replayRoll, 10), "post-result retry test accepts initial roll");
    transport.tick(130);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::RollResult,
                 "post-result retry test receives initial dice");
    const uint32_t replayTransactionId = event.transactionId;
    ok &= expect(out, transport.send(replayRoll, 131), "post-result retry is accepted");
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult &&
                      event.transactionId == replayTransactionId && event.dieA == 3 && event.dieB == 4,
                 "post-result retry replays the cached authoritative dice");

    transport.setScenario(DemoScenario::PaymentCash);
    const uint32_t paymentStartMs = 1000;
    transport.begin(paymentStartMs);
    transport.tick(paymentStartMs);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentRequired &&
                      event.deadlineMs == paymentStartMs + 10000 && event.transactionId != 0,
                 "payment cash exposes an absolute ten second deadline");
    const uint32_t paymentTransactionId = event.transactionId;
    transport.tick(paymentStartMs + 9999);
    ok &= expect(out, !transport.poll(event), "payment cash does not complete before its deadline");
    transport.tick(paymentStartMs + 10000);
    ok &= expect(out, !transport.poll(event),
                 "payment transport waits for the app's deadline PayNow request");
    TransportCommand deadlinePayNow{TransportCommandKind::PayNow, 804, 41,
                                    paymentTransactionId, 0};
    ok &= expect(out, transport.send(deadlinePayNow, paymentStartMs + 10000),
                 "deadline PayNow is accepted with a nonzero request id");
    transport.tick(paymentStartMs + 10119);
    ok &= expect(out, !transport.poll(event), "deadline PayNow completion waits 120ms");
    transport.tick(paymentStartMs + 10120);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.requestId == 804 && event.transactionId == paymentTransactionId &&
                      !transport.poll(event),
                 "deadline PayNow completes once with its request id");

    transport.begin(paymentStartMs);
    transport.tick(paymentStartMs);
    transport.poll(event);
    const uint32_t correlatedPaymentTransactionId = event.transactionId;
    TransportCommand wrongPayNow{TransportCommandKind::PayNow, 800, 41,
                                  correlatedPaymentTransactionId + 1, 0};
    ok &= expect(out, !transport.send(wrongPayNow, paymentStartMs + 100),
                 "wrong payment transaction is rejected");
    transport.tick(paymentStartMs + 220);
    ok &= expect(out, !transport.poll(event),
                 "wrong payment transaction cannot settle the active payment early");
    TransportCommand correlatedPayNow{TransportCommandKind::PayNow, 803, 41,
                                       correlatedPaymentTransactionId, 0};
    ok &= expect(out, transport.send(correlatedPayNow, paymentStartMs + 200),
                 "matching payment transaction is accepted");
    transport.tick(paymentStartMs + 319);
    ok &= expect(out, !transport.poll(event), "matching PayNow completion waits 120ms");
    transport.tick(paymentStartMs + 320);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.requestId == 803 && event.transactionId == correlatedPaymentTransactionId &&
                      !transport.poll(event),
                 "matching payment transaction settles exactly once");

    transport.begin(paymentStartMs);
    transport.tick(paymentStartMs);
    transport.poll(event);
    const uint32_t earlyPaymentTransactionId = event.transactionId;
    TransportCommand earlyPayNow{TransportCommandKind::PayNow, 801, 41, earlyPaymentTransactionId, 0};
    ok &= expect(out, transport.send(earlyPayNow, paymentStartMs + 100), "early PayNow is accepted");
    transport.tick(paymentStartMs + 219);
    ok &= expect(out, !transport.poll(event), "early PayNow completion waits 120ms");
    transport.tick(paymentStartMs + 220);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.requestId == 801 && event.transactionId == earlyPaymentTransactionId,
                 "early PayNow response wins before payment deadline");
    transport.tick(paymentStartMs + 10000);
    ok &= expect(out, !transport.poll(event), "early PayNow cancels deadline completion duplicate");

    transport.begin(paymentStartMs);
    transport.tick(paymentStartMs);
    transport.poll(event);
    const uint32_t latePaymentTransactionId = event.transactionId;
    TransportCommand latePayNow{TransportCommandKind::PayNow, 802, 41, latePaymentTransactionId, 0};
    ok &= expect(out, transport.send(latePayNow, paymentStartMs + 9881), "late PayNow is accepted without duplication");
    transport.tick(paymentStartMs + 9999);
    ok &= expect(out, !transport.poll(event), "late PayNow cannot complete before deadline");
    transport.tick(paymentStartMs + 10000);
    ok &= expect(out, !transport.poll(event),
                 "late PayNow still waits for its correlated response after deadline");
    transport.tick(paymentStartMs + 10001);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.requestId == 802 &&
                      event.transactionId == latePaymentTransactionId && !transport.poll(event),
                 "late PayNow completes with its own request id");

    const uint32_t paymentWrapStartMs = 0xFFFFFF00u;
    transport.begin(paymentWrapStartMs);
    transport.tick(paymentWrapStartMs);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentRequired &&
                      event.deadlineMs == paymentWrapStartMs + 10000u,
                 "payment cash deadline wraps from scenario start");
    const uint32_t wrappedPaymentTransactionId = event.transactionId;
    transport.tick(paymentWrapStartMs + 9999u);
    ok &= expect(out, !transport.poll(event), "wrapped payment deadline is not early");
    transport.tick(paymentWrapStartMs + 10000u);
    ok &= expect(out, !transport.poll(event),
                 "wrapped payment deadline still requires PayNow authority request");
    TransportCommand wrappedPayNow{TransportCommandKind::PayNow, 805, 41,
                                   wrappedPaymentTransactionId, 0};
    ok &= expect(out, transport.send(wrappedPayNow, paymentWrapStartMs + 10000u),
                 "wrapped deadline PayNow is accepted");
    transport.tick(paymentWrapStartMs + 10120u);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.requestId == 805 && !transport.poll(event),
                 "wrapped payment request completes exactly once");

    transport.setScenario(DemoScenario::Waiting);
    transport.begin(0);
    bool paymentQueueFilled = true;
    for (uint32_t requestId = 100; requestId < 108; ++requestId) {
        paymentQueueFilled &= transport.send(
            TransportCommand{TransportCommandKind::MoveManualConfirmRequest, requestId, 41, 0, 0, 0, 17}, 0);
    }
    ok &= expect(out, paymentQueueFilled, "commands fill the scheduled queue");
    ok &= expect(out, !transport.send(
                      TransportCommand{TransportCommandKind::MoveManualConfirmRequest, 108, 41, 0, 0, 0, 17}, 0),
                 "full scheduled queue rejects command atomically");
    TransportCommand blockedRoll{TransportCommandKind::RollRequest, 109, 41, 0, 0};
    ok &= expect(out, !transport.send(blockedRoll, 0),
                 "full scheduled queue atomically rejects roll");
    transport.tick(120);
    uint8_t paymentEventCount = 0;
    while (transport.poll(event)) {
        paymentEventCount += event.kind == TransportEventKind::RfidPositionConfirmed ? 1 : 0;
    }
    ok &= expect(out, paymentEventCount == 8, "full queue retains all scheduled commands at 120ms");
    TransportCommand recoveredRoll{TransportCommandKind::RollRequest, 110, 41, 0, 0};
    ok &= expect(out, transport.send(recoveredRoll, 120),
                 "atomic roll rejection leaves later roll available");
    transport.tick(240);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult && event.requestId == 110,
                 "later roll is not stranded after capacity rejection");

    transport.begin(0);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 201, 41, 0, 0}, 0);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 202, 41, 0, 0}, 200);
    transport.tick(120);
    transport.poll(event);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 203, 41, 0, 0}, 400);
    transport.tick(520);
    ok &= expect(out, transport.poll(event) && event.requestId == 202,
                 "scheduled events use chronological due order after slot reuse");
    ok &= expect(out, transport.poll(event) && event.requestId == 203,
                 "scheduled events keep later due event after older due event");

    transport.setScenario(DemoScenario::Waiting);
    transport.begin(0);
    ok &= expect(out, transport.send(
                      TransportCommand{TransportCommandKind::RollRequest, 301, 41, 0, 0}, 0),
                 "first unresolved roll is accepted");
    ok &= expect(out, transport.send(
                      TransportCommand{TransportCommandKind::RollRequest, 302, 41, 0, 0}, 1),
                 "different unresolved roll gets a rejection event");
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::CommandRejected &&
                      event.error == TransportError::ActionNotAllowed && event.requestId == 302,
                 "different unresolved roll is rejected deterministically");

    transport.begin(0);
    const uint32_t wrapStartMs = 0xFFFFFFF0u;
    ok &= expect(out, transport.send(
                      TransportCommand{TransportCommandKind::RollRequest, 401, 41, 0, 0}, wrapStartMs),
                 "wraparound roll is accepted");
    transport.tick(0x67u);
    ok &= expect(out, !transport.poll(event), "wraparound roll is not emitted early");
    transport.tick(0x68u);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult && event.requestId == 401,
                 "wraparound roll emits at the wrapped deadline");

    transport.setScenario(DemoScenario::ConnectionDropAndRecover);
    transport.begin(1000);
    transport.tick(1000);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::ConnectionLost,
                 "connection scenario emits loss");
    ok &= expect(out, !transport.send(
                      TransportCommand{TransportCommandKind::RollRequest, 501, 41, 0, 0}, 1001),
                 "connection scenario does not queue offline roll");
    transport.tick(3999);
    ok &= expect(out, !transport.poll(event), "connection snapshot waits three seconds");
    transport.tick(4000);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::StateSnapshotApplied && event.cash == 1860 &&
                      event.playerPosition == 17 && event.stateVersion == 42,
                 "connection scenario applies authoritative recovery snapshot");
    ok &= expect(out, transport.send(
                      TransportCommand{TransportCommandKind::RollRequest, 502, 41, 0, 0}, 4001),
                 "connection recovery accepts a new roll");

    ok &= expect(out, kMechanicalCorrectionDeg == 60 && kFirmwareRotationDeg == 0,
                 "M4 pin 3 is mechanically aligned at six o'clock");
    return ok;
}

bool runPureLogicTests(Stream &out)
{
    bool ok = runStateLogicTests(out);
    ok &= runModalAuthorityTests(out);
    gridopoly::protocol::StateSnapshot snapshot{};
    snapshot.seatId = 1;
    snapshot.phase = 1;
    snapshot.activePlayerId = 3;
    snapshot.round = 4;
    snapshot.boardSize = 28;
    snapshot.selfPosition = 17;
    snapshot.selfCash = 1860;
    snapshot.availableActions = 0;
    snapshot.playerCount = 5;
    snapshot.pendingTarget = 0xFF;
    snapshot.stateVersion = 77;
    snapshot.decisionPlayerId = 3;
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        snapshot.players[index].playerId = static_cast<uint8_t>(index + 1);
        snapshot.players[index].position = static_cast<uint8_t>(index * 3 + 2);
        snapshot.players[index].cash = 1200 + index * 100;
        snapshot.players[index].flags = 0x04;
    }
    snapshot.players[0].position = snapshot.selfPosition;
    snapshot.players[0].cash = snapshot.selfCash;
    TransportEvent authorityEvent{};
    ok &= expect(out, authoritySnapshotToEvent(snapshot, true, authorityEvent) &&
                      authorityEvent.kind == TransportEventKind::StateSnapshotApplied &&
                      authorityEvent.playerCount == 5 && authorityEvent.resync,
                 "server snapshot maps to one bounded console event");
    AppState connected{};
    appInit(connected, 0);
    appHandleTransportEvent(connected, authorityEvent, 10);
    ok &= expect(out, connected.authorityOnline && connected.authoritySnapshotValid &&
                      connected.selfSeatId == 1 && connected.money == 1860 &&
                      connected.position == 17 && connected.turnsUntilYou == 3 &&
                      connected.homePhase == HomePhase::Waiting &&
                      connected.authorityPlayers[4].cash == 1600,
                 "five-player authority snapshot drives global console state");

    snapshot.activePlayerId = 1;
    snapshot.decisionPlayerId = 1;
    snapshot.availableActions = 1;
    snapshot.stateVersion = 78;
    ok &= expect(out, authoritySnapshotToEvent(snapshot, false, authorityEvent),
                 "incremental authority snapshot remains valid");
    appHandleTransportEvent(connected, authorityEvent, 20);
    ok &= expect(out, connected.homePhase == HomePhase::MyTurn &&
                      connected.turnsUntilYou == 0 && appPageContentCount(connected) == 5,
                 "roll action changes Home to the green five-action turn state");

    gridopoly::protocol::StateSnapshot duplicate = snapshot;
    ok &= expect(out, authoritySnapshotsEqual(snapshot, duplicate),
                 "identical heartbeat snapshot is suppressible");
    duplicate.players[2].cash -= 1;
    ok &= expect(out, !authoritySnapshotsEqual(snapshot, duplicate),
                 "global player cash change invalidates snapshot equality");
    return ok;
}

bool runLvglComponentTests(Stream &out)
{
    bool ok = true;
    ok &= runModalComponentTests(out);
    ok &= runCenterListComponentTests(out);
    ok &= runCarouselComponentTests(out);
    ok &= runHomeRendererTests(out);
    return ok;
}

bool runLogicTests(Stream &out)
{
    bool ok = runLvglComponentTests(out);
    ok &= runPureLogicTests(out);
    return ok;
}
