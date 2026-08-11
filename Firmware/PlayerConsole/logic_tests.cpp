#include "logic_tests.h"

#include "avatar_component_math.h"

#include "app_config.h"
#include "app_state.h"
#include "authority_snapshot_reducer.h"
#include "demo_data.h"
#include "demo_transport.h"
#include "ui_layout.h"
#include "ui_carousel.h"
#include "ui_center_list.h"
#include "ui_handwriting.h"
#include "ui_modal.h"
#include "ui_motion.h"
#include "ui_primitives.h"
#include "ui_renderer.h"
#include "transport_event_cursor.h"
#include "grid_city_visual_catalog.h"
#include "remote_tile_cache_policy.h"
#include "src/assets/grid_city_tile_images.h"

#include <GridopolyCore.h>
#include <gridopoly/protocol/UdpEnvelope.h>
#include <limits.h>
#include <string.h>

namespace {

const char *firstFailure = nullptr;

bool expect(Stream &out, bool condition, const char *name)
{
    if (!condition && firstFailure == nullptr) firstFailure = name;
    out.printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

bool runTransportEventCursorTests(Stream &out)
{
    bool ok = true;
    TransportEventCursor cursor{};
    cursor.applied = 40;
    cursor.queued = 43;
    cursor.beginResync(90);
    ok &= expect(out, cursor.applied == 40 && cursor.queued == 43 &&
                      cursor.resyncBaselinePending,
                 "authority resync does not acknowledge unconsumed activity");

    ok &= expect(out,
                 cursor.prepare(60, true) == TransportSequenceDisposition::Accept &&
                     cursor.queued == 59,
                 "first retained resync event establishes a bounded history baseline");
    cursor.markQueued(60);
    ok &= expect(out, cursor.applied == 40 && cursor.queued == 60,
                 "queueing replay history does not advance the heartbeat acknowledgement");
    ok &= expect(out, cursor.markApplied(60, true) && cursor.applied == 60,
                 "application consumption advances across an unavailable history prefix");
    ok &= expect(out,
                 cursor.prepare(61, true) == TransportSequenceDisposition::Accept,
                 "records remain contiguous after the one-time resync baseline");
    cursor.markQueued(61);
    ok &= expect(out,
                 cursor.prepare(63, true) == TransportSequenceDisposition::Gap,
                 "a later missing resync datagram is not hidden as history truncation");
    ok &= expect(out,
                 cursor.prepare(61, true) == TransportSequenceDisposition::Duplicate,
                 "replayed event sequences remain idempotent");
    return ok;
}

bool runRemoteArtworkCatalogTests(Stream &out)
{
    struct ExpectedArtwork {
        const char *visualId;
        const char *assetKey;
    };
    static constexpr ExpectedArtwork expected[] = {
        {"A1", "a1-rivet-row"},
        {"A2", "a2-copper-lane"},
        {"B1", "b1-lantern-avenue"},
        {"B2", "b2-tideway-drive"},
        {"B3", "b3-beacon-boulevard"},
        {"C1", "c1-canvas-street"},
        {"C2", "c2-bloom-terrace"},
        {"C3", "c3-aurora-avenue"},
        {"D1", "d1-archive-way"},
        {"D2", "d2-forum-drive"},
        {"D3", "d3-meridian-avenue"},
        {"E1", "e1-pulse-street"},
        {"E2", "e2-prism-boulevard"},
        {"E3", "e3-nova-avenue"},
        {"F1", "f1-sunstep-terrace"},
        {"F2", "f2-helix-way"},
        {"F3", "f3-horizon-drive"},
        {"G1", "g1-canopy-lane"},
        {"G2", "g2-verdant-avenue"},
        {"G3", "g3-summit-boulevard"},
        {"H1", "h1-crown-promenade"},
        {"H2", "h2-grand-meridian"},
        {"T-WEST", "transit-westline-terminal"},
        {"T-NORTH", "transit-northloop-station"},
        {"T-EAST", "transit-eastgate-terminal"},
        {"T-SOUTH", "transit-southline-depot"},
        {"U-ENERGY", "utility-metro-grid"},
        {"U-WATER", "utility-bluewater-works"},
        {"CARD-CE-1", "cover-chance"},
        {"CARD-CF-1", "cover-community-fund"},
        {"FEE-CITY", "cover-income-tax"},
        {"FEE-DENSITY", "cover-luxury-tax"},
        {"CORNER-START", "corner-central-launch"},
        {"CORNER-HOLD", "corner-civic-hold"},
        {"CORNER-REST", "corner-free-plaza"},
        {"CORNER-GOTO", "corner-hold-order"},
    };

    bool allMapped = sizeof(expected) / sizeof(expected[0]) + 1u ==
                     static_cast<size_t>(GridCityArtwork::Count);
    for (const ExpectedArtwork &item : expected) {
        const GridCityVisualDefinition *const visual = gridCityVisualById(item.visualId);
        const char *const actual = visual == nullptr
            ? nullptr : gridCityArtworkAssetKey(visual->artwork);
        allMapped = allMapped && actual != nullptr && strcmp(actual, item.assetKey) == 0;
    }
    bool ok = expect(out, allMapped,
                     "all 36 board visuals map one-to-one to remote RGB565 asset keys");
    ok &= expect(out,
                 gridCityArtworkAssetKey(GridCityArtwork::Fallback) == nullptr,
                 "the built-in fallback is never requested from the server");
    return ok;
}

bool runRemoteTileCachePolicyTests(Stream &out)
{
    bool ok = expect(out,
                     kRemoteImageCacheBudgetBytes == 1024u * 1024u &&
                         kRemoteTileCacheCapacity == 20 &&
                         kRemoteAvatarSetupCacheBudgetBytes == 2u * 1024u * 1024u &&
                         kRemoteAvatarDownloadWorkerCount == 4 &&
                         kRemoteAvatarPreviewBytes + 6u * kRemoteAvatarFinalBytes <=
                              kRemoteAvatarCacheBudgetBytes,
                     "gameplay artwork stays within 1 MiB and setup gets a bounded 2 MiB pool");
    uint64_t lastUsed[37]{};
    uint64_t readyMask = 0;
    for (uint8_t index = 1; index <= 4; ++index) {
        readyMask |= uint64_t{1} << index;
        lastUsed[index] = static_cast<uint64_t>(index) * 10u;
    }
    ok &= expect(out,
                 remoteTileCacheSelectLru(lastUsed, readyMask, 37, -1) == 1,
                 "LRU eviction selects the least recently used ready tile");
    ok &= expect(out,
                 remoteTileCacheSelectLru(lastUsed, readyMask, 37, 1) == 2,
                 "LRU eviction never releases the currently displayed tile");
    lastUsed[2] = lastUsed[3] = 20;
    ok &= expect(out,
                 remoteTileCacheSelectLru(lastUsed, readyMask, 37, 1) == 2,
                 "LRU ties use a stable lowest-slot order");
    ok &= expect(out,
                 remoteTileCacheSelectLru(lastUsed, 0, 37, -1) == -1,
                 "LRU eviction reports no candidate when the cache is empty");
    return ok;
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
    lv_obj_update_layout(carousel.container);
    ok &= expect(out, carousel.currentOpacity[2] > 0 &&
                      lv_obj_get_x(carousel.items[2]) < lv_obj_get_x(carousel.items[0]) &&
                      lv_obj_get_x(carousel.items[1]) > lv_obj_get_x(carousel.items[0]),
                 "three-action Home wraps Trade left of focused Assets and Players right");

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
    const uint8_t endTurnCount = uiCarouselActions(HomePhase::MyTurnEnd, actions);
    uiCarouselCreate(carousel, parent, actions, endTurnCount, 0);
    uiCarouselSetEndTurnExitProgress(carousel, 500);
    const uint8_t endTurnHalfOpacity = carousel.currentOpacity[0];
    uiCarouselSetEndTurnExitProgress(carousel, 1000);
    lv_obj_update_layout(carousel.container);
    ok &= expect(out, endTurnHalfOpacity > 0 && endTurnHalfOpacity < LV_OPA_COVER &&
                      carousel.currentOpacity[0] == 0 &&
                      carousel.currentOpacity[1] == LV_OPA_COVER &&
                      lv_obj_get_x(carousel.items[3]) < lv_obj_get_x(carousel.items[1]) &&
                      lv_obj_get_x(carousel.items[2]) > lv_obj_get_x(carousel.items[1]),
                 "END TURN fades out while Assets Players Trade settle into three-item slots");

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
                      strcmp(lv_label_get_text(list.countLabel), "2 / 8") == 0 &&
                      lv_label_get_long_mode(
                          lv_obj_get_child(lv_obj_get_child(list.track, 0), 0)
                      ) == LV_LABEL_LONG_DOT &&
                      lv_label_get_long_mode(
                          lv_obj_get_child(lv_obj_get_child(list.track, 1), 0)
                      ) == LV_LABEL_LONG_SCROLL_CIRCULAR,
                 "center-list focus updates only previous/new rows without duplicate content writes");
    const uint32_t labelsBeforeAssetChange = list.labelTextWrites;
    const uint32_t flagsBeforeAssetChange = list.clickableFlagWrites;
    items[3] = UiListItemView{"Changed", "Meta", "Disabled", false, true, 0, "", true};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    lv_obj_t *checkedIndicator = lv_obj_get_child(lv_obj_get_child(list.track, 3), 2);
    lv_obj_t *checkedMark = checkedIndicator == nullptr
        ? nullptr : lv_obj_get_child(checkedIndicator, 0);
    ok &= expect(out, list.labelTextWrites == labelsBeforeAssetChange + 2 &&
                      list.clickableFlagWrites == flagsBeforeAssetChange + 1 &&
                      checkedIndicator != nullptr && checkedMark != nullptr &&
                      !lv_obj_has_flag(checkedIndicator, LV_OBJ_FLAG_HIDDEN) &&
                      !lv_obj_has_flag(checkedMark, LV_OBJ_FLAG_HIDDEN) &&
                      strcmp(lv_label_get_text(checkedMark), LV_SYMBOL_OK) == 0,
                 "center-list refreshes changed text and uses a graphical selected indicator");
    items[3] = UiListItemView{"Item", "Meta", "", true, false};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    ok &= expect(out, lv_obj_has_flag(checkedIndicator, LV_OBJ_FLAG_HIDDEN),
                 "ordinary center-list rows hide the multi-select indicator");
    items[3] = UiListItemView{"Owned activity", "", "", true, false,
                              0x52DCB7, "", false, "MY"};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    lv_obj_t *ownershipTag = lv_obj_get_child(lv_obj_get_child(list.track, 3), 3);
    lv_obj_t *ownershipLabel = ownershipTag == nullptr
        ? nullptr : lv_obj_get_child(ownershipTag, 0);
    ok &= expect(out, ownershipTag != nullptr && ownershipLabel != nullptr &&
                      !lv_obj_has_flag(ownershipTag, LV_OBJ_FLAG_HIDDEN) &&
                      strcmp(lv_label_get_text(ownershipLabel), "MY") == 0,
                 "center-list renders a compact semantic ownership pill without title text mutation");
    items[3] = UiListItemView{"Item", "Meta", "", true, false};
    uiCenterListUpdate(list, items, 8, 1, "Back", true, false, false);
    ok &= expect(out, lv_obj_has_flag(ownershipTag, LV_OBJ_FLAG_HIDDEN),
                 "center-list removes the ownership pill when the next row has no tag");
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
        retargeted->end_value == uiCenterListTrackY(2, 58, 58) &&
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
    if (parent != nullptr) lv_scr_load(parent);
    UiModalView view{
        "Confirm trade", "$ 300", "Player 2 / Cash transfer", "AVAILABLE CASH  $1860",
        "9.8s remaining", "HOLD 1.2S TO CONFIRM", 0, true, true, false, false, false,
    };
    uiSetEventSink(captureModalEvent);
    uiModalCreate(modal, parent, view);
    if (parent != nullptr) lv_obj_update_layout(parent);

    const bool completeTree = modal.shade != nullptr && modal.panel != nullptr &&
        modal.confirm != nullptr && modal.holdTrack != nullptr &&
        modal.confirmFill != nullptr && modal.holdLabel != nullptr &&
        modal.back != nullptr && modal.countdown != nullptr && modal.cashLabel != nullptr;
    ok &= expect(out, completeTree && lv_obj_get_parent(modal.shade) == parent &&
                      lv_obj_get_parent(modal.panel) == modal.shade &&
                      lv_obj_get_parent(modal.confirm) == modal.panel &&
                      lv_obj_get_parent(modal.holdTrack) == modal.confirm &&
                      lv_obj_get_parent(modal.confirmFill) == modal.holdTrack &&
                      lv_obj_get_parent(modal.holdLabel) == modal.confirm &&
                      lv_obj_get_parent(modal.cashLabel) == modal.panel &&
                      lv_obj_get_parent(modal.back) == modal.panel,
                 "modal retains the approved shade panel confirm track fill and Back tree");
    ok &= expect(out, completeTree &&
                      strcmp(lv_label_get_text(modal.cashLabel), "AVAILABLE CASH  $1860") == 0,
                 "payment modal keeps available cash visible above its controls");
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

    if (display != nullptr) {
        lv_refr_now(display);
        // The fake driver can retain its initial full-screen invalidation after
        // loading a second screen. Reset only the test counter so this assertion
        // measures invalidations caused by the stable update below.
        display->inv_p = 0;
    }
    uiModalUpdate(modal, view);
    const uint16_t invalidationsAfterStableUpdate = display == nullptr ? 1 : display->inv_p;
    ok &= expect(out, invalidationsAfterStableUpdate == 0,
                 "unchanged modal updates do not invalidate the RGB framebuffer");
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
    view.title = "PAYMENT REQUIRED";
    view.amount = "PAY  $120";
    view.detail = "PAY RENT  |  CASH $1860";
    view.cashText = "";
    view.countdownText = "8.4s remaining";
    view.recipientCaption = "PAY TO";
    view.recipientName = "MORGAN";
    view.recipientToken = "M";
    view.recipientAccent = 0xEF7168;
    view.showRecipient = true;
    uiModalCreate(modal, parent, view);
    if (parent != nullptr) lv_obj_update_layout(parent);
    const bool recipientTree = modal.recipientCard != nullptr &&
        modal.recipientBadge != nullptr &&
        modal.recipientBadgeLabel != nullptr && modal.recipientCaptionLabel != nullptr &&
        modal.recipientNameLabel != nullptr;
    ok &= expect(out, recipientTree &&
                      strcmp(lv_label_get_text(modal.recipientBadgeLabel), "M") == 0 &&
                      strcmp(lv_label_get_text(modal.recipientCaptionLabel),
                             "PAY TO") == 0 &&
                      strcmp(lv_label_get_text(modal.recipientNameLabel), "MORGAN") == 0 &&
                      lv_obj_get_x(modal.recipientCard) == 20 &&
                      lv_obj_get_y(modal.recipientCard) == 36 &&
                      lv_obj_get_width(modal.recipientCard) == 232 &&
                      lv_obj_get_x(modal.recipientBadge) == 8 &&
                      lv_obj_get_y(modal.recipientBadge) == 7 &&
                      lv_obj_get_style_text_align(modal.detailLabel, 0) ==
                          LV_TEXT_ALIGN_CENTER &&
                      lv_obj_get_style_text_font(modal.detailLabel, 0) ==
                          &lv_font_montserrat_12 &&
                      lv_obj_get_style_bg_color(modal.recipientBadge, 0).full ==
                          lv_color_hex(0xEF7168).full,
                 "payment modal gives the recipient a prominent deterministic identity row");

    view.recipientCaption = "PAY TO";
    view.recipientName = "CITY BANK";
    view.recipientToken = "$";
    view.recipientAccent = 0xF2C453;
    uiModalUpdate(modal, view);
    ok &= expect(out, recipientTree &&
                      strcmp(lv_label_get_text(modal.recipientBadgeLabel), "$") == 0 &&
                      strcmp(lv_label_get_text(modal.recipientCaptionLabel),
                             "PAY TO") == 0 &&
                      strcmp(lv_label_get_text(modal.recipientNameLabel), "CITY BANK") == 0 &&
                      lv_obj_get_style_bg_color(modal.recipientBadge, 0).full ==
                          lv_color_hex(0xF2C453).full,
                 "system payments use the bank identity treatment without an avatar asset");

    uiModalDestroy(modal);
    ok &= expect(out, modal.shade == nullptr && parent != nullptr &&
                      lv_obj_get_child_cnt(parent) == 0,
                 "modal destroys its retained top-level tree without stale ownership");
    uiSetEventSink(nullptr);
    if (display != nullptr && display->act_scr == parent) display->act_scr = nullptr;
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
    state.stateVersion = 41;
    state.authoritySnapshotValid = true;
    state.tradeGiveAssetMask = 1u;
    state.nav.current = NavigationEntry{ScreenPage::Trade, 3, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}

void openDebtFixture(AppState &state, int32_t amountDue, int32_t cash,
                     uint32_t eligibleMask, uint32_t availableActions = (1u << 5))
{
    appInit(state, 0);
    static TransportEvent event{};
    event = TransportEvent{};
    event.kind = TransportEventKind::DebtResolutionRequired;
    event.transactionId = 90;
    event.stateVersion = 2;
    event.amount = amountDue;
    event.cash = cash;
    event.assetMask = eligibleMask;
    event.availableActions = availableActions;
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
    static TransportEvent unrelatedRejected{};
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

    static TransportEvent wrongKind{};
    wrongKind.kind = TransportEventKind::PaymentCompleted;
    wrongKind.requestId = tradeRequestId;
    wrongKind.cash = 1;
    appHandleTransportEvent(state, wrongKind, 7020);
    static TransportEvent zeroCompletion{};
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

    static TransportEvent tradeCompleted{};
    tradeCompleted.kind = TransportEventKind::TradeResponseReceived;
    tradeCompleted.requestId = tradeRequestId;
    tradeCompleted.stateVersion = 42;
    tradeCompleted.tradeOperation = TransportTradeOperation::Create;
    tradeCompleted.tradeResult = TransportTradeResult::Ok;
    tradeCompleted.tradeStatus = TransportTradeStatus::Offered;
    tradeCompleted.tradeFlags = (1u << 0) | (1u << 2) | (1u << 3);
    tradeCompleted.tradeCounterpartyId = 2;
    tradeCompleted.tradeId = 77;
    tradeCompleted.tradeRevision = 1;
    tradeCompleted.tradeExpiresInMs = 120000;
    tradeCompleted.assetMask = 1u;
    appHandleTransportEvent(state, tradeCompleted, 7050);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      state.nav.current.page == ScreenPage::TradeOffer &&
                      state.tradeOffer.active && state.tradeOffer.tradeId == 77 &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::TradeCreate)] == 0 &&
                      state.stateVersion == 42,
                  "matching trade response dismisses once and presents the authoritative offer");
    const uint32_t acceptedRevision = state.revision;
    appHandleTransportEvent(state, tradeCompleted, 7060);
    ok &= expect(out, state.nav.current.page == ScreenPage::TradeOffer &&
                      state.revision == acceptedRevision,
                  "duplicate trade response is inert");

    appInit(state, 0);
    static TransportEvent paymentRequired{};
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
    appTick(state, 1300);
    ok &= expect(out, state.modal.submitting && !state.modal.holding &&
                      appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::PayNow && command.requestId != 0,
                 "forced payment auto-submits at 1.2 seconds without waiting for release");
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1310}, 1310);
    const int32_t cashBeforeCompletion = state.money;
    static TransportEvent wrongPayment{};
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
    static TransportEvent event{};
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
    trade.tradeOperation = TransportTradeOperation::Create;
    trade.targetPlayerId = 2;
    trade.assetMask = 1u;
    trade.argument = 150;
    ok &= expect(out, transport.send(trade, 20), "demo accepts trade confirmation");
    transport.tick(139);
    ok &= expect(out, !transport.poll(event), "trade completion is not emitted before 120ms");
    transport.tick(140);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::TradeResponseReceived &&
                      event.requestId == 602 && event.tradeId != 0 &&
                      event.tradeStatus == TransportTradeStatus::Offered &&
                      event.assetMask == 1u && event.tradeSelfGivesCash == 150 &&
                      !transport.poll(event),
                  "trade response returns the active authoritative offer within 150ms");

    transport.setScenario(DemoScenario::Waiting);
    transport.begin(0);
    TransportCommand detailRequest{};
    detailRequest.kind = TransportCommandKind::PlayerDetailRequest;
    detailRequest.requestId = 603;
    detailRequest.stateVersion = 41;
    detailRequest.targetPlayerId = 3;
    ok &= expect(out, transport.send(detailRequest, 30),
                 "demo accepts an on-demand player detail request");
    transport.tick(209);
    ok &= expect(out, !transport.poll(event),
                 "player detail is not emitted before the simulated response delay");
    transport.tick(210);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::PlayerDetailReceived &&
                      event.requestId == 603 && event.detailPlayerId == 3 &&
                      event.detailAssetCount == 4 &&
                      event.financialRecordCount == kPlayerFinanceCapacity &&
                      event.playerDetail != nullptr &&
                      event.playerDetail->assets[1].assetIndex == 4 &&
                      event.playerDetail->financialRecords[0].amount == -220,
                 "demo returns bounded assets and the latest ten finance records on demand");

    return ok;
}

bool runTradeLifecycleTests(Stream &out)
{
    bool ok = true;
    static AppState state{};
    TransportCommand command{};

    appInit(state, 0);
    state.stateVersion = 0;
    state.authoritySnapshotValid = true;
    state.tradeGiveAssetMask = 1u;
    state.nav.current = NavigationEntry{ScreenPage::Trade, 3, 0};
    shortPress(state, 10, 80);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 100}, 100);
    appTick(state, 1300);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1300}, 1300);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                      !state.modal.submitting && state.commandCount == 0 &&
                      strcmp(state.toast, "WAITING FOR GAME SYNC") == 0,
                 "trade mutation never queues a zero state-version placeholder");

    appInit(state, 2000);
    state.stateVersion = 4;
    state.authoritySnapshotValid = true;
    static TransportEvent incoming{};
    incoming = TransportEvent{};
    incoming.kind = TransportEventKind::TradeResponseReceived;
    incoming.stateVersion = 5;
    incoming.tradeOperation = TransportTradeOperation::Query;
    incoming.tradeResult = TransportTradeResult::Ok;
    incoming.tradeStatus = TransportTradeStatus::Offered;
    incoming.tradeCounterpartyId = 2;
    incoming.tradeId = 300;
    incoming.tradeRevision = 1;
    incoming.tradeExpiresInMs = 90000;
    incoming.assetMask = 1u << 1;
    incoming.counterpartyAssetMask = 1u << 4;
    incoming.tradeSelfGivesCash = 50;
    incoming.tradeCounterpartyGivesCash = 200;
    appHandleTransportEvent(state, incoming, 2010);
    ok &= expect(out, state.nav.current.page == ScreenPage::TradeOffer &&
                      state.tradeOffer.active && state.tradeOffer.tradeId == 300 &&
                      state.tradeOffer.revision == 1 &&
                      appTradeOfferActionCount(state) == 3,
                 "unsolicited active quote opens one recipient-relative trade review page");
    const uint32_t offerRevision = state.revision;
    appHandleTransportEvent(state, incoming, 2020);
    ok &= expect(out, state.revision == offerRevision,
                 "duplicate same-revision quote is presentation-idempotent");

    shortPress(state, 2030, 2100);
    ok &= expect(out, state.modal.kind == ModalKind::TradeAction &&
                      state.modal.tradeOperation == TransportTradeOperation::Confirm,
                 "Receive Only opens an explicit accept confirmation");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 2200}, 2200);
    appTick(state, 3400);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 3400}, 3400);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::TradeConfirm &&
                      command.tradeOperation == TransportTradeOperation::Confirm &&
                      command.tradeId == 300 && command.tradeRevision == 1 &&
                      command.stateVersion == 5 && command.stateVersion != 0,
                 "accept queues one exact-version Confirm for the reviewed revision");

    static TransportEvent settled{};
    settled = TransportEvent{};
    settled.kind = TransportEventKind::TradeResponseReceived;
    settled.requestId = command.requestId;
    settled.stateVersion = 6;
    settled.tradeOperation = TransportTradeOperation::Confirm;
    settled.tradeResult = TransportTradeResult::Ok;
    settled.tradeStatus = TransportTradeStatus::Settled;
    settled.tradeFlags = 1u << 5;
    settled.tradeCounterpartyId = 2;
    settled.tradeId = 300;
    settled.tradeRevision = 1;
    appHandleTransportEvent(state, settled, 3410);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      !state.tradeOffer.active && state.modal.kind == ModalKind::None &&
                      strcmp(state.toast, "TRADE COMPLETE") == 0,
                 "settled response closes the offer and returns to Home once");

    static TransportEvent noActive{};
    noActive = TransportEvent{};
    noActive.kind = TransportEventKind::TradeResponseReceived;
    noActive.stateVersion = 6;
    noActive.tradeOperation = TransportTradeOperation::Query;
    noActive.tradeResult = TransportTradeResult::NoActiveTrade;
    noActive.tradeStatus = TransportTradeStatus::None;
    noActive.tradeFlags = 1u << 4;
    const uint32_t noActiveRevision = state.revision;
    appHandleTransportEvent(state, noActive, 3420);
    ok &= expect(out, state.revision == noActiveRevision &&
                      strcmp(state.toast, "TRADE COMPLETE") == 0,
                 "routine NoActiveTrade resync is silent when no stale trade UI exists");

    incoming.stateVersion = 7;
    incoming.tradeId = 301;
    incoming.tradeRevision = 2;
    appHandleTransportEvent(state, incoming, 3500);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 3510}, 3510);
    shortPress(state, 3520, 3580);
    ok &= expect(out, state.nav.current.page == ScreenPage::Trade &&
                      state.tradeEntryMode == TradeEntryMode::CounterLocked &&
                      state.tradeReceiver == 1 && appTradeReceiverLocked(state) &&
                      state.tradeGiveAssetMask == incoming.assetMask,
                 "Receive and Give Back enters a receiver-locked counter draft");
    state.tradeGiveAssetMask = 1u << 7;
    state.tradeAmount = 150;
    state.nav.current.focus = 2;
    shortPress(state, 3600, 3670);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 3700}, 3700);
    appTick(state, 4900);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 4900}, 4900);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::TradeUpdate &&
                      command.tradeOperation == TransportTradeOperation::Update &&
                      command.tradeId == 301 && command.tradeRevision == 2 &&
                      command.stateVersion == 7 &&
                      command.assetMask == (1u << 7) && command.argument == 150 &&
                      command.counterpartyAssetMask == incoming.counterpartyAssetMask &&
                      command.counterpartyArgument == incoming.tradeCounterpartyGivesCash,
                 "counteroffer preserves the received side and submits the edited give side");

    noActive.stateVersion = 8;
    appHandleTransportEvent(state, noActive, 5000);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      state.modal.kind == ModalKind::None && !state.tradeOffer.active &&
                      state.pendingCommandMask == 0,
                 "NoActiveTrade resync clears a stale counter draft and its modal");

    incoming.stateVersion = 7;
    incoming.tradeId = 302;
    appHandleTransportEvent(state, incoming, 5010);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      !state.tradeOffer.active && state.stateVersion == 8,
                 "older active quote cannot reopen a trade after a newer clear projection");
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

uint16_t countObjectClass(lv_obj_t *object, const lv_obj_class_t *objectClass)
{
    if (object == nullptr || objectClass == nullptr) return 0;
    uint16_t count = lv_obj_check_type(object, objectClass) ? 1u : 0u;
    const uint32_t childCount = lv_obj_get_child_cnt(object);
    for (uint32_t index = 0; index < childCount; ++index) {
        count = static_cast<uint16_t>(
            count + countObjectClass(lv_obj_get_child(object, index), objectClass)
        );
    }
    return count;
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

lv_obj_t *findDirectChildContainingLabel(lv_obj_t *parent, const char *text)
{
    if (parent == nullptr) return nullptr;
    const uint32_t childCount = lv_obj_get_child_cnt(parent);
    for (uint32_t index = 0; index < childCount; ++index) {
        lv_obj_t *const child = lv_obj_get_child(parent, index);
        if (treeContainsLabelText(child, text)) return child;
    }
    return nullptr;
}

uint8_t countDirectChildrenWithSizeAtY(lv_obj_t *parent, int16_t y,
                                       int16_t width, int16_t height)
{
    if (parent == nullptr) return 0;
    uint8_t count = 0;
    const uint32_t childCount = lv_obj_get_child_cnt(parent);
    for (uint32_t index = 0; index < childCount; ++index) {
        lv_obj_t *const child = lv_obj_get_child(parent, index);
        if (lv_obj_get_y(child) == y && lv_obj_get_width(child) == width &&
            lv_obj_get_height(child) == height) {
            ++count;
        }
    }
    return count;
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
    state.modal.counterparty = "PLAYER";
    state.modal.purpose = "PAY RENT";
    state.modal.amount = 120;
    state.debtCreditorId = 2;
    state.rosterSnapshotValid = true;
    snprintf(state.rosterNames[1], sizeof(state.rosterNames[1]), "%s", "BOT 1");
    state.money = 1860;
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
    const bool modalAmountVisible = treeContainsLabelText(modalPanel, "PAY  $120");
    const bool modalCashVisible = treeContainsLabelText(modalPanel,
                                                         "PAY RENT  |  CASH $1860");
    const bool modalRecipientVisible = treeContainsLabelText(modalPanel, "PAY TO") &&
                                       treeContainsLabelText(modalPanel, "BOT 1") &&
                                       treeContainsLabelText(modalPanel, "B1") &&
                                       !treeContainsLabelText(modalPanel, "PLAYER");
    ok &= expect(out, modalAmountVisible,
                 "forced-payment modal labels the amount due");
    ok &= expect(out, modalCashVisible,
                 "forced-payment modal combines purpose and current cash in one metadata row");
    ok &= expect(out, modalRecipientVisible,
                 "forced-payment modal refreshes a placeholder from the latest roster");

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

    state.modal = ModalState{};
    state.toast = "";
    state.toastUntilMs = 0;
    state.page = ScreenPage::Purchase;
    state.nav.current.page = ScreenPage::Purchase;
    state.focus = 0;
    state.nav.current.focus = 0;
    state.tileAssetIndex = 0;
    state.availableActions = (1u << 2) | (1u << 3);
    ++state.revision;
    uiRendererRender(state, 11);
    const bool purchasePriceVisible = treeContainsLabelText(lv_scr_act(), "$280");
    const bool purchaseCashVisible = treeContainsLabelText(lv_scr_act(), "$2345");
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "PURCHASE PRICE"),
                 "Purchase labels the property price");
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "AVAILABLE CASH"),
                 "Purchase labels current available cash");
    ok &= expect(out, purchasePriceVisible,
                 "Purchase shows the exact property price");
    ok &= expect(out, purchaseCashVisible,
                 "Purchase shows the exact current available cash");
    uiRendererResetTestStats();
    state.focus = 1;
    state.nav.current.focus = 1;
    ++state.revision;
    uiRendererRender(state, 12);
    const UiRendererTestStats purchaseStats = uiRendererGetTestStats();
    lv_obj_t *const buyButton = findDirectChildContainingLabel(lv_scr_act(), "BUY");
    lv_obj_t *const auctionButton =
        findDirectChildContainingLabel(lv_scr_act(), "AUCTION");
    lv_obj_t *const buyLabel = buyButton == nullptr ? nullptr : lv_obj_get_child(buyButton, 0);
    lv_obj_t *const auctionLabel =
        auctionButton == nullptr ? nullptr : lv_obj_get_child(auctionButton, 0);
    ok &= expect(out, purchaseStats.incrementalRenders == 1 &&
                      purchaseStats.rebuildRenders == 0 &&
                      buyButton != nullptr && auctionButton != nullptr &&
                      buyLabel != nullptr && auctionLabel != nullptr &&
                      lv_obj_get_style_border_color(buyButton, 0).full ==
                          lv_color_hex(0x263234).full &&
                      lv_obj_get_style_text_color(buyLabel, 0).full ==
                          lv_color_hex(0xEDF3F1).full &&
                      lv_obj_get_style_border_color(auctionButton, 0).full ==
                          lv_color_hex(0xF2C453).full &&
                      lv_obj_get_style_text_color(auctionLabel, 0).full ==
                          lv_color_hex(0xF2C453).full,
                  "Purchase focus updates both button and label locally without a server frame");

    state.authorityOnline = false;
    ++state.revision;
    uiRendererRender(state, 13);
    lv_obj_t *const offlineBadge = findDirectChildAt(lv_scr_act(), UiRect{218, 10, 44, 38});
    ok &= expect(out, offlineBadge != nullptr &&
                      treeContainsLabelText(offlineBadge, LV_SYMBOL_WIFI) &&
                      lv_obj_get_index(offlineBadge) ==
                          static_cast<int32_t>(lv_obj_get_child_cnt(lv_scr_act()) - 1),
                 "Offline Wi-Fi badge is visible at 12 o'clock above every page layer");

    state.page = ScreenPage::Assets;
    state.nav.current.page = ScreenPage::Assets;
    state.focus = 0;
    state.nav.current.focus = 0;
    ++state.revision;
    uiRendererRender(state, 14);
    ok &= expect(out,
                 findDirectChildAt(lv_scr_act(), UiRect{218, 10, 44, 38}) != nullptr,
                 "Offline Wi-Fi badge persists across page rebuilds");

    state.authorityOnline = true;
    ++state.revision;
    uiRendererRender(state, 15);
    ok &= expect(out,
                 findDirectChildAt(lv_scr_act(), UiRect{218, 10, 44, 38}) == nullptr,
                 "Offline Wi-Fi badge disappears after authority reconnects");

#if GRIDOPOLY_SELF_TEST == 1
    uiRendererResetTestStats();
    uiRendererInvalidateArtwork();
    uiRendererRender(state, 16);
    const UiRendererTestStats artworkRefreshStats = uiRendererGetTestStats();
    ok &= expect(out, artworkRefreshStats.rebuildRenders == 1,
                 "a published remote artwork rebuilds the visible page without a state revision");
#endif

    state.page = ScreenPage::DiceStage;
    state.nav.current.page = ScreenPage::DiceStage;
    state.boardSize = 16;
    state.rollAnimating = true;
    state.rollResolved = true;
    state.rollTarget = 14;
    state.rolledSteps = 7;
    uiRendererResetTestStats();
    ++state.revision;
    uiRendererRender(state, 17);
    uiRendererRender(state, 18);
    const UiRendererTestStats prefetchStats = uiRendererGetTestStats();
    ok &= expect(out, prefetchStats.artworkPrefetches == 1,
                 "an authoritative dice target queues one artwork prefetch during the roll");

    state.page = ScreenPage::MoveGuide;
    state.nav.current.page = ScreenPage::MoveGuide;
    ++state.revision;
    uiRendererRender(state, 19);
    lv_obj_t *const loadingArtwork =
        findDirectChildAt(lv_scr_act(), UiRect{176, 94, 128, 128});
    ok &= expect(out, loadingArtwork != nullptr &&
                      countObjectClass(loadingArtwork, &lv_spinner_class) == 1 &&
                      countObjectClass(loadingArtwork, &lv_img_class) == 0,
                 "arrival artwork shows a spinner and no unrelated image while loading");

    state.page = ScreenPage::Home;
    state.nav.current.page = ScreenPage::Home;
    state.rollAnimating = false;
    state.rollResolved = false;
    state.rollTarget = 0xFF;
    state.homePhase = HomePhase::Waiting;
    state.modal = ModalState{};
    state.tradeReceiverPickerOpen = false;
    state.buttonHeld = false;
    state.inlineEditField = InlineEditField::None;
    state.endTurnPresentation = EndTurnPresentationPhase::None;
    state.selfSeatId = 1;
    strcpy(state.rosterNames[1], "MORGAN");
    state.rosterSnapshotValid = true;
    state.activity = ActivityState{};
    state.activity.entries[0].event.sequence = 500;
    state.activity.entries[0].event.kind = 9;
    state.activity.entries[0].event.actorId = 2;
    state.activity.entries[0].event.targetId = 1;
    state.activity.entries[0].event.assetIndex = 0;
    state.activity.entries[0].event.amount = 6;
    state.activity.entries[0].announced = true;
    state.activity.head = 1;
    state.activity.count = 1;
    state.activity.bannerSequence = 500;
    state.activity.bannerUntilMs = 5000;
    ok &= expect(out, appActivityBannerVisible(state, 20),
                 "passive activity banner is eligible on an uninterrupted ordinary page");
    ++state.revision;
    uiRendererRender(state, 20);
    char activitySummary[96]{};
    snprintf(activitySummary, sizeof(activitySummary),
             "MORGAN  |  PAID YOU $6 RENT");
    lv_obj_t *const activityBanner =
        findDirectChildAt(lv_scr_act(), UiRect{104, 50, 272, 48});
    ok &= expect(out, activityBanner != nullptr &&
                      treeContainsLabelText(activityBanner, activitySummary),
                 "safe-area activity banner uses YOU for the viewing player");

    state.activity.entries[1].event.sequence = 501;
    state.activity.entries[1].event.kind = 3;
    state.activity.entries[1].event.actorId = 2;
    state.activity.entries[1].event.amount = 8;
    state.activity.entries[1].announced = true;
    state.activity.head = 2;
    state.activity.count = 2;
    state.activity.bannerSequence = 501;
    state.activity.bannerUntilMs = 5000;
    ++state.revision;
    uiRendererRender(state, 21);
    lv_obj_t *const outgoingActivityBanner =
        findDirectChildContainingLabel(lv_scr_act(), "MORGAN  |  PAID YOU $6 RENT");
    lv_obj_t *const incomingActivityBanner =
        findDirectChildContainingLabel(lv_scr_act(), "MORGAN  |  ROLLED 8");
    ok &= expect(out,
                 outgoingActivityBanner != nullptr && incomingActivityBanner != nullptr &&
                     outgoingActivityBanner != incomingActivityBanner,
                 "a newer activity keeps both banner surfaces alive during the cross-slide");
    ok &= expect(out,
                 countDirectChildrenWithSizeAtY(lv_scr_act(), 50, 272, 48) == 2,
                 "activity replacement retains fixed safe-area geometry during the overlap");
    ok &= expect(out,
                 outgoingActivityBanner != nullptr && incomingActivityBanner != nullptr &&
                     !lv_obj_has_flag(outgoingActivityBanner, LV_OBJ_FLAG_CLICKABLE) &&
                     lv_obj_has_flag(incomingActivityBanner, LV_OBJ_FLAG_CLICKABLE),
                 "only the incoming activity banner accepts touch during replacement");

    state.activity.bannerSequence = 0;
    state.activity.bannerUntilMs = 0;
    ++state.revision;
    uiRendererRender(state, 22);
    lv_obj_t *const activityButton =
        findDirectChildAt(lv_scr_act(), UiRect{218, 10, 44, 38});
    lv_obj_t *const activityButtonLabel = activityButton == nullptr
        ? nullptr : lv_obj_get_child(activityButton, 0);
    if (activityButton != nullptr) lv_obj_update_layout(activityButton);
    lv_area_t activityButtonArea{};
    lv_area_t activityButtonLabelArea{};
    if (activityButton != nullptr) lv_obj_get_coords(activityButton, &activityButtonArea);
    if (activityButtonLabel != nullptr) {
        lv_obj_get_coords(activityButtonLabel, &activityButtonLabelArea);
    }
    const int16_t activityCenterDeltaX = static_cast<int16_t>(
        activityButtonArea.x1 + activityButtonArea.x2 -
        activityButtonLabelArea.x1 - activityButtonLabelArea.x2
    );
    const int16_t activityCenterDeltaY = static_cast<int16_t>(
        activityButtonArea.y1 + activityButtonArea.y2 -
        activityButtonLabelArea.y1 - activityButtonLabelArea.y2
    );
    ok &= expect(out, activityButton != nullptr && activityButtonLabel != nullptr &&
                      treeContainsLabelText(activityButton, "ACT") &&
                      activityCenterDeltaX >= -1 && activityCenterDeltaX <= 1 &&
                      activityCenterDeltaY >= -1 && activityCenterDeltaY <= 1,
                 "activity entry is a fixed ACT button with geometrically centered text");

    state.page = ScreenPage::Activity;
    state.nav.current.page = ScreenPage::Activity;
    state.activity.bannerSequence = 0;
    state.activity.bannerUntilMs = 0;
    ++state.revision;
    uiRendererRender(state, 23);
    char activityRow[96]{};
    snprintf(activityRow, sizeof(activityRow), "MORGAN: PAID YOU $6 RENT");
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "LIVE ACTIVITY") &&
                      treeContainsLabelText(lv_scr_act(), "LATEST 2") &&
                      treeContainsLabelText(lv_scr_act(), activityRow) &&
                      treeContainsLabelText(lv_scr_act(), "MORGAN: ROLLED 8"),
                 "activity history keeps both replaced events in its scrollable center list");

    state.page = ScreenPage::Auction;
    state.nav.current.page = ScreenPage::Auction;
    state.nav.current.focus = 0;
    state.auctionPresentation = AuctionPresentationPhase::Intro;
    state.auctionAssetIndex = 0;
    state.auctionCurrentBid = 0;
    state.auctionMinimumBid = 10;
    ++state.revision;
    uiRendererRender(state, 16);
    const bool introLotVisible = treeContainsLabelText(lv_scr_act(), "UP FOR AUCTION");
    const bool introOpeningVisible =
        treeContainsLabelText(lv_scr_act(), "BIDDING OPENS SHORTLY");
    ok &= expect(out, introLotVisible &&
                      findDirectChildAt(lv_scr_act(), UiRect{72, 116, 112, 112}) != nullptr,
                 "Auction introduction identifies the lot with fixed artwork geometry");
    ok &= expect(out, introOpeningVisible,
                 "Auction introduction explains that bidding opens after the reveal");
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "$10"),
                 "Auction introduction shows the opening bid");
    const bool introCashTitleVisible = treeContainsLabelText(lv_scr_act(), "AVAILABLE CASH");
    const bool introCashVisible = treeContainsLabelText(lv_scr_act(), "$2345");
    ok &= expect(out, introCashTitleVisible,
                 "Auction introduction labels the player's available cash");
    ok &= expect(out, introCashVisible,
                 "Auction introduction shows the player's current available cash");

    state.auctionPresentation = AuctionPresentationPhase::Live;
    state.auctionFlags = 0x03;
    state.availableActions = 0;
    state.auctionReadyMask = 0x05;
    state.auctionRequiredReadyMask = 0x1F;
    ++state.revision;
    uiRendererRender(state, 17);
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "WAITING FOR PLAYERS"),
                 "Auction opening barrier stays on the unified live waiting stage");
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "READY  2 / 5"),
                 "Auction opening barrier shows exact ready progress without controls");

    state.auctionPresentation = AuctionPresentationPhase::Live;
    state.auctionFlags = 0x01;
    state.auctionCurrentBid = 80;
    state.auctionMinimumBid = 90;
    state.auctionCurrentBidderId = state.selfSeatId;
    state.decisionPlayerId = state.selfSeatId;
    state.availableActions = (1u << 13) | (1u << 14);
    ++state.revision;
    uiRendererRender(state, 18);
    ok &= expect(out, treeContainsLabelText(lv_scr_act(), "CURRENT BID") &&
                      treeContainsLabelText(lv_scr_act(), "$80") &&
                      treeContainsLabelText(lv_scr_act(), "AVAILABLE CASH") &&
                      treeContainsLabelText(lv_scr_act(), "$2345") &&
                      treeContainsLabelText(lv_scr_act(), "YOU PAY IF BIDDING  $90") &&
                      findDirectChildAt(lv_scr_act(), UiRect{72, 116, 112, 112}) != nullptr,
                 "Live auction keeps the lot artwork while separating bid and cash metrics");

    state.auctionPresentation = AuctionPresentationPhase::Result;
    state.auctionResultAssetIndex = 0;
    state.auctionWinnerPlayerId = 2;
    state.auctionResultAmount = 130;
    ++state.revision;
    uiRendererRender(state, 19);
    const bool resultClosedVisible = treeContainsLabelText(lv_scr_act(), "AUCTION CLOSED");
    const bool resultFinalBidVisible = treeContainsLabelText(lv_scr_act(), "FINAL BID");
    const bool resultAmountVisible = treeContainsLabelText(lv_scr_act(), "$130");
    const bool resultAwardedVisible = treeContainsLabelText(lv_scr_act(), "PROPERTY AWARDED");
    ok &= expect(out, resultClosedVisible &&
                      findDirectChildAt(lv_scr_act(), UiRect{72, 116, 112, 112}) != nullptr,
                 "Auction result keeps the same lot artwork and announces closure");
    ok &= expect(out, resultFinalBidVisible,
                 "Auction result labels the settlement amount");
    ok &= expect(out, resultAmountVisible,
                 "Auction result keeps the final price readable");
    ok &= expect(out, resultAwardedVisible,
                 "Auction result confirms that the property was awarded");
    const bool resultCashTitleVisible = treeContainsLabelText(lv_scr_act(), "AVAILABLE CASH");
    const bool resultCashVisible = treeContainsLabelText(lv_scr_act(), "$2345");
    ok &= expect(out, resultCashTitleVisible,
                 "Auction result labels the player's available cash");
    ok &= expect(out, resultCashVisible,
                 "Auction result shows the player's current available cash");
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

void resetLogicTestFailure()
{
    firstFailure = nullptr;
}

const char *firstLogicTestFailure()
{
    return firstFailure == nullptr ? "NONE" : firstFailure;
}

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
    ok &= expect(out, kOuterRing.x + kOuterRing.w / 2 == 240 &&
                      kInnerRing.x + kInnerRing.w / 2 == 240 &&
                      kOuterRing.y + kOuterRing.h / 2 == 240 &&
                      kInnerRing.y + kInnerRing.h / 2 == 240,
                 "display rings share the raster center");
    ok &= expect(out, kNormalListCount.x == 202 && kNormalListCount.y == 328 &&
                      kNormalListCount.w == 76 && kNormalListCount.h == 16,
                 "list count uses the approved below-progress rectangle");
    ok &= expect(out, kPlayerDetailAssets.x == 74 && kPlayerDetailFinance.x == 250 &&
                      kPlayerDetailTrade.y == 282 && kPlayerDetailRefresh.y == 282 &&
                      kPlayerDetailAvatar.x == 104 && kPlayerDetailAvatar.y == 108 &&
                      kPlayerDetailAvatar.w == 88 && kPlayerDetailAvatar.h == 88 &&
                      kPlayerDetailSummary.x == 204 && kPlayerDetailSummary.y == 108 &&
                      kPlayerDetailSummary.w == 168 && kPlayerDetailSummary.h == 88 &&
                      kPlayerDetailAvatar.x + kPlayerDetailAvatar.w + 12 ==
                          kPlayerDetailSummary.x &&
                      uiRectInsideCircle(kPlayerDetailAvatar) &&
                      uiRectInsideCircle(kPlayerDetailSummary) &&
                      uiRectInsideCircle(kPlayerDetailAssets) &&
                      uiRectInsideCircle(kPlayerDetailFinance) &&
                      uiRectInsideCircle(kPlayerDetailTrade) &&
                      uiRectInsideCircle(kPlayerDetailRefresh),
                 "player detail actions remain symmetric and inside the round safe area");
    ok &= expect(out, kAssetDetailArtwork.y > kAssetDetailHeaderBottom &&
                      kAssetDetailArtwork.y + kAssetDetailArtwork.h <= kAssetDetailGroup.y &&
                      kAssetDetailGroup.y + kAssetDetailGroup.h <= kAssetDetailMetrics.y &&
                      kAssetDetailMetrics.y + kAssetDetailMetrics.h <= kAssetDetailCash.y &&
                      kAssetDetailCash.y + kAssetDetailCash.h < kAssetDetailAction0.y &&
                      kAssetDetailAction2.y + kAssetDetailAction2.h < kNormalFooter.y &&
                      uiRectInsideCircle(kAssetDetailArtwork) &&
                      uiRectInsideCircle(kAssetDetailGroup) &&
                      uiRectInsideCircle(kAssetDetailAction0) &&
                      uiRectInsideCircle(kAssetDetailAction1) &&
                      uiRectInsideCircle(kAssetDetailAction2) &&
                      uiRectInsideCircle(kAssetDetailAction3),
                 "asset detail artwork group metrics and four actions do not overlap");
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
    ok &= expect(out, waitingActionCount == 3 && nextActionCount == 3 &&
                      waitingActions[0] == HomeAction::Assets &&
                      waitingActions[1] == HomeAction::Players &&
                      waitingActions[2] == HomeAction::Trade &&
                      nextActions[0] == HomeAction::Assets &&
                      nextActions[1] == HomeAction::Players &&
                      nextActions[2] == HomeAction::Trade,
                 "waiting and next expose Assets Players Trade in order");
    ok &= expect(out, turnActionCount == 4 &&
                      turnActions[0] == HomeAction::Dice &&
                      turnActions[1] == HomeAction::Assets &&
                      turnActions[2] == HomeAction::Players &&
                      turnActions[3] == HomeAction::Trade,
                 "my turn exposes Dice Assets Players Trade in order");
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
                      static_cast<uint8_t>(ModalKind::DebtMortgageConfirm) == 5 &&
                      static_cast<uint8_t>(ModalKind::VoluntaryUnmortgage) == 6 &&
                      static_cast<uint8_t>(ModalKind::TradeAction) == 7 &&
                      static_cast<uint8_t>(ModalKind::DebtSellBuildingConfirm) == 8,
                  "exact modal kind names compile in contract order");
    static AppState state{};
    appInit(state, 0);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 0,
                 "starts on home");

    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 10}, 10);
    ok &= expect(out, state.nav.current.focus == 2, "home focus wraps left");
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
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerDetail &&
                      state.playerDetail.loadState == PlayerDetailLoadState::Loading &&
                      appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::PlayerDetailRequest &&
                      command.targetPlayerId == 4,
                 "player row opens detail and requests only the selected player");
    static TransportPlayerDetailPayload detailPayload{};
    for (uint8_t i = 0; i < kPlayerDetailAssetCapacity; ++i) {
        detailPayload.assets[i] = TransportPlayerAsset{i, static_cast<uint8_t>(i % 4)};
    }
    for (uint8_t i = 0; i < kPlayerFinanceCapacity; ++i) {
        detailPayload.financialRecords[i] = TransportFinancialRecord{
            static_cast<uint32_t>(91 - i), static_cast<int32_t>(i % 2 == 0 ? -220 : 40),
            static_cast<uint8_t>(i % 2 == 0 ? 8 : 9), 2,
            static_cast<uint8_t>(i), 0
        };
    }
    TransportEvent detailEvent{};
    detailEvent.kind = TransportEventKind::PlayerDetailReceived;
    detailEvent.requestId = command.requestId;
    detailEvent.stateVersion = 44;
    detailEvent.detailPlayerId = 4;
    detailEvent.detailPosition = 17;
    detailEvent.detailCash = 1325;
    detailEvent.detailAssetCount = kPlayerDetailAssetCapacity;
    detailEvent.financialRecordCount = kPlayerFinanceCapacity;
    detailEvent.playerDetail = &detailPayload;
    TransportEvent wrongDetail = detailEvent;
    wrongDetail.requestId = command.requestId + 1;
    appHandleTransportEvent(state, wrongDetail, 523);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Loading,
                 "mismatched player detail response cannot populate the local cache");
    TransportEvent interveningSnapshot{};
    interveningSnapshot.kind = TransportEventKind::StateSnapshotApplied;
    interveningSnapshot.stateVersion = 43;
    interveningSnapshot.cash = state.money;
    interveningSnapshot.playerPosition = state.position;
    appHandleTransportEvent(state, interveningSnapshot, 524);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Loading,
                 "regular realtime snapshot does not replace on-demand player detail");
    appHandleTransportEvent(state, detailEvent, 525);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Ready &&
                      state.playerDetail.cash == 1325 && state.playerDetail.position == 17 &&
                      state.playerDetail.assetCount == kPlayerDetailAssetCapacity &&
                      state.playerDetail.financialRecordCount == kPlayerFinanceCapacity &&
                      state.playerDetail.assets[1].assetIndex == 1 &&
                      state.playerDetail.financialRecords[0].amount == -220,
                 "matching player detail response populates the bounded local cache");
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 526);
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerAssets &&
                      appFocusCount(state) == kPlayerDetailAssetCapacity + 1,
                 "player asset detail uses a bounded scroll list plus fixed Back");
    for (uint8_t i = 0; i < kPlayerDetailAssetCapacity; ++i) {
      appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 527);
    }
    ok &= expect(out, appFocusIsFooter(state) &&
                      state.playerAssetListIndex == kPlayerDetailAssetCapacity - 1,
                 "player asset list scrolls through every owned asset and stops at Back");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 528);
    state.nav.current.focus = 1;
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 529);
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerFinance &&
                      appFocusCount(state) == kPlayerFinanceCapacity + 1,
                 "latest ten finance records use a scroll list plus fixed Back");
    for (uint8_t i = 0; i < kPlayerFinanceCapacity; ++i) {
      appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 530);
    }
    ok &= expect(out, appFocusIsFooter(state) &&
                      state.playerFinanceListIndex == kPlayerFinanceCapacity - 1,
                 "finance list scrolls through ten records and clamps at Back");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 531);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 532);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players &&
                      state.nav.current.focus == 3 && state.playerListIndex == 3 &&
                      state.playerDetail.loadState == PlayerDetailLoadState::Empty,
                 "player detail back restores players focus and drops its query cache");
    appHandleTransportEvent(state, detailEvent, 533);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Empty,
                 "late player detail response is ignored after leaving the detail page");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 540);

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 550);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 551);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 560);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 2,
                 "trade back restores home trade focus");

    appInit(state, 0);
    state.nav.current = NavigationEntry{ScreenPage::Players, 2, 2};
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 10);
    TransportCommand firstDetailRequest{};
    ok &= expect(out, appPollCommand(state, firstDetailRequest) &&
                      firstDetailRequest.kind == TransportCommandKind::PlayerDetailRequest &&
                      state.playerDetail.loadState == PlayerDetailLoadState::Loading &&
                      appFocusCount(state) == 1 && appFocusIsFooter(state),
                 "player detail Loading keeps only the fixed Back focus active");
    appTick(state, 4510);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Failed &&
                      appFocusCount(state) == 2 && !appFocusIsFooter(state),
                 "player detail timeout exposes Retry before fixed Back");
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 4520);
    TransportCommand retryDetailRequest{};
    ok &= expect(out, appPollCommand(state, retryDetailRequest) &&
                      retryDetailRequest.kind == TransportCommandKind::PlayerDetailRequest &&
                      retryDetailRequest.requestId != firstDetailRequest.requestId &&
                      state.playerDetail.loadState == PlayerDetailLoadState::Loading,
                 "failed player detail retries with a fresh correlation id");
    appHandleTransportEvent(state, TransportEvent{TransportEventKind::ConnectionLost}, 4530);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Failed &&
                      state.playerDetail.requestId == 0,
                 "connection loss invalidates the current on-demand detail cache");
    TransportEvent detailResync{};
    detailResync.kind = TransportEventKind::StateSnapshotApplied;
    detailResync.stateVersion = 50;
    detailResync.cash = state.money;
    detailResync.playerPosition = state.position;
    detailResync.resync = true;
    appHandleTransportEvent(state, detailResync, 4540);
    ok &= expect(out, state.playerDetail.loadState == PlayerDetailLoadState::Empty &&
                      state.nav.current.page == ScreenPage::Home,
                 "full resync discards cached detail instead of treating it as realtime state");

    appInit(state, 0);
    state.homePhase = HomePhase::Waiting;
    state.nav.current.focus = 1;
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

    static AppState assetActions{};
    appInit(assetActions, 0);
    assetActions.boardSize = 40;
    assetActions.selfSeatId = 1;
    assetActions.money = 1500;
    assetActions.fullAuthoritySnapshotValid = true;
    assetActions.authoritySnapshotValid = true;
    assetActions.authorityAssetCount = 28;
    assetActions.availableActions = 0xFFFFFFFFu;
    assetActions.selectedAsset = 0;
    assetActions.authorityAssets[0].ownerId = 1;
    assetActions.authorityAssets[1].ownerId = 1;
    uint8_t ownedInGroup = 0;
    uint8_t totalInGroup = 0;
    ok &= expect(out,
                 appAssetGroupProgress(assetActions, 0, ownedInGroup, totalInGroup) &&
                     ownedInGroup == 2 && totalInGroup == 2 &&
                     appAssetDetailActionCount(assetActions) == 4 &&
                     appAssetDetailActionAt(assetActions, 0) ==
                         AssetDetailAction::MortgageOrRedeem &&
                     appAssetDetailActionAt(assetActions, 1) == AssetDetailAction::Build &&
                     appAssetDetailActionAt(assetActions, 2) ==
                         AssetDetailAction::SellBuilding &&
                     appAssetDetailActionAt(assetActions, 3) == AssetDetailAction::Trade &&
                     appAssetDetailActionVisible(assetActions,
                                                 AssetDetailAction::MortgageOrRedeem) &&
                     appAssetDetailActionVisible(assetActions, AssetDetailAction::Build) &&
                     !appAssetDetailActionVisible(assetActions,
                                                  AssetDetailAction::SellBuilding) &&
                     appAssetDetailActionVisible(assetActions, AssetDetailAction::Trade) &&
                     appAssetDetailActionEnabled(assetActions, AssetDetailAction::Build) &&
                     appAssetDetailActionEnabled(assetActions, AssetDetailAction::Trade),
                 "asset actions retain fixed Mortgage Build Sell Trade semantic slots");
    assetActions.authorityAssets[0].buildingLevel = 1;
    assetActions.authorityAssets[1].buildingLevel = 1;
    ok &= expect(out,
                 appAssetDetailActionCount(assetActions) == 4 &&
                     appAssetDetailActionAt(assetActions, 2) ==
                         AssetDetailAction::SellBuilding &&
                     !appAssetDetailActionEnabled(assetActions,
                                                  AssetDetailAction::MortgageOrRedeem) &&
                     appAssetDetailActionVisible(assetActions,
                                                 AssetDetailAction::SellBuilding) &&
                     appAssetDetailActionEnabled(assetActions,
                                                 AssetDetailAction::SellBuilding) &&
                     appAssetDetailActionVisible(assetActions, AssetDetailAction::Trade) &&
                     !appAssetDetailActionEnabled(assetActions, AssetDetailAction::Trade),
                 "buildings reveal fixed Sell slot and disable mortgage/trade until clear");
    assetActions.authorityAssets[0].buildingLevel = 0;
    assetActions.authorityAssets[1].buildingLevel = 0;
    assetActions.authorityAssets[1].ownerId = 2;
    ok &= expect(out,
                 appAssetGroupProgress(assetActions, 0, ownedInGroup, totalInGroup) &&
                     ownedInGroup == 1 && totalInGroup == 2 &&
                     appAssetDetailActionCount(assetActions) == 4 &&
                     !appAssetDetailActionVisible(assetActions, AssetDetailAction::Build) &&
                     !appAssetDetailActionVisible(assetActions,
                                                  AssetDetailAction::SellBuilding) &&
                     appAssetDetailActionAt(assetActions, 3) == AssetDetailAction::Trade &&
                     appAssetDetailActionVisible(assetActions, AssetDetailAction::Trade),
                 "partial group hides Build and Sell without moving Trade from slot three");
    assetActions.nav.current = NavigationEntry{ScreenPage::AssetDetail, 0, 0};
    appHandleInput(assetActions, InputEvent{InputKind::Rotate, 1, 10}, 10);
    ok &= expect(out, assetActions.nav.current.focus == 3,
                 "asset rotary focus skips hidden Build and Sell slots to fixed Trade");
    appHandleInput(assetActions, InputEvent{InputKind::Rotate, 1, 20}, 20);
    ok &= expect(out, assetActions.nav.current.focus == 4 &&
                      appFocusIsFooter(assetActions),
                 "asset rotary focus reaches Back after the final visible fixed slot");
    appHandleInput(assetActions, InputEvent{InputKind::Rotate, -1, 30}, 30);
    ok &= expect(out, assetActions.nav.current.focus == 3,
                 "asset rotary reverse skips hidden slots symmetrically");
    assetActions.authorityAssets[1].ownerId = 1;
    assetActions.authorityAssets[0].flags = 1;
    ok &= expect(out,
                 appAssetDetailActionCount(assetActions) == 4 &&
                     appAssetDetailActionAt(assetActions, 0) ==
                         AssetDetailAction::MortgageOrRedeem &&
                     appAssetDetailActionAt(assetActions, 1) == AssetDetailAction::Build &&
                     appAssetDetailActionAt(assetActions, 3) == AssetDetailAction::Trade &&
                     appAssetDetailActionVisible(assetActions, AssetDetailAction::Build) &&
                     !appAssetDetailActionVisible(assetActions, AssetDetailAction::Trade) &&
                     !appTradeAssetEligible(assetActions, 0),
                 "mortgaged property hides fixed Trade slot without moving Build");

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
                 "Trade Browse rotation moves from Receiver to Give Assets without mutating values");
    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 130}, 130);
    shortPress(state, 140, 200);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      state.tradeReceiverPickerOpen &&
                      state.tradeReceiverPickerIndex == 0 &&
                      state.tradeReceiver == initialTradeReceiver &&
                      state.commandCount == 0,
                 "short press opens the Receiver picker without mutating the offer");
    const uint8_t receiverCandidateCount = appTradeReceiverCandidateCount(state);
    appHandleInput(
        state,
        InputEvent{InputKind::Rotate, static_cast<int16_t>(receiverCandidateCount), 205},
        205
    );
    ok &= expect(out, state.tradeReceiverPickerOpen &&
                      state.tradeReceiverPickerIndex == receiverCandidateCount,
                 "Receiver picker rotation reaches the fixed Back control");
    shortPress(state, 206, 208);
    ok &= expect(out, !state.tradeReceiverPickerOpen &&
                      state.tradeReceiver == initialTradeReceiver,
                 "pressing focused Receiver Back closes without changing the target");
    shortPress(state, 209, 210);
    ok &= expect(out, state.tradeReceiverPickerOpen &&
                      state.tradeReceiverPickerIndex == 0,
                 "Receiver picker reopens on the current target after Back");
    const int32_t receiverEditAmount = state.tradeAmount;
    appHandleInput(state, InputEvent{InputKind::Rotate, 2, 210}, 210);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      state.tradeReceiver == initialTradeReceiver &&
                      state.tradeReceiverPickerIndex == 2 &&
                      state.tradeAmount == receiverEditAmount &&
                      state.tradeReceiverPickerOpen &&
                      state.commandCount == 0,
                 "Receiver picker rotates its highlight without editing the draft");
    shortPress(state, 220, 280);
    const uint8_t committedReceiver = state.tradeReceiver;
    ok &= expect(out, !state.tradeReceiverPickerOpen &&
                      committedReceiver != initialTradeReceiver &&
                      state.commandCount == 0,
                 "short press commits the highlighted Receiver and closes the picker");

    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 290}, 290);
    shortPress(state, 300, 350);
    ok &= expect(out, state.nav.current.page == ScreenPage::TradeAssetSelect &&
                      state.tradeAssetListIndex == 0 && state.tradeGiveAssetMask == 0,
                 "Give Assets opens a scrollable multi-select page");
    shortPress(state, 360, 410);
    const uint8_t firstTradeAsset = appTradeAssetIndex(state, 0);
    ok &= expect(out, state.nav.current.page == ScreenPage::TradeAssetSelect &&
                      appTradeAssetSelected(state, firstTradeAsset),
                 "press toggles the focused eligible trade asset on");
    shortPress(state, 420, 470);
    ok &= expect(out, !appTradeAssetSelected(state, firstTradeAsset),
                 "pressing the same trade asset again toggles it off");
    shortPress(state, 480, 530);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 540);
    ok &= expect(out, state.nav.current.page == ScreenPage::Trade &&
                      state.nav.current.focus == 1 &&
                      appTradeAssetSelected(state, firstTradeAsset),
                 "Back returns to the same Give Assets field with the draft preserved");
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 550}, 550);
    appHandleTouch(state, TouchAction::TradeAmount, 560);
    ok &= expect(out, appInlineFieldEditing(state, InlineEditField::TradeAmount),
                 "tap on focused Amount enters Editing");
    const uint8_t amountEditReceiver = state.tradeReceiver;
    appHandleInput(state, InputEvent{InputKind::Rotate, 20, 570}, 570);
    ok &= expect(out, state.nav.current.focus == 2 && state.tradeAmount == 1000 &&
                      state.tradeReceiver == amountEditReceiver &&
                      appInlineFieldEditing(state, InlineEditField::TradeAmount),
                 "Amount Editing applies 50-dollar steps and keeps focus fixed");
    appHandleInput(state, InputEvent{InputKind::Rotate, -40, 580}, 580);
    ok &= expect(out, state.tradeAmount == 0 && state.tradeReceiver == amountEditReceiver,
                 "Amount Editing clamps at zero");
    appHandleTouch(state, TouchAction::TradeAmount, 590);
    ok &= expect(out, state.inlineEditField == InlineEditField::None &&
                      state.tradeAmount == 0 && state.commandCount == 0,
                 "second Amount tap commits locally without transport");

    appHandleTouch(state, TouchAction::TradeAmount, 340);
    const uint8_t beforeDifferentFieldReceiver = state.tradeReceiver;
    const int32_t beforeDifferentFieldAmount = state.tradeAmount;
    appHandleTouch(state, TouchAction::TradeReceiver, 350);
    ok &= expect(out, state.nav.current.focus == 0 &&
                      state.inlineEditField == InlineEditField::None &&
                      state.tradeReceiverPickerOpen &&
                      state.tradeReceiver == beforeDifferentFieldReceiver &&
                      state.tradeAmount == beforeDifferentFieldAmount,
                 "Receiver tap exits Amount editing and opens the top-level picker");
    const uint8_t receiverBeforeCancel = state.tradeReceiver;
    appHandleTouch(state, TouchAction::TradeReceiverCancel, 360);
    ok &= expect(out, !state.tradeReceiverPickerOpen &&
                      state.tradeReceiver == receiverBeforeCancel,
                 "Receiver picker cancellation preserves the existing target");
    appHandleTouch(state, TouchAction::TradeConfirm, 370);
    ok &= expect(out, state.nav.current.focus == 3 &&
                      state.inlineEditField == InlineEditField::None &&
                      state.modal.kind == ModalKind::None && state.commandCount == 0,
                 "first Submit tap moves unified focus without opening a modal");
    appHandleTouch(state, TouchAction::TradeConfirm, 380);
    ok &= expect(out, state.modal.kind == ModalKind::TradeCreate && state.commandCount == 0,
                 "separate Submit activation opens TradeCreate without queuing transport");

    appInit(state, 0);
    state.selectedPlayer = 3;
    state.playerDetail.loadState = PlayerDetailLoadState::Ready;
    state.playerDetail.playerId = 4;
    state.nav.current = NavigationEntry{ScreenPage::PlayerDetail, 2, 0};
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 400);
    ok &= expect(out, state.nav.current.page == ScreenPage::Trade &&
                      state.tradeEntryMode == TradeEntryMode::PlayerLocked &&
                      appTradeReceiverLocked(state) && state.tradeReceiver == 3 &&
                      state.nav.current.focus == 0 &&
                      state.inlineEditField == InlineEditField::None &&
                      appPageContentCount(state) == 3,
                 "Player Detail Trade locks selected Receiver and starts at Give Assets");
    const uint8_t lockedReceiver = state.tradeReceiver;
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 410}, 410);
    ok &= expect(out, state.nav.current.focus == 1 && state.tradeReceiver == lockedReceiver &&
                      state.inlineEditField == InlineEditField::None,
                 "locked Trade rotary browses from Give Assets to Amount without Receiver mutation");
    shortPress(state, 430, 480);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 490}, 490);
    const int32_t lockedEditedAmount = state.tradeAmount;
    appHandleTouch(state, TouchAction::TradeReceiver, 500);
    ok &= expect(out, state.nav.current.focus == 1 && state.tradeReceiver == lockedReceiver &&
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
    state.nav.current = NavigationEntry{ScreenPage::Trade, 2, 0};
    appHandleTouch(state, TouchAction::TradeAmount, 600);
    static TransportEvent editingSnapshot{TransportEventKind::StateSnapshotApplied};
    editingSnapshot.stateVersion = 2;
    editingSnapshot.cash = state.money;
    editingSnapshot.playerPosition = state.position;
    appHandleTransportEvent(state, editingSnapshot, 610);
    ok &= expect(out, state.inlineEditField == InlineEditField::None,
                 "authoritative snapshot clears inline Editing");
    appHandleTouch(state, TouchAction::TradeAmount, 620);
    static TransportEvent editingAuthorityEvent{TransportEventKind::RollResult};
    editingAuthorityEvent.stateVersion = 3;
    appHandleTransportEvent(state, editingAuthorityEvent, 630);
    ok &= expect(out, state.inlineEditField == InlineEditField::None,
                 "authoritative event clears inline Editing");

    appHandleTouch(state, TouchAction::TradeAmount, 640);
    static TransportEvent forcedReplacement{TransportEventKind::DebtResolutionRequired};
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
    ok &= expect(out, state.nav.current.page == ScreenPage::DiceStage &&
                      appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::RollRequest,
                 "my turn centered Dice starts animation and queues the roll action");

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
    state.playerDetail.loadState = PlayerDetailLoadState::Ready;
    state.playerDetail.playerId = 3;
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
    state.nav.current = NavigationEntry{ScreenPage::Home, 0, 0};
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
    state.selectedAsset = 3;
    state.nav.current = NavigationEntry{ScreenPage::AssetDetail, 0, 0};
    appHandleTouch(state, TouchAction::DetailPrimary, 110);
    ok &= expect(out, state.modal.kind == ModalKind::VoluntaryUnmortgage &&
                      state.modal.cancelAllowed && state.modal.amount == 110 &&
                      !appPollCommand(state, command),
                 "redeem opens a cancellable 110-percent confirmation without submitting");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 120}, 120);
    appTick(state, 1319);
    ok &= expect(out, !appPollCommand(state, command) && !state.modal.submitting,
                 "redeem hold does not submit before 1.2 seconds");
    appTick(state, 1320);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1320}, 1320);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::UnmortgageRequest &&
                      command.assetIndex == 3 && state.modal.submitting,
                 "redeem hold submits one authoritative unmortgage request");
    static TransportEvent redeemCompleted{};
    redeemCompleted = TransportEvent{};
    redeemCompleted.kind = TransportEventKind::CommandCompleted;
    redeemCompleted.requestId = command.requestId + 1;
    appHandleTransportEvent(state, redeemCompleted, 1330);
    ok &= expect(out, state.modal.kind == ModalKind::VoluntaryUnmortgage &&
                      state.modal.submitting,
                 "unrelated completion cannot dismiss redeem confirmation");
    redeemCompleted.requestId = command.requestId;
    appHandleTransportEvent(state, redeemCompleted, 1340);
    ok &= expect(out, state.modal.kind == ModalKind::None &&
                      strcmp(state.toast, "Property redeemed") == 0,
                 "matching authority completion dismisses redeem confirmation once");

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
    static TransportEvent payment{TransportEventKind::PaymentRequired};
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
    payment.deadlineMs = 14000;
    appHandleTransportEvent(state, payment, 5750);
    ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                      state.modal.submitting &&
                      state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::PayNow)] != 0,
                 "repeated payment presentation preserves the active PayNow submission");
    static TransportEvent insufficient{TransportEventKind::CommandRejected};
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
    static TransportEvent completed{TransportEventKind::PaymentCompleted};
    completed.requestId = command.requestId;
    completed.transactionId = 90;
    completed.cash = 240;
    appHandleTransportEvent(state, completed, 13100);
    ok &= expect(out, state.money == initialCash &&
                      state.modal.kind == ModalKind::ForcedPayment &&
                      state.modal.insufficient,
                 "completion after rejected payment cannot bypass debt resolution");

    appInit(state, 0);
    payment.transactionId = 91;
    payment.amount = 5;
    payment.deadlineMs = 0;
    appHandleTransportEvent(state, payment, 100);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 200}, 200);
    appTick(state, 1400);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 1400}, 1400);
    static TransportCommand orphanedPayNow{};
    orphanedPayNow = TransportCommand{};
    ok &= expect(out, appPollCommand(state, orphanedPayNow) &&
                      orphanedPayNow.kind == TransportCommandKind::PayNow,
                 "orphan recovery fixture starts with one submitted PayNow request");
    state.modal.submitting = false;
    appTick(state, 1401);
    ok &= expect(out,
                 state.pendingRequestIds[static_cast<uint8_t>(TransportCommandKind::PayNow)] == 0 &&
                     strcmp(state.toast, "PAYMENT READY - HOLD TO RETRY") == 0,
                 "orphaned PayNow lock is cleared when the modal is visibly retryable");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1500}, 1500);
    appTick(state, 2700);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2700}, 2700);
    static TransportCommand retriedPayNow{};
    retriedPayNow = TransportCommand{};
    ok &= expect(out, appPollCommand(state, retriedPayNow) &&
                      retriedPayNow.kind == TransportCommandKind::PayNow &&
                      retriedPayNow.requestId != orphanedPayNow.requestId,
                 "recovered payment modal can submit a fresh PayNow request");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    shortPress(state, 1, 5);
    appHandleTransportEvent(state, TransportEvent{TransportEventKind::ConnectionLost}, 10);
    shortPress(state, 20, 100);
    ok &= expect(out, !state.authorityOnline && state.pendingCommandMask == 0 &&
                      state.commandCount == 0 && !appPollCommand(state, command),
                 "connection loss clears queued request locks and blocks dangerous commands");
    state.modal.kind = ModalKind::TradeCreate;
    state.modal.submitting = true;
    static TransportEvent snapshot{TransportEventKind::StateSnapshotApplied};
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

    static TransportEvent stale{TransportEventKind::PaymentCompleted};
    stale.stateVersion = 8;
    stale.cash = 1;
    appHandleTransportEvent(state, stale, 210);
    ok &= expect(out, state.money == 1775 && state.stateVersion == 9,
                 "lower state version is ignored");
    static TransportEvent newer{TransportEventKind::PaymentCompleted};
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
    static TransportEvent event{};
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
    static TransportEvent unsolicitedBankruptcy{};
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
    static TransportEvent mortgageCompleted{};
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

    static AppState buildingDebt{};
    appInit(buildingDebt, 0);
    buildingDebt.boardSize = 40;
    buildingDebt.selfSeatId = 1;
    buildingDebt.fullAuthoritySnapshotValid = true;
    buildingDebt.authoritySnapshotValid = true;
    buildingDebt.authorityAssetCount = 28;
    buildingDebt.availableActions = (1u << 5) | (1u << 8);
    buildingDebt.authorityAssets[0].ownerId = 1;
    buildingDebt.authorityAssets[1].ownerId = 1;
    buildingDebt.authorityAssets[0].buildingLevel = 2;
    buildingDebt.authorityAssets[1].buildingLevel = 1;
    buildingDebt.money = 240;
    buildingDebt.debtAmount = 680;
    buildingDebt.debt = DebtState{};
    buildingDebt.debt.transactionId = 90;
    buildingDebt.debt.amountDue = 680;
    buildingDebt.debt.cashBefore = 240;
    buildingDebt.debt.eligibleMask = 0x0FFFFFFFu;
    buildingDebt.nav.current = NavigationEntry{ScreenPage::DebtAssets, 0, 0};
    buildingDebt.debtListIndex = 0;
    const int32_t firstSaleProceeds = appDebtBuildingSaleProceeds(buildingDebt, 0);
    ok &= expect(out, appDebtBuildingSaleEligible(buildingDebt, 0) &&
                      !appDebtBuildingSaleEligible(buildingDebt, 1) &&
                      firstSaleProceeds > 0,
                 "debt exposes only an evenly sellable highest building");
    shortPress(buildingDebt, 2600, 2700);
    ok &= expect(out,
                 buildingDebt.modal.kind == ModalKind::DebtSellBuildingConfirm &&
                     !buildingDebt.modal.cancelAllowed &&
                     buildingDebt.modal.amount == firstSaleProceeds,
                 "debt building row opens non-cancelable proceeds confirmation");
    appHandleInput(buildingDebt, InputEvent{InputKind::ButtonDown, 0, 2800}, 2800);
    appTick(buildingDebt, 4000);
    appHandleInput(buildingDebt, InputEvent{InputKind::ButtonUp, 0, 4000}, 4000);
    TransportCommand buildingSaleCommand{};
    ok &= expect(out, appPollCommand(buildingDebt, buildingSaleCommand) &&
                      buildingSaleCommand.kind == TransportCommandKind::SellBuildingRequest &&
                      buildingSaleCommand.transactionId == 90 &&
                      buildingSaleCommand.assetIndex == 0,
                 "debt building hold queues one authoritative sale request");
    buildingDebt.authorityAssets[0].buildingLevel = 1;
    buildingDebt.money += firstSaleProceeds;
    buildingDebt.stateVersion = 2;
    static TransportEvent buildingSold{};
    buildingSold = TransportEvent{};
    buildingSold.kind = TransportEventKind::CommandCompleted;
    buildingSold.requestId = buildingSaleCommand.requestId;
    buildingSold.stateVersion = 2;
    appHandleTransportEvent(buildingDebt, buildingSold, 4010);
    ok &= expect(out, buildingDebt.modal.kind == ModalKind::None &&
                      buildingDebt.nav.current.page == ScreenPage::DebtAssets &&
                      strcmp(buildingDebt.toast, "BUILDING SOLD") == 0,
                 "completed building sale returns to the same forced funding flow");

    shortPress(buildingDebt, 4100, 4200);
    appHandleInput(buildingDebt, InputEvent{InputKind::ButtonDown, 0, 4300}, 4300);
    appTick(buildingDebt, 5500);
    appHandleInput(buildingDebt, InputEvent{InputKind::ButtonUp, 0, 5500}, 5500);
    TransportCommand finalBuildingSale{};
    ok &= expect(out, appPollCommand(buildingDebt, finalBuildingSale) &&
                      finalBuildingSale.kind == TransportCommandKind::SellBuildingRequest,
                 "next evenly sellable building can be liquidated without leaving debt flow");
    buildingDebt.authorityAssets[0].buildingLevel = 0;
    buildingDebt.money = 700;
    buildingDebt.availableActions = 1u << 11;
    buildingDebt.stateVersion = 3;
    buildingSold.requestId = finalBuildingSale.requestId;
    buildingSold.stateVersion = 3;
    appHandleTransportEvent(buildingDebt, buildingSold, 5510);
    ok &= expect(out, buildingDebt.modal.kind == ModalKind::ForcedPayment &&
                      buildingDebt.modal.transactionId == 3 &&
                      buildingDebt.modal.amount == 680,
                 "sale completion opens forced payment when authority exposes PayDebt");

    openDebtFixture(state, /*amountDue=*/500, /*cash=*/240, /*eligibleMask=*/0,
                    /*availableActions=*/1u << 12);
    ok &= expect(out, state.nav.current.page == ScreenPage::Bankruptcy && !appCanNavigateBack(state) &&
                      appFocusCount(state) == 1 && !state.debt.bankruptcyPending,
                  "authority-only insolvency enters bankruptcy page without locking sync");
    shortPress(state, 5600, 5700);
    TransportCommand bankruptcyCommand{};
    ok &= expect(out, appPollCommand(state, bankruptcyCommand) &&
                      bankruptcyCommand.kind == TransportCommandKind::DeclareBankruptcyRequest &&
                      state.debt.bankruptcyPending,
                 "bankruptcy becomes terminal only after the player submits it");
    static AppState pendingBankruptcy{};
    pendingBankruptcy = state;
    static TransportEvent wrongBankruptcy{};
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
    static TransportEvent unrelatedBankruptcyEvent{};
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
        static TransportEvent eventAfterBankruptcy{};
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
    static TransportEvent voluntaryCompleted{};
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
    static TransportEvent refreshedDebt{};
    refreshedDebt.kind = TransportEventKind::DebtResolutionRequired;
    refreshedDebt.transactionId = 90;
    refreshedDebt.stateVersion = 3;
    refreshedDebt.amount = 680;
    refreshedDebt.cash = 240;
    refreshedDebt.assetMask = 0x54;
    refreshedDebt.availableActions = 1u << 5;
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

bool runAuctionLifecycleTests(Stream &out)
{
    bool ok = true;
    static AppState state{};
    appInit(state, 0);

    const gridopoly::core::BoardDefinition *board =
        gridopoly::core::BoardCatalog::findBySize(24);
    const uint32_t boardHash = board == nullptr ? 0 : gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(board->id), strlen(board->id));

    auto stateEvent = [](uint32_t roomId, uint32_t version, AuthorityPhase phase,
                         uint8_t assetIndex, uint32_t actions) {
        TransportEvent event{};
        event.kind = TransportEventKind::StateSnapshotApplied;
        event.roomId = roomId;
        event.resync = true;
        event.stateVersion = version;
        event.phase = phase;
        event.selfSeatId = 1;
        event.activePlayerId = 1;
        event.decisionPlayerId = phase == AuthorityPhase::AwaitAuction ? 0 : 1;
        event.playerCount = 2;
        event.boardSize = 24;
        event.cash = 1200;
        event.playerPosition = 0;
        event.pendingTarget = 0xFF;
        event.tileAssetIndex = 0xFF;
        event.debtAssetIndex = 0xFF;
        event.auctionAssetIndex = assetIndex;
        event.auctionMinimumBid = 10;
        event.availableActions = actions;
        for (uint8_t index = 0; index < 2; ++index) {
            event.players[index].playerId = static_cast<uint8_t>(index + 1);
            event.players[index].cash = 1200;
            event.players[index].flags = 0x04;
        }
        return event;
    };
    auto fullEvent = [board, boardHash](uint32_t roomId, uint32_t version,
                                        uint32_t generation, uint8_t assetIndex,
                                        bool opening) {
        TransportEvent event{};
        event.kind = TransportEventKind::AuthoritySnapshotApplied;
        event.roomId = roomId;
        event.resync = true;
        event.stateVersion = version;
        event.phase = AuthorityPhase::AwaitAuction;
        event.activePlayerId = 1;
        event.decisionPlayerId = opening ? 0 : 1;
        event.playerCount = 2;
        event.boardSize = 24;
        event.assetCount = board == nullptr ? 0 : board->assetCount;
        event.boardIdHash = boardHash;
        event.auctionFlags = opening ? 0x03u : 0x01u;
        event.auctionAssetIndex = assetIndex;
        event.auctionReadyMask = opening ? 0 : 0x03u;
        event.auctionRequiredReadyMask = 0x03u;
        event.auctionCurrentBid = 10;
        event.auctionGeneration = generation;
        for (uint8_t index = 0; index < 2; ++index) {
            event.players[index].playerId = static_cast<uint8_t>(index + 1);
            event.players[index].cash = 1200;
            event.players[index].flags = 0x04;
        }
        return event;
    };

    uint8_t introCount = 0;
    AuctionPresentationPhase observed = AuctionPresentationPhase::None;
    auto observePresentation = [&]() {
        if (state.auctionPresentation == AuctionPresentationPhase::Intro &&
            observed != AuctionPresentationPhase::Intro) {
            ++introCount;
        }
        observed = state.auctionPresentation;
    };

    TransportEvent compact = stateEvent(101, 10, AuthorityPhase::AwaitAuction, 6,
                                        1u << 15);
    appHandleTransportEvent(state, compact, 90);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::None &&
                      state.nav.current.page == ScreenPage::Home && introCount == 0,
                 "compact auction state waits for a generation-bearing authority snapshot");

    TransportEvent authority = fullEvent(101, 10, 77, 6, true);
    appHandleTransportEvent(state, authority, 100);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Intro &&
                      state.nav.current.page == ScreenPage::Auction && introCount == 1 &&
                      state.seenAuctionGeneration == 77 &&
                      state.seenAuctionAssetIndex == 6,
                 "first full auction key presents exactly one introduction");

    appNotifyFramePresented(state, 101);
    TransportCommand ready{};
    ok &= expect(out, appPollCommand(state, ready) &&
                      ready.kind == TransportCommandKind::AuctionReadyRequest &&
                      static_cast<uint32_t>(ready.argument) == 77,
                 "the rendered introduction sends one generation-bound ready");

    appHandleTransportEvent(state, authority, 1000);
    observePresentation();
    compact.stateVersion = 11;
    appHandleTransportEvent(state, compact, 1001);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Intro &&
                      introCount == 1,
                 "same-key authority and compact resync at 900ms preserve the running intro");
    appTick(state, 1899);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Intro,
                 "auction introduction remains visible until its local deadline");
    appTick(state, 1900);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Live &&
                      appAuctionOpening(state) && appFocusCount(state) == 0 && introCount == 1,
                 "introduction enters the disabled live-ready page at 1800ms without OpeningWait");

    appNotifyFramePresented(state, 1901);
    TransportCommand resyncReady{};
    ok &= expect(out, appPollCommand(state, resyncReady) &&
                      resyncReady.kind == TransportCommandKind::AuctionReadyRequest &&
                      static_cast<uint32_t>(resyncReady.argument) == 77 &&
                      resyncReady.requestId != ready.requestId,
                 "same-key resync retries a missing Ready without replaying the intro");

    appHandleTransportEvent(state, TransportEvent{TransportEventKind::ConnectionLost}, 2000);
    appHandleTransportEvent(state, authority, 2001);
    observePresentation();
    appHandleTransportEvent(state, compact, 2002);
    observePresentation();
    appNotifyFramePresented(state, 2003);
    TransportCommand recoveredReady{};
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Live &&
                      introCount == 1 && appPollCommand(state, recoveredReady) &&
                      recoveredReady.kind == TransportCommandKind::AuctionReadyRequest &&
                      recoveredReady.requestId != resyncReady.requestId,
                 "reconnect and reversed projection order restore live and idempotently resend ready");

    compact.availableActions = (1u << 13) | (1u << 14);
    compact.decisionPlayerId = 1;
    compact.stateVersion = 12;
    appHandleTransportEvent(state, compact, 2100);
    authority = fullEvent(101, 12, 77, 6, false);
    appHandleTransportEvent(state, authority, 2101);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Live &&
                      !appAuctionOpening(state) && appFocusCount(state) == 2 &&
                      introCount == 1,
                  "last ready opens BID and PASS in place on the existing live page");

    TransportEvent staleSettled = stateEvent(101, 11, AuthorityPhase::TurnEnd, 0xFF,
                                              1u << 4);
    appHandleTransportEvent(state, staleSettled, 2150);
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Live &&
                      state.authorityPhase == AuthorityPhase::AwaitAuction &&
                      state.auctionGeneration == 77 && state.auctionAssetIndex == 6,
                 "same-room lower-version resync cannot settle or roll back a live auction");

    TransportEvent settled = stateEvent(101, 13, AuthorityPhase::TurnEnd, 0xFF,
                                        1u << 4);
    settled.decisionPlayerId = 1;
    appHandleTransportEvent(state, settled, 2200);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Result &&
                      state.completedAuctionGeneration == 77 && introCount == 1,
                 "auction settlement records a terminal generation before showing its result");

    compact.stateVersion = 12;
    appHandleTransportEvent(state, compact, 2201);
    TransportEvent completedReplay = fullEvent(101, 14, 77, 6, true);
    appHandleTransportEvent(state, completedReplay, 2202);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Result &&
                      state.authorityPhase == AuthorityPhase::TurnEnd &&
                      state.auctionGeneration == 77 && state.auctionAssetIndex == 0xFF &&
                      introCount == 1,
                 "completed-generation authority is rejected before it can roll back state");
    appTick(state, 5200);
    observePresentation();
    appHandleTransportEvent(state, completedReplay, 5201);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::None &&
                      state.nav.current.page == ScreenPage::Home &&
                      state.authorityPhase == AuthorityPhase::TurnEnd &&
                      state.auctionGeneration == 77 && state.auctionAssetIndex == 0xFF &&
                      introCount == 1,
                 "a completed generation stays closed after its result page exits");

    TransportEvent nextCompact = stateEvent(101, 14, AuthorityPhase::AwaitAuction, 7,
                                            1u << 15);
    appHandleTransportEvent(state, nextCompact, 5300);
    appHandleTransportEvent(state, fullEvent(101, 14, 78, 7, true), 5301);
    observePresentation();
    ok &= expect(out, state.auctionPresentation == AuctionPresentationPhase::Intro &&
                      state.seenAuctionGeneration == 78 && introCount == 2,
                 "a newer generation receives its own single introduction");

    appHandleTransportEvent(state, fullEvent(101, 13, 77, 6, true), 5302);
    observePresentation();
    ok &= expect(out, state.auctionGeneration == 78 &&
                      state.auctionAssetIndex == 7 && introCount == 2,
                 "a late older generation is ignored after a newer auction begins");

    TransportEvent newRoomCompact = stateEvent(202, 1, AuthorityPhase::AwaitAuction, 2,
                                               1u << 15);
    appHandleTransportEvent(state, newRoomCompact, 5400);
    observePresentation();
    appHandleTransportEvent(state, fullEvent(202, 1, 1, 2, true), 5401);
    observePresentation();
    ok &= expect(out, state.authorityRoomId == 202 && state.stateVersion == 1 &&
                      state.auctionPresentation == AuctionPresentationPhase::Intro &&
                      state.seenAuctionGeneration == 1 &&
                      state.seenAuctionAssetIndex == 2 && introCount == 3,
                 "a new room may restart at a lower version and clears auction history");
    return ok;
}

bool runIdentityLifecycleTests(Stream &out)
{
    bool ok = true;
    ok &= expect(out,
                 !transportEventAdvancesAppliedStateVersion(
                     TransportEventKind::AuthoritySnapshotApplied, true) &&
                     transportEventAdvancesAppliedStateVersion(
                         TransportEventKind::IdentitySnapshotReceived, true) &&
                     transportEventAdvancesAppliedStateVersion(
                         TransportEventKind::AuthoritySnapshotApplied, false),
                 "identity setup only advances applied state from IdentitySnapshot");
    ok &= expect(out,
                 identitySnapshotCompletesPendingOperation(
                     TransportIdentityOperation::ConfirmAvatar, 1, 0x01u, 0) &&
                     identitySnapshotCompletesPendingOperation(
                         TransportIdentityOperation::ConfirmName, 2, 0, 0x02u) &&
                     !identitySnapshotCompletesPendingOperation(
                         TransportIdentityOperation::ConfirmAvatar, 2, 0x01u, 0),
                 "requestId-zero identity completion is bound to operation and self seat");
    uint8_t hairVector[4]{100, 120, 140, 173};
    avatarTintHairPixel(hairVector, AvatarComponentRgb{176, 83, 43});
    uint8_t skinVector[4]{190, 140, 100, 211};
    avatarTintSkinPixel(skinVector, AvatarComponentRgb{202, 141, 82});
    uint8_t blendVector[4]{10, 30, 50, 128};
    const uint8_t blendSource[4]{200, 100, 20, 96};
    avatarSourceOver(blendVector, blendSource);
    ok &= expect(out,
                 hairVector[0] == 147 && hairVector[1] == 69 &&
                     hairVector[2] == 36 && hairVector[3] == 173 &&
                     skinVector[0] == 167 && skinVector[1] == 117 &&
                     skinVector[2] == 68 && skinVector[3] == 211 &&
                     blendVector[0] == 114 && blendVector[1] == 68 &&
                     blendVector[2] == 34 && blendVector[3] == 176,
                 "avatar tint and source-over match canonical integer vectors");
    AppState state{};
    appInit(state, 0);

    TransportAvatarRecipe invalidRecipe{};
    invalidRecipe.catalogVersion = 0;
    invalidRecipe.hairPresetId = 0;
    invalidRecipe.hairColorId = 21;
    invalidRecipe.facePresetId = 11;
    invalidRecipe.skinToneId = 0;
    invalidRecipe.outfitPresetId = 255;
    const TransportAvatarRecipe normalizedRecipe =
        normalizedTransportAvatarRecipe(invalidRecipe);
    ok &= expect(out,
                 normalizedRecipe.catalogVersion == 1 &&
                     normalizedRecipe.hairPresetId == 1 &&
                     normalizedRecipe.hairColorId == 1 &&
                     normalizedRecipe.facePresetId == 1 &&
                     normalizedRecipe.skinToneId == 1 &&
                     normalizedRecipe.outfitPresetId == 1,
                 "unset or out-of-range avatar fields normalize to preset one");

    TransportIdentityPayload payload{};
    for (uint8_t index = 0; index < 3; ++index) {
        payload.seats[index].playerId = static_cast<uint8_t>(index + 1);
        payload.seats[index].seatRevision = 1;
        payload.seats[index].avatarRevision = index == 0 ? 0 : 1;
        payload.seats[index].recipe = TransportAvatarRecipe{};
        payload.seats[index].flags = index == 0 ? 0x01u : 0x1Fu;
    }

    auto identityEvent = [&](uint32_t roomId, uint32_t version,
                             TransportIdentityPhase phase) {
        TransportEvent event{};
        event.kind = TransportEventKind::IdentitySnapshotReceived;
        event.roomId = roomId;
        event.stateVersion = version;
        event.selfSeatId = 1;
        event.identityPhase = phase;
        event.identityResult = TransportIdentityResult::Ok;
        event.identityRevision = version;
        event.identitySeatCount = 3;
        event.identityHumanMask = 0x01u;
        event.identityAvatarReadyMask = 0x06u;
        event.identityNameReadyMask = 0x06u;
        event.identityReadyMask = 0x06u;
        event.identityOnlineMask = 0x07u;
        event.identity = &payload;
        return event;
    };

    TransportEvent avatar = identityEvent(500, 3, TransportIdentityPhase::AwaitAvatar);
    appHandleTransportEvent(state, avatar, 100);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::AvatarLoading &&
                     state.identity.phase == IdentityClientPhase::AvatarLoading &&
                     state.identity.readyMask == 0x06u &&
                     state.identity.draftRecipe.hairPresetId == 1,
                 "new human room prepares the complete avatar library before editing");
    appUpdateAvatarPreloadProgress(state, 12, 30, false);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::AvatarLoading &&
                     state.identity.avatarPreloadReadyCount == 12,
                 "avatar preparation reports partial component progress");
    appUpdateAvatarPreloadProgress(state, 30, 30, true);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::AvatarSetup &&
                     state.identity.phase == IdentityClientPhase::AvatarEditing &&
                     state.identity.avatarAssetsReady,
                 "avatar editing opens after all component files are cached");

    AppState completedEdgeLost = state;
    completedEdgeLost.nav.current.page = ScreenPage::AvatarLoading;
    completedEdgeLost.identity.phase = IdentityClientPhase::AvatarSubmitting;
    completedEdgeLost.identity.avatarAssetsReady = true;
    appUpdateAvatarPreloadProgress(completedEdgeLost, 30, 30, true);
    ok &= expect(out,
                 completedEdgeLost.nav.current.page == ScreenPage::AvatarSetup &&
                     completedEdgeLost.identity.phase == IdentityClientPhase::AvatarEditing,
                 "completed preload heals a lost completion edge after identity resync");

    shortPress(state, 110, 180);
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 190}, 190);
    ok &= expect(out,
                 state.identity.editingValue &&
                     state.identity.draftRecipe.hairPresetId == 2,
                 "avatar row press enters edit mode and wheel changes the live recipe");
    shortPress(state, 200, 270);
    state.nav.current.focus = static_cast<uint8_t>(AvatarEditField::Confirm);
    state.focus = state.nav.current.focus;
    shortPress(state, 280, 350);

    TransportCommand avatarCommand{};
    ok &= expect(out,
                 appPollCommand(state, avatarCommand) &&
                     avatarCommand.kind == TransportCommandKind::IdentityRequest &&
                     avatarCommand.identityOperation ==
                         TransportIdentityOperation::ConfirmAvatar &&
                     avatarCommand.stateVersion == 3 &&
                     avatarCommand.identitySeatRevision == 1 &&
                     avatarCommand.avatarRecipe.hairPresetId == 2,
                 "avatar confirm carries recipe plus exact game and seat revisions");

    payload.seats[0].seatRevision = 2;
    payload.seats[0].avatarRevision = 1;
    payload.seats[0].recipe = avatarCommand.avatarRecipe;
    TransportEvent name = identityEvent(500, 4, TransportIdentityPhase::AwaitName);
    // Reproduce an async completion after the immediate request response was
    // lost: the authoritative completion is requestId zero and must still
    // clear the local SAVING state.
    name.requestId = 0;
    name.identityAvatarReadyMask = 0x07u;
    appHandleTransportEvent(state, name, 400);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::NameReview &&
                     state.identity.phase == IdentityClientPhase::NameReview &&
                     state.identity.pendingRequestId == 0 &&
                     (state.pendingCommandMask &
                      (1u << static_cast<uint8_t>(TransportCommandKind::IdentityRequest))) == 0,
                 "accepted avatar advances to the separate name review stage");

    AppState intentionalBack = state;
    intentionalBack.nav.current.focus = 2;
    intentionalBack.focus = 2;
    shortPress(intentionalBack, 401, 450);
    appTick(intentionalBack, 451);
    ok &= expect(out,
                 intentionalBack.nav.current.page == ScreenPage::AvatarSetup &&
                     intentionalBack.identity.phase == IdentityClientPhase::AvatarEditing &&
                     intentionalBack.identity.avatarEditOverride,
                 "an intentional Name Review back action keeps the warm avatar editor open");

    AppState coldBack = state;
    coldBack.identity.avatarAssetsReady = false;
    coldBack.nav.current.focus = 2;
    coldBack.focus = 2;
    shortPress(coldBack, 401, 450);
    appTick(coldBack, 451);
    ok &= expect(out,
                 coldBack.nav.current.page == ScreenPage::AvatarLoading &&
                     coldBack.identity.phase == IdentityClientPhase::AvatarLoading &&
                     coldBack.identity.avatarEditOverride,
                 "a cold Name Review back action prepares all avatar components first");
    TransportEvent coldBackSync = identityEvent(
        500, 4, TransportIdentityPhase::AwaitName);
    coldBackSync.identityAvatarReadyMask = 0x07u;
    appHandleTransportEvent(coldBack, coldBackSync, 452);
    ok &= expect(out,
                 coldBack.nav.current.page == ScreenPage::AvatarLoading &&
                     coldBack.identity.avatarEditOverride,
                 "authoritative NameSetup cannot interrupt an intentional cold preload");
    appUpdateAvatarPreloadProgress(coldBack, 30, 30, true);
    ok &= expect(out,
                 coldBack.nav.current.page == ScreenPage::AvatarSetup &&
                     coldBack.identity.phase == IdentityClientPhase::AvatarEditing,
                 "cold edit navigation opens only after preload completion");

    // A reconnect may restore the authoritative per-seat stage before the
    // aggregate ready mask is observed locally. NameSetup is itself proof
    // that avatar generation finished, so it must evict a stale preview page
    // and any orphaned ConfirmAvatar request.
    AppState recovered{};
    appInit(recovered, 0);
    recovered.authorityRoomId = 500;
    recovered.stateVersion = 3;
    recovered.selfSeatId = 1;
    recovered.identity.phase = IdentityClientPhase::AvatarEditing;
    recovered.identity.pendingRequestId = 77;
    recovered.pendingCommandMask |=
        1u << static_cast<uint8_t>(TransportCommandKind::IdentityRequest);
    recovered.pendingRequestIds[static_cast<uint8_t>(
        TransportCommandKind::IdentityRequest)] = 77;
    recovered.nav.current = NavigationEntry{ScreenPage::AvatarSetup, 0, 0};
    recovered.page = ScreenPage::AvatarSetup;
    TransportEvent recoveredName = identityEvent(
        500, 4, TransportIdentityPhase::AwaitName);
    recoveredName.identityAvatarReadyMask = 0x06u;
    recoveredName.requestId = 0;
    appHandleTransportEvent(recovered, recoveredName, 405);
    ok &= expect(out,
                 recovered.nav.current.page == ScreenPage::NameReview &&
                     recovered.page == ScreenPage::NameReview &&
                     recovered.identity.phase == IdentityClientPhase::NameReview &&
                     recovered.identity.pendingRequestId == 0 &&
                     (recovered.pendingCommandMask &
                      (1u << static_cast<uint8_t>(
                          TransportCommandKind::IdentityRequest))) == 0,
                 "authoritative NameSetup evicts a stale avatar loading page");

    // The async final-avatar projection is authoritative per seat. Its raw
    // self stage and seat flags must advance the UI even if an aggregate mask
    // from the same delivery is stale.
    AppState seatFinalRecovered{};
    appInit(seatFinalRecovered, 0);
    seatFinalRecovered.authorityRoomId = 500;
    seatFinalRecovered.stateVersion = 3;
    seatFinalRecovered.selfSeatId = 1;
    seatFinalRecovered.identity.phase = IdentityClientPhase::AvatarSubmitting;
    seatFinalRecovered.identity.pendingRequestId = 88;
    seatFinalRecovered.pendingCommandMask |=
        1u << static_cast<uint8_t>(TransportCommandKind::IdentityRequest);
    seatFinalRecovered.pendingRequestIds[static_cast<uint8_t>(
        TransportCommandKind::IdentityRequest)] = 88;
    seatFinalRecovered.nav.current = NavigationEntry{ScreenPage::AvatarSetup, 0, 0};
    seatFinalRecovered.page = ScreenPage::AvatarSetup;
    TransportEvent seatFinalName = identityEvent(
        500, 4, TransportIdentityPhase::AwaitAvatar);
    seatFinalName.identityAvatarReadyMask = 0x06u;
    seatFinalName.identitySelfStage = static_cast<uint8_t>(
        gridopoly::protocol::IdentitySeatStage::NameSetup);
    payload.seats[0].flags = static_cast<uint8_t>(
        gridopoly::protocol::IdentitySeatPresent |
        gridopoly::protocol::IdentitySeatHuman |
        gridopoly::protocol::IdentitySeatAvatarFinal |
        gridopoly::protocol::IdentitySeatConnected);
    appHandleTransportEvent(seatFinalRecovered, seatFinalName, 405);
    ok &= expect(out,
                 seatFinalRecovered.nav.current.page == ScreenPage::NameReview &&
                     seatFinalRecovered.page == ScreenPage::NameReview &&
                     seatFinalRecovered.identity.phase == IdentityClientPhase::NameReview &&
                     seatFinalRecovered.identity.pendingRequestId == 0,
                 "per-seat NameSetup evicts loading even when aggregate mask is stale");
    payload.seats[0].flags = 0x01u;

    // If a later local lifecycle update retained the submitting page after the
    // authoritative seat was stored, the regular tick must self-heal without
    // affecting the intentional Name Review -> Avatar Setup back path.
    AppState retainedSubmitting = seatFinalRecovered;
    retainedSubmitting.identity.phase = IdentityClientPhase::AvatarSubmitting;
    retainedSubmitting.identity.pendingRequestId = 89;
    retainedSubmitting.pendingCommandMask |=
        1u << static_cast<uint8_t>(TransportCommandKind::IdentityRequest);
    retainedSubmitting.pendingRequestIds[static_cast<uint8_t>(
        TransportCommandKind::IdentityRequest)] = 89;
    retainedSubmitting.nav.current = NavigationEntry{ScreenPage::AvatarSetup, 0, 0};
    retainedSubmitting.page = ScreenPage::AvatarSetup;
    retainedSubmitting.identity.seats[0].flags = static_cast<uint8_t>(
        gridopoly::protocol::IdentitySeatPresent |
        gridopoly::protocol::IdentitySeatHuman |
        gridopoly::protocol::IdentitySeatAvatarFinal |
        gridopoly::protocol::IdentitySeatConnected);
    appTick(retainedSubmitting, 406);
    ok &= expect(out,
                 retainedSubmitting.nav.current.page == ScreenPage::NameReview &&
                     retainedSubmitting.page == ScreenPage::NameReview &&
                     retainedSubmitting.identity.phase == IdentityClientPhase::NameReview &&
                     retainedSubmitting.identity.pendingRequestId == 0,
                 "tick heals a retained submitting page after avatar final");

    TransportEvent recoveredGameplay{};
    recoveredGameplay.kind = TransportEventKind::StateSnapshotApplied;
    recoveredGameplay.roomId = 500;
    recoveredGameplay.stateVersion = 4;
    recoveredGameplay.selfSeatId = 1;
    recoveredGameplay.playerCount = 3;
    recoveredGameplay.resync = true;
    appHandleTransportEvent(recovered, recoveredGameplay, 406);
    ok &= expect(out,
                 recovered.nav.current.page == ScreenPage::NameReview &&
                     recovered.page == ScreenPage::NameReview,
                 "gameplay projections cannot restore the stale avatar page after NameSetup");

    ok &= expect(out,
                 appIdentityAppendCharacter(state, 'A') &&
                     appIdentityAppendCharacter(state, 'l') &&
                     appIdentityAppendCharacter(state, 'e') &&
                     appIdentityAppendCharacter(state, 'x') &&
                     strcmp(state.identity.draftName, "ALEX") == 0,
                 "handwriting reducer normalizes a bounded player name");
    state.nav.current.focus = 1;
    state.focus = 1;
    shortPress(state, 410, 480);

    TransportCommand nameCommand{};
    ok &= expect(out,
                 appPollCommand(state, nameCommand) &&
                     nameCommand.kind == TransportCommandKind::IdentityRequest &&
                     nameCommand.identityOperation == TransportIdentityOperation::ConfirmName &&
                     nameCommand.stateVersion == 4 &&
                     nameCommand.identitySeatRevision == 2 &&
                     strcmp(nameCommand.identityName, "ALEX") == 0,
                 "name confirm submits the reviewed text without a fourth ready action");

    payload.seats[0].seatRevision = 3;
    TransportEvent ready = identityEvent(500, 5, TransportIdentityPhase::Ready);
    ready.requestId = nameCommand.requestId;
    ready.identityAvatarReadyMask = 0x07u;
    ready.identityNameReadyMask = 0x07u;
    ready.identityReadyMask = 0x07u;
    appHandleTransportEvent(state, ready, 500);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::PlayerReady &&
                     state.identity.phase == IdentityClientPhase::Ready &&
                     appIdentityReadyCount(state) == 3,
                 "all confirmed seats appear together on the ready stage");

    TransportEvent countdown = ready;
    countdown.requestId = 0;
    countdown.stateVersion = 6;
    countdown.identityPhase = TransportIdentityPhase::Countdown;
    countdown.deadlineMs = 6000;
    appHandleTransportEvent(state, countdown, 1000);
    const uint32_t firstDeadline = state.identity.countdownDeadlineMs;
    countdown.deadlineMs = 6000;
    appHandleTransportEvent(state, countdown, 1500);
    ok &= expect(out,
                 state.identity.phase == IdentityClientPhase::Countdown &&
                     firstDeadline == 6000 && state.identity.countdownDeadlineMs == 6000 &&
                     appIdentityCountdownRemainingMs(state, 1500) == 4500,
                 "replayed countdown snapshots preserve one server-owned five second deadline");

    TransportEvent ordinaryResync{};
    ordinaryResync.kind = TransportEventKind::StateSnapshotApplied;
    ordinaryResync.roomId = 500;
    ordinaryResync.stateVersion = 7;
    ordinaryResync.selfSeatId = 1;
    ordinaryResync.playerCount = 3;
    ordinaryResync.resync = true;
    appHandleTransportEvent(state, ordinaryResync, 1600);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::PlayerReady &&
                     state.identity.phase == IdentityClientPhase::Countdown,
                 "ordinary projection resync cannot eject the identity countdown page");

    TransportEvent disconnected{};
    disconnected.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(state, disconnected, 1700);
    ok &= expect(out,
                 state.nav.current.page == ScreenPage::PlayerReady &&
                     strcmp(state.identity.draftName, "ALEX") == 0,
                 "connection loss keeps the local identity draft and ready presentation");

    payload.seats[0] = TransportIdentitySeat{};
    payload.seats[0].playerId = 1;
    payload.seats[0].seatRevision = 1;
    payload.seats[0].recipe.catalogVersion = 0;
    payload.seats[0].recipe.hairPresetId = 0;
    payload.seats[0].recipe.hairColorId = 0;
    payload.seats[0].recipe.facePresetId = 0;
    payload.seats[0].recipe.skinToneId = 0;
    payload.seats[0].recipe.outfitPresetId = 0;
    TransportEvent nextRoom = identityEvent(501, 1, TransportIdentityPhase::AwaitAvatar);
    appHandleTransportEvent(state, nextRoom, 1800);
    ok &= expect(out,
                 state.authorityRoomId == 501 &&
                     state.stateVersion == 1 &&
                     state.nav.current.page == ScreenPage::AvatarLoading &&
                     state.identity.phase == IdentityClientPhase::AvatarLoading &&
                     !state.identity.avatarAssetsReady &&
                     state.identity.draftName[0] == '\0' &&
                     state.identity.draftRecipe.hairPresetId == 1 &&
                     state.identity.draftRecipe.hairColorId == 1 &&
                     state.identity.draftRecipe.facePresetId == 1 &&
                     state.identity.draftRecipe.skinToneId == 1 &&
                     state.identity.draftRecipe.outfitPresetId == 1,
                 "a new room resets cache readiness and gives an unset seat a valid default avatar");

    TransportEvent active = nextRoom;
    active.stateVersion = 2;
    active.identityPhase = TransportIdentityPhase::Active;
    active.identityAvatarReadyMask = 0x07u;
    active.identityNameReadyMask = 0x07u;
    active.identityReadyMask = 0x07u;
    appHandleTransportEvent(state, active, 1900);
    ok &= expect(out,
                 state.identity.phase == IdentityClientPhase::Inactive &&
                     state.nav.current.page == ScreenPage::Home,
                 "server Active phase releases every console into gameplay");
    return ok;
}

bool runPureLogicTests(Stream &out)
{
    bool ok = runTransportEventCursorTests(out);
    ok &= expect(out, uiHandwritingNeuralTestAccuracy() >= 0.87f,
                 "quantized EMNIST neural model clears its accuracy gate");
    ok &= expect(out, uiHandwritingSamplesShouldConnect(100, 200),
                 "handwriting joins samples at the 100 ms boundary");
    ok &= expect(out, !uiHandwritingSamplesShouldConnect(100, 201),
                 "handwriting starts a new stroke after a 100 ms sample gap");
    ok &= expect(out, !uiHandwritingSamplesShouldConnect(false, 100, 150),
                 "a physical pen lift starts a new stroke even inside 100 ms");
    ok &= expect(out, uiHandwritingSamplesShouldConnect(true, 100, 200),
                 "one physical contact joins samples at the 100 ms boundary");
    ok &= expect(out,
                 uiHandwritingSamplesShouldConnect(UINT32_MAX - 49u, 50u),
                 "handwriting stroke timing remains correct across millis wrap");
    ok &= expect(out, !uiHandwritingRecognitionDue(false, 0, 100, 5000),
                 "handwriting idle timer ignores an empty canvas");
    ok &= expect(out, !uiHandwritingRecognitionDue(true, 12, 100, 5000),
                 "any canvas touch blocks handwriting recognition");
    ok &= expect(out, !uiHandwritingRecognitionDue(false, 12, 800, 1999),
                 "the canvas waits the complete 1.2 second idle window");
    ok &= expect(out, uiHandwritingRecognitionDue(false, 12, 800, 2000),
                 "handwriting recognizes only after 1.2 seconds without touch");
    ok &= expect(out,
                 uiHandwritingRecognitionDue(false, 12,
                                             UINT32_MAX - 500u, 699u),
                 "handwriting idle timing remains correct across millis wrap");
    constexpr UiHandwritingSample kLetterR[] = {
        {0, 100, 0, false}, {0, 0, 40, true},
        {55, 0, 80, false}, {80, 15, 120, true}, {80, 40, 160, true},
        {55, 52, 200, true}, {0, 52, 240, true},
        {42, 52, 280, false}, {85, 100, 320, true},
    };
    constexpr UiHandwritingSample kLetterK[] = {
        {0, 0, 0, false}, {0, 100, 40, true},
        {80, 0, 80, false}, {0, 52, 120, true},
        {80, 100, 160, false}, {0, 52, 200, true},
    };
    constexpr UiHandwritingSample kLetterT[] = {
        {0, 0, 0, false}, {100, 0, 40, true},
        {50, 0, 80, false}, {50, 100, 120, true},
    };
    constexpr UiHandwritingSample kLetterY[] = {
        {0, 0, 0, false}, {50, 50, 40, true},
        {100, 0, 80, false}, {50, 50, 120, true}, {50, 100, 160, true},
    };
    ok &= expect(out,
                 uiHandwritingRecognizeSamples(
                     kLetterR, sizeof(kLetterR) / sizeof(kLetterR[0])) == 'R' &&
                 uiHandwritingRecognizeSamples(
                     kLetterK, sizeof(kLetterK) / sizeof(kLetterK[0])) == 'K',
                 "handwriting preserves the loop/diagonal distinction between R and K");
    ok &= expect(out,
                 uiHandwritingRecognizeSamples(
                     kLetterY, sizeof(kLetterY) / sizeof(kLetterY[0])) == 'Y' &&
                 uiHandwritingRecognizeSamples(
                     kLetterT, sizeof(kLetterT) / sizeof(kLetterT[0])) == 'T',
                 "handwriting preserves diagonal Y arms versus the horizontal T bar");
    ok &= runRemoteArtworkCatalogTests(out);
    ok &= runRemoteTileCachePolicyTests(out);
    ok &= runStateLogicTests(out);
    ok &= runModalAuthorityTests(out);
    ok &= runTradeLifecycleTests(out);
    ok &= runAuctionLifecycleTests(out);
    ok &= runIdentityLifecycleTests(out);
    static gridopoly::protocol::StateSnapshot snapshot{};
    snapshot = gridopoly::protocol::StateSnapshot{};
    snapshot.seatId = 1;
    snapshot.phase = 1;
    snapshot.activePlayerId = 3;
    snapshot.round = 4;
    snapshot.boardSize = 32;
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
    static TransportEvent authorityEvent{};
    ok &= expect(out, authoritySnapshotToEvent(snapshot, true, authorityEvent) &&
                      authorityEvent.kind == TransportEventKind::StateSnapshotApplied &&
                      authorityEvent.playerCount == 5 && authorityEvent.resync,
                 "server snapshot maps to one bounded console event");
    static AppState connected{};
    appInit(connected, 0);
    appHandleTransportEvent(connected, authorityEvent, 10);
    ok &= expect(out, connected.authorityOnline && connected.authoritySnapshotValid &&
                      connected.selfSeatId == 1 && connected.money == 1860 &&
                      connected.position == 17 && connected.turnsUntilYou == 3 &&
                      connected.homePhase == HomePhase::Waiting &&
                      connected.authorityPlayers[4].cash == 1600,
                 "five-player authority snapshot drives global console state");

    static gridopoly::protocol::StateSnapshot delayedSnapshot{};
    delayedSnapshot = snapshot;
    delayedSnapshot.phase = 1;
    delayedSnapshot.activePlayerId = 1;
    delayedSnapshot.decisionPlayerId = 1;
    delayedSnapshot.availableActions = 1;
    delayedSnapshot.stateVersion = 78;
    static AppState delayedRoll{};
    appInit(delayedRoll, 0);
    authoritySnapshotToEvent(delayedSnapshot, true, authorityEvent);
    appHandleTransportEvent(delayedRoll, authorityEvent, 100);
    appHandleUiEvent(delayedRoll, UiEvent{UiEventKind::SelectHomeAction, 0}, 200);
    appTick(delayedRoll, 5000);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::DiceStage &&
                      delayedRoll.rollAnimating && !delayedRoll.rollResolved,
                 "server latency keeps the local dice loop visible indefinitely");
    delayedSnapshot.phase = 2;
    delayedSnapshot.pendingTarget = 24;
    delayedSnapshot.availableActions = 1u << 1;
    delayedSnapshot.stateVersion = 79;
    authoritySnapshotToEvent(delayedSnapshot, false, authorityEvent);
    appHandleTransportEvent(delayedRoll, authorityEvent, 5000);
    TransportEvent delayedDiceEvent{};
    delayedDiceEvent.kind = TransportEventKind::GameEventReceived;
    delayedDiceEvent.stateVersion = 79;
    delayedDiceEvent.gameEvent.sequence = 1;
    delayedDiceEvent.gameEvent.kind = 3;
    delayedDiceEvent.gameEvent.actorId = 1;
    delayedDiceEvent.gameEvent.amount = 7;
    delayedDiceEvent.gameEvent.detail = 3u | (4u << 8);
    appHandleTransportEvent(delayedRoll, delayedDiceEvent, 5000);
    ok &= expect(out, delayedRoll.rollStartedMs == 200 &&
                      appDiceResultVisible(delayedRoll, 5000),
                 "the exact late dice event settles the existing timeline without restarting it");
    appTick(delayedRoll, 5000);
    const uint32_t settledRollStartedMs = delayedRoll.rollStartedMs;
    const uint32_t settledRollRevealMs = delayedRoll.rollRevealMs;
    delayedRoll.authorityRoomId = 303;
    authoritySnapshotToEvent(delayedSnapshot, true, authorityEvent);
    authorityEvent.roomId = 303;
    appHandleTransportEvent(delayedRoll, authorityEvent, 5100);
    appHandleTransportEvent(delayedRoll, authorityEvent, 5200);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::DiceStage &&
                      delayedRoll.rollResolved && delayedRoll.rollRevealPresented &&
                      delayedRoll.rollStartedMs == settledRollStartedMs &&
                      delayedRoll.rollRevealMs == settledRollRevealMs &&
                      delayedRoll.rolledSteps == 7 && delayedRoll.rollTarget == 24,
                 "same pending-move resyncs preserve one settled dice presentation timeline");

    static AppState heldRoll{};
    appInit(heldRoll, 0);
    static gridopoly::protocol::StateSnapshot heldStartSnapshot{};
    heldStartSnapshot = delayedSnapshot;
    heldStartSnapshot.phase = 1;
    heldStartSnapshot.activePlayerId = 1;
    heldStartSnapshot.decisionPlayerId = 1;
    heldStartSnapshot.boardSize = 16;
    heldStartSnapshot.selfPosition = 4;
    heldStartSnapshot.pendingTarget = 0xFF;
    heldStartSnapshot.availableActions = 1u;
    heldStartSnapshot.stateVersion = 78;
    heldStartSnapshot.playerCount = 1;
    heldStartSnapshot.players[0].playerId = 1;
    heldStartSnapshot.players[0].position = 4;
    heldStartSnapshot.players[0].flags = 0x01;
    authoritySnapshotToEvent(heldStartSnapshot, false, authorityEvent);
    appHandleTransportEvent(heldRoll, authorityEvent, 10);
    appHandleUiEvent(heldRoll, UiEvent{UiEventKind::SelectHomeAction, 0}, 100);
    TransportCommand heldRollCommand{};
    ok &= expect(out, heldRoll.nav.current.page == ScreenPage::DiceStage &&
                      appPollCommand(heldRoll, heldRollCommand) &&
                      heldRollCommand.kind == TransportCommandKind::RollRequest,
                 "a held player starts the same authoritative dice presentation");

    TransportEvent heldRollResult{};
    heldRollResult.kind = TransportEventKind::RollResult;
    heldRollResult.requestId = heldRollCommand.requestId;
    heldRollResult.stateVersion = 79;
    heldRollResult.dieA = 4;
    heldRollResult.dieB = 5;
    heldRollResult.playerPosition = 4;
    appHandleTransportEvent(heldRoll, heldRollResult, 300);

    static gridopoly::protocol::StateSnapshot heldTurnEndSnapshot{};
    heldTurnEndSnapshot = delayedSnapshot;
    heldTurnEndSnapshot.phase = 6;
    heldTurnEndSnapshot.activePlayerId = 1;
    heldTurnEndSnapshot.decisionPlayerId = 1;
    heldTurnEndSnapshot.boardSize = 16;
    heldTurnEndSnapshot.selfPosition = 4;
    heldTurnEndSnapshot.pendingTarget = 0xFF;
    heldTurnEndSnapshot.availableActions = 1u << 4;
    heldTurnEndSnapshot.stateVersion = 80;
    heldTurnEndSnapshot.playerCount = 1;
    heldTurnEndSnapshot.players[0].playerId = 1;
    heldTurnEndSnapshot.players[0].position = 4;
    heldTurnEndSnapshot.players[0].flags = 0x01;
    authoritySnapshotToEvent(heldTurnEndSnapshot, false, authorityEvent);
    appHandleTransportEvent(heldRoll, authorityEvent, 400);
    appTick(heldRoll, 2699);
    ok &= expect(out, heldRoll.nav.current.page == ScreenPage::DiceStage &&
                      heldRoll.rollResolved && heldRoll.rollTarget == 0xFF &&
                      heldRoll.rolledSteps == 9,
                 "a failed hold roll keeps its result readable without inventing a move target");
    appTick(heldRoll, 2700);
    ok &= expect(out, heldRoll.nav.current.page == ScreenPage::Home &&
                      heldRoll.homePhase == HomePhase::MyTurnEnd &&
                      !heldRoll.rollAnimating && !heldRoll.moveArrivalPending,
                 "a no-move hold roll exits to End Turn after the full dice result hold");

    static AppState releaseHoldRoll{};
    appInit(releaseHoldRoll, 0);
    authoritySnapshotToEvent(heldStartSnapshot, false, authorityEvent);
    appHandleTransportEvent(releaseHoldRoll, authorityEvent, 10);
    appHandleUiEvent(releaseHoldRoll, UiEvent{UiEventKind::SelectHomeAction, 0}, 100);
    TransportCommand releaseHoldCommand{};
    ok &= expect(out, appPollCommand(releaseHoldRoll, releaseHoldCommand) &&
                      releaseHoldCommand.kind == TransportCommandKind::RollRequest,
                 "third hold attempt starts one authoritative roll transaction");

    TransportEvent releaseHoldDice{};
    releaseHoldDice.kind = TransportEventKind::GameEventReceived;
    releaseHoldDice.stateVersion = 79;
    releaseHoldDice.gameEvent.sequence = 1;
    releaseHoldDice.gameEvent.kind = 3;
    releaseHoldDice.gameEvent.actorId = 1;
    releaseHoldDice.gameEvent.amount = 10;
    releaseHoldDice.gameEvent.detail = 4u | (6u << 8);
    appHandleTransportEvent(releaseHoldRoll, releaseHoldDice, 300);

    static gridopoly::protocol::StateSnapshot releaseDebtState{};
    releaseDebtState = heldStartSnapshot;
    releaseDebtState.phase = 5;
    releaseDebtState.decisionPlayerId = 1;
    releaseDebtState.availableActions = 1u << 11;
    releaseDebtState.debtAmount = 20;
    releaseDebtState.debtCreditorId = 0;
    releaseDebtState.stateVersion = 80;
    authoritySnapshotToEvent(releaseDebtState, false, authorityEvent);
    appHandleTransportEvent(releaseHoldRoll, authorityEvent, 400);

    static gridopoly::protocol::AuthoritySnapshot releaseDebtAuthority{};
    releaseDebtAuthority.phase = 5;
    releaseDebtAuthority.activePlayerId = 1;
    releaseDebtAuthority.decisionPlayerId = 1;
    releaseDebtAuthority.boardSize = 16;
    releaseDebtAuthority.playerCount = 1;
    releaseDebtAuthority.assetCount = 0;
    releaseDebtAuthority.stateVersion = 80;
    const gridopoly::core::BoardDefinition *releaseBoard =
        gridopoly::core::BoardCatalog::findBySize(16);
    releaseDebtAuthority.boardIdHash = releaseBoard == nullptr ? 0 :
        gridopoly::protocol::crc32(
            reinterpret_cast<const uint8_t *>(releaseBoard->id),
            strlen(releaseBoard->id));
    releaseDebtAuthority.players[0].playerId = 1;
    releaseDebtAuthority.players[0].position = 4;
    releaseDebtAuthority.players[0].cash = 241;
    releaseDebtAuthority.players[0].flags = 0x01;
    releaseDebtAuthority.debtFlags = 1;
    releaseDebtAuthority.debtDebtorId = 1;
    releaseDebtAuthority.debtCreditorId = 0;
    releaseDebtAuthority.debtAssetIndex = 0xFF;
    releaseDebtAuthority.debtPaymentEvent =
        static_cast<uint8_t>(gridopoly::core::EventKind::FeePaid);
    releaseDebtAuthority.debtContinuation =
        static_cast<uint8_t>(gridopoly::core::DebtContinuation::ReleaseHoldAndMove);
    releaseDebtAuthority.debtDieA = 4;
    releaseDebtAuthority.debtDieB = 6;
    releaseDebtAuthority.debtAmount = 20;
    TransportEvent releaseDebtFullEvent{};
    ok &= expect(out, fullAuthoritySnapshotToEvent(
                          releaseDebtAuthority, false, releaseDebtFullEvent),
                 "hold-release debt authority maps to the console event");
    appHandleTransportEvent(releaseHoldRoll, releaseDebtFullEvent, 401);

    TransportEvent conflictingRollCompletion{};
    conflictingRollCompletion.kind = TransportEventKind::RollResult;
    conflictingRollCompletion.requestId = releaseHoldCommand.requestId;
    conflictingRollCompletion.stateVersion = 80;
    conflictingRollCompletion.dieA = 5;
    conflictingRollCompletion.dieB = 5;
    conflictingRollCompletion.playerPosition = 4;
    appHandleTransportEvent(releaseHoldRoll, conflictingRollCompletion, 500);
    ok &= expect(out, releaseHoldRoll.dieA == 4 && releaseHoldRoll.dieB == 6 &&
                      releaseHoldRoll.rolledSteps == 10,
                 "later roll completion cannot replace the first exact dice pair");

    appTick(releaseHoldRoll, 2699);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::DiceStage &&
                      releaseHoldRoll.modal.kind == ModalKind::None,
                 "release payment waits until the complete dice presentation finishes");
    appTick(releaseHoldRoll, 2700);
    ok &= expect(out, releaseHoldRoll.modal.kind == ModalKind::ForcedPayment &&
                      releaseHoldRoll.rollPresentationComplete &&
                      strcmp(releaseHoldRoll.modal.title, "RELEASE FEE") == 0 &&
                      strcmp(releaseHoldRoll.modal.purpose, "RELEASE FROM HOLD") == 0 &&
                      releaseHoldRoll.modal.amount == 20,
                 "the pre-move hold fee is identified separately from landing debt");

    static gridopoly::protocol::StateSnapshot releaseMoveState{};
    releaseMoveState = releaseDebtState;
    releaseMoveState.phase = 2;
    releaseMoveState.pendingTarget = 14;
    releaseMoveState.availableActions = 1u << 1;
    releaseMoveState.debtAmount = 0;
    releaseMoveState.stateVersion = 81;
    authoritySnapshotToEvent(releaseMoveState, false, authorityEvent);
    appHandleTransportEvent(releaseHoldRoll, authorityEvent, 2800);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::MoveGuide &&
                      !releaseHoldRoll.rollAnimating &&
                      releaseHoldRoll.rollTarget == 14 &&
                      releaseHoldRoll.modal.kind == ModalKind::None,
                 "paying the release fee advances directly to movement without replaying dice");

    releaseDebtAuthority.phase = 2;
    releaseDebtAuthority.stateVersion = 81;
    releaseDebtAuthority.debtFlags = 0;
    releaseDebtAuthority.debtContinuation = 0;
    releaseDebtAuthority.debtAmount = 0;
    releaseDebtAuthority.pendingMoveFlags = 1;
    releaseDebtAuthority.pendingMovePlayerId = 1;
    releaseDebtAuthority.pendingMoveOrigin = 4;
    releaseDebtAuthority.pendingMoveTarget = 14;
    releaseDebtAuthority.pendingMoveDieA = 4;
    releaseDebtAuthority.pendingMoveDieB = 6;
    fullAuthoritySnapshotToEvent(releaseDebtAuthority, false, releaseDebtFullEvent);
    appHandleTransportEvent(releaseHoldRoll, releaseDebtFullEvent, 2801);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::MoveGuide &&
                      releaseHoldRoll.dieA == 4 && releaseHoldRoll.dieB == 6,
                 "pending movement keeps the original authoritative dice pair");

    TransportEvent releaseArrival{};
    releaseArrival.kind = TransportEventKind::RfidPositionConfirmed;
    releaseArrival.stateVersion = 81;
    releaseArrival.targetPosition = 14;
    releaseArrival.observedPosition = 14;
    appHandleTransportEvent(releaseHoldRoll, releaseArrival, 2900);

    static gridopoly::protocol::StateSnapshot landingDebtState{};
    landingDebtState = releaseMoveState;
    landingDebtState.phase = 5;
    landingDebtState.pendingTarget = 0xFF;
    landingDebtState.selfPosition = 14;
    landingDebtState.players[0].position = 14;
    landingDebtState.availableActions = 1u << 11;
    landingDebtState.debtAmount = 70;
    landingDebtState.stateVersion = 82;
    authoritySnapshotToEvent(landingDebtState, false, authorityEvent);
    appHandleTransportEvent(releaseHoldRoll, authorityEvent, 3000);
    releaseDebtAuthority.phase = 5;
    releaseDebtAuthority.stateVersion = 82;
    releaseDebtAuthority.players[0].position = 14;
    releaseDebtAuthority.pendingMoveFlags = 0;
    releaseDebtAuthority.debtFlags = 1;
    releaseDebtAuthority.debtDebtorId = 1;
    releaseDebtAuthority.debtPaymentEvent =
        static_cast<uint8_t>(gridopoly::core::EventKind::FeePaid);
    releaseDebtAuthority.debtContinuation =
        static_cast<uint8_t>(gridopoly::core::DebtContinuation::FinishLanding);
    releaseDebtAuthority.debtDieA = 0;
    releaseDebtAuthority.debtDieB = 0;
    releaseDebtAuthority.debtAmount = 70;
    fullAuthoritySnapshotToEvent(releaseDebtAuthority, false, releaseDebtFullEvent);
    appHandleTransportEvent(releaseHoldRoll, releaseDebtFullEvent, 3001);
    appTick(releaseHoldRoll, 4099);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::MoveGuide &&
                      releaseHoldRoll.modal.kind == ModalKind::None,
                 "landing debt remains behind the confirmed-arrival checkpoint");
    appTick(releaseHoldRoll, 4100);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::TileEvent &&
                      releaseHoldRoll.modal.kind == ModalKind::None &&
                      !releaseHoldRoll.landingEventAcknowledged &&
                      releaseHoldRoll.debtAmount == 70,
                 "the destination fee first explains the landing event");

    TransportEvent landingDebtResync{};
    ok &= expect(out, fullAuthoritySnapshotToEvent(
                          releaseDebtAuthority, true, landingDebtResync),
                 "landing debt resync maps to the console event");
    appHandleTransportEvent(releaseHoldRoll, landingDebtResync, 4101);
    ok &= expect(out, releaseHoldRoll.nav.current.page == ScreenPage::TileEvent &&
                      releaseHoldRoll.modal.kind == ModalKind::None &&
                      !releaseHoldRoll.landingEventAcknowledged,
                 "same-room debt resync neither skips nor replays the landing explanation");

    appHandleUiEvent(releaseHoldRoll, UiEvent{UiEventKind::ActivateFocused, 0}, 4110);
    ok &= expect(out, releaseHoldRoll.landingEventAcknowledged &&
                      releaseHoldRoll.modal.kind == ModalKind::ForcedPayment &&
                      strcmp(releaseHoldRoll.modal.purpose, "CITY FEE") == 0 &&
                      releaseHoldRoll.modal.amount == 70,
                 "continuing the destination fee explanation opens forced payment once");
    appHandleTransportEvent(releaseHoldRoll, landingDebtResync, 4111);
    ok &= expect(out, releaseHoldRoll.modal.kind == ModalKind::ForcedPayment &&
                      releaseHoldRoll.landingEventAcknowledged,
                 "same landing debt keeps payment above the acknowledged explanation");

    static AppState rentLanding{};
    appInit(rentLanding, 0);
    rentLanding.boardSize = 16;
    rentLanding.selfSeatId = 1;
    rentLanding.activePlayerId = 1;
    rentLanding.decisionPlayerId = 1;
    rentLanding.authorityPhase = AuthorityPhase::AwaitDebt;
    rentLanding.availableActions = 1u << 11;
    rentLanding.stateVersion = 90;
    rentLanding.rollTarget = 1;
    rentLanding.rolledSteps = 4;
    rentLanding.moveArrivalPending = true;
    rentLanding.moveArrivalConfirmed = true;
    rentLanding.arrivalContinueAtMs = 100;
    rentLanding.debtCreditorId = 2;
    rentLanding.debtPaymentEvent =
        static_cast<uint8_t>(gridopoly::core::EventKind::RentPaid);
    rentLanding.debtAmount = 35;
    rentLanding.money = 410;
    rentLanding.rosterSnapshotValid = true;
    strcpy(rentLanding.rosterNames[1], "Bot 1");
    rentLanding.nav.current.page = ScreenPage::MoveGuide;
    appTick(rentLanding, 100);
    ok &= expect(out, rentLanding.nav.current.page == ScreenPage::TileEvent &&
                      rentLanding.modal.kind == ModalKind::None,
                 "rent first opens the landing explanation instead of payment");
    appHandleUiEvent(rentLanding, UiEvent{UiEventKind::ActivateFocused, 0}, 110);
    ok &= expect(out, rentLanding.modal.kind == ModalKind::ForcedPayment &&
                      strcmp(rentLanding.modal.counterparty, "Bot 1") == 0 &&
                      strcmp(rentLanding.modal.purpose, "PAY RENT") == 0 &&
                      rentLanding.modal.amount == 35,
                 "rent explanation continues to the named-owner payment modal");

    static AppState earlyDebt{};
    earlyDebt = delayedRoll;
    static gridopoly::protocol::StateSnapshot earlyDebtSnapshot{};
    earlyDebtSnapshot = delayedSnapshot;
    earlyDebtSnapshot.phase = 5;
    earlyDebtSnapshot.decisionPlayerId = 1;
    earlyDebtSnapshot.pendingTarget = 0xFF;
    earlyDebtSnapshot.debtCreditorId = 0;
    earlyDebtSnapshot.debtAmount = 680;
    earlyDebtSnapshot.availableActions = 1u << 11;
    earlyDebtSnapshot.stateVersion = 80;
    authoritySnapshotToEvent(earlyDebtSnapshot, false, authorityEvent);
    appHandleTransportEvent(earlyDebt, authorityEvent, 5100);
    ok &= expect(out, earlyDebt.nav.current.page == ScreenPage::DiceStage &&
                      earlyDebt.modal.kind == ModalKind::None,
                 "early forced payment cannot cover the active dice presentation");
    appTick(earlyDebt, 5700);
    ok &= expect(out, earlyDebt.nav.current.page == ScreenPage::MoveGuide &&
                      earlyDebt.modal.kind == ModalKind::None,
                 "forced payment remains deferred while arrival details are shown");
    appHandleUiEvent(earlyDebt, UiEvent{UiEventKind::ActivateFocused, 0}, 5700);
    appTick(earlyDebt, 5700);
    ok &= expect(out, earlyDebt.modal.kind == ModalKind::ForcedPayment,
                 "forced payment opens only after the confirmed arrival checkpoint");

    static AppState cardDebt{};
    appInit(cardDebt, 0);
    cardDebt.boardSize = 40;
    cardDebt.selfSeatId = 1;
    cardDebt.activePlayerId = 1;
    cardDebt.decisionPlayerId = 1;
    cardDebt.authorityPhase = AuthorityPhase::AwaitCard;
    cardDebt.availableActions = 1u << 16;
    cardDebt.debtAmount = 50;
    cardDebt.rollTarget = 7;
    cardDebt.rolledSteps = 6;
    cardDebt.rollResolved = true;
    cardDebt.moveArrivalPending = true;
    cardDebt.moveArrivalConfirmed = true;
    cardDebt.arrivalContinueAtMs = 100;
    cardDebt.nav.current.page = ScreenPage::MoveGuide;
    cardDebt.page = ScreenPage::MoveGuide;
    appTick(cardDebt, 100);
    ok &= expect(out, cardDebt.nav.current.page == ScreenPage::CardReveal &&
                      cardDebt.cardPresentation == CardPresentationPhase::Drawing &&
                      cardDebt.modal.kind == ModalKind::None,
                 "Chance arrival inserts a card draw gate before early debt");
    appHandleUiEvent(cardDebt, UiEvent{UiEventKind::ActivateFocused, 0}, 500);
    ok &= expect(out, cardDebt.nav.current.page == ScreenPage::CardReveal &&
                      cardDebt.modal.kind == ModalKind::None,
                 "card draw cannot be skipped before the result is revealed");
    appTick(cardDebt, 2000);
    ok &= expect(out, cardDebt.cardPresentation == CardPresentationPhase::Drawing,
                 "card animation waits safely when the result event arrives late");

    gridopoly::protocol::PlayerCardEvent drawnCard{};
    drawnCard.stage = gridopoly::protocol::PlayerCardStage::Drawn;
    drawnCard.domainEventType = gridopoly::protocol::kDomainEventCardDrawn;
    drawnCard.stateVersion = 81;
    drawnCard.eventSequence = 1;
    drawnCard.playerId = 1;
    drawnCard.deckId = 1;
    drawnCard.cardIndex = 1;
    drawnCard.cardInstanceId = 77;
    drawnCard.cardCatalogId = 2;
    drawnCard.effectId = 2;
    drawnCard.amount = -50;
    static uint8_t cardPayload[gridopoly::protocol::kPlayerCardEventSize]{};
    size_t cardPayloadLength = 0;
    gridopoly::protocol::PlayerCardEvent decodedCard{};
    static TransportEvent cardEvent{};
    ok &= expect(out,
                 gridopoly::protocol::encodePlayerCardEvent(
                     drawnCard, cardPayload, sizeof(cardPayload), cardPayloadLength) &&
                 gridopoly::protocol::decodePlayerCardEvent(
                     cardPayload, cardPayloadLength, decodedCard) &&
                 playerCardEventToTransportEvent(decodedCard, false, cardEvent),
                 "private CardDrawn codec maps to one bounded player event");
    appHandleTransportEvent(cardDebt, cardEvent, 2000);
    appTick(cardDebt, 2249);
    ok &= expect(out, cardDebt.cardPresentation == CardPresentationPhase::Drawing &&
                      cardDebt.cardResultValid && cardDebt.cardChance &&
                       cardDebt.cardIndex == 1 && cardDebt.cardAmount == -50 &&
                       cardDebt.cardInstanceId == 77,
                  "late card result settles the current animation without skipping it");
    appTick(cardDebt, 2250);
    ok &= expect(out, cardDebt.cardPresentation == CardPresentationPhase::Revealed &&
                      cardDebt.modal.kind == ModalKind::None,
                 "card result becomes readable before any payment page opens");
    appHandleUiEvent(cardDebt, UiEvent{UiEventKind::ActivateFocused, 0}, 2300);
    TransportCommand cardContinue{};
    ok &= expect(out, cardDebt.cardPresentation == CardPresentationPhase::Settling &&
                       cardDebt.modal.kind == ModalKind::None &&
                       appPollCommand(cardDebt, cardContinue) &&
                       cardContinue.kind == TransportCommandKind::CardContinueRequest &&
                       cardContinue.argument == 77,
                 "explicit Continue submits the revealed instance without guessing its effect");

    const uint32_t cardStartedBeforeDuplicate = cardDebt.cardStartedMs;
    appHandleTransportEvent(cardDebt, cardEvent, 2325);
    ok &= expect(out, cardDebt.cardPresentation == CardPresentationPhase::Settling &&
                       cardDebt.cardStartedMs == cardStartedBeforeDuplicate &&
                       cardDebt.seenCardInstanceId == 77 &&
                       cardDebt.seenCardDrawEventSequence == 1,
                 "duplicate CardDrawn after Continue cannot restart the reveal animation");

    gridopoly::protocol::StateSnapshot cardDebtSnapshot{};
    cardDebtSnapshot.seatId = 1;
    cardDebtSnapshot.phase = 5;
    cardDebtSnapshot.activePlayerId = 1;
    cardDebtSnapshot.decisionPlayerId = 1;
    cardDebtSnapshot.boardSize = 40;
    cardDebtSnapshot.playerCount = 1;
    cardDebtSnapshot.selfPosition = 7;
    cardDebtSnapshot.selfCash = 410;
    cardDebtSnapshot.pendingTarget = 0xFF;
    cardDebtSnapshot.debtCreditorId = 0;
    cardDebtSnapshot.debtAmount = 50;
    cardDebtSnapshot.availableActions = 1u << 11;
    cardDebtSnapshot.stateVersion = 82;
    cardDebtSnapshot.players[0].playerId = 1;
    cardDebtSnapshot.players[0].position = 7;
    cardDebtSnapshot.players[0].cash = 410;
    authoritySnapshotToEvent(cardDebtSnapshot, false, authorityEvent);
    appHandleTransportEvent(cardDebt, authorityEvent, 2350);
    ok &= expect(out, cardDebt.modal.kind == ModalKind::ForcedPayment &&
                       cardDebt.modal.amount == 50,
                 "authoritative debt opens only after CardContinue leaves AwaitCard");

    gridopoly::protocol::PlayerCardEvent appliedCard = drawnCard;
    appliedCard.stage = gridopoly::protocol::PlayerCardStage::EffectApplied;
    appliedCard.domainEventType = gridopoly::protocol::kDomainEventCardEffectApplied;
    appliedCard.stateVersion = 83;
    appliedCard.eventSequence = 2;
    appliedCard.outcome = 1;
    gridopoly::protocol::encodePlayerCardEvent(
        appliedCard, cardPayload, sizeof(cardPayload), cardPayloadLength);
    gridopoly::protocol::decodePlayerCardEvent(
        cardPayload, cardPayloadLength, decodedCard);
    playerCardEventToTransportEvent(decodedCard, false, cardEvent);
    appHandleTransportEvent(cardDebt, cardEvent, 2360);
    ok &= expect(out, cardDebt.completedCardInstanceId == 77 &&
                       cardDebt.completedCardDrawEventSequence == 1 &&
                       cardDebt.cardEffectApplied,
                 "CardEffectApplied closes the displayed card lifecycle");

    gridopoly::protocol::encodePlayerCardEvent(
        drawnCard, cardPayload, sizeof(cardPayload), cardPayloadLength);
    gridopoly::protocol::decodePlayerCardEvent(
        cardPayload, cardPayloadLength, decodedCard);
    playerCardEventToTransportEvent(decodedCard, false, cardEvent);
    cardDebt.nav.current.page = ScreenPage::Home;
    cardDebt.page = ScreenPage::Home;
    appHandleTransportEvent(cardDebt, cardEvent, 2370);
    ok &= expect(out, cardDebt.nav.current.page == ScreenPage::Home &&
                       cardDebt.cardPresentation == CardPresentationPhase::Settling &&
                       cardDebt.completedCardInstanceId == 77,
                 "late CardDrawn after settlement cannot reopen the card page");

    gridopoly::protocol::AuthoritySnapshot cardRestore{};
    cardRestore.phase = 8;
    cardRestore.activePlayerId = 1;
    cardRestore.decisionPlayerId = 1;
    cardRestore.boardSize = 40;
    cardRestore.playerCount = 1;
    cardRestore.assetCount = 0;
    cardRestore.stateVersion = 83;
    cardRestore.lastEventSequence = 1;
    const char cardBoardId[] = "grid-city-40-v1";
    cardRestore.boardIdHash = gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(cardBoardId), strlen(cardBoardId));
    cardRestore.pendingCardFlags = 0x03;
    cardRestore.pendingCardPlayerId = 1;
    cardRestore.pendingCardDeckId = 1;
    cardRestore.pendingCardIndex = 1;
    cardRestore.pendingCardInstanceId = 77;
    cardRestore.pendingCardCatalogId = 2;
    cardRestore.pendingCardEffectId = 2;
    cardRestore.pendingCardDisplayAmount = -50;
    cardRestore.pendingCardDrawEventSequence = 1;
    cardRestore.players[0].playerId = 1;
    cardRestore.players[0].position = 7;
    cardRestore.players[0].cash = 410;
    static uint8_t cardAuthorityPayload[gridopoly::protocol::kMaxPayloadSize]{};
    size_t cardAuthorityLength = 0;
    gridopoly::protocol::AuthoritySnapshot decodedCardRestore{};
    TransportEvent cardRestoreEvent{};
    static AppState restoredCard{};
    appInit(restoredCard, 0);
    ok &= expect(out,
                 gridopoly::protocol::encodeAuthoritySnapshot(
                     cardRestore, cardAuthorityPayload, sizeof(cardAuthorityPayload),
                     cardAuthorityLength) &&
                 gridopoly::protocol::decodeAuthoritySnapshot(
                     cardAuthorityPayload, cardAuthorityLength, decodedCardRestore) &&
                 fullAuthoritySnapshotToEvent(decodedCardRestore, true, cardRestoreEvent),
                 "Authority v3 preserves the pending card context through its codec");
    appHandleTransportEvent(restoredCard, cardRestoreEvent, 2400);
    ok &= expect(out, restoredCard.nav.current.page == ScreenPage::CardReveal &&
                       restoredCard.cardPresentation == CardPresentationPhase::Revealed &&
                       restoredCard.cardInstanceId == 77,
                 "0x03 resync restores the revealed card without replaying the draw animation");
    cardRestore.pendingCardFlags = 0x0F;
    gridopoly::protocol::encodeAuthoritySnapshot(
        cardRestore, cardAuthorityPayload, sizeof(cardAuthorityPayload), cardAuthorityLength);
    gridopoly::protocol::decodeAuthoritySnapshot(
        cardAuthorityPayload, cardAuthorityLength, decodedCardRestore);
    fullAuthoritySnapshotToEvent(decodedCardRestore, true, cardRestoreEvent);
    appHandleTransportEvent(restoredCard, cardRestoreEvent, 2410);
    ok &= expect(out, restoredCard.cardPresentation == CardPresentationPhase::Settling &&
                       restoredCard.cardPresentationAcknowledged,
                 "0x0F resync restores settlement waiting without a second reveal");

    delayedSnapshot.phase = 3;
    delayedSnapshot.selfPosition = 24;
    delayedSnapshot.players[0].position = 24;
    delayedSnapshot.pendingTarget = 0xFF;
    delayedSnapshot.tileAssetIndex = 4;
    delayedSnapshot.availableActions = (1u << 2) | (1u << 3);
    delayedSnapshot.stateVersion = 80;
    authoritySnapshotToEvent(delayedSnapshot, false, authorityEvent);
    appHandleTransportEvent(delayedRoll, authorityEvent, 5100);
    appTick(delayedRoll, 5699);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::DiceStage &&
                      delayedRoll.rolledSteps == 7,
                 "fast authority advance cannot hide the readable roll result");
    appTick(delayedRoll, 5700);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::MoveGuide &&
                      delayedRoll.moveArrivalConfirmed,
                 "advanced purchase state still presents the arrival checkpoint first");
    appTick(delayedRoll, 6899);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::MoveGuide,
                 "auto-confirmed arrival remains readable before its next event");
    appTick(delayedRoll, 6900);
    ok &= expect(out, delayedRoll.nav.current.page == ScreenPage::Purchase,
                 "deferred purchase opens only after dice and arrival presentations complete");

    snapshot.activePlayerId = 1;
    snapshot.decisionPlayerId = 1;
    snapshot.availableActions = 1;
    snapshot.stateVersion = 78;
    ok &= expect(out, authoritySnapshotToEvent(snapshot, false, authorityEvent),
                 "incremental authority snapshot remains valid");
    appHandleTransportEvent(connected, authorityEvent, 20);
    ok &= expect(out, connected.homePhase == HomePhase::MyTurn &&
                      connected.turnsUntilYou == 0 && appPageContentCount(connected) == 4,
                 "roll action changes Home to the green four-action turn state");

    snapshot.phase = 2;
    snapshot.availableActions = 1u << 1;
    snapshot.pendingTarget = 24;
    snapshot.stateVersion = 79;
    ok &= expect(out, authoritySnapshotToEvent(snapshot, false, authorityEvent),
                 "move-confirm authority snapshot remains valid");
    appHandleTransportEvent(connected, authorityEvent, 30);
    TransportEvent connectedDiceEvent{};
    connectedDiceEvent.kind = TransportEventKind::GameEventReceived;
    connectedDiceEvent.stateVersion = 79;
    connectedDiceEvent.gameEvent.sequence = 1;
    connectedDiceEvent.gameEvent.kind = 3;
    connectedDiceEvent.gameEvent.actorId = 1;
    connectedDiceEvent.gameEvent.amount = 7;
    connectedDiceEvent.gameEvent.detail = 3u | (4u << 8);
    appHandleTransportEvent(connected, connectedDiceEvent, 30);
    ok &= expect(out, connected.homePhase == HomePhase::MyTurn &&
                      connected.nav.current.page == ScreenPage::DiceStage &&
                      connected.rolledSteps == 7 && connected.rollTarget == 24 &&
                      connected.rollStartedMs == 30,
                 "move snapshot plus exact dice event expose seven spaces without inference");
    appTick(connected, 1929);
    ok &= expect(out, !appDiceResultVisible(connected, 1929),
                 "spaces remain hidden until the physical dice animation settles");
    appTick(connected, 1930);
    ok &= expect(out, appDiceResultVisible(connected, 1930),
                 "spaces appear exactly when the dice settle");
    appTick(connected, 2629);
    ok &= expect(out, connected.nav.current.page == ScreenPage::DiceStage,
                 "dice result occupies the full 2.6-second presentation window");
    appTick(connected, 2630);
    ok &= expect(out, connected.nav.current.page == ScreenPage::MoveGuide,
                 "completed dice animation advances to physical move confirmation");

    snapshot.phase = 3;
    snapshot.selfPosition = 24;
    snapshot.players[0].position = 24;
    snapshot.pendingTarget = 0xFF;
    snapshot.tileAssetIndex = 4;
    snapshot.availableActions = (1u << 2) | (1u << 3);
    snapshot.stateVersion = 80;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 2700);
    ok &= expect(out, connected.nav.current.page == ScreenPage::MoveGuide &&
                      connected.moveArrivalConfirmed && connected.tileAssetIndex == 4,
                 "purchase snapshot is buffered behind the arrival checkpoint");
    appHandleUiEvent(connected, UiEvent{UiEventKind::ActivateFocused, 0}, 2700);
    appTick(connected, 2700);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Purchase &&
                      appFocusCount(connected) == 2,
                 "confirmed arrival can continue to the two-choice purchase page");

    static AppState offlinePurchase{};
    offlinePurchase = connected;
    appHandleInput(offlinePurchase, InputEvent{InputKind::Rotate, 1, 2710}, 2710);
    TransportEvent connectionLost{};
    connectionLost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(offlinePurchase, connectionLost, 2720);
    appHandleInput(offlinePurchase, InputEvent{InputKind::Rotate, -1, 2730}, 2730);
    appHandleUiEvent(offlinePurchase, UiEvent{UiEventKind::ActivateFocused, 0}, 2740);
    TransportCommand offlineCommand{};
    ok &= expect(out, offlinePurchase.nav.current.page == ScreenPage::Purchase &&
                      offlinePurchase.nav.current.focus == 0 &&
                      !offlinePurchase.authorityOnline &&
                      !appPollCommand(offlinePurchase, offlineCommand) &&
                      strcmp(offlinePurchase.toast, "RECONNECTING...") == 0,
                 "purchase focus remains local while an offline confirmation reports reconnecting");

    snapshot.phase = 6;
    snapshot.availableActions = 1u << 4;
    snapshot.stateVersion = 81;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 2800);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Home &&
                      connected.homePhase == HomePhase::MyTurnEnd &&
                      appPageContentCount(connected) == 4,
                 "turn-end phase returns to green Home with END TURN priority");
    static AppState endTurnTransition{};
    endTurnTransition = connected;
    endTurnTransition.nav.current.focus = 0;
    appHandleUiEvent(endTurnTransition, UiEvent{UiEventKind::ActivateFocused, 0}, 2810);
    TransportCommand endTurn{};
    ok &= expect(out, appPollCommand(endTurnTransition, endTurn) &&
                      endTurn.kind == TransportCommandKind::EndTurnRequest &&
                      endTurnTransition.endTurnPresentation ==
                          EndTurnPresentationPhase::Exiting &&
                      appPresentedHomePhase(endTurnTransition) == HomePhase::MyTurnEnd,
                 "END TURN queues once and begins its local exit presentation");

    snapshot.phase = 0;
    snapshot.activePlayerId = 2;
    snapshot.decisionPlayerId = 2;
    snapshot.availableActions = 0;
    snapshot.stateVersion = 82;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(endTurnTransition, authorityEvent, 2900);
    appTick(endTurnTransition, 3130);
    ok &= expect(out, endTurnTransition.endTurnAccepted &&
                      endTurnTransition.endTurnPresentation ==
                          EndTurnPresentationPhase::WaitingHold &&
                      appPresentedHomePhase(endTurnTransition) == HomePhase::Waiting &&
                      appPageContentCount(endTurnTransition) == 3 &&
                      endTurnTransition.nav.current.focus == 0,
                 "server turn advance settles into a centered three-action waiting menu");

    snapshot.activePlayerId = 1;
    snapshot.decisionPlayerId = 1;
    snapshot.availableActions = 1u;
    snapshot.stateVersion = 83;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(endTurnTransition, authorityEvent, 3200);
    appTick(endTurnTransition, 4009);
    ok &= expect(out, endTurnTransition.homePhase == HomePhase::MyTurn &&
                      appPresentedHomePhase(endTurnTransition) == HomePhase::Waiting &&
                      appPageContentCount(endTurnTransition) == 3,
                 "an instant bot cycle cannot pull focus back to DICE before the hold ends");
    appTick(endTurnTransition, 4010);
    ok &= expect(out, endTurnTransition.endTurnPresentation ==
                          EndTurnPresentationPhase::None &&
                      appPresentedHomePhase(endTurnTransition) == HomePhase::MyTurn &&
                      appPageContentCount(endTurnTransition) == 4 &&
                      endTurnTransition.nav.current.focus == 0,
                 "DICE regains priority only after the minimum waiting presentation");

    static AppState extraRoll{};
    appInit(extraRoll, 0);
    extraRoll.authorityRoomId = 7001;
    extraRoll.selfSeatId = 1;
    extraRoll.playerCount = 2;
    extraRoll.activePlayerId = 1;
    extraRoll.decisionPlayerId = 1;
    extraRoll.authorityPlayers[0].playerId = 1;
    extraRoll.authorityPlayers[1].playerId = 2;
    extraRoll.nav.current.page = ScreenPage::DiceStage;
    extraRoll.page = ScreenPage::DiceStage;
    extraRoll.rollAnimating = true;
    extraRoll.rollStartedMs = 100;
    TransportEvent doubleSixes{};
    doubleSixes.kind = TransportEventKind::GameEventReceived;
    doubleSixes.roomId = 7001;
    doubleSixes.stateVersion = 10;
    doubleSixes.gameEvent.sequence = 1;
    doubleSixes.gameEvent.kind = 3;
    doubleSixes.gameEvent.actorId = 1;
    doubleSixes.gameEvent.amount = 12;
    doubleSixes.gameEvent.detail = 6u | (6u << 8);
    appHandleTransportEvent(extraRoll, doubleSixes, 200);
    ok &= expect(out,
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Pending &&
                 extraRoll.extraRollDieA == 6 && extraRoll.extraRollDieB == 6 &&
                 extraRoll.rolledSteps == 12,
                 "double sixes reserve one extra-roll reward while landing still resolves");
    // The landing/arrival presentation has completed before the server opens
    // the purchase decision. Keep the earned reward pending behind that
    // decision without leaving the synthetic fixture locked on DiceStage.
    extraRoll.rollAnimating = false;
    extraRoll.rollResolved = false;
    extraRoll.rollPresentationComplete = false;
    extraRoll.nav.current.page = ScreenPage::Home;
    extraRoll.page = ScreenPage::Home;

    TransportEvent extraRollAuthority{};
    extraRollAuthority.kind = TransportEventKind::StateSnapshotApplied;
    extraRollAuthority.roomId = 7001;
    extraRollAuthority.stateVersion = 10;
    extraRollAuthority.selfSeatId = 1;
    extraRollAuthority.activePlayerId = 1;
    extraRollAuthority.decisionPlayerId = 1;
    extraRollAuthority.playerCount = 2;
    extraRollAuthority.boardSize = 16;
    extraRollAuthority.cash = 1450;
    extraRollAuthority.playerPosition = 19;
    extraRollAuthority.players[0].playerId = 1;
    extraRollAuthority.players[0].position = 19;
    extraRollAuthority.players[0].cash = 1450;
    extraRollAuthority.players[0].doublesStreak = 1;
    extraRollAuthority.players[1].playerId = 2;
    extraRollAuthority.phase = AuthorityPhase::AwaitPurchase;
    extraRollAuthority.availableActions = (1u << 2) | (1u << 3);
    extraRollAuthority.tileAssetIndex = 13;
    appHandleTransportEvent(extraRoll, extraRollAuthority, 300);
    ok &= expect(out,
                 extraRoll.nav.current.page == ScreenPage::Purchase &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Pending,
                 "purchase and payment decisions remain ahead of the pending doubles reward");

    const gridopoly::core::BoardDefinition *extraRollBoard =
        gridopoly::core::BoardCatalog::findBySize(16);
    TransportEvent extraRollFullAuthority = extraRollAuthority;
    extraRollFullAuthority.kind = TransportEventKind::AuthoritySnapshotApplied;
    extraRollFullAuthority.assetCount =
        extraRollBoard == nullptr ? 0 : extraRollBoard->assetCount;
    extraRollFullAuthority.boardIdHash = extraRollBoard == nullptr ? 0 :
        gridopoly::protocol::crc32(
            reinterpret_cast<const uint8_t *>(extraRollBoard->id),
            strlen(extraRollBoard->id));
    appHandleTransportEvent(extraRoll, extraRollFullAuthority, 350);
    ok &= expect(out,
                 extraRoll.fullAuthoritySnapshotValid &&
                 extraRoll.authorityPlayers[0].doublesStreak == 1 &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Pending,
                 "full authority preserves the earned doubles streak while purchase resolves");

    extraRollAuthority.stateVersion = 11;
    extraRollAuthority.phase = AuthorityPhase::AwaitRoll;
    extraRollAuthority.availableActions = 1u;
    extraRollAuthority.tileAssetIndex = 0xFF;
    // Compact StateSnapshot does not transmit doublesStreak. The reducer must
    // retain the value from the preceding full projection.
    extraRollAuthority.players[0].doublesStreak = 0;
    extraRollAuthority.resync = true;
    appHandleTransportEvent(extraRoll, extraRollAuthority, 500);
    const uint32_t rewardDeadline = extraRoll.extraRollRewardUntilMs;
    ok &= expect(out,
                 extraRoll.nav.current.page == ScreenPage::ExtraRollReward &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Reward &&
                 extraRoll.extraRollStreak == 1 && rewardDeadline == 2500,
                 "settled doubles open the dedicated reward once even when completion uses a full resync");
    appHandleTransportEvent(extraRoll, extraRollAuthority, 900);
    ok &= expect(out,
                 extraRoll.nav.current.page == ScreenPage::ExtraRollReward &&
                 extraRoll.extraRollRewardUntilMs == rewardDeadline,
                 "a repeated same-version authority frame cannot restart the reward timer");
    appTick(extraRoll, 2499);
    ok &= expect(out, extraRoll.nav.current.page == ScreenPage::ExtraRollReward,
                 "the reward remains visible for its complete local duration");
    appTick(extraRoll, 2500);
    ok &= expect(out,
                 extraRoll.nav.current.page == ScreenPage::Home &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Ready &&
                 extraRoll.nav.current.focus == 0,
                 "reward completion returns to Home with the bonus roll focused");

    extraRollAuthority.resync = true;
    appHandleTransportEvent(extraRoll, extraRollAuthority, 2600);
    ok &= expect(out,
                 extraRoll.nav.current.page == ScreenPage::Home &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::Ready &&
                 extraRoll.extraRollRewardUntilMs == 0,
                 "same-room resync restores bonus-roll readiness without replaying the reward");

    static AppState reversedExtraRollProjection{};
    appInit(reversedExtraRollProjection, 0);
    reversedExtraRollProjection.authorityRoomId = 7003;
    reversedExtraRollProjection.selfSeatId = 1;
    reversedExtraRollProjection.playerCount = 2;
    reversedExtraRollProjection.activePlayerId = 1;
    reversedExtraRollProjection.decisionPlayerId = 1;
    reversedExtraRollProjection.authorityPlayers[0].playerId = 1;
    reversedExtraRollProjection.authorityPlayers[1].playerId = 2;
    reversedExtraRollProjection.nav.current.page = ScreenPage::DiceStage;
    reversedExtraRollProjection.page = ScreenPage::DiceStage;
    reversedExtraRollProjection.rollAnimating = true;
    TransportEvent reversedDoubleSixes = doubleSixes;
    reversedDoubleSixes.roomId = 7003;
    reversedDoubleSixes.stateVersion = 20;
    appHandleTransportEvent(reversedExtraRollProjection, reversedDoubleSixes, 100);

    TransportEvent compactExtraRollReady = extraRollAuthority;
    compactExtraRollReady.roomId = 7003;
    compactExtraRollReady.stateVersion = 21;
    compactExtraRollReady.resync = false;
    compactExtraRollReady.players[0].doublesStreak = 0;
    appHandleTransportEvent(reversedExtraRollProjection, compactExtraRollReady, 300);
    ok &= expect(out,
                 reversedExtraRollProjection.extraRollPresentation ==
                     ExtraRollPresentationPhase::Pending,
                 "compact state alone cannot erase or invent full-only doubles metadata");

    TransportEvent fullExtraRollReady = compactExtraRollReady;
    fullExtraRollReady.kind = TransportEventKind::AuthoritySnapshotApplied;
    fullExtraRollReady.assetCount =
        extraRollBoard == nullptr ? 0 : extraRollBoard->assetCount;
    fullExtraRollReady.boardIdHash = extraRollFullAuthority.boardIdHash;
    fullExtraRollReady.players[0].doublesStreak = 1;
    appHandleTransportEvent(reversedExtraRollProjection, fullExtraRollReady, 400);
    ok &= expect(out,
                 reversedExtraRollProjection.nav.current.page ==
                     ScreenPage::ExtraRollReward &&
                 reversedExtraRollProjection.extraRollPresentation ==
                     ExtraRollPresentationPhase::Reward &&
                 reversedExtraRollProjection.extraRollRewardUntilMs == 2400,
                 "late full authority still opens the dedicated reward after compact state arrived first");

    static AppState lateDoubleEvent{};
    appInit(lateDoubleEvent, 0);
    lateDoubleEvent.selfSeatId = 1;
    lateDoubleEvent.authorityPlayers[0].playerId = 1;
    lateDoubleEvent.authorityPlayers[1].playerId = 2;
    TransportEvent restoredExtraRollState = extraRollAuthority;
    restoredExtraRollState.stateVersion = 11;
    restoredExtraRollState.phase = AuthorityPhase::AwaitRoll;
    restoredExtraRollState.availableActions = 1u;
    restoredExtraRollState.tileAssetIndex = 0xFF;
    restoredExtraRollState.resync = true;
    restoredExtraRollState.players[0].doublesStreak = 0;
    appHandleTransportEvent(lateDoubleEvent, restoredExtraRollState, 1790);
    TransportEvent restoredExtraRoll = extraRollFullAuthority;
    restoredExtraRoll.stateVersion = 11;
    restoredExtraRoll.phase = AuthorityPhase::AwaitRoll;
    restoredExtraRoll.availableActions = 1u;
    restoredExtraRoll.tileAssetIndex = 0xFF;
    restoredExtraRoll.resync = true;
    restoredExtraRoll.players[0].doublesStreak = 1;
    appHandleTransportEvent(lateDoubleEvent, restoredExtraRoll, 1800);
    TransportEvent lateDoubleSixes = doubleSixes;
    lateDoubleSixes.gameEvent.sequence = 1;
    appHandleTransportEvent(lateDoubleEvent, lateDoubleSixes, 1810);
    ok &= expect(out,
                 lateDoubleEvent.nav.current.page == ScreenPage::Home &&
                 lateDoubleEvent.extraRollPresentation == ExtraRollPresentationPhase::Ready &&
                 lateDoubleEvent.extraRollRewardUntilMs == 0,
                 "a late dice event cannot downgrade restored extra-roll readiness or replay the reward");

    extraRollAuthority.stateVersion = 12;
    extraRollAuthority.activePlayerId = 2;
    extraRollAuthority.decisionPlayerId = 2;
    extraRollAuthority.players[0].doublesStreak = 0;
    appHandleTransportEvent(lateDoubleEvent, extraRollAuthority, 1820);
    ok &= expect(out,
                 lateDoubleEvent.extraRollPresentation == ExtraRollPresentationPhase::None,
                 "turn advance clears a stale extra-roll marker before another player acts");

    extraRollAuthority.activePlayerId = 1;
    extraRollAuthority.decisionPlayerId = 1;
    extraRollAuthority.players[0].doublesStreak = 1;
    extraRollAuthority.stateVersion = 11;
    appHandleUiEvent(extraRoll, UiEvent{UiEventKind::ActivateFocused, 0}, 2610);
    TransportCommand extraRollCommand{};
    ok &= expect(out,
                 appPollCommand(extraRoll, extraRollCommand) &&
                 extraRollCommand.kind == TransportCommandKind::RollRequest &&
                 extraRoll.nav.current.page == ScreenPage::DiceStage &&
                 extraRoll.extraRollPresentation == ExtraRollPresentationPhase::None,
                 "ROLL AGAIN consumes the ready marker and starts exactly one new roll request");

    static AppState thirdDouble{};
    appInit(thirdDouble, 0);
    thirdDouble.selfSeatId = 1;
    thirdDouble.playerCount = 2;
    thirdDouble.activePlayerId = 1;
    thirdDouble.decisionPlayerId = 1;
    thirdDouble.authorityPlayers[0].playerId = 1;
    thirdDouble.authorityPlayers[1].playerId = 2;
    thirdDouble.nav.current.page = ScreenPage::DiceStage;
    thirdDouble.page = ScreenPage::DiceStage;
    thirdDouble.rollAnimating = true;
    TransportEvent thirdDoubleEvent = doubleSixes;
    thirdDoubleEvent.roomId = 7002;
    thirdDoubleEvent.stateVersion = 11;
    thirdDoubleEvent.gameEvent.sequence = 1;
    thirdDoubleEvent.gameEvent.amount = 8;
    thirdDoubleEvent.gameEvent.detail = 4u | (4u << 8);
    appHandleTransportEvent(thirdDouble, thirdDoubleEvent, 100);
    extraRollAuthority.resync = false;
    extraRollAuthority.roomId = 7002;
    extraRollAuthority.stateVersion = 12;
    extraRollAuthority.phase = AuthorityPhase::TurnEnd;
    extraRollAuthority.availableActions = 1u << 4;
    extraRollAuthority.players[0].doublesStreak = 0;
    appHandleTransportEvent(thirdDouble, extraRollAuthority, 200);
    ok &= expect(out,
                 thirdDouble.extraRollPresentation == ExtraRollPresentationPhase::None &&
                 thirdDouble.nav.current.page == ScreenPage::Home,
                 "a third-double hold outcome never exposes an invalid fourth roll");

    snapshot.phase = 5;
    snapshot.activePlayerId = 1;
    snapshot.decisionPlayerId = 1;
    snapshot.debtCreditorId = 2;
    snapshot.debtAmount = 680;
    snapshot.selfCash = 250;
    snapshot.players[0].cash = 250;
    snapshot.availableActions = 1u << 5;
    snapshot.stateVersion = 82;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 2900);
    ok &= expect(out, connected.nav.current.page == ScreenPage::DebtAssets &&
                      connected.debt.amountDue == 680,
                 "insufficient debt snapshot opens the forced asset resolver");

    snapshot.phase = 4;
    snapshot.auctionAssetIndex = 6;
    snapshot.auctionCurrentBid = 110;
    snapshot.auctionMinimumBid = 120;
    snapshot.availableActions = (1u << 13) | (1u << 14);
    snapshot.stateVersion = 83;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 3000);
    const gridopoly::core::BoardDefinition *auctionBoard =
        gridopoly::core::BoardCatalog::findBySize(snapshot.boardSize);
    TransportEvent auctionFullEvent{};
    auctionFullEvent.kind = TransportEventKind::AuthoritySnapshotApplied;
    auctionFullEvent.stateVersion = 83;
    auctionFullEvent.phase = AuthorityPhase::AwaitAuction;
    auctionFullEvent.activePlayerId = 1;
    auctionFullEvent.decisionPlayerId = 1;
    auctionFullEvent.boardSize = snapshot.boardSize;
    auctionFullEvent.playerCount = snapshot.playerCount;
    auctionFullEvent.assetCount = auctionBoard == nullptr ? 0 : auctionBoard->assetCount;
    auctionFullEvent.boardIdHash = auctionBoard == nullptr ? 0 : gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(auctionBoard->id), strlen(auctionBoard->id));
    auctionFullEvent.auctionFlags = 0x01;
    auctionFullEvent.auctionAssetIndex = 6;
    auctionFullEvent.auctionCurrentBidderId = 1;
    auctionFullEvent.auctionCurrentBid = 110;
    auctionFullEvent.auctionGeneration = 55;
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        auctionFullEvent.players[index].playerId = snapshot.players[index].playerId;
        auctionFullEvent.players[index].cash = snapshot.players[index].cash;
        auctionFullEvent.players[index].flags = snapshot.players[index].flags;
    }
    ok &= expect(out, !connected.debt.bankruptcyPending &&
                      !connected.debt.bankruptcyResolved,
                 "auction fixture leaves terminal bankruptcy state clear");
    appHandleTransportEvent(connected, auctionFullEvent, 3000);
    ok &= expect(out, connected.auctionGeneration == 55,
                 "new auction authority applies its generation");
    ok &= expect(out, connected.seenAuctionGeneration == 55 &&
                      connected.seenAuctionAssetIndex == 6,
                 "new auction authority records its one-time presentation key");
    ok &= expect(out, connected.auctionPresentation == AuctionPresentationPhase::Intro,
                 "new auction authority starts the lot introduction");
    ok &= expect(out, !connected.rollAnimating && !connected.rollResolved &&
                      !connected.moveArrivalPending &&
                      connected.cardPresentation == CardPresentationPhase::None,
                 "new auction fixture has no earlier arrival presentation lock");
    ok &= expect(out, connected.nav.current.page == ScreenPage::Auction,
                 "new auction authority opens the auction page");
    ok &= expect(out, connected.auctionMinimumBid == 120,
                 "auction introduction preserves the compact minimum bid");
    ok &= expect(out, appFocusCount(connected) == 0,
                 "auction introduction remains noninteractive");
    TransportCommand auctionPass{};
    appHandleUiEvent(connected, UiEvent{UiEventKind::ActivateFocused, 0}, 3010);
    ok &= expect(out, !appPollCommand(connected, auctionPass),
                 "auction introduction cannot accidentally submit a bid");
    appTick(connected, 4799);
    ok &= expect(out, connected.auctionPresentation == AuctionPresentationPhase::Intro,
                 "auction introduction remains visible for the full reveal interval");
    appTick(connected, 4800);
    ok &= expect(out, connected.auctionPresentation == AuctionPresentationPhase::Live &&
                      appFocusCount(connected) == 2,
                 "auction reveal advances to live bid and pass controls");
    connected.nav.current.focus = 1;
    appHandleUiEvent(connected, UiEvent{UiEventKind::ActivateFocused, 0}, 4810);
    ok &= expect(out, appPollCommand(connected, auctionPass) &&
                      auctionPass.kind == TransportCommandKind::AuctionPassRequest &&
                      connected.auctionPassed && appFocusCount(connected) == 0,
                 "PASS immediately changes the local auction to spectator mode");
    snapshot.decisionPlayerId = 2;
    snapshot.auctionHighestBidderId = 2;
    snapshot.auctionCurrentBid = 130;
    snapshot.auctionMinimumBid = 140;
    snapshot.availableActions = 0;
    snapshot.stateVersion = 84;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 4820);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Auction &&
                      connected.auctionPassed && connected.auctionCurrentBid == 130 &&
                      connected.auctionHighestBidderId == 2 && connected.decisionPlayerId == 2,
                 "spectator auction consumes live bidder and quote snapshots");
    snapshot.phase = 6;
    snapshot.activePlayerId = 1;
    snapshot.decisionPlayerId = 1;
    snapshot.auctionAssetIndex = 0xFF;
    snapshot.auctionHighestBidderId = 0;
    snapshot.auctionCurrentBid = 0;
    snapshot.auctionMinimumBid = 10;
    snapshot.availableActions = 1u << 4;
    snapshot.stateVersion = 85;
    authoritySnapshotToEvent(snapshot, false, authorityEvent);
    appHandleTransportEvent(connected, authorityEvent, 4830);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Auction &&
                      connected.auctionPresentation == AuctionPresentationPhase::Result &&
                      connected.auctionResultAssetIndex == 6 &&
                      connected.auctionWinnerPlayerId == 2 &&
                      connected.auctionResultAmount == 130 && appFocusCount(connected) == 0,
                 "auction settlement retains winner lot and final price on a result page");
    appTick(connected, 7829);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Auction &&
                      connected.auctionPresentation == AuctionPresentationPhase::Result,
                 "auction result remains readable for the full settlement interval");
    appTick(connected, 7830);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Home &&
                      connected.auctionPresentation == AuctionPresentationPhase::None &&
                      !connected.auctionPassed && connected.homePhase == HomePhase::MyTurnEnd,
                 "auction result exits to the already-authoritative next phase after presentation");

    static AppState auctionBarrier{};
    appInit(auctionBarrier, 0);
    static gridopoly::protocol::StateSnapshot openingState{};
    openingState = gridopoly::protocol::StateSnapshot{};
    openingState.seatId = 1;
    openingState.phase = 4;
    openingState.activePlayerId = 1;
    openingState.decisionPlayerId = 0;
    openingState.boardSize = 24;
    openingState.selfPosition = 0;
    openingState.selfCash = 1200;
    openingState.playerCount = 2;
    openingState.pendingTarget = 0xFF;
    openingState.tileAssetIndex = 0xFF;
    openingState.debtAssetIndex = 0xFF;
    openingState.auctionAssetIndex = 6;
    openingState.auctionMinimumBid = 10;
    openingState.availableActions = 1u << 15;
    openingState.stateVersion = 10;
    for (uint8_t index = 0; index < openingState.playerCount; ++index) {
        openingState.players[index].playerId = static_cast<uint8_t>(index + 1);
        openingState.players[index].cash = 1200;
        openingState.players[index].flags = 0x04;
    }
    authoritySnapshotToEvent(openingState, false, authorityEvent);
    appHandleTransportEvent(auctionBarrier, authorityEvent, 100);

    static gridopoly::protocol::AuthoritySnapshot openingFull{};
    openingFull = gridopoly::protocol::AuthoritySnapshot{};
    openingFull.phase = 4;
    openingFull.activePlayerId = 1;
    openingFull.decisionPlayerId = 0;
    openingFull.boardSize = 24;
    openingFull.playerCount = 2;
    const gridopoly::core::BoardDefinition *openingBoard =
        gridopoly::core::BoardCatalog::findBySize(24);
    openingFull.assetCount = openingBoard == nullptr ? 0 : openingBoard->assetCount;
    openingFull.boardIdHash = openingBoard == nullptr ? 0 : gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(openingBoard->id), strlen(openingBoard->id));
    openingFull.stateVersion = 10;
    openingFull.auctionFlags = 0x03;
    openingFull.auctionAssetIndex = 6;
    openingFull.auctionReadyMask = 0;
    openingFull.auctionRequiredReadyMask = 0x03;
    openingFull.auctionCurrentBid = 0;
    openingFull.auctionGeneration = 77;
    for (uint8_t index = 0; index < openingFull.playerCount; ++index) {
        openingFull.players[index].playerId = static_cast<uint8_t>(index + 1);
        openingFull.players[index].cash = 1200;
        openingFull.players[index].flags = 0x04;
    }
    static TransportEvent openingFullEvent{};
    fullAuthoritySnapshotToEvent(openingFull, false, openingFullEvent);
    appHandleTransportEvent(auctionBarrier, openingFullEvent, 101);
    TransportCommand auctionReady{};
    ok &= expect(out, auctionBarrier.auctionPresentation == AuctionPresentationPhase::Intro &&
                      !appPollCommand(auctionBarrier, auctionReady),
                 "AuctionReady is not queued before the introduction frame is presented");
    appNotifyFramePresented(auctionBarrier, 102);
    ok &= expect(out, appPollCommand(auctionBarrier, auctionReady) &&
                      auctionReady.kind == TransportCommandKind::AuctionReadyRequest &&
                      auctionReady.assetIndex == 6 &&
                      static_cast<uint32_t>(auctionReady.argument) == 77 &&
                      auctionReady.stateVersion == 10,
                 "presented auction introduction queues one generation-bound AuctionReady");
    appNotifyFramePresented(auctionBarrier, 103);
    TransportCommand duplicateReady{};
    ok &= expect(out, !appPollCommand(auctionBarrier, duplicateReady),
                 "repeated rendered frames do not enqueue duplicate AuctionReady requests");

    TransportEvent auctionConnectionLost{};
    auctionConnectionLost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(auctionBarrier, auctionConnectionLost, 104);
    authoritySnapshotToEvent(openingState, true, authorityEvent);
    appHandleTransportEvent(auctionBarrier, authorityEvent, 105);
    fullAuthoritySnapshotToEvent(openingFull, true, openingFullEvent);
    appHandleTransportEvent(auctionBarrier, openingFullEvent, 106);
    appNotifyFramePresented(auctionBarrier, 107);
    TransportCommand recoveredReady{};
    ok &= expect(out, appPollCommand(auctionBarrier, recoveredReady) &&
                      recoveredReady.kind == TransportCommandKind::AuctionReadyRequest &&
                      recoveredReady.assetIndex == 6 &&
                      static_cast<uint32_t>(recoveredReady.argument) == 77 &&
                      recoveredReady.requestId != auctionReady.requestId,
                 "resync requeues current AuctionReady when the own ready bit is still absent");

    TransportEvent readyCompleted{};
    readyCompleted.kind = TransportEventKind::CommandCompleted;
    readyCompleted.requestId = recoveredReady.requestId;
    readyCompleted.stateVersion = 11;
    appHandleTransportEvent(auctionBarrier, readyCompleted, 108);
    openingState.availableActions = 0;
    openingState.stateVersion = 11;
    authoritySnapshotToEvent(openingState, false, authorityEvent);
    appHandleTransportEvent(auctionBarrier, authorityEvent, 109);
    openingFull.auctionReadyMask = 0x01;
    openingFull.stateVersion = 11;
    fullAuthoritySnapshotToEvent(openingFull, false, openingFullEvent);
    appHandleTransportEvent(auctionBarrier, openingFullEvent, 110);
    appNotifyFramePresented(auctionBarrier, 111);
    ok &= expect(out, !appPollCommand(auctionBarrier, duplicateReady),
                 "authority own-ready bit suppresses every later AuctionReady frame");
    appTick(auctionBarrier, 1900);
    ok &= expect(out,
                 auctionBarrier.auctionPresentation == AuctionPresentationPhase::Intro,
                 "same-generation resync does not shorten the one-time auction introduction");
    appTick(auctionBarrier, 1901);
    ok &= expect(out,
                 auctionBarrier.auctionPresentation == AuctionPresentationPhase::Live &&
                 appAuctionOpening(auctionBarrier) &&
                 appFocusCount(auctionBarrier) == 0,
                 "opening barrier continues on the disabled live page after reveal");

    openingState.decisionPlayerId = 1;
    openingState.availableActions = (1u << 13) | (1u << 14);
    openingState.stateVersion = 12;
    authoritySnapshotToEvent(openingState, false, authorityEvent);
    appHandleTransportEvent(auctionBarrier, authorityEvent, 1910);
    openingFull.decisionPlayerId = 1;
    openingFull.auctionFlags = 0x01;
    openingFull.auctionCurrentBidderId = 1;
    openingFull.auctionReadyMask = 0x03;
    openingFull.stateVersion = 12;
    fullAuthoritySnapshotToEvent(openingFull, false, openingFullEvent);
    appHandleTransportEvent(auctionBarrier, openingFullEvent, 1911);
    ok &= expect(out,
                 auctionBarrier.auctionPresentation == AuctionPresentationPhase::Live &&
                 auctionBarrier.decisionPlayerId == 1 && appFocusCount(auctionBarrier) == 2,
                 "last required ready snapshot opens live BID and PASS exactly once");

    static gridopoly::protocol::StateSnapshot newGame{};
    newGame = gridopoly::protocol::StateSnapshot{};
    newGame.seatId = 1;
    newGame.phase = 1;
    newGame.activePlayerId = 2;
    newGame.decisionPlayerId = 2;
    newGame.round = 1;
    newGame.boardSize = 32;
    newGame.selfPosition = 0;
    newGame.selfCash = 1500;
    newGame.playerCount = 4;
    newGame.pendingTarget = 0xFF;
    newGame.tileAssetIndex = 0xFF;
    newGame.debtAssetIndex = 0xFF;
    newGame.auctionAssetIndex = 0xFF;
    newGame.stateVersion = 6;
    for (uint8_t index = 0; index < newGame.playerCount; ++index) {
        newGame.players[index].playerId = static_cast<uint8_t>(index + 1);
        newGame.players[index].cash = 1500;
        newGame.players[index].flags = 0x04;
    }
    connected.debt.bankruptcyResolved = true;
    authoritySnapshotToEvent(newGame, false, authorityEvent);
    connected.authorityRoomId = 101;
    authorityEvent.roomId = 101;
    appHandleTransportEvent(connected, authorityEvent, 2500);
    ok &= expect(out, connected.stateVersion == 85 && connected.playerCount == 5,
                 "lower-version snapshot remains rejected without resync");
    authoritySnapshotToEvent(newGame, true, authorityEvent);
    authorityEvent.roomId = 202;
    appHandleTransportEvent(connected, authorityEvent, 2510);
    ok &= expect(out, connected.stateVersion == 6 && connected.money == 1500 &&
                      connected.position == 0 && connected.playerCount == 4 &&
                      connected.turnsUntilYou == 3 && connected.homePhase == HomePhase::Waiting &&
                      connected.nav.current.page == ScreenPage::Home &&
                      !connected.debt.bankruptcyResolved && connected.modal.kind == ModalKind::None,
                 "new-room resync replaces terminal old-game state and recalculates queue");

    static gridopoly::protocol::AuthoritySnapshot full{};
    full = gridopoly::protocol::AuthoritySnapshot{};
    full.phase = 2;
    full.activePlayerId = 1;
    full.decisionPlayerId = 1;
    full.boardSize = 24;
    full.playerCount = 3;
    full.assetCount = 16;
    full.round = 2;
    full.stateVersion = 7;
    full.lastEventSequence = 42;
    const char boardId[] = "grid-city-24-v1";
    full.boardIdHash = gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(boardId), strlen(boardId));
    full.pendingMoveFlags = 1;
    full.pendingMovePlayerId = 1;
    full.pendingMoveOrigin = 0;
    full.pendingMoveTarget = 9;
    full.pendingMoveDieA = 4;
    full.pendingMoveDieB = 5;
    full.auctionFlags = 0x02;
    full.auctionAssetIndex = 7;
    full.auctionCurrentBidderId = 2;
    full.auctionHighestBidderId = 3;
    full.auctionPassedMask = 0x04;
    full.auctionReadyMask = 0x05;
    full.auctionRequiredReadyMask = 0x07;
    full.auctionCurrentBid = 240;
    full.auctionGeneration = 0x89ABCDEFu;
    for (uint8_t index = 0; index < full.playerCount; ++index) {
        full.players[index].playerId = static_cast<uint8_t>(index + 1);
        full.players[index].position = static_cast<uint8_t>(index * 4);
        full.players[index].cash = 1750 - index * 100;
        full.players[index].flags = 0x04;
    }
    full.assets[2].ownerId = 1;
    full.assets[10].ownerId = 1;
    full.assets[10].buildingLevel = 2;
    full.assets[10].flags = 1;
    static uint8_t authorityPayload[gridopoly::protocol::kMaxPayloadSize]{};
    size_t authorityLength = 0;
    static gridopoly::protocol::AuthoritySnapshot decodedFull{};
    ok &= expect(out,
                 gridopoly::protocol::encodeAuthoritySnapshot(
                     full, authorityPayload, sizeof(authorityPayload), authorityLength) &&
                 gridopoly::protocol::decodeAuthoritySnapshot(
                     authorityPayload, authorityLength, decodedFull) &&
                 fullAuthoritySnapshotsEqual(full, decodedFull),
                 "full authority snapshot codec preserves every synchronized field");
    static TransportEvent fullEvent{};
    ok &= expect(out, fullAuthoritySnapshotToEvent(decodedFull, false, fullEvent) &&
                      fullEvent.auctionFlags == 0x02 &&
                      fullEvent.auctionReadyMask == 0x05 &&
                      fullEvent.auctionRequiredReadyMask == 0x07 &&
                      fullEvent.auctionGeneration == 0x89ABCDEFu,
                 "full authority snapshot maps to one bounded application event");
    appHandleTransportEvent(connected, fullEvent, 4000);
    ok &= expect(out, connected.fullAuthoritySnapshotValid &&
                      connected.boardCatalogCompatible && connected.money == 1750 &&
                      connected.position == 0 && connected.dieA == 4 &&
                      connected.dieB == 5 && connected.rolledSteps == 9 &&
                      appVisibleAssetCount(connected) == 2 &&
                      appVisibleAssetIndex(connected, 0) == 2 &&
                      appVisibleAssetIndex(connected, 1) == 10 &&
                      strcmp(appAssetDisplayName(connected, 10), "Aurora Avenue") == 0 &&
                      appAssetMortgaged(connected, 10) &&
                      connected.auctionFlags == 0x02 &&
                      connected.auctionReadyMask == 0x05 &&
                      connected.auctionRequiredReadyMask == 0x07 &&
                      connected.auctionGeneration == 0x89ABCDEFu,
                 "authority projection drives exact dice and owned-asset rows");

    static gridopoly::protocol::RosterSnapshot roster{};
    roster = gridopoly::protocol::RosterSnapshot{};
    roster.stateVersion = 7;
    roster.playerCount = 3;
    for (uint8_t index = 0; index < roster.playerCount; ++index) {
        roster.playerIds[index] = static_cast<uint8_t>(index + 1);
    }
    strcpy(roster.displayNames[0].data(), "ALEX");
    strcpy(roster.displayNames[1].data(), "MORGAN");
    strcpy(roster.displayNames[2].data(), "CASEY");
    static TransportEvent rosterEvent{};
    ok &= expect(out, rosterSnapshotToEvent(roster, false, rosterEvent),
                 "roster snapshot maps to one bounded application event");
    appHandleTransportEvent(connected, rosterEvent, 4010);
    ok &= expect(out, connected.rosterSnapshotValid &&
                      strcmp(appPlayerDisplayName(connected, 0), "ALEX") == 0 &&
                      strcmp(appPlayerDisplayName(connected, 2), "CASEY") == 0,
                 "roster projection replaces demo player names");

    gridopoly::protocol::GameEventRecord bidRecord{};
    bidRecord.sequence = 43;
    bidRecord.kind = 23;
    bidRecord.actorId = 2;
    bidRecord.assetIndex = 10;
    bidRecord.amount = 230;
    static TransportEvent bidEvent{};
    ok &= expect(out, gameEventToTransportEvent(bidRecord, 7, false, bidEvent),
                 "game event maps without changing authoritative economy state");
    const char *toastBeforeBid = connected.toast;
    appHandleTransportEvent(connected, bidEvent, 4020);
    const ActivityEntry *bidActivity = appActivityEntryAt(connected, 0);
    ok &= expect(out, connected.lastGameEvent.sequence == 43 &&
                      connected.lastGameEvent.amount == 230 &&
                      connected.activity.count == 1 &&
                       connected.activity.bannerSequence == 43 &&
                       bidActivity != nullptr && bidActivity->event.actorId == 2 &&
                       bidActivity->selfOwnedAsset &&
                       strcmp(appPlayerNameById(connected, 2), "MORGAN") == 0 &&
                      connected.toast == toastBeforeBid &&
                      connected.money == 1750,
                 "other-player event enters the named passive feed without mutating cash or toast");

    const uint32_t activityRevision = connected.revision;
    appHandleTransportEvent(connected, bidEvent, 4021);
    ok &= expect(out, connected.activity.count == 1 &&
                      connected.lastGameEvent.sequence == 43 &&
                      connected.revision == activityRevision,
                 "duplicate event sequence cannot duplicate the activity feed or banner");

    gridopoly::protocol::GameEventRecord rentRecord{};
    rentRecord.sequence = 44;
    rentRecord.kind = 9;
    rentRecord.actorId = 3;
    rentRecord.targetId = 1;
    rentRecord.assetIndex = 0;
    rentRecord.amount = 6;
    static TransportEvent rentEvent{};
    ok &= expect(out, gameEventToTransportEvent(rentRecord, 8, false, rentEvent),
                 "other-player rent maps to a passive activity event");
    appHandleTransportEvent(connected, rentEvent, 4025);
    ok &= expect(out, connected.activity.count == 2 &&
                       connected.activity.bannerSequence == 44 &&
                       appActivityEntryAt(connected, 0)->event.targetId == connected.selfSeatId &&
                       appActivityEntryAt(connected, 0)->selfOwnedAsset &&
                       appActivityEntryAt(connected, 1)->announced,
                 "a newer activity marks self-owned property without rewriting its title");

    connected.nav.current = NavigationEntry{ScreenPage::Trade, 2, 0};
    connected.inlineEditField = InlineEditField::TradeAmount;
    const int32_t editingAmount = connected.tradeAmount;
    gridopoly::protocol::GameEventRecord moveRecord{};
    moveRecord.sequence = 45;
    moveRecord.kind = 5;
    moveRecord.actorId = 3;
    moveRecord.amount = 11;
    static TransportEvent moveEvent{};
    ok &= expect(out, gameEventToTransportEvent(moveRecord, 8, false, moveEvent),
                 "other-player move maps to a passive activity event");
    appHandleTransportEvent(connected, moveEvent, 4030);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Trade &&
                      connected.nav.current.focus == 2 &&
                       connected.inlineEditField == InlineEditField::TradeAmount &&
                       connected.tradeAmount == editingAmount &&
                       connected.activity.count == 3 &&
                       !appActivityEntryAt(connected, 0)->selfOwnedAsset,
                 "passive activity never interrupts the current page, focus, or inline edit");

    appHandleInput(connected, InputEvent{InputKind::Rotate, 1, 4040}, 4040);
    ok &= expect(out, connected.activity.bannerSequence == 0 &&
                      appActivityEntryAt(connected, 0)->announced &&
                      appActivityEntryAt(connected, 1)->announced &&
                      connected.nav.current.focus == 2 &&
                      connected.inlineEditField == InlineEditField::TradeAmount,
                 "user input dismisses queued passive banners while preserving edit semantics");

    appHandleTouch(connected, TouchAction::ActivityOpen, 4050);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Activity &&
                      appActivityCount(connected) == 3,
                 "activity button opens the scrollable feed without unread state");

    connected.nav.current.focus = 1;
    connected.nav.current.listAnchor = 1;
    connected.activityListIndex = 1;
    const uint8_t activityDepth = connected.nav.depth;
    TransportEvent activityResync{};
    activityResync.kind = TransportEventKind::StateSnapshotApplied;
    activityResync.roomId = connected.authorityRoomId;
    activityResync.resync = true;
    activityResync.stateVersion = connected.stateVersion;
    activityResync.phase = connected.authorityPhase;
    activityResync.selfSeatId = connected.selfSeatId;
    activityResync.activePlayerId = connected.activePlayerId;
    activityResync.decisionPlayerId = connected.decisionPlayerId;
    activityResync.playerCount = connected.playerCount;
    activityResync.boardSize = connected.boardSize;
    activityResync.cash = connected.money;
    activityResync.playerPosition = connected.position;
    activityResync.pendingTarget = 0xFF;
    activityResync.tileAssetIndex = connected.tileAssetIndex;
    activityResync.debtAssetIndex = 0xFF;
    activityResync.auctionAssetIndex = 0xFF;
    activityResync.availableActions = connected.availableActions;
    for (uint8_t index = 0; index < connected.playerCount; ++index) {
        activityResync.players[index] = connected.authorityPlayers[index];
    }
    appHandleTransportEvent(connected, activityResync, 4055);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Activity &&
                      connected.nav.current.focus == 1 &&
                      connected.nav.current.listAnchor == 1 &&
                      connected.nav.depth == activityDepth,
                 "same-room resync preserves the activity page, row, anchor, and Back stack");

    appHandleTouch(connected, TouchAction::Footer, 4060);
    ok &= expect(out, connected.nav.current.page == ScreenPage::Trade &&
                      connected.nav.current.focus == 2,
                 "activity Back restores the exact page and focus without losing work");

    gridopoly::protocol::GameEventRecord replayRecord{};
    replayRecord.sequence = 46;
    replayRecord.kind = 10;
    replayRecord.actorId = 2;
    replayRecord.amount = 75;
    static TransportEvent replayEvent{};
    ok &= expect(out, gameEventToTransportEvent(replayRecord, 8, true, replayEvent),
                 "resync history maps to the same bounded activity event");
    connected.inlineEditField = InlineEditField::None;
    appHandleTransportEvent(connected, replayEvent, 4070);
    ok &= expect(out, connected.activity.count == 4 &&
                       connected.activity.bannerSequence == 46 &&
                       appActivityEntryAt(connected, 0) != nullptr &&
                       appActivityEntryAt(connected, 0)->announced,
                 "new resync activity immediately replaces the HUD banner");

    gridopoly::protocol::GameEventRecord ownedArrivalRecord{};
    ownedArrivalRecord.sequence = 47;
    ownedArrivalRecord.kind = 5;
    ownedArrivalRecord.actorId = 2;
    ownedArrivalRecord.amount = 5; // grid-city-24-v1 tile 5 -> owned asset 2
    static TransportEvent ownedArrivalEvent{};
    ok &= expect(out, gameEventToTransportEvent(ownedArrivalRecord, 8, false,
                                                ownedArrivalEvent),
                 "move-completed destination maps to a passive activity event");
    appHandleTransportEvent(connected, ownedArrivalEvent, 4075);
    ok &= expect(out, connected.activity.count == 5 &&
                       appActivityEntryAt(connected, 0)->event.assetIndex == 0xFF &&
                       appActivityEntryAt(connected, 0)->selfOwnedAsset,
                 "arrival at a self-owned tile resolves through the board without a wire asset index");

    connected.authorityAssets[2].ownerId = 2;
    const ActivityEntry *historicOwnedArrival = appActivityEntryAt(connected, 0);
    gridopoly::protocol::GameEventRecord transferredArrivalRecord = ownedArrivalRecord;
    transferredArrivalRecord.sequence = 48;
    transferredArrivalRecord.actorId = 3;
    static TransportEvent transferredArrivalEvent{};
    ok &= expect(out, gameEventToTransportEvent(transferredArrivalRecord, 8, false,
                                                transferredArrivalEvent),
                 "post-transfer arrival maps to a new passive activity event");
    appHandleTransportEvent(connected, transferredArrivalEvent, 4076);
    ok &= expect(out, connected.activity.count == 6 &&
                       historicOwnedArrival != nullptr &&
                       historicOwnedArrival->selfOwnedAsset &&
                       !appActivityEntryAt(connected, 0)->selfOwnedAsset &&
                       appActivityEntryAt(connected, 1)->selfOwnedAsset,
                 "asset transfer preserves the historic MY pill and omits it from new activity");

    gridopoly::protocol::Heartbeat heartbeat{};
    heartbeat.flags = 1;
    heartbeat.appliedStateVersion = 7;
    heartbeat.appliedEventSequence = 43;
    uint8_t heartbeatPayload[16]{};
    size_t heartbeatLength = 0;
    gridopoly::protocol::Heartbeat decodedHeartbeat{};
    ok &= expect(out,
                 gridopoly::protocol::encodeHeartbeat(
                     heartbeat, heartbeatPayload, sizeof(heartbeatPayload), heartbeatLength) &&
                 heartbeatLength == 12 &&
                 gridopoly::protocol::decodeHeartbeat(
                     heartbeatPayload, heartbeatLength, decodedHeartbeat) &&
                 decodedHeartbeat.flags == 1 &&
                 decodedHeartbeat.appliedStateVersion == 7 &&
                 decodedHeartbeat.appliedEventSequence == 43,
                 "heartbeat carries cumulative state and event acknowledgements");

    gridopoly::protocol::PlayerDetailRequest detailWireRequest{};
    detailWireRequest.requestId = 0x10203040u;
    detailWireRequest.targetPlayerId = 3;
    detailWireRequest.expectedStateVersion = 77;
    uint8_t detailRequestPayload[gridopoly::protocol::kPlayerDetailRequestSize]{};
    size_t detailRequestLength = 0;
    gridopoly::protocol::PlayerDetailRequest decodedDetailRequest{};
    ok &= expect(out,
                 gridopoly::protocol::encodePlayerDetailRequest(
                     detailWireRequest, detailRequestPayload, sizeof(detailRequestPayload),
                     detailRequestLength) &&
                 detailRequestLength == gridopoly::protocol::kPlayerDetailRequestSize &&
                 gridopoly::protocol::decodePlayerDetailRequest(
                     detailRequestPayload, detailRequestLength, decodedDetailRequest) &&
                 decodedDetailRequest.requestId == detailWireRequest.requestId &&
                 decodedDetailRequest.targetPlayerId == 3 &&
                 decodedDetailRequest.expectedStateVersion == 77,
                 "player detail request codec preserves correlation and target");

    static gridopoly::protocol::PlayerDetailResponse detailWireResponse{};
    detailWireResponse = gridopoly::protocol::PlayerDetailResponse{};
    detailWireResponse.requestId = detailWireRequest.requestId;
    detailWireResponse.stateVersion = 78;
    detailWireResponse.cash = 1234;
    detailWireResponse.targetPlayerId = 3;
    detailWireResponse.position = 17;
    detailWireResponse.assetCount = gridopoly::protocol::kMaxPlayerDetailAssets;
    detailWireResponse.totalOwnedAssets = detailWireResponse.assetCount;
    detailWireResponse.ledgerCount = gridopoly::protocol::kMaxPlayerDetailLedgerEntries;
    for (uint8_t index = 0; index < detailWireResponse.assetCount; ++index) {
        detailWireResponse.assets[index].assetIndex = index;
        detailWireResponse.assets[index].state = static_cast<uint8_t>(index & 0x0Fu);
    }
    for (uint8_t index = 0; index < detailWireResponse.ledgerCount; ++index) {
        detailWireResponse.ledger[index].sequence = 200 - index;
        detailWireResponse.ledger[index].amount = index == 9 ? -375 : 50 + index;
        detailWireResponse.ledger[index].kind = static_cast<uint8_t>(8 + index);
        detailWireResponse.ledger[index].counterpartyId = static_cast<uint8_t>(index % 5 + 1);
        detailWireResponse.ledger[index].assetIndex = index;
        detailWireResponse.ledger[index].flags = index == 9 ? 0 :
            gridopoly::protocol::PlayerDetailLedgerFlagCredit;
    }
    static uint8_t detailResponsePayload[gridopoly::protocol::kMaxPayloadSize]{};
    size_t detailResponseLength = 0;
    static gridopoly::protocol::PlayerDetailResponse decodedDetailResponse{};
    static uint8_t detailFrame[gridopoly::protocol::kMaxFrameSize]{};
    size_t detailFrameLength = 0;
    gridopoly::protocol::Header detailHeader{};
    detailHeader.type = gridopoly::protocol::MessageType::PlayerDetailResponse;
    detailHeader.flags = gridopoly::protocol::FlagResponse;
    detailHeader.sequence = 9;
    detailHeader.acknowledgement = 8;
    detailHeader.roomId = 7;
    detailHeader.deviceId = 6;
    ok &= expect(out,
                 gridopoly::protocol::encodePlayerDetailResponse(
                     detailWireResponse, detailResponsePayload, sizeof(detailResponsePayload),
                     detailResponseLength) &&
                 detailResponseLength == gridopoly::protocol::kMaxPlayerDetailResponseSize &&
                 gridopoly::protocol::decodePlayerDetailResponse(
                     detailResponsePayload, detailResponseLength, decodedDetailResponse) &&
                 decodedDetailResponse.assetCount == 28 &&
                 decodedDetailResponse.ledgerCount == 10 &&
                 decodedDetailResponse.assets[27].assetIndex == 27 &&
                 decodedDetailResponse.ledger[9].amount == -375 &&
                 gridopoly::protocol::encodeFrame(
                     detailHeader, detailResponsePayload, detailResponseLength, detailFrame,
                     sizeof(detailFrame), detailFrameLength) &&
                 detailFrameLength == 228,
                 "maximum player detail response round-trips in one ESP-NOW frame");

    gridopoly::protocol::TradeRequest tradeMutation{};
    tradeMutation.operation = gridopoly::protocol::TradeOperation::Create;
    tradeMutation.targetPlayerId = 2;
    tradeMutation.requestId = 0x55667788u;
    tradeMutation.selfGivesCash = 150;
    tradeMutation.selfAssetCount = 1;
    tradeMutation.selfAssets[0] = 7;
    uint8_t tradePayload[gridopoly::protocol::kMaxTradeRequestSize]{};
    size_t tradeLength = 0;
    ok &= expect(out,
                 !gridopoly::protocol::encodeTradeRequest(
                     tradeMutation, tradePayload, sizeof(tradePayload), tradeLength),
                 "trade mutation codec rejects a zero expected state version");
    tradeMutation.expectedStateVersion = 91;
    gridopoly::protocol::TradeRequest decodedTradeMutation{};
    ok &= expect(out,
                 gridopoly::protocol::encodeTradeRequest(
                     tradeMutation, tradePayload, sizeof(tradePayload), tradeLength) &&
                 gridopoly::protocol::decodeTradeRequest(
                     tradePayload, tradeLength, decodedTradeMutation) &&
                 decodedTradeMutation.expectedStateVersion == 91 &&
                 decodedTradeMutation.selfAssetCount == 1 &&
                 decodedTradeMutation.selfAssets[0] == 7,
                 "trade mutation codec preserves its exact nonzero state version and assets");
    gridopoly::protocol::TradeRequest tradeQuery{};
    tradeQuery.operation = gridopoly::protocol::TradeOperation::Query;
    tradeQuery.requestId = 0x10293847u;
    ok &= expect(out,
                 gridopoly::protocol::encodeTradeRequest(
                     tradeQuery, tradePayload, sizeof(tradePayload), tradeLength) &&
                 tradeLength == gridopoly::protocol::kTradeRequestBaseSize,
                 "trade Query alone may use expected state version zero");

    gridopoly::protocol::PairAccept pairAccept{};
    pairAccept.accepted = 1;
    pairAccept.seatId = 2;
    pairAccept.wifiChannel = 0;
    pairAccept.serverDeviceId = 0x11223344u;
    pairAccept.stateVersion = 91;
    pairAccept.sessionId = 0x55667788u;
    uint8_t pairAcceptPayload[20]{};
    size_t pairAcceptLength = 0;
    gridopoly::protocol::PairAccept decodedPairAccept{};
    ok &= expect(out,
                 gridopoly::protocol::encodePairAccept(
                     pairAccept, pairAcceptPayload, sizeof(pairAcceptPayload),
                     pairAcceptLength) &&
                 pairAcceptLength == 17 &&
                 gridopoly::protocol::decodePairAccept(
                     pairAcceptPayload, pairAcceptLength, decodedPairAccept) &&
                 decodedPairAccept.wifiChannel == 0 &&
                 decodedPairAccept.sessionId == pairAccept.sessionId,
                 "UDP PairAccept v2 preserves its nonzero session id");

    static std::array<uint8_t, gridopoly::protocol::kUdpKeySize> udpPairKey{};
    static std::array<uint8_t, gridopoly::protocol::kUdpKeySize> udpSessionKey{};
    gridopoly::protocol::deriveUdpPairKey(
        "gridopoly-selftest-commissioning-key", udpPairKey);
    gridopoly::protocol::deriveUdpSessionKey(
        udpPairKey, pairAccept.serverDeviceId, 0xAABBCCDDu, 0x10203040u,
        0x33445566u, pairAccept.sessionId, udpSessionKey);
    gridopoly::protocol::UdpEnvelopeHeader udpHeader{};
    udpHeader.sessionId = pairAccept.sessionId;
    udpHeader.senderDeviceId = 0xAABBCCDDu;
    udpHeader.packetSequence = 0x100000002ULL;
    udpHeader.frameLength = static_cast<uint16_t>(detailFrameLength);
    static uint8_t udpDatagram[gridopoly::protocol::kMaxUdpDatagramSize]{};
    size_t udpDatagramLength = 0;
    gridopoly::protocol::DecodedUdpDatagram decodedUdp{};
    const bool udpEncoded = gridopoly::protocol::encodeUdpDatagram(
        udpHeader, udpSessionKey, detailFrame, detailFrameLength, udpDatagram,
        sizeof(udpDatagram), udpDatagramLength);
    const bool udpDecoded = udpEncoded && gridopoly::protocol::decodeUdpDatagram(
        udpDatagram, udpDatagramLength, udpSessionKey, decodedUdp);
    ok &= expect(out,
                 udpDecoded &&
                 udpDatagramLength == detailFrameLength +
                     gridopoly::protocol::kUdpEnvelopeHeaderSize &&
                 decodedUdp.header.sessionId == pairAccept.sessionId &&
                 decodedUdp.header.packetSequence == udpHeader.packetSequence &&
                 decodedUdp.header.frameLength == detailFrameLength &&
                 memcmp(decodedUdp.frame, detailFrame, detailFrameLength) == 0,
                 "UDP envelope round-trips one complete existing Gridopoly frame");
    udpDatagram[udpDatagramLength - 1] ^= 0x01u;
    ok &= expect(out,
                 !gridopoly::protocol::decodeUdpDatagram(
                     udpDatagram, udpDatagramLength, udpSessionKey, decodedUdp),
                 "UDP envelope rejects a frame changed after HMAC generation");
    udpDatagram[udpDatagramLength - 1] ^= 0x01u;

    gridopoly::protocol::UdpReplayWindow replayWindow{};
    ok &= expect(out,
                 replayWindow.accept(100) && replayWindow.accept(102) &&
                 replayWindow.accept(101) && !replayWindow.accept(101) &&
                 !replayWindow.accept(38),
                 "UDP replay window accepts bounded reordering and rejects duplicates or stale packets");

    static gridopoly::protocol::StateSnapshot duplicate{};
    duplicate = snapshot;
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
