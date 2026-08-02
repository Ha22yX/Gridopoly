#include "ui_renderer.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "app_config.h"
#include "app_state.h"
#include "demo_data.h"
#include "src/assets/sample_assets.h"
#include "src/fonts/ui_fonts.h"
#include "ui_carousel.h"
#include "ui_center_list.h"
#include "ui_motion.h"
#include "ui_primitives.h"

// Keep the call sites compact while replacing LVGL's incomplete built-in CJK
// font with the project's verified Noto Sans SC subset.
#define lv_font_simsun_16_cjk ui_font_16

namespace {

constexpr uint32_t kBg = 0x090E10;
constexpr uint32_t kPanel = 0x11191B;
constexpr uint32_t kLine = 0x263234;
constexpr uint32_t kText = 0xEDF3F1;
constexpr uint32_t kMuted = 0x81908C;
constexpr uint32_t kGreen = 0x52DCB7;
constexpr uint32_t kYellow = 0xF2C453;
constexpr uint32_t kRed = 0xEF7168;
constexpr uint32_t kBlue = 0x58A7EB;

lv_obj_t *root = nullptr;
uint32_t renderedRevision = 0;
uint32_t lastDynamicDrawMs = 0;
lv_obj_t *modalFill = nullptr;
lv_obj_t *modalCountdown = nullptr;
UiCarousel homeCarousel{};
UiCenterList centerList{};
struct TapBinding {
    lv_obj_t *object;
    TouchAction action;
};
TapBinding tapBindings[12] = {};
uint8_t tapBindingCount = 0;
TouchAction touchQueue[12] = {};
uint8_t touchHead = 0;
uint8_t touchTail = 0;
uint8_t touchCount = 0;
int8_t focusMotion = 0;
bool hasRenderedState = false;
ScreenPage previousPage = ScreenPage::Home;
uint8_t previousFocus = 0;
HomePhase previousHomePhase = HomePhase::Waiting;
InlineEditField previousInlineEditField = InlineEditField::None;
lv_style_transition_dsc_t pressTransition;
const lv_style_prop_t pressTransitionProps[] = {
    LV_STYLE_TRANSFORM_ZOOM,
    LV_STYLE_BG_OPA,
    static_cast<lv_style_prop_t>(0),
};

constexpr UiRect childRect(UiRect absolute, UiRect parent)
{
    return UiRect{static_cast<int16_t>(absolute.x - parent.x),
                  static_cast<int16_t>(absolute.y - parent.y), absolute.w, absolute.h};
}

constexpr UiRect modalChildRect(UiRect absolute)
{
    return childRect(absolute, kModalRect);
}

constexpr UiRect kModalTitle{120, 120, 240, 28};
constexpr UiRect kModalAmount{128, 156, 224, 36};
constexpr UiRect kModalDetail{120, 198, 240, 28};
constexpr UiRect kModalCountdown{128, 230, 224, 20};
constexpr UiRect kModalConfirmLabel{140, 272, 200, 32};
constexpr UiRect kHomeTitle{70, 66, 340, 44};
constexpr UiRect kHomeCashLabel{150, 132, 180, 22};
constexpr UiRect kHomeCashAmount{70, 158, 340, 48};
constexpr UiRect kHomeLocation{90, 226, 300, 28};
constexpr UiRect kHomeDivider{96, 270, 288, 1};
constexpr UiRect kDetailPrimary{128, 278, 224, 56};
constexpr UiRect kTradeReceiver{104, 128, 272, 50};
constexpr UiRect kTradeAmount{104, 190, 272, 50};
constexpr UiRect kTradeSubmit{128, 262, 224, 56};
constexpr uint8_t kDemoListCount = 6;

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t kLockedMarkerMap[] = {
    0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x7F, 0xF8, 0x20, 0x20, 0x01, 0x80,
    0xFF, 0xFC, 0x63, 0x26, 0x7F, 0xFE, 0x00, 0x0C, 0x7D, 0x24, 0x7F, 0xFE,
    0x00, 0x0C, 0xC0, 0x20, 0x40, 0x02, 0x60, 0x0C, 0xFB, 0xFE, 0x7F, 0xFE,
    0x60, 0x0C, 0x7B, 0x06, 0xFF, 0xFE, 0x7F, 0xFC, 0x22, 0x22, 0x01, 0x80,
    0x60, 0x0C, 0xFA, 0x26, 0x11, 0x80, 0x60, 0x00, 0xFA, 0x26, 0x11, 0x80,
    0x60, 0x00, 0x22, 0x26, 0x31, 0xFC, 0x60, 0x02, 0x22, 0x66, 0x31, 0x80,
    0x60, 0x06, 0x3E, 0x72, 0x39, 0x80, 0x60, 0x06, 0x39, 0xDC, 0x6D, 0x80,
    0x3F, 0xFC, 0x37, 0x86, 0xC7, 0xFF, 0x00, 0x00, 0x06, 0x02, 0xC0, 0x00,
};

const lv_img_dsc_t *lockedMarkerImage()
{
    static lv_img_dsc_t image{};
    static bool initialized = false;
    if (!initialized) {
        image.header.cf = LV_IMG_CF_ALPHA_1BIT;
        image.header.always_zero = 0;
        image.header.reserved = 0;
        image.header.w = 48;
        image.header.h = 16;
        image.data_size = sizeof(kLockedMarkerMap);
        image.data = kLockedMarkerMap;
        initialized = true;
    }
    return &image;
}

constexpr bool rectInside(UiRect child, UiRect parent)
{
    return child.x >= parent.x && child.y >= parent.y &&
           child.x + child.w <= parent.x + parent.w && child.y + child.h <= parent.y + parent.h;
}

static_assert(rectInside(kModalTitle, kModalRect) && rectInside(kModalAmount, kModalRect) &&
              rectInside(kModalDetail, kModalRect) && rectInside(kModalCountdown, kModalRect) &&
              rectInside(kModalConfirm, kModalRect) && rectInside(kModalConfirmLabel, kModalConfirm),
              "modal children must remain inside their parent rectangles");
static_assert(kModalTitle.y + kModalTitle.h <= kModalAmount.y &&
              kModalAmount.y + kModalAmount.h <= kModalDetail.y &&
              kModalDetail.y + kModalDetail.h <= kModalCountdown.y &&
              kModalCountdown.y + kModalCountdown.h <= kModalConfirm.y &&
              kModalConfirmLabel.y + kModalConfirmLabel.h <= kModalConfirm.y + kModalConfirm.h - 6,
              "modal content bands must not overlap");

void setAnimatedX(void *object, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value));
}

void setAnimatedOpacity(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void animateFocusEntry(lv_obj_t *obj)
{
    lv_obj_update_layout(obj);
    const int16_t targetX = lv_obj_get_x(obj);
    const int16_t offset = focusMotion == 0 ? 0 : static_cast<int16_t>(focusMotion * 14);
    lv_obj_set_x(obj, targetX + offset);
    lv_obj_set_style_opa(obj, LV_OPA_40, 0);

    lv_anim_t move;
    lv_anim_init(&move);
    lv_anim_set_var(&move, obj);
    lv_anim_set_exec_cb(&move, setAnimatedX);
    lv_anim_set_values(&move, targetX + offset, targetX);
    lv_anim_set_time(&move, 160);
    lv_anim_set_path_cb(&move, lv_anim_path_ease_out);
    lv_anim_start(&move);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_exec_cb(&fade, setAnimatedOpacity);
    lv_anim_set_values(&fade, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&fade, 140);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
}

void enqueueTouch(TouchAction action)
{
    if (touchCount >= 12) return;
    touchQueue[touchTail] = action;
    touchTail = static_cast<uint8_t>((touchTail + 1) % 12);
    ++touchCount;
}

void handleUiEvent(const UiEvent &event)
{
    if (event.kind == UiEventKind::HoldDown) {
        enqueueTouch(TouchAction::PressDown);
    } else if (event.kind == UiEventKind::HoldUp) {
        enqueueTouch(TouchAction::PressUp);
    } else if (event.kind == UiEventKind::ActivateFocused) {
        enqueueTouch(static_cast<TouchAction>(event.value));
    } else if (event.kind == UiEventKind::SelectHomeAction && event.value >= 0 && event.value < 5) {
        enqueueTouch(static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::HomeAssets) + event.value));
    } else if (event.kind == UiEventKind::SelectListItem && event.value >= 0 && event.value < 16) {
        enqueueTouch(static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::ListItem0) + event.value));
    } else if (event.kind == UiEventKind::ListPrevious) {
        enqueueTouch(TouchAction::ListPrevious);
    } else if (event.kind == UiEventKind::ListNext) {
        enqueueTouch(TouchAction::ListNext);
    } else if (event.kind == UiEventKind::SelectFooter) {
        enqueueTouch(TouchAction::Footer);
    } else if (event.kind == UiEventKind::Back) {
        enqueueTouch(TouchAction::Back);
    }
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int16_t x, int16_t y, int16_t width,
                const lv_font_t *font, uint32_t rgb, lv_text_align_t align = LV_TEXT_ALIGN_CENTER)
{
    return uiLabel(parent, text, UiRect{x, y, width, 32}, font, rgb, align);
}

lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t bg,
              uint32_t border, uint8_t radius = 6)
{
    return uiBox(parent, UiRect{x, y, w, h}, bg, border, radius);
}

void makeClickable(lv_obj_t *obj, TouchAction action)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(obj);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_obj_set_style_transition(obj, &pressTransition, LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(obj, 244, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, LV_STATE_PRESSED);
    uiBindTap(obj, UiEventKind::ActivateFocused, static_cast<int16_t>(action));
    if (tapBindingCount < sizeof(tapBindings) / sizeof(tapBindings[0])) {
        tapBindings[tapBindingCount++] = TapBinding{obj, action};
    }
}

uint32_t focusAccent(const AppState &state)
{
    switch (state.page) {
        case ScreenPage::Players:
        case ScreenPage::PlayerDetail: return kYellow;
        case ScreenPage::Trade:
        case ScreenPage::DemoLab: return kBlue;
        case ScreenPage::Debt: return kRed;
        default: return kGreen;
    }
}

bool isCenterListPage(ScreenPage page)
{
    return page == ScreenPage::Assets || page == ScreenPage::Players ||
           page == ScreenPage::DemoLab;
}

uint8_t tradeFocusForAction(const AppState &state, TouchAction action)
{
    const bool locked = appTradeReceiverLocked(state);
    if (action == TouchAction::TradeReceiver) return locked ? 0xff : 0;
    if (action == TouchAction::TradeAmount) return locked ? 0 : 1;
    if (action == TouchAction::TradeConfirm) return locked ? 1 : 2;
    return locked ? 2 : 3;
}

bool bindingIsFocused(const AppState &state, TouchAction action)
{
    if (action == TouchAction::Footer || action == TouchAction::Back) {
        return appFocusIsFooter(state);
    }
    switch (state.page) {
        case ScreenPage::Home:
            return false;
        case ScreenPage::Assets:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Asset0) + state.focus);
        case ScreenPage::Players:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Player0) + state.focus);
        case ScreenPage::AssetDetail:
        case ScreenPage::PlayerDetail:
            return action == (state.focus == 0 ? TouchAction::DetailPrimary : TouchAction::DetailBack);
        case ScreenPage::Trade:
            return tradeFocusForAction(state, action) == state.focus;
        case ScreenPage::More:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::MoreDemoLab) + state.focus);
        case ScreenPage::DemoLab:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::DemoWaiting) + state.focus);
        case ScreenPage::Debt:
            return action == TouchAction::DetailPrimary && state.focus == 0;
        default: return false;
    }
}

void refreshSamePageFocus(const AppState &state)
{
    const uint32_t accent = focusAccent(state);
    for (uint8_t index = 0; index < tapBindingCount; ++index) {
        TapBinding &binding = tapBindings[index];
        const bool focused = bindingIsFocused(state, binding.action);
        lv_obj_set_style_bg_color(binding.object, lv_color_hex(focused ? 0x16302A : kPanel), 0);
        lv_obj_set_style_border_color(binding.object, lv_color_hex(focused ? accent : kLine), 0);
        if (focused) animateFocusEntry(binding.object);
    }
}

void fillAssetItems(UiListItemView (&items)[kAssetCount],
                    char (&meta)[kAssetCount][24])
{
    for (uint8_t index = 0; index < kAssetCount; ++index) {
        snprintf(meta[index], sizeof(meta[index]),
                 kAssets[index].mortgaged ? "已抵押" : "$%d", kAssets[index].value);
        items[index] = UiListItemView{kAssets[index].name, meta[index], "", true, false};
    }
}

void fillPlayerItems(UiListItemView (&items)[kPlayerCount],
                     char (&meta)[kPlayerCount][32])
{
    for (uint8_t index = 0; index < kPlayerCount; ++index) {
        snprintf(meta[index], sizeof(meta[index]), "$%ld | %u",
                 static_cast<long>(kPlayers[index].money), kPlayers[index].position);
        items[index] = UiListItemView{kPlayers[index].name, meta[index], "", true, false};
    }
}

void fillDemoItems(UiListItemView (&items)[kDemoListCount])
{
    static const char *titles[kDemoListCount] = {
        "Waiting turn", "Next turn", "My turn", "20s rent", "10s payment", "Debt resolution",
    };
    for (uint8_t index = 0; index < kDemoListCount; ++index) {
        items[index] = UiListItemView{titles[index], "", "", true, false};
    }
}

void updateCenterListFocus(const AppState &state)
{
    if (state.page == ScreenPage::Assets) {
        UiListItemView items[kAssetCount]{};
        char meta[kAssetCount][24]{};
        fillAssetItems(items, meta);
        uiCenterListUpdate(centerList, items, kAssetCount, state.assetListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::Players) {
        UiListItemView items[kPlayerCount]{};
        char meta[kPlayerCount][32]{};
        fillPlayerItems(items, meta);
        uiCenterListUpdate(centerList, items, kPlayerCount, state.playerListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::DemoLab) {
        UiListItemView items[kDemoListCount]{};
        fillDemoItems(items);
        uiCenterListUpdate(centerList, items, kDemoListCount, state.demoListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    }
}

lv_obj_t *drawFooter(const char *text, bool focused)
{
    lv_obj_t *footer = uiBox(root, kNormalFooter, focused ? 0x16302A : kPanel,
                             focused ? kGreen : kLine, 6);
    if (footer == nullptr) return nullptr;
    uiLabel(footer, text, UiRect{8, 16, static_cast<int16_t>(kNormalFooter.w - 16), 28},
            &ui_font_16, focused ? kGreen : kText);
    makeClickable(footer, TouchAction::Footer);
    if (focused) animateFocusEntry(footer);
    return footer;
}

void drawOuterRing(uint32_t accent = kGreen)
{
    lv_obj_t *ring = box(root, 31, 31, 418, 418, kBg, accent, 209);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 5, 0);
    lv_obj_t *inner = box(root, 44, 44, 392, 392, kBg, kLine, 196);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 2, 0);
}

void drawHeader(const char *title, const char *eyebrow = "GRIDOPOLY")
{
    label(root, eyebrow, 100, 55, 280, &ui_font_14, kMuted);
    label(root, title, 70, 77, 340, &lv_font_simsun_16_cjk, kText);
}

void drawToast(const AppState &state, uint32_t nowMs)
{
    if (state.toastUntilMs == 0 ||
        static_cast<int32_t>(nowMs - state.toastUntilMs) >= 0) return;
    lv_obj_t *toast = box(root, 112, 397, 256, 36, 0x172224, kGreen, 6);
    label(toast, state.toast, 8, 7, 240, &lv_font_simsun_16_cjk, kText);
}

void drawHome(const AppState &state)
{
    const uint32_t accent = state.homePhase == HomePhase::MyTurn ? kYellow : kGreen;
    drawOuterRing(accent);
    const char *title = "WAITING | 3 PLAYERS AHEAD";
    if (state.homePhase == HomePhase::Waiting) {
        title = "WAITING | 3 PLAYERS AHEAD";
    } else if (state.homePhase == HomePhase::NextPlayer) {
        title = "NEXT | YOUR TURN IS CLOSE";
    } else {
        title = "MY TURN | ACTIONS";
    }
    uiLabel(root, title, kHomeTitle, &lv_font_montserrat_16, kText);
    uiLabel(root, "AVAILABLE CASH", kHomeCashLabel, &lv_font_montserrat_14, kMuted);
    char money[24];
    snprintf(money, sizeof(money), "$ %ld", static_cast<long>(state.money));
    const uint8_t moneyPx = uiMoneyFontPx(state.money);
    const lv_font_t *moneyFont = moneyPx == 40 ? &lv_font_montserrat_40 :
                                 (moneyPx == 32 ? &lv_font_montserrat_32 :
                                                  &lv_font_montserrat_24);
    uiLabel(root, money, kHomeCashAmount, moneyFont, kText);
    char location[48];
    snprintf(location, sizeof(location), "TILE %u | RAINBOW HARBOR", state.position);
    uiLabel(root, location, kHomeLocation, &lv_font_montserrat_14, kGreen);
    lv_obj_t *divider = uiBox(root, kHomeDivider, kLine, kLine, 0);
    if (divider != nullptr) lv_obj_set_style_border_width(divider, 0, 0);

    HomeAction actions[5]{};
    const uint8_t actionCount = uiCarouselActions(state.homePhase, actions);
    uiCarouselCreate(homeCarousel, root, actions, actionCount, state.focus);
}

void drawListRow(int16_t y, const char *title, const char *meta, bool selected, TouchAction action)
{
    lv_obj_t *row = box(root, 72, y, 336, 48, selected ? 0x16302A : kPanel,
                        selected ? kGreen : kLine, 6);
    label(row, title, 14, 7, 190, &lv_font_simsun_16_cjk, selected ? kGreen : kText, LV_TEXT_ALIGN_LEFT);
    label(row, meta, 210, 7, 110, &lv_font_simsun_16_cjk, kMuted, LV_TEXT_ALIGN_RIGHT);
    makeClickable(row, action);
    if (selected) animateFocusEntry(row);
}

void drawAssets(const AppState &state)
{
    drawOuterRing(kGreen);
    drawHeader("我的资产", "8 ASSETS");
    UiListItemView items[kAssetCount]{};
    char meta[kAssetCount][24]{};
    fillAssetItems(items, meta);
    uiCenterListCreate(centerList, root, items, kAssetCount, state.assetListIndex,
                       "返回", true, appFocusIsFooter(state));
}

void drawAssetDetail(const AppState &state)
{
    drawOuterRing(kBlue);
    const AssetData &asset = kAssets[state.selectedAsset];
    drawHeader(asset.name, asset.group);
    const lv_img_dsc_t *sample = &asset_property_neon_harbor;
    if (state.selectedAsset == 1) sample = &asset_transit_cloudrail_central;
    else if (state.selectedAsset == 2) sample = &asset_utility_quantum_grid;
    lv_obj_t *art = lv_img_create(root);
    lv_img_set_src(art, sample);
    lv_obj_set_pos(art, 160, 101);
    char info[64];
    snprintf(info, sizeof(info), "估值 $%d  ·  地租 $%d", asset.value, asset.rent);
    label(root, info, 80, 242, 320, &lv_font_simsun_16_cjk, kText);
    lv_obj_t *primary = uiBox(root, kDetailPrimary,
                              state.focus == 0 ? 0x16302A : kPanel,
                              state.focus == 0 ? kGreen : kLine, 6);
    if (primary != nullptr) {
        uiLabel(primary, asset.mortgaged ? "解除抵押" : "抵押",
                UiRect{8, 16, static_cast<int16_t>(kDetailPrimary.w - 16), 28},
                &ui_font_16, state.focus == 0 ? kGreen : kText);
        makeClickable(primary, TouchAction::DetailPrimary);
        if (state.focus == 0) animateFocusEntry(primary);
    }
    drawFooter("返回", state.focus == 1);
}

void drawPlayers(const AppState &state)
{
    drawOuterRing(kYellow);
    drawHeader("玩家情况", "5 PLAYERS");
    UiListItemView items[kPlayerCount]{};
    char meta[kPlayerCount][32]{};
    fillPlayerItems(items, meta);
    uiCenterListCreate(centerList, root, items, kPlayerCount, state.playerListIndex,
                       "返回", true, appFocusIsFooter(state));
}

void drawPlayerDetail(const AppState &state)
{
    drawOuterRing(kYellow);
    const PlayerData &player = kPlayers[state.selectedPlayer];
    drawHeader(player.name, player.token);
    if (state.selectedPlayer == 0) {
        lv_obj_t *avatar = lv_img_create(root);
        lv_img_set_src(avatar, &asset_avatar_p1_lingxi);
        lv_obj_set_pos(avatar, 176, 108);
    } else {
        lv_obj_t *avatar = box(root, 178, 112, 124, 124, 0x2B2618, kYellow, 62);
        label(avatar, player.token, 22, 38, 80, &lv_font_montserrat_32, kYellow);
    }
    char info[48];
    snprintf(info, sizeof(info), "$ %ld  ·  第 %u 格", static_cast<long>(player.money), player.position);
    label(root, info, 90, 242, 300, &lv_font_simsun_16_cjk, kText);
    lv_obj_t *primary = uiBox(root, kDetailPrimary,
                              state.focus == 0 ? 0x2B2618 : kPanel,
                              state.focus == 0 ? kYellow : kLine, 6);
    if (primary != nullptr) {
        uiLabel(primary, "发起交易",
                UiRect{8, 16, static_cast<int16_t>(kDetailPrimary.w - 16), 28},
                &ui_font_16, state.focus == 0 ? kYellow : kText);
        makeClickable(primary, TouchAction::DetailPrimary);
        if (state.focus == 0) animateFocusEntry(primary);
    }
    drawFooter("返回", state.focus == 1);
}

void drawTradeField(UiRect rect, const char *text, const char *marker,
                    bool focused, bool editing, TouchAction action, bool enabled)
{
    lv_obj_t *field = uiBox(root, rect, focused ? 0x14242A : kPanel,
                            focused ? kBlue : kLine, 6);
    if (field == nullptr) return;
    if (editing) {
        lv_obj_t *inner = uiBox(field, UiRect{4, 4, static_cast<int16_t>(rect.w - 8),
                                             static_cast<int16_t>(rect.h - 8)},
                                kPanel, kBlue, 4);
        if (inner != nullptr) {
            lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    uiLabel(field, text, UiRect{12, 9, 184, 28}, &ui_font_16,
            enabled ? (focused ? kBlue : kText) : kMuted, LV_TEXT_ALIGN_LEFT);
    if (!enabled && !editing) {
        lv_obj_t *lockedMarker = lv_img_create(field);
        if (lockedMarker != nullptr) {
            lv_img_set_src(lockedMarker, lockedMarkerImage());
            lv_obj_set_pos(lockedMarker, 210, 17);
            lv_obj_set_style_img_recolor(lockedMarker, lv_color_hex(kMuted), 0);
            lv_obj_set_style_img_recolor_opa(lockedMarker, LV_OPA_COVER, 0);
            lv_obj_clear_flag(lockedMarker, LV_OBJ_FLAG_CLICKABLE);
        }
    } else {
        uiLabel(field, editing ? "EDIT" : marker, UiRect{198, 9, 60, 28},
                &lv_font_montserrat_14, editing ? kBlue : kMuted,
                LV_TEXT_ALIGN_RIGHT);
    }
    if (enabled) makeClickable(field, action);
    if (focused && !editing) animateFocusEntry(field);
}

void drawTrade(const AppState &state)
{
    drawOuterRing(kBlue);
    drawHeader("交易草案", "不会自动扣款");
    const bool receiverLocked = appTradeReceiverLocked(state);
    const uint8_t receiverFocus = 0;
    const uint8_t amountFocus = receiverLocked ? 0 : 1;
    const uint8_t submitFocus = receiverLocked ? 1 : 2;
    const uint8_t backFocus = receiverLocked ? 2 : 3;
    char receiver[48];
    snprintf(receiver, sizeof(receiver), "接收方  %s", kPlayers[state.tradeReceiver].name);
    char amount[48];
    snprintf(amount, sizeof(amount), "金额  $ %ld", static_cast<long>(state.tradeAmount));
    drawTradeField(kTradeReceiver, receiver, "",
                   !receiverLocked && state.focus == receiverFocus,
                   appInlineFieldEditing(state, InlineEditField::TradeReceiver),
                   TouchAction::TradeReceiver, !receiverLocked);
    drawTradeField(kTradeAmount, amount, "", state.focus == amountFocus,
                   appInlineFieldEditing(state, InlineEditField::TradeAmount),
                   TouchAction::TradeAmount, true);
    lv_obj_t *submit = uiBox(root, kTradeSubmit,
                             state.focus == submitFocus ? 0x14242A : kPanel,
                             state.focus == submitFocus ? kBlue : kLine, 6);
    if (submit != nullptr) {
        uiLabel(submit, "提交交易",
                UiRect{8, 16, static_cast<int16_t>(kTradeSubmit.w - 16), 28},
                &ui_font_16, state.focus == submitFocus ? kBlue : kText);
        makeClickable(submit, TouchAction::TradeConfirm);
        if (state.focus == submitFocus) animateFocusEntry(submit);
    }
    drawFooter("返回", state.focus == backFocus);
}

void drawMore(const AppState &state)
{
    drawOuterRing(kGreen);
    drawHeader("更多", "PLAYER CONSOLE");
    drawListRow(154, "Demo Lab", "场景测试", state.focus == 0, TouchAction::MoreDemoLab);
    drawListRow(222, "关于设备", "v0.1", state.focus == 1, TouchAction::MoreAbout);
    drawFooter("返回", state.focus == 2);
}

void drawDemoLab(const AppState &state)
{
    drawOuterRing(kBlue);
    drawHeader("Demo Lab", "SCENARIOS");
    UiListItemView items[kDemoListCount]{};
    fillDemoItems(items);
    uiCenterListCreate(centerList, root, items, kDemoListCount, state.demoListIndex,
                       "返回", true, appFocusIsFooter(state));
}

void drawDebt(const AppState &state)
{
    drawOuterRing(kRed);
    drawHeader("资金不足", "DEBT RESOLUTION");
    label(root, "还需筹集", 150, 140, 180, &lv_font_simsun_16_cjk, kMuted);
    label(root, "$ 240", 100, 176, 280, &lv_font_montserrat_40, kRed);
    label(root, "出售房屋或抵押地产后继续付款", 70, 246, 340, &lv_font_simsun_16_cjk, kText);
    lv_obj_t *button = uiBox(root, kDetailPrimary,
                             state.focus == 0 ? 0x2B1718 : kPanel,
                             state.focus == 0 ? kRed : kLine, 6);
    if (button != nullptr) {
        uiLabel(button, "查看可处理资产",
                UiRect{8, 16, static_cast<int16_t>(kDetailPrimary.w - 16), 28},
                &lv_font_simsun_16_cjk, state.focus == 0 ? kRed : kText);
        makeClickable(button, TouchAction::DetailPrimary);
        if (state.focus == 0) animateFocusEntry(button);
    }
    drawFooter("返回", state.focus == 1);
}

void drawModal(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None) return;
    lv_obj_t *shade = box(root, 0, 0, 480, 480, 0x000000, 0x000000, 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    lv_obj_t *modal = uiBox(root, kModalRect, 0x101719, kGreen, 8);
    if (modal == nullptr) return;
    uiLabel(modal, state.modal.title, modalChildRect(kModalTitle), &lv_font_simsun_16_cjk, kText);
    char amount[32];
    snprintf(amount, sizeof(amount), "$ %ld", static_cast<long>(state.modal.amount));
    uiLabel(modal, amount, modalChildRect(kModalAmount), &lv_font_montserrat_36, kGreen);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s · %s", state.modal.counterparty, state.modal.purpose);
    uiLabel(modal, detail, modalChildRect(kModalDetail), &lv_font_simsun_16_cjk, kMuted);

    const uint32_t remaining = appModalRemainingMs(state, nowMs);
    if (state.modal.deadlineMs != 0) {
        char countdown[32];
        snprintf(countdown, sizeof(countdown), "剩余 %.1f 秒", remaining / 1000.0f);
        modalCountdown = uiLabel(modal, countdown, modalChildRect(kModalCountdown),
                                 &lv_font_simsun_16_cjk,
                                 state.modal.kind == ModalKind::ForcedPayment ? kYellow : kMuted);
    }

    lv_obj_t *hold = uiBox(modal, modalChildRect(kModalConfirm), 0x172224, kGreen, 6);
    if (hold == nullptr) return;
    uiBindHold(hold);
    const uint16_t progress = appHoldProgressPermille(state, nowMs);
    modalFill = uiBox(hold, UiRect{0, static_cast<int16_t>(kModalConfirm.h - 6),
                                   static_cast<int16_t>(kModalConfirm.w * progress / 1000), 6},
                        0x20483E, kGreen, 3);
    if (modalFill != nullptr) lv_obj_clear_flag(modalFill, LV_OBJ_FLAG_CLICKABLE);
    uiLabel(hold, "按住 1.2 秒确认", childRect(kModalConfirmLabel, kModalConfirm),
            &lv_font_simsun_16_cjk, kText);
}

void rebuild(const AppState &state, uint32_t nowMs)
{
    modalFill = nullptr;
    modalCountdown = nullptr;
    tapBindingCount = 0;
    uiCarouselDestroy(homeCarousel);
    uiCenterListDestroy(centerList);
    lv_obj_clean(root);
    switch (state.page) {
        case ScreenPage::Home: drawHome(state); break;
        case ScreenPage::Assets: drawAssets(state); break;
        case ScreenPage::AssetDetail: drawAssetDetail(state); break;
        case ScreenPage::Players: drawPlayers(state); break;
        case ScreenPage::PlayerDetail: drawPlayerDetail(state); break;
        case ScreenPage::Trade: drawTrade(state); break;
        case ScreenPage::More: drawMore(state); break;
        case ScreenPage::DemoLab: drawDemoLab(state); break;
        case ScreenPage::Debt: drawDebt(state); break;
    }
    drawToast(state, nowMs);
    drawModal(state, nowMs);
}

void updateDynamic(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None) return;
    if (modalFill != nullptr) {
        const uint16_t progress = appHoldProgressPermille(state, nowMs);
        lv_obj_set_width(modalFill, static_cast<int16_t>(kModalConfirm.w * progress / 1000));
    }
    if (modalCountdown != nullptr && state.modal.deadlineMs != 0) {
        char countdown[32];
        snprintf(countdown, sizeof(countdown), "剩余 %.1f 秒", appModalRemainingMs(state, nowMs) / 1000.0f);
        lv_label_set_text(modalCountdown, countdown);
    }
}

} // namespace

bool uiRendererBegin()
{
    root = lv_obj_create(nullptr);
    if (root == nullptr) return false;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_style_transition_dsc_init(
        &pressTransition,
        pressTransitionProps,
        lv_anim_path_ease_out,
        110,
        0,
        nullptr
    );
    lv_scr_load(root);
    uiSetEventSink(handleUiEvent);
    renderedRevision = 0;
    return true;
}

void uiRendererRender(const AppState &state, uint32_t nowMs)
{
    if (root == nullptr) return;
    if (state.revision != renderedRevision) {
        const bool homeActionsUnchanged = state.page != ScreenPage::Home ||
                                          state.homePhase == previousHomePhase;
        const bool inlineEditUnchanged = state.inlineEditField == previousInlineEditField;
        if (hasRenderedState && state.page == previousPage &&
            state.focus != previousFocus && homeActionsUnchanged && inlineEditUnchanged) {
            const uint8_t count = appFocusCount(state);
            focusMotion = static_cast<uint8_t>((previousFocus + 1) % count) == state.focus ? 1 : -1;
            if (state.page == ScreenPage::Home) {
                uiCarouselSetSelection(homeCarousel, state.focus, true);
            } else if (isCenterListPage(state.page)) {
                updateCenterListFocus(state);
            } else {
                refreshSamePageFocus(state);
            }
            renderedRevision = state.revision;
            previousFocus = state.focus;
            lastDynamicDrawMs = nowMs;
            return;
        } else {
            focusMotion = 0;
        }
        rebuild(state, nowMs);
        renderedRevision = state.revision;
        previousPage = state.page;
        previousFocus = state.focus;
        previousHomePhase = state.homePhase;
        previousInlineEditField = state.inlineEditField;
        hasRenderedState = true;
        lastDynamicDrawMs = nowMs;
        return;
    }
    if (state.modal.kind != ModalKind::None && nowMs - lastDynamicDrawMs >= 50) {
        updateDynamic(state, nowMs);
        lastDynamicDrawMs = nowMs;
    }
}

bool uiRendererPollTouch(TouchAction &action)
{
    if (touchCount == 0) return false;
    action = touchQueue[touchHead];
    touchHead = static_cast<uint8_t>((touchHead + 1) % 12);
    --touchCount;
    return true;
}

void uiRendererShowFault(const char *code)
{
    if (root == nullptr && !uiRendererBegin()) return;
    uiCarouselDestroy(homeCarousel);
    uiCenterListDestroy(centerList);
    lv_obj_clean(root);
    drawOuterRing(kRed);
    drawHeader("设备故障", "GRIDOPOLY");
    label(root, code, 90, 205, 300, &lv_font_montserrat_24, kRed);
}
