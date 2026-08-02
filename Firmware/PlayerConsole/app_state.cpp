#include "app_state.h"

#include "app_config.h"
#include "demo_data.h"

namespace {

void touchRevision(AppState &state)
{
    ++state.revision;
}

void setPage(AppState &state, ScreenPage page, uint8_t focus = 0)
{
    state.page = page;
    state.focus = focus;
    touchRevision(state);
}

void showToast(AppState &state, const char *message, uint32_t nowMs)
{
    state.toast = message;
    state.toastUntilMs = nowMs + 1800;
    touchRevision(state);
}

void showModal(AppState &state, ModalKind kind, const char *title, const char *counterparty,
               const char *purpose, int32_t amount, uint32_t durationMs, uint32_t nowMs)
{
    state.modal.kind = kind;
    state.modal.title = title;
    state.modal.counterparty = counterparty;
    state.modal.purpose = purpose;
    state.modal.amount = amount;
    state.modal.openedMs = nowMs;
    state.modal.deadlineMs = durationMs == 0 ? 0 : nowMs + durationMs;
    state.modal.holdStartMs = 0;
    state.modal.holding = false;
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void dismissModal(AppState &state)
{
    state.modal = ModalState{};
    state.buttonHeld = false;
    state.holdActionConsumed = false;
    touchRevision(state);
}

void back(AppState &state)
{
    if (state.modal.kind != ModalKind::None) {
        dismissModal(state);
        return;
    }
    switch (state.page) {
        case ScreenPage::Home: return;
        case ScreenPage::AssetDetail: setPage(state, ScreenPage::Assets, state.selectedAsset); return;
        case ScreenPage::PlayerDetail: setPage(state, ScreenPage::Players, state.selectedPlayer); return;
        default: setPage(state, ScreenPage::Home, 0); return;
    }
}

void completeModal(AppState &state, uint32_t nowMs)
{
    switch (state.modal.kind) {
        case ModalKind::CollectRent:
            state.money += state.modal.amount;
            dismissModal(state);
            showToast(state, "已确认收租", nowMs);
            return;
        case ModalKind::Payment:
            state.money -= state.modal.amount;
            dismissModal(state);
            showToast(state, "付款已发送", nowMs);
            return;
        case ModalKind::Mortgage:
            state.money += state.modal.amount;
            dismissModal(state);
            showToast(state, "抵押请求已发送", nowMs);
            return;
        case ModalKind::Trade:
            dismissModal(state);
            showToast(state, "交易提案已发送", nowMs);
            return;
        case ModalKind::None: return;
    }
}

void activate(AppState &state, uint32_t nowMs)
{
    if (state.modal.kind != ModalKind::None) return;

    switch (state.page) {
        case ScreenPage::Home:
            if (state.homePhase == HomePhase::MyTurn && state.focus == 0) {
                showToast(state, "骰子结果 3 + 4 = 7", nowMs);
                state.position = static_cast<uint8_t>((state.position + 7) % 28);
                return;
            }
            {
                const uint8_t menuFocus = state.homePhase == HomePhase::MyTurn ? state.focus - 1 : state.focus;
                if (menuFocus == 0) setPage(state, ScreenPage::Assets);
                else if (menuFocus == 1) setPage(state, ScreenPage::Players);
                else if (menuFocus == 2) setPage(state, ScreenPage::Trade);
                else setPage(state, ScreenPage::More);
            }
            return;
        case ScreenPage::Assets:
            state.selectedAsset = state.focus;
            setPage(state, ScreenPage::AssetDetail);
            return;
        case ScreenPage::AssetDetail:
            if (state.focus == 0) {
                const AssetData &asset = kAssets[state.selectedAsset];
                showModal(state, ModalKind::Mortgage, "确认抵押", "城市银行", asset.name,
                          asset.value / 2, 0, nowMs);
            } else back(state);
            return;
        case ScreenPage::Players:
            state.selectedPlayer = state.focus;
            setPage(state, ScreenPage::PlayerDetail);
            return;
        case ScreenPage::PlayerDetail:
            if (state.focus == 0) {
                state.tradeReceiver = state.selectedPlayer;
                setPage(state, ScreenPage::Trade);
            } else back(state);
            return;
        case ScreenPage::Trade:
            if (state.focus == 0) state.tradeReceiver = static_cast<uint8_t>((state.tradeReceiver + 1) % kPlayerCount);
            else if (state.focus == 1) state.tradeAmount = state.tradeAmount >= 500 ? 50 : state.tradeAmount + 50;
            else if (state.focus == 2) {
                showModal(state, ModalKind::Trade, "发送交易", kPlayers[state.tradeReceiver].name,
                          "现金转账", state.tradeAmount, 0, nowMs);
            } else back(state);
            touchRevision(state);
            return;
        case ScreenPage::More:
            if (state.focus == 0) setPage(state, ScreenPage::DemoLab);
            else showToast(state, "Gridopoly Player Console", nowMs);
            return;
        case ScreenPage::DemoLab:
            if (state.focus == 0) { state.homePhase = HomePhase::Waiting; setPage(state, ScreenPage::Home); }
            else if (state.focus == 1) { state.homePhase = HomePhase::NextPlayer; setPage(state, ScreenPage::Home); }
            else if (state.focus == 2) { state.homePhase = HomePhase::MyTurn; setPage(state, ScreenPage::Home); }
            else if (state.focus == 3) showModal(state, ModalKind::CollectRent, "等待收租", "砾川", "霓虹港湾地租", 120, 20000, nowMs);
            else if (state.focus == 4) showModal(state, ModalKind::Payment, "即将付款", "岑蓝", "天穹广场地租", 175, 10000, nowMs);
            else if (state.focus == 5) setPage(state, ScreenPage::Debt);
            else back(state);
            return;
        case ScreenPage::Debt:
            setPage(state, ScreenPage::Assets);
            return;
    }
}

void beginPress(AppState &state, uint32_t nowMs)
{
    state.buttonHeld = true;
    state.buttonDownMs = nowMs;
    state.holdActionConsumed = false;
    if (state.modal.kind != ModalKind::None) {
        state.modal.holding = true;
        state.modal.holdStartMs = nowMs;
    }
}

void endPress(AppState &state, uint32_t nowMs)
{
    if (!state.buttonHeld) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    const bool consumed = state.holdActionConsumed;
    state.buttonHeld = false;
    state.modal.holding = false;
    if (state.modal.kind != ModalKind::None) return;
    if (consumed) return;
    if (heldMs <= kShortPressMaxMs) activate(state, nowMs);
    else if (heldMs >= kBackPressMs && heldMs < kHomePressMs) back(state);
}

} // namespace

void appInit(AppState &state, uint32_t nowMs)
{
    state = AppState{};
    state.toast = "旋转浏览 · 按下选择";
    state.toastUntilMs = nowMs + 2500;
}

uint8_t appFocusCount(const AppState &state)
{
    switch (state.page) {
        case ScreenPage::Home: return state.homePhase == HomePhase::MyTurn ? 5 : 4;
        case ScreenPage::Assets: return kAssetCount;
        case ScreenPage::AssetDetail: return 2;
        case ScreenPage::Players: return kPlayerCount;
        case ScreenPage::PlayerDetail: return 2;
        case ScreenPage::Trade: return 4;
        case ScreenPage::More: return 2;
        case ScreenPage::DemoLab: return 7;
        case ScreenPage::Debt: return 1;
    }
    return 1;
}

void appHandleInput(AppState &state, const InputEvent &event, uint32_t nowMs)
{
    if (event.kind == InputKind::ButtonDown) {
        beginPress(state, nowMs);
        return;
    }
    if (event.kind == InputKind::ButtonUp) {
        endPress(state, nowMs);
        return;
    }
    if (event.kind != InputKind::Rotate || state.modal.kind != ModalKind::None) return;
    const uint8_t count = appFocusCount(state);
    int32_t next = static_cast<int32_t>(state.focus) + event.delta;
    while (next < 0) next += count;
    state.focus = static_cast<uint8_t>(next % count);
    touchRevision(state);
}

void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs)
{
    if (action == TouchAction::PressDown) { beginPress(state, nowMs); return; }
    if (action == TouchAction::PressUp) { endPress(state, nowMs); return; }
    if (state.modal.kind != ModalKind::None) return;

    const uint16_t raw = static_cast<uint16_t>(action);
    if (raw >= 100 && raw <= 103) {
        state.page = ScreenPage::Home;
        state.focus = static_cast<uint8_t>(raw - 100 + (state.homePhase == HomePhase::MyTurn ? 1 : 0));
        activate(state, nowMs);
        return;
    }
    if (raw >= 200 && raw <= 204) { state.page = ScreenPage::Assets; state.focus = raw - 200; activate(state, nowMs); return; }
    if (raw >= 300 && raw <= 304) { state.page = ScreenPage::Players; state.focus = raw - 300; activate(state, nowMs); return; }
    if (raw >= 400 && raw <= 403) { state.page = ScreenPage::Trade; state.focus = raw - 400; activate(state, nowMs); return; }
    if (raw >= 500 && raw <= 501) { state.page = ScreenPage::More; state.focus = raw - 500; activate(state, nowMs); return; }
    if (raw >= 600 && raw <= 606) { state.page = ScreenPage::DemoLab; state.focus = raw - 600; activate(state, nowMs); return; }
    if (raw >= 700 && raw <= 701) { state.focus = raw - 700; activate(state, nowMs); return; }
}

void appTick(AppState &state, uint32_t nowMs)
{
    if (state.toastUntilMs != 0 && nowMs >= state.toastUntilMs) {
        state.toastUntilMs = 0;
        touchRevision(state);
    }

    if (state.modal.kind != ModalKind::None && state.modal.deadlineMs != 0 && nowMs >= state.modal.deadlineMs) {
        const bool autoPay = state.modal.kind == ModalKind::Payment;
        if (autoPay) state.money -= state.modal.amount;
        dismissModal(state);
        showToast(state, autoPay ? "倒计时结束，已自动付款" : "收租超时，已放弃", nowMs);
        return;
    }

    if (!state.buttonHeld || state.holdActionConsumed) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    if (state.modal.kind != ModalKind::None) {
        if (heldMs >= kConfirmHoldMs) {
            state.holdActionConsumed = true;
            completeModal(state, nowMs);
        }
        return;
    }
    if (heldMs >= kHomePressMs) {
        state.holdActionConsumed = true;
        if (state.page == ScreenPage::Home) setPage(state, ScreenPage::DemoLab);
        else setPage(state, ScreenPage::Home);
    }
}

uint16_t appHoldProgressPermille(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None || !state.modal.holding) return 0;
    const uint32_t elapsed = nowMs - state.modal.holdStartMs;
    return static_cast<uint16_t>(elapsed >= kConfirmHoldMs ? 1000 : elapsed * 1000 / kConfirmHoldMs);
}

uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None || state.modal.deadlineMs == 0 || nowMs >= state.modal.deadlineMs) return 0;
    return state.modal.deadlineMs - nowMs;
}
