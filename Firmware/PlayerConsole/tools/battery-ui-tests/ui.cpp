#include "../../ui_handwriting.cpp"
#include "../../ui_renderer.cpp"
#include "../../ui_battery.h"
#include "../home-build-fixtures.h"
#include <vector>
#include <fstream>

void remoteAvatarCacheBegin() {}
void remoteAvatarCachePreload(const TransportAvatarRecipe &) {}
RemoteAvatarPreloadProgress remoteAvatarCachePreloadProgress() { return {}; }
void remoteAvatarCacheReleaseSetup() {}
void remoteAvatarCacheReleaseFinals() {}
void remoteAvatarCacheRequestPreview(const TransportAvatarRecipe &) {}
RemoteAvatarPreviewFrame remoteAvatarPreviewFrame(const TransportAvatarRecipe &) { return {}; }
const lv_img_dsc_t *remoteAvatarPreview(const TransportAvatarRecipe &) { return nullptr; }
const lv_img_dsc_t *remoteAvatarFinal(uint32_t, uint8_t, uint16_t, uint64_t, uint16_t) { return nullptr; }
bool remoteAvatarCacheConsumeUpdate() { return false; }
void remoteTileCacheBegin() {}
void remoteTileCacheSetView(uint32_t, uint8_t, GridCityArtwork, const char *, uint8_t) {}
void remoteTileCacheReleaseImage(const lv_img_dsc_t *) {}
void remoteTileCachePrefetch(GridCityArtwork, const char *, uint8_t) {}
bool buildArtworkPreview = false;
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork, const char *key, uint8_t level) {
    if (!buildArtworkPreview || !key || std::strcmp(key, "a1-rivet-row") != 0) return nullptr;
    static uint8_t pixels[4][32768]{};
    static lv_img_dsc_t descriptors[4]{};
    static bool loaded = false;
    if (!loaded) {
        const char *files[] = {"build-l0.rgb565", "build-l1.rgb565", "build-l4.rgb565", "build-l5.rgb565"};
        for (uint8_t i = 0; i < 4; ++i) {
            std::ifstream input(files[i], std::ios::binary);
            input.read(reinterpret_cast<char *>(pixels[i]), sizeof(pixels[i]));
            if (input.gcount() != sizeof(pixels[i])) { std::printf("Missing build preview fixture %s\n", files[i]); std::exit(2); }
            descriptors[i].header.cf = LV_IMG_CF_TRUE_COLOR;
            descriptors[i].header.w = descriptors[i].header.h = 128;
            descriptors[i].data = pixels[i]; descriptors[i].data_size = sizeof(pixels[i]);
        }
        loaded = true;
    }
    if (level != 0 && level != 1 && level != 4 && level != 5) return nullptr;
    return &descriptors[level == 4 ? 2 : level == 5 ? 3 : level];
}
bool remoteTileCacheConsumeUpdate() { return false; }

namespace {
lv_color_t frame[480 * 480];
void screenshot(const char *name)
{
    lv_refr_now(nullptr);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n480 480\n255\n";
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
        const uint32_t pixel = lv_color_to32(frame[y * 480 + x]);
        const bool inside = (x - 240) * (x - 240) + (y - 240) * (y - 240) <= 240 * 240;
        const char rgb[3] = {inside ? char(pixel >> 16) : char(24), inside ? char(pixel >> 8) : char(24), inside ? char(pixel) : char(24)};
        out.write(rgb, 3);
    }
}
bool overlaps(lv_obj_t *a, lv_obj_t *b)
{
    lv_area_t x{}, y{}; lv_obj_get_coords(a, &x); lv_obj_get_coords(b, &y);
    return x.x1 <= y.x2 && y.x1 <= x.x2 && x.y1 <= y.y2 && y.y1 <= x.y2;
}
bool contentClear(lv_obj_t *object, lv_obj_t *badge)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return true;
    lv_area_t visible{}, batteryArea{};
    lv_obj_get_coords(object, &visible); lv_obj_get_coords(badge, &batteryArea);
    for (auto *parent = lv_obj_get_parent(object); parent; parent = lv_obj_get_parent(parent)) {
        if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return true;
        if (lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) continue;
        lv_area_t clip{}; lv_obj_get_coords(parent, &clip);
        visible.x1 = std::max(visible.x1, clip.x1); visible.y1 = std::max(visible.y1, clip.y1);
        visible.x2 = std::min(visible.x2, clip.x2); visible.y2 = std::min(visible.y2, clip.y2);
        if (visible.x1 > visible.x2 || visible.y1 > visible.y2) return true;
    }
    const bool collision = visible.x1 <= batteryArea.x2 && batteryArea.x1 <= visible.x2 &&
        visible.y1 <= batteryArea.y2 && batteryArea.y1 <= visible.y2;
    if ((lv_obj_check_type(object, &lv_label_class) || lv_obj_check_type(object, &lv_img_class)) && collision) return false;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
        if (!contentClear(lv_obj_get_child(object, i), badge)) return false;
    return true;
}
}

int main()
{
    unsigned passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); (ok ? passed : failed)++; };
    lv_init();
    static lv_color_t pixels[480 * 20];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 480 * 20);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display); display.hor_res = display.ver_res = 480; display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *d, const lv_area_t *area, lv_color_t *data) {
        for (int y = area->y1; y <= area->y2; ++y) for (int x = area->x1; x <= area->x2; ++x)
            frame[y * 480 + x] = *data++;
        lv_disp_flush_ready(d);
    };
    lv_disp_drv_register(&display);
    check(uiRendererBegin(), "real LVGL production renderer initializes");
    using namespace gridopoly::player_console;
    {
        BatteryIndicatorFilter filter;
        BatteryStatus s{}; s.enabled = s.linkReady = s.gpioValid = true;
        s.external = ExternalPower::Present; s.charge = ChargeIndicator::Active;
        auto sample = [&](uint32_t at) { s.gpioAtMs = at; return filter.update(s, at); };
        bool noEarlyCharge = true;
        for (uint32_t at = 0; at < 1500; at += 75) noEarlyCharge &= sample(at) == UiBatteryIndicator::None;
        check(noEarlyCharge && sample(1500) == UiBatteryIndicator::Charging,
              "charging icon waits for 1.5 seconds of successive fresh evidence");
        s.charge = ChargeIndicator::Inactive;
        const bool glitchHeld = sample(1575) == UiBatteryIndicator::Charging && sample(1650) == UiBatteryIndicator::Charging;
        s.charge = ChargeIndicator::Active;
        check(glitchHeld && sample(1725) == UiBatteryIndicator::Charging,
              "brief charge/not-charge bounce does not toggle the visible icon");
        s.external = ExternalPower::Absent; s.charge = ChargeIndicator::Inactive;
        bool held = true;
        for (uint32_t at = 1800; at < 3300; at += 75) held &= sample(at) == UiBatteryIndicator::Charging;
        check(held && sample(3300) == UiBatteryIndicator::None,
              "sustained unplug/not-charging clears the icon after the stability interval");
        filter = BatteryIndicatorFilter{};
        s.external = ExternalPower::Present; s.charge = ChargeIndicator::Active; s.gpioAtMs = 4000;
        bool duplicateHeld = true;
        for (uint32_t at = 4000; at <= 6000; at += 20) duplicateHeld &= filter.update(s, at) == UiBatteryIndicator::None;
        check(duplicateHeld, "repainting one old GPIO sample never establishes charging");
        for (uint32_t at = 7000; at <= 8500; at += 75) sample(at);
        s.gpioValid = false;
        check(filter.update(s, 8900) == UiBatteryIndicator::Charging && filter.update(s, 9501) == UiBatteryIndicator::None,
              "missed GPIO frame has bounded one-second display grace then clears");
        s.gpioValid = true;
        for (uint32_t at = 10000; at <= 11500; at += 75) sample(at);
        s.linkReady = false;
        check(filter.update(s, 11501) == UiBatteryIndicator::None, "confirmed bridge failure clears charging immediately");
        s.linkReady = true; s.charge = ChargeIndicator::FaultCandidate;
        check(sample(12000) == UiBatteryIndicator::Warning, "protocol-confirmed blink fault bypasses display debounce");
        s.charge = ChargeIndicator::Active; held = true;
        for (uint32_t at = 12075; at < 13575; at += 75) held &= sample(at) == UiBatteryIndicator::Warning;
        check(held && sample(13575) == UiBatteryIndicator::Charging, "warning clears only after stable recovery");
        filter = BatteryIndicatorFilter{};
        for (uint32_t elapsed = 0; elapsed <= 1500; elapsed += 75) sample(UINT32_MAX - 500U + elapsed);
        check(filter.update(s, s.gpioAtMs) == UiBatteryIndicator::Charging, "charging hysteresis survives millis wrap");
    }
    BatteryStatus battery{}; UiBattery ui{};
    uint32_t batteryClock = 200;
    auto settleBattery = [&]() {
        for (uint32_t elapsed = 0; elapsed <= BatteryIndicatorFilter::kTransitionMs; elapsed += 75) {
            batteryClock += 75;
            battery.measurementAtMs = battery.gpioAtMs = batteryClock;
            uiBatteryRender(ui, battery, batteryClock);
        }
    };
    const auto initialChildren = lv_obj_get_child_cnt(lv_layer_top());
    uiBatteryRender(ui, battery, 0);
    check(ui.root == nullptr && lv_obj_get_child_cnt(lv_layer_top()) == initialChildren,
          "disabled battery UI creates no object or placeholder");
    AppState app{}; appInit(app, 0);
    app.authorityOnline = true; app.authorityRoomId = 109; app.selfSeatId = 1; app.hasAuthoritativeState = true;
    app.page = app.nav.current.page = ScreenPage::NameHandwriting;
    app.identity.phase = IdentityClientPhase::NameHandwriting;
    app.identity.authorityPhase = TransportIdentityPhase::AwaitName;
    uiRendererRender(app, 1); lv_obj_update_layout(lv_scr_act());
    appendPhysicalSample(20, 20, 10, false); appendPhysicalSample(20, 80, 40, true);
    auto *canvas = activeCanvas; const auto generation = captureGeneration;
    const auto rendererStats = uiRendererGetTestStats();
    battery.enabled = true;
    uiBatteryRender(ui, battery, 100);
    check(std::strcmp(lv_label_get_text(ui.label), "--") == 0 && !ui.measurement,
          "enabled but unavailable shows unknown, not zero or full");
    const auto unknownUpdates = ui.labelUpdates, unknownIconUpdates = ui.iconUpdates;
    uiBatteryRender(ui, battery, 101);
    check(ui.labelUpdates == unknownUpdates && ui.iconUpdates == unknownIconUpdates,
          "unchanged displayed values never rewrite labels or invalidate the icon");
    battery.linkReady = battery.measurementValid = battery.gpioValid = true;
    battery.percent = 78; battery.external = ExternalPower::Present; battery.charge = ChargeIndicator::Active;
    settleBattery(); lv_obj_update_layout(lv_layer_top());
    check(std::strcmp(lv_label_get_text(ui.label), "78%") == 0 && ui.indicator == UiBatteryIndicator::Charging &&
          ui.percent == 78 && ui.previousColor == 0x48D597,
          "valid SOC drives the green fill and charging requires confirmed external input");
    check(activeCanvas == canvas && pointCount == 2 && captureGeneration == generation &&
          uiRendererGetTestStats().rebuildRenders == rendererStats.rebuildRenders,
          "battery updates preserve the handwriting canvas, ink and renderer revision");
    check(!lv_obj_has_flag(ui.root, LV_OBJ_FLAG_CLICKABLE) && !lv_obj_has_flag(ui.label, LV_OBJ_FLAG_CLICKABLE) &&
          lv_obj_get_group(ui.root) == nullptr,
          "status layer cannot intercept touch or join encoder focus");
    check(uiRectInsideCircle(kBatteryBadge, 238) && !overlaps(ui.root, activeCanvas), "entire battery badge is inside the round raster and outside canvas");
    check(lv_font_montserrat_12.line_height <= lv_obj_get_height(ui.label), "complete font line height fits without vertical clipping");
    screenshot("battery-handwriting.ppm");
    battery.charge = ChargeIndicator::Inactive; battery.percent = 42;
    settleBattery();
    check(std::strcmp(lv_label_get_text(ui.label), "42%") == 0 && ui.indicator == UiBatteryIndicator::None &&
          ui.percent == 42 && ui.previousColor == 0xF4C35E,
          "inactive CHG does not claim full charge");
    battery.charge = ChargeIndicator::FaultCandidate;
    battery.measurementAtMs = battery.gpioAtMs = ++batteryClock;
    uiBatteryRender(ui, battery, batteryClock);
    check(std::strcmp(lv_label_get_text(ui.label), "42%") == 0 && ui.indicator == UiBatteryIndicator::Warning,
          "2 Hz candidate shows a warning without altering the measured charge");
    bool widths = true;
    for (const char *text : {"--", "100%", "50%", "15%", "0%"}) {
        lv_point_t size{}; lv_txt_get_size(&size, text, &lv_font_montserrat_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        widths &= size.x <= lv_obj_get_width(ui.label);
    }
    check(widths, "every power label fits the badge at its real font width");
    batteryClock += 4000; uiBatteryRender(ui, battery, batteryClock);
    check(std::strcmp(lv_label_get_text(ui.label), "--") == 0 && !ui.measurement && ui.indicator == UiBatteryIndicator::None,
          "rendering an old snapshot cannot keep stale values visible");
    bool pageClear = true, sameOverlay = true;
    auto *overlay = ui.root;
    for (uint8_t page = 0; page <= static_cast<uint8_t>(ScreenPage::WaitingForGame); ++page) {
        app.page = app.nav.current.page = static_cast<ScreenPage>(page); app.revision++;
        uiRendererRender(app, 4100 + page); lv_obj_update_layout(lv_scr_act());
        bool clear = true;
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) clear &= contentClear(lv_obj_get_child(root, i), ui.root);
        for (uint8_t i = 0; i < tapBindingCount; ++i) clear &= !overlaps(tapBindings[i].object, ui.root);
        if (!clear) std::printf("OVERLAP page=%u\n", page);
        pageClear &= clear; sameOverlay &= ui.root == overlay;
        if (page == static_cast<uint8_t>(ScreenPage::Assets)) screenshot("battery-assets-unknown.ppm");
    }
    check(pageClear, "all 31 page labels, images, top indicators and input surfaces avoid the badge");
    check(sameOverlay, "page rebuilds preserve the independent status object");
    app.page = app.nav.current.page = ScreenPage::Home; app.revision++;
    uiRendererRender(app, 5000);
    battery.percent = 100; settleBattery(); screenshot("battery-home-fault.ppm");
    app.modal.kind = ModalKind::VoluntaryMortgage; app.modal.amount = 50;
    app.modal.title = "MORTGAGE"; app.modal.cancelAllowed = true; app.revision++;
    uiRendererRender(app, 5001); lv_obj_update_layout(lv_scr_act());
    check(activeModal.confirm != nullptr && activeModal.back != nullptr &&
          !overlaps(ui.root, activeModal.confirm) && !overlaps(ui.root, activeModal.back) &&
          contentClear(activeModal.panel, ui.root), "real modal labels and both decision buttons remain clear");
    battery.percent = 0; battery.external = ExternalPower::Absent; battery.charge = ChargeIndicator::Inactive;
    settleBattery();
    check(std::strcmp(lv_label_get_text(ui.label), "0%") == 0 && ui.measurement && ui.percent == 0 &&
          ui.previousColor == 0xFF6878 && ui.indicator == UiBatteryIndicator::None,
          "valid empty battery and absent external input are displayed separately from unknown");
    screenshot("battery-modal-zero.ppm");
    {
        HomeBuildFixture f; appInit(app, 0, true); f.apply(app, 6000);
        app.nav.current.focus = app.focus = homeFocusFor(app, HomeAction::Build);
        appHandleUiEvent(app, UiEvent{UiEventKind::ActivateFocused, 0}, 6010);
        buildArtworkPreview = true;
        battery.measurementValid = battery.gpioValid = true;
        battery.percent = 78;
        battery.external = ExternalPower::Present; battery.charge = ChargeIndicator::Active;
        uiRendererRender(app, 6010); settleBattery(); lv_obj_update_layout(lv_scr_act());
        bool clear = contentClear(root, ui.root);
        for (uint8_t i = 0; i < tapBindingCount; ++i) clear &= !overlaps(tapBindings[i].object, ui.root);
        check(app.page == ScreenPage::Build && clear, "Build controls and artwork remain clear of actual battery overlay");
        screenshot("battery-build-house.ppm");
        app.authorityAssets[f.asset].buildingLevel = app.authorityAssets[f.peer].buildingLevel = 4;
        ++app.revision; uiRendererRender(app, 6020); uiBatteryRender(ui, battery, ++batteryClock);
        screenshot("battery-build-hotel.ppm");
        check(contentClear(root, ui.root), "hotel upgrade preview and battery remain separate");
        app.authorityAssets[f.asset].buildingLevel = 1; app.authorityAssets[f.peer].buildingLevel = 0;
        ++app.revision; uiRendererRender(app, 6030); uiBatteryRender(ui, battery, ++batteryClock);
        screenshot("battery-build-even.ppm");
        check(contentClear(root, ui.root) && appAssetBuildReason(app, f.asset) != nullptr,
              "even-building blocker and battery are visible together");
        app.authorityAssets[f.asset].buildingLevel = app.authorityAssets[f.peer].buildingLevel = 5;
        ++app.revision; uiRendererRender(app, 6040); uiBatteryRender(ui, battery, ++batteryClock);
        screenshot("battery-build-maximum.ppm");
        buildArtworkPreview = false;
    }
    appInit(app, 0); app.page = app.nav.current.page = ScreenPage::WaitingForGame;
    uiRendererRender(app, 7000);
    battery.measurementValid = battery.gpioValid = true;
    battery.external = ExternalPower::Absent; battery.charge = ChargeIndicator::Inactive;
    for (const uint8_t percent : {100, 96, 51, 50, 25, 15, 5, 1, 0}) {
        battery.percent = percent; settleBattery();
        char path[48]; std::snprintf(path, sizeof(path), "battery-arc-%u.ppm", percent); screenshot(path);
    }
    battery.percent = 96; battery.external = ExternalPower::Present; battery.charge = ChargeIndicator::Active;
    settleBattery(); screenshot("battery-arc-charging.ppm");
    batteryClock += 4000; uiBatteryRender(ui, battery, batteryClock); screenshot("battery-arc-unknown.ppm");
    battery.enabled = false; uiBatteryRender(ui, battery, batteryClock);
    check(ui.root == nullptr && lv_obj_get_child_cnt(lv_layer_top()) == initialChildren, "disabling removes only the battery status layer");
    std::printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
