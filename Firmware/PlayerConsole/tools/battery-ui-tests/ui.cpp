#include "../../ui_handwriting.cpp"
#include "../../ui_renderer.cpp"
#include "../../ui_battery.h"
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
const lv_img_dsc_t *remoteAvatarFinal(uint32_t, uint8_t, uint16_t, uint64_t) { return nullptr; }
bool remoteAvatarCacheConsumeUpdate() { return false; }
void remoteTileCacheBegin() {}
void remoteTileCacheSetView(uint32_t, uint8_t, GridCityArtwork, const char *, uint8_t) {}
void remoteTileCacheReleaseImage(const lv_img_dsc_t *) {}
void remoteTileCachePrefetch(GridCityArtwork, const char *, uint8_t) {}
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork, const char *, uint8_t) { return nullptr; }
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
    BatteryStatus battery{}; UiBattery ui{};
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
    check(std::strcmp(lv_label_get_text(ui.label), "--%  POWER --") == 0,
          "enabled but unavailable shows unknown, not zero or full");
    const auto unknownUpdates = ui.labelUpdates;
    uiBatteryRender(ui, battery, 101);
    check(ui.labelUpdates == unknownUpdates, "unchanged displayed values never rewrite labels");
    battery.measurementValid = battery.gpioValid = true; battery.measurementAtMs = battery.gpioAtMs = 200;
    battery.percent = 78; battery.external = ExternalPower::Present; battery.charge = ChargeIndicator::Active;
    uiBatteryRender(ui, battery, 200); lv_obj_update_layout(lv_layer_top());
    check(std::strcmp(lv_label_get_text(ui.label), "78%  EXT / CHG") == 0,
          "SOC, external supply and charging indicator are distinct");
    check(activeCanvas == canvas && pointCount == 2 && captureGeneration == generation &&
          uiRendererGetTestStats().rebuildRenders == rendererStats.rebuildRenders,
          "battery updates preserve the handwriting canvas, ink and renderer revision");
    check(!lv_obj_has_flag(ui.root, LV_OBJ_FLAG_CLICKABLE) && !lv_obj_has_flag(ui.label, LV_OBJ_FLAG_CLICKABLE) &&
          lv_obj_get_group(ui.root) == nullptr,
          "status layer cannot intercept touch or join encoder focus");
    check(uiRectInsideCircle(kBatteryBadge, 238) && !overlaps(ui.root, activeCanvas), "entire battery badge is inside the round raster and outside canvas");
    check(lv_font_montserrat_14.line_height <= lv_obj_get_height(ui.label), "complete font line height fits without vertical clipping");
    screenshot("battery-handwriting.ppm");
    battery.charge = ChargeIndicator::Inactive; battery.percent = 42;
    uiBatteryRender(ui, battery, 201);
    check(std::strcmp(lv_label_get_text(ui.label), "42%  EXT") == 0,
          "inactive CHG does not claim full charge");
    battery.charge = ChargeIndicator::FaultCandidate;
    uiBatteryRender(ui, battery, 202);
    check(std::strcmp(lv_label_get_text(ui.label), "42%  CHECK CHG") == 0, "2 Hz candidate is shown as a check, not a definitive diagnosis");
    bool widths = true;
    for (const char *text : {"--%  POWER --", "100%  EXT / CHG", "100%  NO EXT", "100%  CHECK CHG", "100%  CHECK PWR"}) {
        lv_point_t size{}; lv_txt_get_size(&size, text, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        widths &= size.x <= kBatteryBadge.w;
    }
    check(widths, "every power label fits the badge at its real font width");
    uiBatteryRender(ui, battery, 4000);
    check(std::strcmp(lv_label_get_text(ui.label), "--%  POWER --") == 0,
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
    battery.measurementAtMs = battery.gpioAtMs = 5000; battery.percent = 100;
    uiBatteryRender(ui, battery, 5000); screenshot("battery-home-fault.ppm");
    app.modal.kind = ModalKind::VoluntaryMortgage; app.modal.amount = 50;
    app.modal.title = "MORTGAGE"; app.modal.cancelAllowed = true; app.revision++;
    uiRendererRender(app, 5001); lv_obj_update_layout(lv_scr_act());
    check(activeModal.confirm != nullptr && activeModal.back != nullptr &&
          !overlaps(ui.root, activeModal.confirm) && !overlaps(ui.root, activeModal.back) &&
          contentClear(activeModal.panel, ui.root), "real modal labels and both decision buttons remain clear");
    battery.percent = 0; battery.external = ExternalPower::Absent; battery.charge = ChargeIndicator::Inactive;
    uiBatteryRender(ui, battery, 5001);
    check(std::strcmp(lv_label_get_text(ui.label), "0%  NO EXT") == 0,
          "valid empty battery and absent external input are displayed separately from unknown");
    screenshot("battery-modal-zero.ppm");
    battery.enabled = false; uiBatteryRender(ui, battery, 5001);
    check(ui.root == nullptr && lv_obj_get_child_cnt(lv_layer_top()) == initialChildren, "disabling removes only the battery status layer");
    std::printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
