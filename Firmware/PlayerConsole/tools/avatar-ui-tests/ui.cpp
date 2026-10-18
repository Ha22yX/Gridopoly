#include "../../ui_handwriting.cpp"
#include "../../ui_renderer.cpp"
#include "../../ui_battery.h"
#include <fstream>
#include <vector>

static TransportAvatarRecipe previewRequested{};
static unsigned previewRequests;
static lv_img_dsc_t actualPreview{};
static std::vector<uint8_t> actualPixels;
void remoteAvatarCacheBegin() {}
void remoteAvatarCachePreload(const TransportAvatarRecipe &) {}
RemoteAvatarPreloadProgress remoteAvatarCachePreloadProgress() { return {}; }
void remoteAvatarCacheReleaseSetup() {}
void remoteAvatarCacheReleaseFinals() {}
void remoteAvatarCacheRequestPreview(const TransportAvatarRecipe &) {}
RemoteAvatarPreviewFrame remoteAvatarPreviewFrame(const TransportAvatarRecipe &recipe) {
    previewRequested = recipe; ++previewRequests;
    if (recipe.catalogVersion == 2 && recipe.hairPresetId == 8 && recipe.hairColorId == 1 &&
        recipe.facePresetId == 7 && recipe.skinToneId == 8 && recipe.outfitPresetId == 2) return {&actualPreview, true};
    return {}; // No invented picture for recipes without a real fixture.
}
const lv_img_dsc_t *remoteAvatarPreview(const TransportAvatarRecipe &r) { return remoteAvatarPreviewFrame(r).image; }
const lv_img_dsc_t *remoteAvatarFinal(uint32_t, uint8_t, uint16_t, uint64_t, uint16_t) { return nullptr; }
bool remoteAvatarCacheConsumeUpdate() { return false; }
void remoteTileCacheBegin() {}
void remoteTileCacheSetView(uint32_t, uint8_t, GridCityArtwork, const char *, uint8_t) {}
void remoteTileCacheReleaseImage(const lv_img_dsc_t *) {}
void remoteTileCachePrefetch(GridCityArtwork, const char *, uint8_t) {}
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork, const char *, uint8_t) { return nullptr; }
bool remoteTileCacheConsumeUpdate() { return false; }

namespace {
lv_color_t capturedFrame[480 * 480];
unsigned checks = 0, failures = 0;
bool colorEqual(lv_color_t left, lv_color_t right) { return left.full == right.full; }
void check(bool value, const char *name) {
    ++checks; if (!value) ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}
void settle() {
    for (unsigned i = 0; i < 30; ++i) { lv_tick_inc(20); lv_timer_handler(); }
    lv_refr_now(nullptr);
    // The enabled LVGL performance monitor can invalidate its own label at
    // the end of refresh. Drain that overlay before measuring page changes.
    lv_refr_now(nullptr);
}
void screenshot(const char *name) {
    settle(); std::ofstream out(name, std::ios::binary); out << "P6\n480 480\n255\n";
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
        const bool inside = (x - 240) * (x - 240) + (y - 240) * (y - 240) <= 240 * 240;
        const uint32_t p = lv_color_to32(capturedFrame[y * 480 + x]);
        const char rgb[] = {inside ? char(p >> 16) : char(24), inside ? char(p >> 8) : char(24), inside ? char(p) : char(24)};
        out.write(rgb, sizeof(rgb));
    }
}
TapBinding *binding(TouchAction action) {
    for (uint8_t i = 0; i < tapBindingCount; ++i) if (tapBindings[i].action == action) return &tapBindings[i];
    return nullptr;
}
bool circleSafe(lv_obj_t *object) {
    lv_area_t a{}; lv_obj_get_coords(object, &a);
    for (int x : {a.x1, a.x2}) for (int y : {a.y1, a.y2})
        if ((x - 240) * (x - 240) + (y - 240) * (y - 240) > 232 * 232) return false;
    return true;
}
bool overlap(lv_obj_t *a, lv_obj_t *b) {
    lv_area_t x{}, y{}; lv_obj_get_coords(a, &x); lv_obj_get_coords(b, &y);
    return x.x1 <= y.x2 && y.x1 <= x.x2 && x.y1 <= y.y2 && y.y1 <= x.y2;
}
AppState pageState(ScreenPage page, uint8_t focus) {
    AppState app{}; appInit(app, 0);
    app.authorityOnline = true; app.authorityRoomId = 100; app.selfSeatId = 1; app.stateVersion = 1;
    app.toast = ""; app.toastUntilMs = 0;
    app.nav.current = NavigationEntry{page, focus, 0}; app.page = page; app.focus = focus;
    app.identity.phase = page == ScreenPage::NameReview ? IdentityClientPhase::NameReview :
        page == ScreenPage::NameHandwriting ? IdentityClientPhase::NameHandwriting : IdentityClientPhase::AvatarEditing;
    app.identity.avatarAssetsReady = true; app.identity.draftInitialized = true;
    app.identity.ownSeatRevision = 1; app.identity.focus = focus;
    app.identity.draftRecipe = TransportAvatarRecipe{2,8,1,7,8,2};
    std::strcpy(app.identity.draftName, "ALEX");
    return app;
}
void startPage(AppState &app) {
    uiRendererResetForTest(); check(uiRendererBegin(), "real renderer starts");
    uiRendererRender(app, 1000); settle(); uiRendererResetTestStats();
}
void quietUpdateChecks(ScreenPage page, uint8_t focus) {
    auto app = pageState(page, focus); startPage(app);
    unsigned invalidated = 0;
    for (unsigned tick = 0; tick < 12; ++tick) {
        uiRendererRender(app, 2000 + tick * 50);
        invalidated += lv_disp_get_default()->inv_p != 0; lv_refr_now(nullptr);
    }
    check(uiRendererGetTestStats().rebuildRenders == 0 && invalidated == 0,
          "unchanged ticks do not rebuild or invalidate settled selected buttons");
    const char *pageName = page == ScreenPage::NameReview ? "NameReview" : "NameHandwriting";
    for (unsigned cause = 0; cause < 3; ++cause) {
        settle(); uiRendererResetTestStats(); invalidated = 0;
        unsigned fades = 0;
        for (unsigned tick = 0; tick < 12; ++tick) {
            if (cause == 0) ++app.revision;
            if (cause == 1) {
                ++app.revision; ++app.identity.revision; ++app.identity.seats[1].seatRevision;
                app.identity.onlineMask ^= 2;
            }
            if (cause == 2) uiRendererInvalidateArtwork();
            uiRendererRender(app, 3000 + cause * 1000 + tick * 50);
            const TouchAction selected = page == ScreenPage::NameReview
                ? (focus == 0 ? TouchAction::NameEdit : focus == 1 ? TouchAction::NameConfirm : TouchAction::NameBack)
                : (focus == 0 ? TouchAction::NameDelete : TouchAction::HandwritingConfirm);
            fades += lv_obj_get_style_opa(binding(selected)->object, 0) < LV_OPA_COVER;
            invalidated += lv_disp_get_default()->inv_p != 0; lv_refr_now(nullptr);
        }
        const auto stats = uiRendererGetTestStats();
        std::printf("METRIC page=%s focus=%u cause=%u updates=12 rebuilds=%u invalidated=%u fades=%u\n",
                    pageName, focus, cause, stats.rebuildRenders, invalidated, fades);
        check(stats.rebuildRenders == 0 && invalidated == 0 && fades == 0,
              "unrelated revision/seat/artwork update retains settled name page without redraw");
    }
    if (page == ScreenPage::NameReview) {
        auto *oldButton = binding(TouchAction::NameEdit)->object;
        std::strcpy(app.identity.draftName, "BOB"); ++app.revision;
        uiRendererRender(app, 7000); settle();
        check(nameDraftLabel && std::strcmp(lv_label_get_text(nameDraftLabel), "BOB") == 0 &&
              binding(TouchAction::NameEdit)->object == oldButton,
              "changed name updates label while retaining buttons");
        if (focus == 2) screenshot("name-review-back-focused.ppm");
        app.identity.draftName[0] = '\0'; ++app.revision; uiRendererRender(app, 7010);
        check(std::strcmp(lv_label_get_text(nameDraftLabel), "YOUR NAME") == 0 &&
              colorEqual(lv_obj_get_style_text_color(nameDraftLabel, 0), lv_color_hex(kMuted)),
              "empty review draft shows muted placeholder");
        std::strcpy(app.identity.draftName, "YOUR NAME"); ++app.revision; uiRendererRender(app, 7020);
        check(colorEqual(lv_obj_get_style_text_color(nameDraftLabel, 0), lv_color_hex(kText)),
              "literal placeholder name still switches to actual-name color");
        appHandleInput(app, InputEvent{InputKind::Rotate, focus == 2 ? int16_t(-1) : int16_t(1), 7030}, 7030);
        uiRendererRender(app, 7030); settle();
        const TouchAction selected = app.focus == 1 ? TouchAction::NameConfirm : TouchAction::NameBack;
        check(binding(TouchAction::NameEdit)->object == oldButton && bindingIsFocused(app, selected) &&
              colorEqual(lv_obj_get_style_border_color(binding(selected)->object, 0),
                          lv_color_hex(app.focus == 1 ? kGreen : kBlue)),
              "name review rotary focus changes highlight on retained controls");
        app.authorityOnline = false; ++app.revision; uiRendererRender(app, 7040);
        check(connectionBadge != nullptr && binding(TouchAction::NameEdit)->object == oldButton,
              "connection loss updates badge without rebuilding name controls");
        app.authorityOnline = true; ++app.revision; uiRendererRender(app, 7050);
        check(connectionBadge == nullptr, "connection recovery removes retained badge");
        app.toast = "WRITE A NAME FIRST"; app.toastUntilMs = 7090; ++app.revision;
        uiRendererRender(app, 7060);
        check(toastObject != nullptr && binding(TouchAction::NameEdit)->object == oldButton,
              "name validation toast appears without rebuilding buttons");
        uiRendererRender(app, 7090);
        check(toastObject == nullptr, "name validation toast expires on unchanged revision");
        const auto before = uiRendererGetTestStats().rebuildRenders;
        ++app.authorityRoomId; ++app.revision; uiRendererRender(app, 7100);
        check(uiRendererGetTestStats().rebuildRenders == before + 1, "new room rebuilds name review context");
        ++app.selfSeatId; ++app.revision; uiRendererRender(app, 7110);
        check(uiRendererGetTestStats().rebuildRenders == before + 2, "new seat rebuilds name review context");
        lv_event_send(binding(TouchAction::NameEdit)->object, LV_EVENT_CLICKED, nullptr);
        TouchAction action{};
        check(uiRendererPollTouch(action) && action == TouchAction::NameEdit,
              "retained Edit Name button still emits its touch action");
        appHandleTouch(app, action, 7120); uiRendererRender(app, 7120);
        check(app.page == ScreenPage::NameHandwriting && activeCanvas != nullptr &&
              uiRendererGetTestStats().rebuildRenders == before + 3,
              "entering handwriting constructs its distinct canvas page");
    }
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("Starting real LVGL avatar/name regression");
    lv_init(); static lv_color_t pixels[480 * 20]; static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = display.ver_res = 480; display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
        for (int y = a->y1; y <= a->y2; ++y) for (int x = a->x1; x <= a->x2; ++x) capturedFrame[y * 480 + x] = *p++;
        lv_disp_flush_ready(d);
    };
    lv_disp_drv_register(&display);
    std::puts("Display registered");
    std::ifstream in("avatar-preview.rgb565", std::ios::binary);
    actualPixels.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (actualPixels.size() != 132000) return 2;
    actualPreview.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
    actualPreview.header.w = 220; actualPreview.header.h = 300;
    actualPreview.data = actualPixels.data(); actualPreview.data_size = static_cast<uint32_t>(actualPixels.size());
    std::puts("Actual RGB565 preview loaded");
    auto avatar = pageState(ScreenPage::AvatarSetup, 6); startPage(avatar);
    auto *random = binding(TouchAction::IdentityRandom); auto *confirm = binding(TouchAction::IdentityConfirm);
    check(random && confirm && std::strcmp(lv_label_get_text(random->label), "RANDOM") == 0, "RANDOM has an English touch binding");
    if (!random || !confirm) return 3;
    check(lv_obj_get_width(random->object) < lv_obj_get_width(confirm->object) &&
          lv_obj_get_height(random->object) < lv_obj_get_height(confirm->object) &&
          lv_obj_get_y(random->object) > lv_obj_get_y(confirm->object) + lv_obj_get_height(confirm->object),
          "random is smaller and below confirm");
    check(circleSafe(random->object) && circleSafe(confirm->object) &&
          !overlap(random->object, confirm->object) && !overlap(random->object, avatarPortraitViewport),
          "both buttons fit circular safe area and random does not overlap portrait");
    UiBattery battery{}; gridopoly::player_console::BatteryStatus status{};
    status.enabled = true;
    uiBatteryRender(battery, status, 1000); settle();
    check(battery.root != nullptr, "enabled battery badge exists for geometry check");
    if (!battery.root) return 4;
    check(!overlap(random->object, battery.root), "random clears bottom battery badge");
    screenshot("avatar-random-focused.ppm");
    const auto oldRequests = previewRequests;
    lv_event_send(random->object, LV_EVENT_CLICKED, nullptr);
    TouchAction action{};
    check(uiRendererPollTouch(action) && action == TouchAction::IdentityRandom, "real LVGL tap emits Random action");
    appHandleTouch(avatar, action, 2000); uiRendererRender(avatar, 2000);
    check(previewRequests > oldRequests && previewRequested.hairPresetId == avatar.identity.draftRecipe.hairPresetId &&
          previewRequested.outfitPresetId == avatar.identity.draftRecipe.outfitPresetId && previewRequested.catalogVersion == 2,
          "random draft immediately reaches retained local preview request");
    TransportCommand command{};
    check(!appPollCommand(avatar, command) && avatar.identity.phase == IdentityClientPhase::AvatarEditing,
          "real random tap does not confirm avatar");
    check(uiRendererGetTestStats().rebuildRenders == 0, "random updates existing avatar page without rebuild");
    appHandleInput(avatar, InputEvent{InputKind::Rotate, -1, 2100}, 2100);
    uiRendererRender(avatar, 2100); settle();
    check(bindingIsFocused(avatar, TouchAction::IdentityConfirm) && !bindingIsFocused(avatar, TouchAction::IdentityRandom),
          "retained focus highlight moves from random to confirm");
    avatar.identity.draftRecipe = TransportAvatarRecipe{2,8,1,7,8,2}; ++avatar.revision;
    uiRendererRender(avatar, 2200); screenshot("avatar-confirm-focused.ppm");
    avatar.identity.phase = IdentityClientPhase::AvatarSubmitting; ++avatar.revision;
    uiRendererRender(avatar, 2300);
    check(lv_obj_has_state(random->object, LV_STATE_DISABLED), "retained random button disables during submission");
    avatar.identity.phase = IdentityClientPhase::AvatarEditing; ++avatar.revision;
    uiRendererRender(avatar, 2400);
    check(!lv_obj_has_state(random->object, LV_STATE_DISABLED), "retained random button re-enables after submission");
    for (uint8_t focus = 0; focus < 3; ++focus) quietUpdateChecks(ScreenPage::NameReview, focus);
    for (uint8_t focus = 0; focus < 2; ++focus) quietUpdateChecks(ScreenPage::NameHandwriting, focus);
    std::printf("%u UI checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
