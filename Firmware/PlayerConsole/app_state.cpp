#include "app_state.h"

#include "app_config.h"
#include "demo_data.h"

namespace {

void touchRevision(AppState &state)
{
    ++state.revision;
}

bool isOrdinaryPage(ScreenPage page)
{
    switch (page) {
        case ScreenPage::Home:
        case ScreenPage::Assets:
        case ScreenPage::AssetDetail:
        case ScreenPage::Players:
        case ScreenPage::PlayerDetail:
        case ScreenPage::Trade:
        case ScreenPage::More:
        case ScreenPage::DemoLab:
        case ScreenPage::Debt:
            return true;
        case ScreenPage::DiceStage:
        case ScreenPage::MoveGuide:
        case ScreenPage::DebtAssets:
        case ScreenPage::Bankruptcy:
            return false;
    }
    return false;
}

uint8_t contentCount(const AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Home: return state.homePhase == HomePhase::MyTurn ? 5 : 4;
        case ScreenPage::Assets: return kAssetCount;
        case ScreenPage::AssetDetail: return 1;
        case ScreenPage::Players: return kPlayerCount;
        case ScreenPage::PlayerDetail: return 1;
        case ScreenPage::Trade: return 3;
        case ScreenPage::More: return 2;
        case ScreenPage::DemoLab: return 6;
        case ScreenPage::Debt: return 0;
        case ScreenPage::DiceStage:
        case ScreenPage::MoveGuide:
        case ScreenPage::DebtAssets:
        case ScreenPage::Bankruptcy:
            return 0;
    }
    return 0;
}

uint8_t &listAnchor(AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Assets: return state.assetListIndex;
        case ScreenPage::Players: return state.playerListIndex;
        case ScreenPage::DemoLab: return state.demoListIndex;
        case ScreenPage::Debt:
        case ScreenPage::DebtAssets: return state.debtListIndex;
        default: return state.nav.current.listAnchor;
    }
}

uint8_t listAnchor(const AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Assets: return state.assetListIndex;
        case ScreenPage::Players: return state.playerListIndex;
        case ScreenPage::DemoLab: return state.demoListIndex;
        case ScreenPage::Debt:
        case ScreenPage::DebtAssets: return state.debtListIndex;
        default: return state.nav.current.listAnchor;
    }
}

void syncLegacyState(AppState &state)
{
    state.page = state.nav.current.page;
    state.focus = state.nav.current.focus;
}

void saveCurrentListAnchor(AppState &state)
{
    state.nav.current.listAnchor = listAnchor(state, state.nav.current.page);
}

void restoreListAnchor(AppState &state)
{
    const uint8_t count = contentCount(state, state.nav.current.page);
    if (count == 0) return;
    uint8_t anchor = state.nav.current.listAnchor;
    if (anchor >= count) anchor = count - 1;
    listAnchor(state, state.nav.current.page) = anchor;
}

void setFocus(AppState &state, uint8_t focus)
{
    state.nav.current.focus = focus;
    const uint8_t count = contentCount(state, state.nav.current.page);
    if (state.nav.current.page != ScreenPage::Home && focus < count) {
        listAnchor(state, state.nav.current.page) = focus;
        state.nav.current.listAnchor = focus;
    }
    syncLegacyState(state);
    touchRevision(state);
}

void pushPage(AppState &state, ScreenPage page, uint8_t focus = 0)
{
    saveCurrentListAnchor(state);
    if (state.nav.depth < sizeof(state.nav.stack) / sizeof(state.nav.stack[0])) {
        state.nav.stack[state.nav.depth++] = state.nav.current;
    }
    state.nav.current = NavigationEntry{page, focus, focus};
    const uint8_t count = contentCount(state, page);
    if (page != ScreenPage::Home && count != 0 && focus < count) {
        listAnchor(state, page) = focus;
    }
    syncLegacyState(state);
    touchRevision(state);
}

void openDemoLab(AppState &state)
{
    if (state.nav.current.page == ScreenPage::More && state.nav.depth != 0) {
        state.nav.current = state.nav.stack[--state.nav.depth];
        restoreListAnchor(state);
        const uint8_t moreFocus = state.homePhase == HomePhase::MyTurn ? 4 : 3;
        state.nav.current.focus = moreFocus;
        syncLegacyState(state);
    }
    pushPage(state, ScreenPage::DemoLab);
}

void replaceForcedPage(AppState &state, ScreenPage page)
{
    state.nav.current = NavigationEntry{page, 0, 0};
    syncLegacyState(state);
    touchRevision(state);
}

void resetHome(AppState &state, uint8_t focus = 0)
{
    state.nav = NavigationState{};
    state.nav.current = NavigationEntry{ScreenPage::Home, focus, 0};
    syncLegacyState(state);
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
    if (!appCanNavigateBack(state)) return;
    state.nav.current = state.nav.stack[--state.nav.depth];
    restoreListAnchor(state);
    syncLegacyState(state);
    touchRevision(state);
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
        case ModalKind::None:
            return;
    }
}

void activate(AppState &state, uint32_t nowMs)
{
    if (state.modal.kind != ModalKind::None) return;

    const ScreenPage page = state.nav.current.page;
    const uint8_t focus = state.nav.current.focus;
    if (appFocusIsFooter(state)) {
        back(state);
        return;
    }

    switch (page) {
        case ScreenPage::Home: {
            if (state.homePhase == HomePhase::MyTurn && focus == 0) {
                showToast(state, "骰子结果 3 + 4 = 7", nowMs);
                state.position = static_cast<uint8_t>((state.position + 7) % 28);
                return;
            }
            const uint8_t menuFocus = state.homePhase == HomePhase::MyTurn ? focus - 1 : focus;
            if (menuFocus == 0) pushPage(state, ScreenPage::Assets);
            else if (menuFocus == 1) pushPage(state, ScreenPage::Players);
            else if (menuFocus == 2) pushPage(state, ScreenPage::Trade);
            else pushPage(state, ScreenPage::More);
            return;
        }
        case ScreenPage::Assets:
            state.selectedAsset = focus;
            pushPage(state, ScreenPage::AssetDetail);
            return;
        case ScreenPage::AssetDetail:
            showModal(state, ModalKind::Mortgage, "确认抵押", "城市银行",
                      kAssets[state.selectedAsset].name, kAssets[state.selectedAsset].value / 2, 0, nowMs);
            return;
        case ScreenPage::Players:
            state.selectedPlayer = focus;
            pushPage(state, ScreenPage::PlayerDetail);
            return;
        case ScreenPage::PlayerDetail:
            state.tradeReceiver = state.selectedPlayer;
            pushPage(state, ScreenPage::Trade);
            return;
        case ScreenPage::Trade:
            if (focus == 0) state.tradeReceiver = static_cast<uint8_t>((state.tradeReceiver + 1) % kPlayerCount);
            else if (focus == 1) state.tradeAmount = state.tradeAmount >= 500 ? 50 : state.tradeAmount + 50;
            else showModal(state, ModalKind::Trade, "发送交易", kPlayers[state.tradeReceiver].name,
                           "现金转账", state.tradeAmount, 0, nowMs);
            touchRevision(state);
            return;
        case ScreenPage::More:
            if (focus == 0) openDemoLab(state);
            else showToast(state, "Gridopoly Player Console", nowMs);
            return;
        case ScreenPage::DemoLab:
            if (focus == 0) { state.homePhase = HomePhase::Waiting; resetHome(state, 3); }
            else if (focus == 1) { state.homePhase = HomePhase::NextPlayer; resetHome(state, 3); }
            else if (focus == 2) { state.homePhase = HomePhase::MyTurn; resetHome(state, 4); }
            else if (focus == 3) showModal(state, ModalKind::CollectRent, "等待收租", "砾川", "霓虹港湾地租", 120, 20000, nowMs);
            else if (focus == 4) showModal(state, ModalKind::Payment, "即将付款", "岑蓝", "天穹广场地租", 175, 10000, nowMs);
            else replaceForcedPage(state, ScreenPage::DebtAssets);
            return;
        case ScreenPage::Debt:
            pushPage(state, ScreenPage::Assets);
            return;
        case ScreenPage::DiceStage:
        case ScreenPage::MoveGuide:
        case ScreenPage::DebtAssets:
        case ScreenPage::Bankruptcy:
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
    if (state.modal.kind != ModalKind::None || consumed) return;
    if (heldMs <= kShortPressMaxMs) activate(state, nowMs);
    else if (heldMs >= kBackPressMs && heldMs < kHomePressMs && isOrdinaryPage(state.nav.current.page)) back(state);
}

void moveFocus(AppState &state, int16_t delta)
{
    const uint8_t count = appFocusCount(state);
    if (count == 0 || delta == 0 || !isOrdinaryPage(state.nav.current.page)) return;

    int32_t next = static_cast<int32_t>(state.nav.current.focus) + delta;
    if (state.nav.current.page == ScreenPage::Home) {
        while (next < 0) next += count;
        next %= count;
    } else {
        if (next < 0) next = 0;
        if (next >= count) next = count - 1;
    }
    setFocus(state, static_cast<uint8_t>(next));
}

void selectListItem(AppState &state, int16_t item, uint32_t nowMs)
{
    if (!isOrdinaryPage(state.nav.current.page) || state.nav.current.page == ScreenPage::Home) return;
    const uint8_t content = appPageContentCount(state);
    if (item < 0 || item >= content) return;
    if (state.nav.current.focus == static_cast<uint8_t>(item)) activate(state, nowMs);
    else setFocus(state, static_cast<uint8_t>(item));
}

} // namespace

void appInit(AppState &state, uint32_t nowMs)
{
    state = AppState{};
    state.toast = "旋转浏览 · 按下选择";
    state.toastUntilMs = nowMs + 2500;
    syncLegacyState(state);
}

bool appCanNavigateBack(const AppState &state)
{
    return isOrdinaryPage(state.nav.current.page) && state.nav.depth != 0;
}

uint8_t appPageContentCount(const AppState &state)
{
    return contentCount(state, state.nav.current.page);
}

uint8_t appFocusCount(const AppState &state)
{
    const uint8_t content = appPageContentCount(state);
    if (state.nav.current.page == ScreenPage::Home) return content;
    return isOrdinaryPage(state.nav.current.page) ? content + 1 : 1;
}

bool appFocusIsFooter(const AppState &state)
{
    return state.nav.current.page != ScreenPage::Home &&
           isOrdinaryPage(state.nav.current.page) &&
           state.nav.current.focus == appPageContentCount(state);
}

void appHandleUiEvent(AppState &state, const UiEvent &event, uint32_t nowMs)
{
    syncLegacyState(state);
    switch (event.kind) {
        case UiEventKind::ActivateFocused:
            activate(state, nowMs);
            return;
        case UiEventKind::SelectHomeAction:
            if (state.nav.current.page != ScreenPage::Home) return;
            if (event.value < 0 || event.value >= 4) return;
            setFocus(state, static_cast<uint8_t>(event.value +
                                                  (state.homePhase == HomePhase::MyTurn ? 1 : 0)));
            activate(state, nowMs);
            return;
        case UiEventKind::SelectListItem:
            selectListItem(state, event.value, nowMs);
            return;
        case UiEventKind::Back:
            back(state);
            return;
        case UiEventKind::HoldDown:
            beginPress(state, nowMs);
            return;
        case UiEventKind::HoldUp:
            endPress(state, nowMs);
            return;
        case UiEventKind::ListPrevious:
            moveFocus(state, -1);
            return;
        case UiEventKind::ListNext:
            moveFocus(state, 1);
            return;
    }
}

void appHandleInput(AppState &state, const InputEvent &event, uint32_t nowMs)
{
    syncLegacyState(state);
    if (event.kind == InputKind::ButtonDown) {
        beginPress(state, nowMs);
        return;
    }
    if (event.kind == InputKind::ButtonUp) {
        endPress(state, nowMs);
        return;
    }
    if (event.kind != InputKind::Rotate || state.modal.kind != ModalKind::None) return;
    moveFocus(state, event.delta);
}

void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs)
{
    if (action == TouchAction::PressDown) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldDown, 0}, nowMs); return; }
    if (action == TouchAction::PressUp) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldUp, 0}, nowMs); return; }
    if (state.modal.kind != ModalKind::None) return;

    const uint16_t raw = static_cast<uint16_t>(action);
    if (raw >= 100 && raw <= 103) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, static_cast<int16_t>(raw - 100)}, nowMs); return; }
    if (raw >= 200 && raw <= 204) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 200)}, nowMs); return; }
    if (raw >= 300 && raw <= 304) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 300)}, nowMs); return; }
    if (raw >= 400 && raw <= 403) {
        if (action == TouchAction::TradeBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        else appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 400)}, nowMs);
        return;
    }
    if (raw >= 500 && raw <= 501) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 500)}, nowMs); return; }
    if (raw >= 600 && raw <= 606) {
        if (action == TouchAction::DemoBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        else appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 600)}, nowMs);
        return;
    }
    if (action == TouchAction::DetailPrimary) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 0}, nowMs); return; }
    if (action == TouchAction::DetailBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
}

void appTick(AppState &state, uint32_t nowMs)
{
    syncLegacyState(state);
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
    if (heldMs < kHomePressMs || !isOrdinaryPage(state.nav.current.page)) return;

    state.holdActionConsumed = true;
    if (state.nav.current.page == ScreenPage::Home) {
        const uint8_t moreFocus = state.homePhase == HomePhase::MyTurn ? 4 : 3;
        setFocus(state, moreFocus);
        openDemoLab(state);
    } else {
        resetHome(state);
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
