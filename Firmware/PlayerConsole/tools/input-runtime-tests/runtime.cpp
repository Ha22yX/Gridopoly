// Include the real implementations to inspect canvas lifetime and retained ink;
// only hardware allocation/tasks/network assets are stubbed, LVGL is real.
#include "../../ui_handwriting.cpp"
#include "../../ui_renderer.cpp"
#include "../../touch_input_filter.h"
#include "handwriting-fixtures.h"
#include "../home-build-fixtures.h"
#include <gridopoly/core/BoardCatalog.h>

void remoteAvatarCacheBegin() {}
void remoteAvatarCachePreload(const TransportAvatarRecipe &) {}
RemoteAvatarPreloadProgress remoteAvatarCachePreloadProgress() { return {}; }
void remoteAvatarCacheReleaseSetup() {}
void remoteAvatarCacheReleaseFinals() {}
void remoteAvatarCacheRequestPreview(const TransportAvatarRecipe &) {}
unsigned previewQueries = 0;
RemoteAvatarPreviewFrame remoteAvatarPreviewFrame(const TransportAvatarRecipe &) { ++previewQueries; return {}; }
const lv_img_dsc_t *remoteAvatarPreview(const TransportAvatarRecipe &) { return nullptr; }
const lv_img_dsc_t *remoteAvatarFinal(uint32_t, uint8_t, uint16_t, uint64_t) { return nullptr; }
bool remoteAvatarCacheConsumeUpdate() { return false; }
void remoteTileCacheBegin() {}
RemoteArtworkRequest selectedArtwork{}, prefetchedArtwork{}, acquiredArtwork{};
unsigned artAcquires = 0, artReleases = 0;
bool artReady = false;
static uint8_t testArtPixels[32768]{};
static lv_img_dsc_t testArtImage{};
void remoteTileCacheSetView(uint32_t, uint8_t, GridCityArtwork art, const char *key, uint8_t level) {
    selectedArtwork = artworkRequest(art, key, level);
}
void remoteTileCacheReleaseImage(const lv_img_dsc_t *image) { if (image == &testArtImage) ++artReleases; }
void remoteTileCachePrefetch(GridCityArtwork art, const char *key, uint8_t level) {
    prefetchedArtwork = artworkRequest(art, key, level);
}
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork art, const char *key, uint8_t level) {
    acquiredArtwork = artworkRequest(art, key, level);
    if (!artReady) return nullptr;
    ++artAcquires; return &testArtImage;
}
bool remoteTileCacheConsumeUpdate() { return false; }

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failed;
    };
    lv_init();
    static lv_color_t pixels[480 * 20];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display); display.hor_res = display.ver_res = 480;
    display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *d, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(d); };
    lv_disp_drv_register(&display);
    check(uiRendererBegin(), "production renderer initializes with real LVGL");
    AppState app{}; appInit(app, 0);
    app.authorityRoomId = 108; app.selfSeatId = 1; app.hasAuthoritativeState = true;
    app.authorityOnline = true;
    app.page = app.nav.current.page = ScreenPage::NameHandwriting;
    app.focus = app.nav.current.focus = 0;
    app.identity.phase = IdentityClientPhase::NameHandwriting;
    app.identity.authorityPhase = TransportIdentityPhase::AwaitName;
    app.identity.revision = 1;
    app.identity.tagPairSnapshotValid = true;
    app.identity.tagPair.flags = kTagPairEnabled | kTagPairSelfBound | kTagPairCanEditIdentity;
    app.identity.tagPair.selfPlayerId = 1;
    app.identity.tagPair.revision = 7;
    uiRendererRender(app, 1); lv_obj_update_layout(lv_scr_act());
    appendPhysicalSample(20, 20, 10, false);
    appendPhysicalSample(20, 80, 40, true);
    const auto generation = captureGeneration;
    auto *canvas = activeCanvas;
    TransportEvent heartbeat{};
    heartbeat.kind = TransportEventKind::TagPairSnapshotReceived;
    heartbeat.roomId = 108; heartbeat.selfSeatId = 1;
    auto pair = app.identity.tagPair; heartbeat.tagPair = &pair;
    appHandleTransportEvent(app, heartbeat, 50);
    uiRendererRender(app, 50);
    check(activeCanvas == canvas && pointCount == 2 && captureGeneration == generation,
          "unchanged production Tag heartbeat preserves canvas, samples and recognition generation");
    appIdentityAppendCharacter(app, 'A');
    uiRendererRender(app, 60);
    check(pointCount == 2 && captureGeneration == generation,
          "name label update preserves the unfinished next character");
    TransportEvent lost{}; lost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(app, lost, 70); uiRendererRender(app, 70);
    check(pointCount == 2 && captureGeneration == generation,
          "offline badge and toast do not erase unfinished handwriting");
    app.authorityOnline = true;
    uiHandwritingClearInk();
    uiHandwritingRecordTouch(true, 100, 140, 100);
    uiHandwritingRecordTouch(true, 100, 280, 240);
    check(pointCount == 2 && points[1].connectsPrevious,
          "continuous contact survives a 140ms controller/LVGL scheduling gap");
    uiHandwritingRecordTouch(false, 100, 280, 250);
    uiHandwritingRecordTouch(true, 220, 140, 260);
    check(pointCount == 3 && !points[2].connectsPrevious,
          "a real lift breaks the stroke even when the next press follows in 10ms");
    uiHandwritingClearInk();
    // Controller samples arrive even when the application does not poll/render.
    for (uint32_t t = 100; t <= 240; t += 10)
        uiHandwritingRecordTouch(true, 100, static_cast<int16_t>(140 + (t - 100)), t);
    check(pointCount == 15 && points[14].connectsPrevious && drawnPointCount >= 14,
          "10ms controller samples preserve and draw a stroke across a 140ms app stall");
    uiHandwritingRecordTouch(false, 100, 280, 250);
    uiHandwritingRecordTouch(true, 220, 140, 260);
    check(pointCount == 16 && !points[15].connectsPrevious,
          "physical lift and new press between app polls never join separate strokes");
    uiHandwritingRecordTouch(false, 220, 140, 270);
    char letter = '\0';
    check(!uiHandwritingPoll(letter, 200) && pointCount == 16,
          "an older caller timestamp cannot force an early recognition");
    check(!uiHandwritingRecognitionDue(false, 3, 700, 1400) &&
          !uiHandwritingRecognitionDue(false, 3, 700, 1899) &&
           uiHandwritingRecognitionDue(false, 3, 700, 1900),
          "recognition waits the full 1200ms after the last physical contact");
    check(uiHandwritingRecognitionDue(false, 3, UINT32_MAX - 599, 600),
          "letter idle deadline survives millis wrap");
    uiHandwritingClearInk();
    uint32_t t = 1000;
    auto stroke = [&](int x1, int y1, int x2, int y2) {
        for (int i = 0; i <= 10; ++i) {
            uiHandwritingRecordTouch(true, x1 + (x2 - x1) * i / 10,
                                     y1 + (y2 - y1) * i / 10, t);
            t += 10;
        }
        uiHandwritingRecordTouch(false, x2, y2, t); t += 40;
    };
    stroke(120, 145, 120, 295);
    stroke(120, 220, 240, 220);
    check(!uiHandwritingPoll(letter, t + 500), "H stays open for its third stroke during short gaps");
    stroke(240, 145, 240, 295);
    check(!uiHandwritingPoll(letter, lastCanvasTouchMs + 1199), "H is not committed before the full lift interval");
    uint32_t hMatch = 0, hOther = 0;
    const char hTemplate = recognizeSamplesTemplate(points, pointCount, &hMatch, &hOther);
    rasterNeuralInput(points, pointCount);
    const auto hNeural = inferNeuralModel();
    const bool hRecognized = uiHandwritingPoll(letter, lastCanvasTouchMs + 1200);
    std::printf("H fixture: result=%c template=%c neural=%c margin=%.3f\n", letter, hTemplate,
                'A' + hNeural.glyph, hNeural.margin);
    std::printf("H template distances: best=%lu other=%lu\n", hMatch, hOther);
    check(hRecognized && letter == 'H' && !uiHandwritingHasInk(),
          "real local model recognizes a complete three-stroke H once");
    UiHandwritingSample sparse[] = {{20, 10, 1, false}, {20, 100, 11, true}, {20, 190, 21, true}};
    check(uiHandwritingRecognizeSamples(sparse, 3) != '\0' &&
          uiHandwritingRecognizeSamples(sparse, 2) != '\0',
          "a sparse two/three-point visible line is not silently rejected");
    unsigned classifierRegressions = 0, beforeCorrect = 0, afterCorrect = 0;
    for (const auto &fixture : letterFixtures()) {
        const auto samples = fixtureSamples(fixture);
        rasterNeuralInput(samples.data(), static_cast<uint16_t>(samples.size()));
        const auto neural = inferNeuralModel();
        const char prior = neural.margin >= kNeuralConfidentMargin ? 'A' + neural.glyph :
            recognizeSamplesTemplate(samples.data(), static_cast<uint16_t>(samples.size()));
        const char after = uiHandwritingRecognizeSamples(samples.data(), static_cast<uint16_t>(samples.size()));
        beforeCorrect += prior == fixture.expected; afterCorrect += after == fixture.expected;
        classifierRegressions += prior == fixture.expected && after != fixture.expected;
        std::printf("letter %c: previous-rule=%c current=%c\n", fixture.expected, prior, after);
        if (fixture.expected == 'R' || fixture.expected == 'K' ||
            fixture.expected == 'T' || fixture.expected == 'Y')
            check(after == fixture.expected, "existing R/K/T/Y stroke distinction is preserved");
        if (fixture.expected == 'H') {
            bool variantsPass = true;
            for (int width : {50, 80, 100, 120}) for (int middle : {45, 50, 55}) {
                const auto variant = fixtureSamples(fixture, width, middle);
                const char result = uiHandwritingRecognizeSamples(variant.data(),
                    static_cast<uint16_t>(variant.size()));
                uint32_t match = 0, other = 0;
                const char shape = recognizeSamplesTemplate(variant.data(),
                    static_cast<uint16_t>(variant.size()), &match, &other);
                std::printf("H variant width=%d bar=%d result=%c template=%c distances=%lu/%lu\n",
                            width, middle, result, shape, match, other);
                variantsPass &= result == 'H';
                const auto dense = densify(variant);
                variantsPass &= uiHandwritingRecognizeSamples(dense.data(),
                    static_cast<uint16_t>(dense.size())) == result;
            }
            check(variantsPass, "three-stroke sparse and dense H agree across twelve width/crossbar variants");
        }
    }
    std::printf("independent A-Z paths: previous=%u/26 current=%u/26 regressions=%u\n",
                beforeCorrect, afterCorrect, classifierRegressions);
    check(classifierRegressions == 0 && afterCorrect > beforeCorrect,
          "geometric certainty gate improves independent A-Z paths without losing a correct baseline letter");
    unsigned matrixPrior = 0, matrixCurrent = 0, matrixLost = 0;
    for (const auto &fixture : letterFixtures())
        for (int width : {50, 80, 100, 120}) for (int middle : {45, 50, 55}) {
            const auto samples = fixtureSamples(fixture, width, middle);
            const auto count = static_cast<uint16_t>(samples.size());
            rasterNeuralInput(samples.data(), count);
            const auto neural = inferNeuralModel();
            uint32_t match = 0, other = 0;
            const char shape = recognizeSamplesTemplate(samples.data(), count, &match, &other);
            const char prior = neural.margin >= kNeuralConfidentMargin ? 'A' + neural.glyph : shape;
            const char current = uiHandwritingRecognizeSamples(samples.data(), count);
            matrixPrior += prior == fixture.expected;
            matrixCurrent += current == fixture.expected;
            if (prior == fixture.expected && current != fixture.expected) {
                ++matrixLost;
                std::printf("matrix regression %c width=%d bar=%d -> %c\n", fixture.expected, width, middle, current);
            }
        }
    std::printf("312 path variants: prior=%u current=%u regressions=%u\n",
                matrixPrior, matrixCurrent, matrixLost);
    check(matrixLost == 0 && matrixCurrent > matrixPrior,
          "general geometry gate improves 312 A-Z path variants without baseline regressions");
    uiHandwritingRecordTouch(true, 100, 150, 3000);
    uiHandwritingRecordTouch(false, 100, 150, 3010);
    appHandleTouch(app, TouchAction::HandwritingConfirm, 3020);
    uiRendererServiceHandwriting(app, 3020);
    check(app.page == ScreenPage::NameHandwriting && uiHandwritingHasInk() && recognitionFailed &&
          !app.identity.handwritingDoneRequested,
          "DONE with unrecognizable tap keeps its ink and stays on the writing page");
    const auto failedGeneration = attemptedGeneration;
    uiRendererServiceHandwriting(app, 3030);
    check(attemptedGeneration == failedGeneration && recognitionFailed,
          "failed unchanged ink is not repeatedly submitted");
    uiHandwritingRecordTouch(true, 100, 150, 3032);
    uiHandwritingRecordTouch(true, 100, 200, 3036);
    uiHandwritingRecordTouch(false, 100, 200, 3038);
    uiRendererServiceHandwriting(app, 3039);
    check(app.page == ScreenPage::NameHandwriting && uiHandwritingHasInk() &&
          !app.identity.handwritingDoneRequested && strcmp(app.identity.draftName, "A") == 0,
          "correction after failed DONE gets a full idle window and cannot commit at release+1ms");
    appHandleTouch(app, TouchAction::NameDelete, 3040);
    uiRendererServiceHandwriting(app, 3040);
    check(!uiHandwritingHasInk() && strcmp(app.identity.draftName, "A") == 0 &&
          !app.identity.handwritingDoneRequested,
          "DELETE clears current ink and cancels DONE before deleting existing draft text");
    appHandleTouch(app, TouchAction::NameDelete, 3050);
    uiRendererServiceHandwriting(app, 3050);
    check(app.identity.draftName[0] == '\0', "DELETE on empty canvas removes the last draft character");
    // Hold the worker so DONE must wait through several service iterations.
    recognitionTaskHandle = reinterpret_cast<void *>(1);
    t = 4000; stroke(120, 145, 120, 295);
    appHandleTouch(app, TouchAction::HandwritingConfirm, t);
    uiRendererServiceHandwriting(app, t);
    check(app.page == ScreenPage::NameHandwriting && recognitionWorkerBusy &&
          app.identity.handwritingDoneRequested, "touch DONE defers navigation while final ink is recognizing");
    recognitionWorkerBusy = false; recognitionResultReady = true;
    recognitionResultGeneration = captureGeneration; recognitionResult = 'I';
    uiRendererServiceHandwriting(app, t + 10);
    check(app.page == ScreenPage::NameReview && strcmp(app.identity.draftName, "I") == 0,
          "deferred DONE appends the last worker result before returning to name review");
    uiRendererRender(app, t + 10);
    appHandleTouch(app, TouchAction::NameEdit, t + 20);
    uiRendererRender(app, t + 20); lv_obj_update_layout(lv_scr_act());
    uiHandwritingRecordTouch(true, 100, 140, t + 30);
    uiHandwritingRecordTouch(true, 100, 200, t + 40);
    appHandleInput(app, InputEvent{InputKind::Rotate, 1, t + 50}, t + 50);
    appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, t + 60}, t + 60);
    appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, t + 90}, t + 90);
    uiRendererServiceHandwriting(app, t + 90);
    check(app.page == ScreenPage::NameHandwriting && app.identity.handwritingDoneRequested &&
          !recognitionWorkerBusy, "physical knob DONE waits until the finger actually lifts");
    uiHandwritingTouchUnavailable(t + 100);
    uiRendererServiceHandwriting(app, t + 100);
    check(!recognitionWorkerBusy && uiHandwritingHasInk(),
          "touch communication failure cannot act as the release for pending DONE");
    uiHandwritingRecordTouch(false, 100, 200, t + 110);
    uiRendererServiceHandwriting(app, t + 110);
    check(recognitionWorkerBusy, "valid release after recovery allows pending DONE to enqueue");
    const auto oldRequest = recognitionRequestGeneration;
    appHandleTouch(app, TouchAction::NameDelete, t + 120);
    uiRendererServiceHandwriting(app, t + 120);
    recognitionWorkerBusy = false; recognitionResultReady = true;
    recognitionResultGeneration = oldRequest; recognitionResult = 'X';
    uiRendererServiceHandwriting(app, t + 2000);
    check(!uiHandwritingHasInk() && strcmp(app.identity.draftName, "I") == 0,
          "deleted worker result cannot resurrect a removed letter");
    recognitionTaskHandle = nullptr;
    // Capacity failure must not consume or clear pending handwriting.
    memset(app.identity.draftName, 'A', sizeof(app.identity.draftName) - 1);
    app.identity.draftName[sizeof(app.identity.draftName) - 1] = '\0';
    t += 3000; stroke(120, 145, 120, 295);
    appHandleTouch(app, TouchAction::HandwritingConfirm, t);
    uiRendererServiceHandwriting(app, t);
    check(app.page == ScreenPage::NameHandwriting && uiHandwritingHasInk() &&
          strcmp(handwritingStatusText(app), "NAME IS FULL - CLEAR INK") == 0,
          "full draft keeps pending ink and explains why DONE cannot append");
    TouchInputState input;
    check(input.update(1, 100, 150, 10) && input.pressed &&
          !input.update(-1, 0, 0, 90) && input.pressed &&
          !input.update(-1, 0, 0, 110) && !input.pressed,
          "I2C error is distinct from physical lift and releases UI controls after 100ms");
    check(input.update(1, 100, 150, 120) && input.update(0, 0, 0, 130) && !input.pressed,
          "valid zero-touch sample releases immediately");
    input.update(1, 100, 150, UINT32_MAX - 49);
    check(!input.update(-1, 0, 0, 50) && !input.pressed,
          "touch error timeout survives millis wrap");
    app.page = app.nav.current.page = ScreenPage::AvatarSetup;
    app.identity.phase = IdentityClientPhase::AvatarEditing;
    app.identity.editingValue = false; app.focus = app.nav.current.focus = 0;
    ++app.revision; uiRendererRender(app, ++t);
    auto *row = avatarRowBindings[0].object;
    lv_refr_now(lv_disp_get_default());
    const unsigned previewBefore = previewQueries;
    const auto rebuildBefore = rendererTestStats.rebuildRenders;
    app.focus = app.nav.current.focus = 2; ++app.revision;
    uiRendererRender(app, ++t);
    auto *activeDisplay = lv_disp_get_default();
    bool untouchedRowDirty = false;
    for (uint16_t index = 0; index < activeDisplay->inv_p; ++index) {
        const auto &area = activeDisplay->inv_areas[index];
        if (area.x1 <= 200 && area.x2 >= 200 && area.y1 <= 175 && area.y2 >= 175)
            untouchedRowDirty = true;
    }
    check(!untouchedRowDirty,
          "avatar focus change does not invalidate an unrelated row");
    check(avatarRowBindings[0].object == row && previewQueries == previewBefore &&
          rendererTestStats.rebuildRenders == rebuildBefore,
          "avatar focus change retains real LVGL rows and does not reacquire/redraw portrait");
    app.identity.editingValue = true; ++app.revision; uiRendererRender(app, ++t);
    ++app.identity.draftRecipe.facePresetId; ++app.revision; uiRendererRender(app, ++t);
    check(avatarRowBindings[0].object == row && previewQueries == previewBefore + 1 &&
          rendererTestStats.rebuildRenders == rebuildBefore,
          "avatar edit refreshes one retained recipe without a full page rebuild");
    uiRendererInvalidateArtwork(); uiRendererRender(app, ++t);
    check(previewQueries == previewBefore + 2 && avatarRowBindings[0].object == row,
          "newly published portrait is still acquired when artwork invalidation arrives");
    bool focusStylesCorrect = true;
    for (uint8_t selected : {5, 1, 3, 0, 4, 2}) {
        app.focus = app.nav.current.focus = selected;
        app.identity.editingValue = false;
        ++app.revision; uiRendererRender(app, ++t);
        for (uint8_t index = 0; index < 5; ++index) {
            focusStylesCorrect &= lv_obj_get_style_bg_color(avatarRowBindings[index].object, 0).full ==
                lv_color_hex(index == selected ? 0x16302A : 0x061017).full;
            focusStylesCorrect &= lv_obj_get_style_border_color(avatarRowBindings[index].object, 0).full ==
                lv_color_hex(index == selected ? kGreen : 0x061017).full;
        }
        focusStylesCorrect &= lv_obj_get_style_bg_color(avatarConfirmObject, 0).full ==
            lv_color_hex(selected == 5 ? 0x16302A : kPanel).full;
        app.identity.editingValue = selected < 5;
        ++app.revision; uiRendererRender(app, ++t);
        for (uint8_t index = 0; index < 5; ++index)
            focusStylesCorrect &= strcmp(lv_label_get_text(avatarRowBindings[index].affordanceLabel),
                                          index == selected ? "<>" : ">") == 0;
    }
    check(focusStylesCorrect,
          "retained rows and confirm clear old highlights and show only the active edit affordance");
    app.page = app.nav.current.page = ScreenPage::NameHandwriting;
    app.identity.phase = IdentityClientPhase::NameHandwriting;
    app.identity.handwritingDoneRequested = false;
    app.focus = app.nav.current.focus = 0;
    std::strcpy(app.identity.draftName, "ABC");
    ++app.revision; uiRendererRender(app, ++t); lv_obj_update_layout(lv_scr_act());
    auto serviceQueuedActions = [&]() {
        uiRendererServiceHandwriting(app, ++t);
        TouchAction action = TouchAction::None;
        while (uiRendererPollTouch(action)) {
            appHandleTouch(app, action, ++t);
            uiRendererServiceHandwriting(app, ++t);
        }
        uiRendererServiceHandwriting(app, ++t);
    };
    enqueueTouch(TouchAction::NameDelete); enqueueTouch(TouchAction::NameDelete);
    serviceQueuedActions();
    check(strcmp(app.identity.draftName, "A") == 0,
          "two queued DELETE actions both take effect in their original order");
    uiHandwritingRecordTouch(true, 100, 150, ++t);
    uiHandwritingRecordTouch(false, 100, 150, ++t);
    enqueueTouch(TouchAction::NameDelete); enqueueTouch(TouchAction::HandwritingConfirm);
    serviceQueuedActions();
    check(app.page == ScreenPage::NameReview && strcmp(app.identity.draftName, "A") == 0,
          "queued DELETE then DONE clears ink and returns to review without losing DONE");
    uiRendererRender(app, ++t);
    appHandleTouch(app, TouchAction::NameEdit, ++t);
    uiRendererRender(app, ++t); lv_obj_update_layout(lv_scr_act());
    std::strcpy(app.identity.draftName, "ABC");
    uiHandwritingRecordTouch(true, 100, 150, ++t);
    uiHandwritingRecordTouch(false, 100, 150, ++t);
    ++t; appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, t}, t);
    t += 30; appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, t}, t);
    enqueueTouch(TouchAction::NameDelete);
    serviceQueuedActions();
    check(!uiHandwritingHasInk() && strcmp(app.identity.draftName, "AB") == 0,
          "physical pending DELETE clears ink before the following touch DELETE removes a draft letter");
    // Unadmitted real devices receive Connecting / WaitingForNewGame on every
    // discovery retry. Exercise the production reducer and actual LVGL objects.
    AppState waitingState{};
    appInit(waitingState, 20000, true);
    waitingState.revision = app.revision + 1;
    uiRendererRender(waitingState, 20000);
    const auto connectingRebuilds = uiRendererGetTestStats().rebuildRenders;
    TransportEvent joinStatus{};
    joinStatus.kind = TransportEventKind::JoinStatusChanged;
    joinStatus.joinStatus = TransportJoinStatus::WaitingForNewGame;
    appHandleTransportEvent(waitingState, joinStatus, 20001);
    uiRendererRender(waitingState, 20001);
    check(uiRendererGetTestStats().rebuildRenders == connectingRebuilds + 1,
          "first admission refusal redraws Connecting as Waiting For New Game");
    lv_refr_now(lv_disp_get_default());
    uiRendererResetTestStats();
    for (uint32_t retry = 0; retry < 60; ++retry) {
        const uint32_t now = 21000 + retry * 1000;
        joinStatus.joinStatus = TransportJoinStatus::Connecting;
        appHandleTransportEvent(waitingState, joinStatus, now);
        uiRendererRender(waitingState, now);
        joinStatus.joinStatus = TransportJoinStatus::WaitingForNewGame;
        appHandleTransportEvent(waitingState, joinStatus, now + 1);
        uiRendererRender(waitingState, now + 1);
    }
    const auto retryStats = uiRendererGetTestStats();
    std::printf("waiting retries: rebuilds=%lu invalid_areas=%u\n",
        static_cast<unsigned long>(retryStats.rebuildRenders),
        static_cast<unsigned>(lv_disp_get_default()->inv_p));
    check(retryStats.rebuildRenders == 0 && lv_disp_get_default()->inv_p == 0,
          "60 ordinary admission retries leave the visible waiting page and LCD dirty areas unchanged");
    check(waitingState.joinStatus == TransportJoinStatus::WaitingForNewGame &&
          waitingState.page == ScreenPage::WaitingForGame && !waitingState.hasAuthoritativeState,
          "retaining the waiting page preserves the actionable admission refusal");
    TransportEvent wifiLost{};
    wifiLost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(waitingState, wifiLost, 82000);
    uiRendererRender(waitingState, 82000);
    check(uiRendererGetTestStats().rebuildRenders == retryStats.rebuildRenders + 1 &&
          waitingState.joinStatus == TransportJoinStatus::Connecting,
          "real connection loss redraws the waiting screen as Connecting");
    lv_refr_now(lv_disp_get_default());
    uiRendererResetTestStats();
    joinStatus.joinStatus = TransportJoinStatus::Syncing;
    appHandleTransportEvent(waitingState, joinStatus, 83000);
    uiRendererRender(waitingState, 83000);
    check(uiRendererGetTestStats().rebuildRenders == 1 &&
          waitingState.joinStatus == TransportJoinStatus::Syncing,
          "actual admission redraws Connecting as Joining Game");
    lv_refr_now(lv_disp_get_default());
    uiRendererResetTestStats();
    appHandleTransportEvent(waitingState, joinStatus, 83001);
    uiRendererRender(waitingState, 83001);
    check(uiRendererGetTestStats().rebuildRenders == 0 && lv_disp_get_default()->inv_p == 0,
          "repeated unchanged Syncing status also leaves the waiting screen clean");

    // Exercise the real renderer and real DELETE events with authoritative levels.
    testArtImage.header.cf = LV_IMG_CF_TRUE_COLOR;
    testArtImage.header.w = testArtImage.header.h = 128;
    testArtImage.data_size = sizeof(testArtPixels); testArtImage.data = testArtPixels;
    artReady = true;
    AppState developed{}; appInit(developed, 90000, true);
    developed.authorityRoomId = 700; developed.boardSize = 16;
    developed.authorityOnline = developed.hasAuthoritativeState = developed.fullAuthoritySnapshotValid = true;
    const auto *board = gridopoly::core::BoardCatalog::findBySize(16);
    developed.authorityAssetCount = board->assetCount;
    uint8_t property = 0xFF, pos = 0xFF;
    for (uint8_t i = 0; i < board->assetCount; ++i)
        if (strcmp(board->assets[i].id, "A1") == 0) property = i;
    for (uint8_t i = 0; i < board->tileCount; ++i)
        if (board->tiles[i].assetIndex == property) pos = i;
    check(property != 0xFF && pos != 0xFF, "development fixture resolves property through current board map");
    developed.selectedAsset = developed.tileAssetIndex = developed.auctionAssetIndex = property;
    developed.position = developed.rollTarget = pos;
    developed.authorityAssets[property].buildingLevel = 3;
    developed.revision = waitingState.revision + 1;
    auto renderPage = [&](ScreenPage page) {
        developed.page = developed.nav.current.page = page;
        ++developed.revision; uiRendererRender(developed, 90000 + developed.revision);
    };
    for (ScreenPage page : {ScreenPage::Home, ScreenPage::AssetDetail, ScreenPage::Purchase,
                            ScreenPage::MoveGuide, ScreenPage::Auction}) {
        renderPage(page);
        check(selectedArtwork.level == 3 && acquiredArtwork.level == 3 &&
              sameArtwork(selectedArtwork, acquiredArtwork), "real page passes current authority level to cache and image");
    }
    check(artAcquires == artReleases + 1, "real page deletion releases every previous image lease");
    renderPage(ScreenPage::AssetDetail);
    for (uint8_t level : {uint8_t(5), uint8_t(4), uint8_t(0)}) {
        developed.authorityAssets[property].buildingLevel = level;
        renderPage(ScreenPage::AssetDetail);
        check(acquiredArtwork.level == level && selectedArtwork.level == level &&
              (level != 0 || acquiredArtwork.catalog == 0), "build hotel downgrade and sell-all replace old level image");
    }
    developed.fullAuthoritySnapshotValid = false;
    renderPage(ScreenPage::AssetDetail);
    check(acquiredArtwork.level == kArtworkLevelUnknown && acquiredArtwork.catalog == 0,
          "missing authoritative level uses default art without inferring demo buildings");
    developed.authoritySnapshotValid = true;
    developed.tileBuildingLevel = 4;
    renderPage(ScreenPage::AssetDetail);
    check(acquiredArtwork.level == 4, "compact snapshot level applies to its explicitly named tile");
    renderPage(ScreenPage::Home);
    const auto beforeCompactFocus = artAcquires;
    developed.tileBuildingLevel = 2;
    developed.focus = developed.nav.current.focus = 1;
    renderPage(ScreenPage::Home);
    check(acquiredArtwork.level == 2 && artAcquires > beforeCompactFocus,
          "simultaneous focus and compact level change replaces stale home artwork");
    developed.tileAssetIndex = 0xFF;
    renderPage(ScreenPage::AssetDetail);
    check(acquiredArtwork.level == kArtworkLevelUnknown, "compact level cannot leak onto another asset");
    developed.tileAssetIndex = property;
    developed.boardCatalogCompatible = false;
    renderPage(ScreenPage::AssetDetail);
    check(acquiredArtwork.level == kArtworkLevelUnknown, "incompatible map does not guess development artwork");
    developed.boardCatalogCompatible = true;
    developed.fullAuthoritySnapshotValid = true;
    developed.authorityAssets[property].buildingLevel = 255;
    renderPage(ScreenPage::AssetDetail);
    check(acquiredArtwork.level == kArtworkLevelUnknown, "invalid authority level is unknown");
    developed.authorityAssets[property].buildingLevel = 2;
    developed.rollResolved = true; renderPage(ScreenPage::MoveGuide);
    check(prefetchedArtwork.level == 2, "move target prefetch receives known level");
    developed.authorityAssets[property].buildingLevel = 1;
    renderPage(ScreenPage::MoveGuide);
    check(prefetchedArtwork.level == 1, "same target prefetch updates after downgrade");
    developed.authorityRoomId = 701;
    const auto beforeRoom = artAcquires;
    renderPage(ScreenPage::AssetDetail);
    check(artAcquires > beforeRoom && artAcquires == artReleases + 1,
          "new room rebuild cannot retain old image object");
    developed.rollResolved = false;
    renderPage(ScreenPage::WaitingForGame);
    check(!selectedArtwork.valid() && !prefetchedArtwork.valid() && artAcquires == artReleases,
          "leaving artwork pages cancels demand and releases final LVGL image");
    artReady = false;
    // Real reducer + renderer: ordered and partial authority updates, not demo assets.
    {
        HomeBuildFixture f; AppState build{};
        auto reset = [&] { appInit(build, 0, true); f.apply(build); };
        auto open = [&] {
            build.nav.current.focus = build.focus = homeFocusFor(build, HomeAction::Build);
            appHandleUiEvent(build, UiEvent{UiEventKind::ActivateFocused, 0}, 200);
        };
        reset(); HomeAction actions[5]{};
        check(appHomeActions(build, actions) == 5 && actions[1] == HomeAction::Build &&
              appPageContentCount(build) == 5 && appHomeBuildAsset(build) == f.asset,
              "Home count input and renderer share the qualified five-action list");
        build.nav.current.focus = build.focus = 1; ++build.revision;
        uiRendererRender(build, 201);
        check(homeCarousel.count == 5 && homeCarousel.actions[homeCarousel.selected] == HomeAction::Build,
              "Home renderer presents the same Build action selected by reducer");
        auto compact = f.compact; auto full = f.full;
        compact.stateVersion = full.stateVersion = 11;
        appHandleTransportEvent(build, compact, 210); uiRendererRender(build, 210);
        check(build.focus == 1 && homeFocusFor(build, HomeAction::Build) == 1 &&
              !appBuildSnapshotCurrent(build) && appAssetBuildReason(build, f.asset) != nullptr,
              "new compact retains selected Build but blocks stale ownership submission");
        appHandleUiEvent(build, UiEvent{UiEventKind::ActivateFocused, 0}, 211);
        check(build.page == ScreenPage::Home && build.commandCount == 0,
              "disabled Home shortcut cannot open or submit during partial sync");
        appHandleTransportEvent(build, full, 220); uiRendererRender(build, 220);
        check(build.focus == 1 && appBuildSnapshotCurrent(build) &&
              homeCarousel.actions[homeCarousel.selected] == HomeAction::Build,
              "matching full restores readiness without changing Build focus");
        compact.resync = true; appHandleTransportEvent(build, compact, 230);
        check(build.focus == 1 && homeFocusFor(build, HomeAction::Build) == 1,
              "same-context resync preserves Home Build action focus");
        open();
        check(build.page == ScreenPage::Build && build.selectedAsset == f.asset &&
              build.modal.kind == ModalKind::None && appBuildContextCurrent(build),
              "Home shortcut opens current owned property without a popup");
        compact.resync = false; compact.stateVersion = full.stateVersion = 12;
        appHandleTransportEvent(build, compact, 240);
        check(build.page == ScreenPage::Build && build.nav.stack[0].focus == 1 &&
              !appBuildContextCurrent(build),
              "partial sync preserves Build page and Home action in navigation stack");
        appHandleTransportEvent(build, full, 250);
        compact.resync = true; appHandleTransportEvent(build, compact, 260);
        check(build.page == ScreenPage::Build && build.nav.stack[0].focus == 1,
              "same-context Build resync keeps page and parent focus");
        appHandleTouch(build, TouchAction::DetailPrimary, 270);
        appHandleInput(build, InputEvent{InputKind::Rotate, 1, 271}, 271);
        check(build.selectedAsset == f.peer && build.inlineEditField == InlineEditField::BuildProperty,
              "rotary group selector chooses another property for even building");
        appHandleTransportEvent(build, full, 272);
        check(build.inlineEditField == InlineEditField::BuildProperty && build.selectedAsset == f.peer,
              "ordinary unchanged authority heartbeat does not steal property editing");
        appHandleTouch(build, TouchAction::DetailPrimary, 280);
        const int32_t cash = build.money;
        appHandleTouch(build, TouchAction::DetailSecondary, 290);
        appHandleTouch(build, TouchAction::DetailSecondary, 291);
        TransportCommand request{};
        const bool issued = appPollCommand(build, request);
        check(issued && request.kind == TransportCommandKind::BuildRequest && request.assetIndex == f.peer &&
              build.commandCount == 0 && build.money == cash && appAssetBuildingLevel(build, f.peer) == 0,
              "one click requests one level; repeated click adds none and assets are not optimistic");
        check(appAssetBuildReason(build, f.peer) &&
              std::strstr(appAssetBuildReason(build, f.peer), "PENDING"),
              "pending build has an explicit disabled reason");
        TransportEvent ack{}; ack.kind = TransportEventKind::CommandCompleted;
        ack.requestId = request.requestId; ack.stateVersion = 13;
        appHandleTransportEvent(build, ack, 300);
        appHandleTouch(build, TouchAction::DetailSecondary, 301);
        check(build.commandCount == 0 && !appBuildSnapshotCurrent(build),
              "accepted command waits for matching authoritative assets before another level");
        compact.resync = false; compact.stateVersion = full.stateVersion = 13;
        full.assets[f.peer].buildingLevel = 1; full.players[0].cash -= appAssetBuildingCost(build, f.peer);
        compact.cash = full.players[0].cash;
        appHandleTransportEvent(build, compact, 310); appHandleTransportEvent(build, full, 311);
        check(build.page == ScreenPage::Build && build.selectedAsset == f.peer &&
              appAssetBuildingLevel(build, f.peer) == 1 &&
              std::strstr(appAssetBuildReason(build, f.peer), "EVENLY"),
              "authority alone updates level and directs user to lower same-group property");
        build.selectedAsset = f.asset;
        check(appAssetBuildReason(build, f.asset) == nullptr,
              "same-group lower property remains buildable without returning to Home");
        build.authorityAssets[f.asset].buildingLevel = build.authorityAssets[f.peer].buildingLevel = 4;
        check(appAssetBuildReason(build, f.asset) == nullptr && appAssetNextRent(build, f.asset) > appAssetRent(build, f.asset),
              "four houses offer the next hotel level and its rent");
        build.authorityAssets[f.asset].buildingLevel = 5;
        check(std::strstr(appAssetBuildReason(build, f.asset), "MAXIMUM") != nullptr,
              "hotel has a maximum-level explanation without another request");
        reset(); open(); build.money = 0;
        check(std::strstr(appAssetBuildReason(build, f.asset), "CASH") != nullptr, "cash shortage is explained");
        build.money = 1000; build.authorityAssets[f.peer].flags = 1;
        check(std::strstr(appAssetBuildReason(build, f.asset), "MORTGAGES") != nullptr, "peer mortgage blocks group building");
        reset(); open();
        for (uint8_t i = 0; i < build.authorityAssetCount; ++i) build.authorityAssets[i].buildingLevel = 4;
        build.authorityAssets[f.asset].buildingLevel = build.authorityAssets[f.peer].buildingLevel = 0;
        check(appBankBuildingStock(build) == 0 && std::strstr(appAssetBuildReason(build, f.asset), "HOUSES"),
              "bank house shortage is explained");
        for (uint8_t i = 0; i < build.authorityAssetCount; ++i) build.authorityAssets[i].buildingLevel = 5;
        build.authorityAssets[f.asset].buildingLevel = build.authorityAssets[f.peer].buildingLevel = 4;
        check(appBankLandmarkStock(build) == 0 && std::strstr(appAssetBuildReason(build, f.asset), "HOTELS"),
              "bank hotel shortage is explained");
        reset(); open(); TransportEvent lostBuild{}; lostBuild.kind = TransportEventKind::ConnectionLost;
        appHandleTransportEvent(build, lostBuild, 400); appHandleTouch(build, TouchAction::DetailSecondary, 401);
        check(build.page == ScreenPage::Build && build.commandCount == 0 &&
              std::strstr(appAssetBuildReason(build, f.asset), "OFFLINE"),
              "offline Build preserves page but cannot issue a command");
        reset(); build.nav.current.focus = build.focus = homeFocusFor(build, HomeAction::Players);
        full = f.full; full.assets[f.peer].ownerId = 2;
        appHandleTransportEvent(build, full, 410);
        check(homeFocusFor(build, HomeAction::Build) == 0xFF &&
              build.focus == homeFocusFor(build, HomeAction::Players),
              "losing full-group ownership removes shortcut while preserving Players action focus");
        reset(); open(); appHandleTransportEvent(build, full, 420);
        check(build.page == ScreenPage::Home, "confirmed group loss closes obsolete Build context");
        reset(); open(); compact = f.compact; compact.stateVersion = 11; compact.playerPosition = 0;
        appHandleTransportEvent(build, compact, 430);
        check(build.page == ScreenPage::Home && homeFocusFor(build, HomeAction::Build) == 0xFF,
              "confirmed position change cannot keep prior property shortcut");
        reset(); open(); compact = f.compact; compact.activePlayerId = 2; compact.decisionPlayerId = 2;
        appHandleTransportEvent(build, compact, 440);
        check(build.page == ScreenPage::Home && homeFocusFor(build, HomeAction::Build) == 0xFF,
              "turn change removes the own-turn shortcut and closes Build");
        reset(); compact = f.compact; compact.phase = AuthorityPhase::AwaitMoveConfirm; compact.pendingTarget = f.position;
        appHandleTransportEvent(build, compact, 450);
        check(appHomeBuildAsset(build) == 0xFF, "pending arrival target never creates a Build shortcut");
        reset(); open(); compact = f.compact; compact.roomId = 801; compact.stateVersion = 1; compact.resync = true;
        appHandleTransportEvent(build, compact, 460);
        check(build.page != ScreenPage::Build && appHomeBuildAsset(build) == 0xFF, "new room drops old ownership and Build context");
        reset(); build.boardCatalogCompatible = false;
        check(appHomeBuildAsset(build) == 0xFF && appAssetBuildReason(build, f.asset) != nullptr,
              "catalog mismatch never enables a build");
        reset(); build.moveArrivalConfirmed = false; build.moveArrivalPending = false;
        build.rollTarget = 0xFF; build.rollResolved = false;
        check(appHomeBuildAsset(build) == f.asset, "stable authoritative location does not rely on clearRollFlow flags");
        compact = f.compact; compact.phase = AuthorityPhase::TurnEnd; compact.availableActions = (1u << 4) | (1u << 7);
        full = f.full; full.phase = AuthorityPhase::TurnEnd;
        appHandleTransportEvent(build, compact, 470); appHandleTransportEvent(build, full, 471);
        check(appHomeBuildAsset(build) == f.asset && homeFocusFor(build, HomeAction::EndTurn) == 0,
              "TurnEnd offers Build alongside the existing End Turn action");
        build.availableActions = 0;
        check(appAssetBuildReason(build, f.asset) != nullptr, "missing authoritative build permission disables submission");
        reset(); build.nav.current.focus = build.focus = homeFocusFor(build, HomeAction::Build);
        full = f.full; full.stateVersion = 11;
        appHandleTransportEvent(build, full, 480);
        check(homeFocusFor(build, HomeAction::Build) == build.focus && !appBuildSnapshotCurrent(build),
              "full-first synchronization retains Home Build focus while submission waits");
        compact = f.compact; compact.stateVersion = 11;
        appHandleTransportEvent(build, compact, 481);
        check(homeFocusFor(build, HomeAction::Build) == build.focus && appBuildSnapshotCurrent(build),
              "matching compact completes full-first synchronization without moving focus");
        reset(); open(); compact = f.compact; compact.boardSize = 24; compact.stateVersion = 11;
        appHandleTransportEvent(build, compact, 490);
        check(build.page != ScreenPage::Build && appHomeBuildAsset(build) == 0xFF,
              "board remapping closes old Build context before its new full projection");
        reset(); build.nav.current.page = build.page = ScreenPage::AssetDetail; build.selectedAsset = f.asset;
        check(appAssetDetailActionEnabled(build, AssetDetailAction::Build) &&
              appAssetBuildReason(build, f.asset) == nullptr,
              "Assets Build action shares the fresh authoritative rule path");
        compact = f.compact; compact.stateVersion = 11;
        appHandleTransportEvent(build, compact, 500);
        check(!appAssetDetailActionEnabled(build, AssetDetailAction::Build),
              "Assets Build action also blocks during split snapshot synchronization");

    }

    {
        uiRendererResetForTest(); uiRendererBegin();
        AppState traced{}; appInit(traced,0);
        traced.page=traced.nav.current.page=ScreenPage::NameHandwriting;
        uiRendererRender(traced,0);lv_obj_update_layout(lv_scr_act());
        uiHandwritingTraceSession(9);
        hostMillis=100;uiHandwritingRecordTouch(true,100,140,100);
        auto traceState=uiHandwritingTraceState(100);
        check(traceState.canvas && traceState.contact && traceState.originX==78 && traceState.originY==112 &&
              points[0].x==22 && points[0].y==28 && traceState.points==1 && traceState.drawn==1,
              "touch trace reports actual canvas origin while preserving physical-to-local coordinates");
        hostMillis=110;uiHandwritingRecordTouch(true,200,200,110);
        traceState=uiHandwritingTraceState(115);
        check(traceState.points==2 && traceState.drawn==1 && traceState.pendingAgeMs==5 && traceState.connects,
              "touch trace distinguishes captured points from pending ink and its true age");
        hostMillis=120;uiHandwritingRecordTouch(false,200,200,120);traceState=uiHandwritingTraceState(120);
        check(!traceState.contact && traceState.drawn==2 && traceState.drawAtMs==120 && traceState.drawLagMs==10,
              "forced release draw records the completion timestamp and oldest pending sample latency");
        hostMillis=130;uiHandwritingRecordTouch(true,250,150,130);
        check(!points[2].connectsPrevious,"diagnostics do not join an observed lift to the next stroke");
        const auto pointsBeforeTraceToggle=pointCount;const auto generationBeforeTraceToggle=captureGeneration;
        uiHandwritingTraceSession(0);uiHandwritingTraceSession(10);
        check(pointCount==pointsBeforeTraceToggle && captureGeneration==generationBeforeTraceToggle,
              "trace off/on preserves ink, contact and recognition generation");
        hostMillis=140;uiHandwritingTouchUnavailable(140);traceState=uiHandwritingTraceState(140);
        check(!traceState.contact && !touchSampleAvailable && pointCount==pointsBeforeTraceToggle,
              "diagnostics preserve the existing unknown-input boundary without discarding ink");
        hostMillis=150;uiHandwritingRecordTouch(false,250,150,150);
        hostMillis=1400;char traceLetter='\0';uiHandwritingPoll(traceLetter,1400,true);
        traceState=uiHandwritingTraceState(1400);
        check(!traceState.recognizerReady && traceState.fallbackCount==1,
              "real classifier allocation fallback is counted without changing recognition semantics");
        uiHandwritingTraceSession(0);
    }
    std::printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
