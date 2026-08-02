#include "ui_renderer.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "app_config.h"
#include "app_state.h"
#include "demo_data.h"
#include "src/assets/sample_assets.h"
#include "src/fonts/ui_fonts.h"

// Keep the call sites compact while replacing LVGL's incomplete built-in CJK
// font with the project's verified Noto Sans SC subset.
#define lv_font_simsun_16_cjk ui_font_16

namespace {

lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }
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
TouchAction touchQueue[12] = {};
uint8_t touchHead = 0;
uint8_t touchTail = 0;
uint8_t touchCount = 0;
int8_t focusMotion = 0;
bool hasRenderedState = false;
ScreenPage previousPage = ScreenPage::Home;
uint8_t previousFocus = 0;
lv_style_transition_dsc_t pressTransition;
const lv_style_prop_t pressTransitionProps[] = {
    LV_STYLE_TRANSFORM_ZOOM,
    LV_STYLE_BG_OPA,
    static_cast<lv_style_prop_t>(0),
};

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

void clickCallback(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const intptr_t raw = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    enqueueTouch(static_cast<TouchAction>(raw));
}

void holdCallback(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) enqueueTouch(TouchAction::PressDown);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) enqueueTouch(TouchAction::PressUp);
}

void baseObject(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int16_t x, int16_t y, int16_t width,
                const lv_font_t *font, uint32_t rgb, lv_text_align_t align = LV_TEXT_ALIGN_CENTER)
{
    lv_obj_t *obj = lv_label_create(parent);
    baseObject(obj);
    lv_label_set_text(obj, text);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_size(obj, width, 32);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color(rgb), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    return obj;
}

lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t bg,
              uint32_t border, uint8_t radius = 6)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
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
    lv_obj_add_event_cb(obj, clickCallback, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(action)));
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
    if (state.toastUntilMs == 0 || nowMs >= state.toastUntilMs) return;
    lv_obj_t *toast = box(root, 112, 397, 256, 36, 0x172224, kGreen, 6);
    label(toast, state.toast, 8, 7, 240, &lv_font_simsun_16_cjk, kText);
}

const char *homeMenuName(uint8_t index)
{
    static const char *names[4] = {"资产", "玩家", "交易", "更多"};
    return names[index % 4];
}

TouchAction homeMenuAction(uint8_t index)
{
    return static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::HomeAssets) + index);
}

void drawHomeWheel(const AppState &state)
{
    uint8_t menuFocus = state.focus;
    if (state.homePhase == HomePhase::MyTurn) menuFocus = state.focus == 0 ? 0 : state.focus - 1;
    const uint8_t indexes[3] = {
        static_cast<uint8_t>((menuFocus + 3) % 4), menuFocus, static_cast<uint8_t>((menuFocus + 1) % 4)
    };
    const int16_t xs[3] = {126, 207, 288};
    for (uint8_t i = 0; i < 3; ++i) {
        const bool selected = i == 1 && !(state.homePhase == HomePhase::MyTurn && state.focus == 0);
        lv_obj_t *item = box(root, xs[i], 326, 66, 58, selected ? 0x16302A : kPanel,
                             selected ? kGreen : kLine, 29);
        label(item, homeMenuName(indexes[i]), 3, 18, 60, &lv_font_simsun_16_cjk,
              selected ? kGreen : kMuted);
        makeClickable(item, homeMenuAction(indexes[i]));
        if (selected) animateFocusEntry(item);
    }
}

void drawHome(const AppState &state)
{
    const uint32_t accent = state.homePhase == HomePhase::MyTurn ? kGreen : kYellow;
    drawOuterRing(accent);
    if (state.homePhase == HomePhase::Waiting) {
        drawHeader("3 位玩家后轮到你", "回合队列  ● ● ● ○ ○");
        label(root, "可用资金", 150, 137, 180, &lv_font_simsun_16_cjk, kMuted);
        char money[24];
        snprintf(money, sizeof(money), "$ %ld", static_cast<long>(state.money));
        label(root, money, 70, 167, 340, &lv_font_montserrat_40, kText);
        label(root, "第 17 格 · 霓虹港湾", 90, 235, 300, &lv_font_simsun_16_cjk, kGreen);
    } else if (state.homePhase == HomePhase::NextPlayer) {
        drawHeader("下一位就是你", "回合即将开始");
        lv_obj_t *avatar = box(root, 197, 128, 86, 86, 0x211A1A, kRed, 43);
        label(avatar, "砾", 27, 25, 32, &lv_font_simsun_16_cjk, kRed);
        label(root, "砾川正在行动", 100, 224, 280, &lv_font_simsun_16_cjk, kText);
        label(root, "准备好旋钮与触摸操作", 90, 252, 300, &lv_font_simsun_16_cjk, kMuted);
    } else {
        drawHeader("你的回合", "行动阶段");
        const bool diceFocused = state.focus == 0;
        lv_obj_t *dice = box(root, 191, 119, 98, 98, diceFocused ? 0x16302A : kPanel,
                             diceFocused ? kGreen : kLine, 8);
        label(dice, "•  •\n  •\n•  •", 15, 12, 68, &lv_font_montserrat_20, kGreen);
        makeClickable(dice, TouchAction::DetailPrimary);
        if (diceFocused) animateFocusEntry(dice);
        label(root, "按下掷骰", 120, 226, 240, &lv_font_simsun_16_cjk,
              diceFocused ? kGreen : kText);
        char summary[48];
        snprintf(summary, sizeof(summary), "$ %ld    第 %u 格", static_cast<long>(state.money), state.position);
        label(root, summary, 100, 268, 280, &lv_font_simsun_16_cjk, kMuted);
    }
    drawHomeWheel(state);
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
    drawHeader("我的资产", "5 项地产");
    for (uint8_t i = 0; i < kAssetCount; ++i) {
        char meta[24];
        snprintf(meta, sizeof(meta), kAssets[i].mortgaged ? "已抵押" : "$%d", kAssets[i].value);
        drawListRow(108 + i * 54, kAssets[i].name, meta, state.focus == i,
                    static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Asset0) + i));
    }
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
    label(root, info, 80, 266, 320, &lv_font_simsun_16_cjk, kText);
    const char *names[2] = {asset.mortgaged ? "解除抵押" : "抵押", "返回"};
    for (uint8_t i = 0; i < 2; ++i) {
        lv_obj_t *button = box(root, 112 + i * 136, 307, 120, 56,
                               state.focus == i ? 0x16302A : kPanel,
                               state.focus == i ? kGreen : kLine, 6);
        label(button, names[i], 8, 17, 104, &lv_font_simsun_16_cjk,
              state.focus == i ? kGreen : kText);
        makeClickable(button, i == 0 ? TouchAction::DetailPrimary : TouchAction::DetailBack);
        if (state.focus == i) animateFocusEntry(button);
    }
}

void drawPlayers(const AppState &state)
{
    drawOuterRing(kYellow);
    drawHeader("玩家情况", "5 位玩家");
    for (uint8_t i = 0; i < kPlayerCount; ++i) {
        char meta[32];
        snprintf(meta, sizeof(meta), "$%ld · %u格", static_cast<long>(kPlayers[i].money), kPlayers[i].position);
        drawListRow(108 + i * 54, kPlayers[i].name, meta, state.focus == i,
                    static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Player0) + i));
    }
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
    label(root, info, 90, 253, 300, &lv_font_simsun_16_cjk, kText);
    const char *buttons[2] = {"发起交易", "返回"};
    for (uint8_t i = 0; i < 2; ++i) {
        lv_obj_t *button = box(root, 112 + i * 136, 307, 120, 56,
                               state.focus == i ? 0x2B2618 : kPanel,
                               state.focus == i ? kYellow : kLine, 6);
        label(button, buttons[i], 8, 17, 104, &lv_font_simsun_16_cjk,
              state.focus == i ? kYellow : kText);
        makeClickable(button, i == 0 ? TouchAction::DetailPrimary : TouchAction::DetailBack);
        if (state.focus == i) animateFocusEntry(button);
    }
}

void drawTrade(const AppState &state)
{
    drawOuterRing(kBlue);
    drawHeader("交易草案", "不会自动扣款");
    char receiver[48];
    snprintf(receiver, sizeof(receiver), "接收方  %s", kPlayers[state.tradeReceiver].name);
    char amount[48];
    snprintf(amount, sizeof(amount), "金额  $ %ld", static_cast<long>(state.tradeAmount));
    const char *rows[4] = {receiver, amount, "长按确认发送", "返回"};
    for (uint8_t i = 0; i < 4; ++i) {
        drawListRow(128 + i * 62, rows[i], i < 2 ? "按下修改" : "", state.focus == i,
                    static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::TradeReceiver) + i));
    }
}

void drawMore(const AppState &state)
{
    drawOuterRing(kGreen);
    drawHeader("更多", "PLAYER CONSOLE");
    drawListRow(154, "Demo Lab", "场景测试", state.focus == 0, TouchAction::MoreDemoLab);
    drawListRow(222, "关于设备", "v0.1", state.focus == 1, TouchAction::MoreAbout);
    label(root, "长按 3 秒可快速进入 Demo Lab", 90, 302, 300, &lv_font_simsun_16_cjk, kMuted);
}

void drawDemoLab(const AppState &state)
{
    drawOuterRing(kBlue);
    drawHeader("Demo Lab", "旋转选择场景");
    static const char *items[7] = {"等待回合", "临近回合", "我的回合", "20秒收租", "10秒付款", "债务处理", "返回"};
    const int8_t first = state.focus < 3 ? 0 : (state.focus > 5 ? 4 : state.focus - 2);
    for (uint8_t row = 0; row < 3; ++row) {
        const uint8_t i = static_cast<uint8_t>(first + row);
        drawListRow(140 + row * 66, items[i], i == state.focus ? "按下演示" : "",
                    i == state.focus, static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::DemoWaiting) + i));
    }
}

void drawDebt(const AppState &state)
{
    (void)state;
    drawOuterRing(kRed);
    drawHeader("资金不足", "DEBT RESOLUTION");
    label(root, "还需筹集", 150, 140, 180, &lv_font_simsun_16_cjk, kMuted);
    label(root, "$ 240", 100, 176, 280, &lv_font_montserrat_40, kRed);
    label(root, "出售房屋或抵押地产后继续付款", 70, 246, 340, &lv_font_simsun_16_cjk, kText);
    lv_obj_t *button = box(root, 130, 305, 220, 56, 0x2B1718, kRed, 6);
    label(button, "查看可处理资产", 12, 17, 196, &lv_font_simsun_16_cjk, kRed);
    makeClickable(button, TouchAction::DetailPrimary);
}

void drawModal(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None) return;
    lv_obj_t *shade = box(root, 0, 0, 480, 480, 0x000000, 0x000000, 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_70, 0);
    lv_obj_t *modal = box(root, 70, 88, 340, 304, 0x101719, kGreen, 8);
    label(modal, state.modal.title, 24, 22, 292, &lv_font_simsun_16_cjk, kText);
    char amount[32];
    snprintf(amount, sizeof(amount), "$ %ld", static_cast<long>(state.modal.amount));
    label(modal, amount, 30, 63, 280, &lv_font_montserrat_36, kGreen);
    char detail[96];
    snprintf(detail, sizeof(detail), "%s · %s", state.modal.counterparty, state.modal.purpose);
    label(modal, detail, 20, 116, 300, &lv_font_simsun_16_cjk, kMuted);

    const uint32_t remaining = appModalRemainingMs(state, nowMs);
    if (state.modal.deadlineMs != 0) {
        char countdown[32];
        snprintf(countdown, sizeof(countdown), "剩余 %.1f 秒", remaining / 1000.0f);
        modalCountdown = label(modal, countdown, 70, 148, 200, &lv_font_simsun_16_cjk,
                               state.modal.kind == ModalKind::Payment ? kYellow : kMuted);
    }

    lv_obj_t *hold = box(modal, 34, 196, 272, 72, 0x172224, kGreen, 6);
    lv_obj_add_flag(hold, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hold, holdCallback, LV_EVENT_ALL, nullptr);
    const uint16_t progress = appHoldProgressPermille(state, nowMs);
    modalFill = box(hold, 0, 0, static_cast<int16_t>(272 * progress / 1000), 72, 0x20483E, kGreen, 6);
    lv_obj_clear_flag(modalFill, LV_OBJ_FLAG_CLICKABLE);
    label(hold, "按住 1.2 秒确认", 12, 24, 248, &lv_font_simsun_16_cjk, kText);
}

void rebuild(const AppState &state, uint32_t nowMs)
{
    modalFill = nullptr;
    modalCountdown = nullptr;
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
        lv_obj_set_width(modalFill, static_cast<int16_t>(272 * progress / 1000));
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
    lv_obj_set_style_bg_color(root, color(kBg), 0);
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
    renderedRevision = 0;
    return true;
}

void uiRendererRender(const AppState &state, uint32_t nowMs)
{
    if (root == nullptr) return;
    if (state.revision != renderedRevision) {
        if (hasRenderedState && state.page == previousPage && state.focus != previousFocus) {
            const uint8_t count = appFocusCount(state);
            focusMotion = static_cast<uint8_t>((previousFocus + 1) % count) == state.focus ? 1 : -1;
        } else {
            focusMotion = 0;
        }
        rebuild(state, nowMs);
        renderedRevision = state.revision;
        previousPage = state.page;
        previousFocus = state.focus;
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
    lv_obj_clean(root);
    drawOuterRing(kRed);
    drawHeader("设备故障", "GRIDOPOLY");
    label(root, code, 90, 205, 300, &lv_font_montserrat_24, kRed);
}
