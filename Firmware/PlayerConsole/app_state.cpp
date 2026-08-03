#include "app_state.h"

#include "app_config.h"
#include "demo_data.h"

#include <limits.h>

namespace {

void touchRevision(AppState &state)
{
    ++state.revision;
}

bool hasReachedDeadline(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
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

bool isDebtAssetScreen(ScreenPage page)
{
    return page == ScreenPage::DebtAssets;
}

bool isInteractiveListPage(ScreenPage page)
{
    return isOrdinaryPage(page) || isDebtAssetScreen(page);
}

bool clearInlineEditing(AppState &state)
{
    if (state.inlineEditField == InlineEditField::None) return false;
    state.inlineEditField = InlineEditField::None;
    return true;
}

InlineEditField focusedTradeField(const AppState &state)
{
    if (state.nav.current.page != ScreenPage::Trade) return InlineEditField::None;
    if (appTradeReceiverLocked(state)) {
        return state.nav.current.focus == 0 ? InlineEditField::TradeAmount
                                            : InlineEditField::None;
    }
    if (state.nav.current.focus == 0) return InlineEditField::TradeReceiver;
    if (state.nav.current.focus == 1) return InlineEditField::TradeAmount;
    return InlineEditField::None;
}

uint8_t tradeSubmitFocus(const AppState &state)
{
    return appTradeReceiverLocked(state) ? 1 : 2;
}

uint8_t contentCount(const AppState &state, ScreenPage page)
{
    switch (page) {
        case ScreenPage::Home: return state.homePhase == HomePhase::MyTurn ? 5 : 4;
        case ScreenPage::Assets: return kAssetCount;
        case ScreenPage::AssetDetail: return 1;
        case ScreenPage::Players: return kPlayerCount;
        case ScreenPage::PlayerDetail: return 1;
        case ScreenPage::Trade: return appTradeReceiverLocked(state) ? 2 : 3;
        case ScreenPage::More: return 2;
        case ScreenPage::DemoLab: return 6;
        case ScreenPage::Debt: return 1;
        case ScreenPage::DiceStage:
        case ScreenPage::MoveGuide:
            return 0;
        case ScreenPage::DebtAssets: return kAssetCount;
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

void pulseListBoundary(AppState &state, int16_t delta)
{
    state.boundaryPulseDirection = delta < 0 ? -1 : 1;
    ++state.boundaryPulseRevision;
    touchRevision(state);
}

bool pushPage(AppState &state, ScreenPage page, uint8_t focus = 0)
{
    if (state.nav.depth >= sizeof(state.nav.stack) / sizeof(state.nav.stack[0])) return false;
    clearInlineEditing(state);
    saveCurrentListAnchor(state);
    state.nav.stack[state.nav.depth++] = state.nav.current;
    state.nav.current = NavigationEntry{page, focus, focus};
    const uint8_t count = contentCount(state, page);
    if (page != ScreenPage::Home && count != 0 && focus < count) {
        listAnchor(state, page) = focus;
    }
    syncLegacyState(state);
    touchRevision(state);
    return true;
}

void openDemoLab(AppState &state)
{
    if (state.nav.depth >= sizeof(state.nav.stack) / sizeof(state.nav.stack[0])) return;
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
    clearInlineEditing(state);
    state.nav.current = NavigationEntry{page, 0, 0};
    syncLegacyState(state);
    touchRevision(state);
}

void resetHome(AppState &state, uint8_t focus = 0)
{
    clearInlineEditing(state);
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

uint32_t legalDebtAssetMask(const AppState &state)
{
    uint32_t mask = state.debt.eligibleMask;
    for (uint8_t index = 0; index < kAssetCount; ++index) {
        const AssetData &asset = kAssets[index];
        if (asset.mortgaged || asset.buildingLevel != 0 || asset.ruleLocked) {
            mask &= ~(static_cast<uint32_t>(1u) << index);
        }
    }
    return mask;
}

int64_t debtEligibleProceeds(const AppState &state)
{
    int64_t proceeds = 0;
    const uint32_t mask = legalDebtAssetMask(state);
    for (uint8_t index = 0; index < kAssetCount; ++index) {
        if ((mask & (static_cast<uint32_t>(1u) << index)) != 0) {
            proceeds += kAssets[index].mortgageValue;
        }
    }
    return proceeds;
}

int32_t saturateInt32(int64_t value)
{
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(value);
}

uint8_t commandIndex(TransportCommandKind kind)
{
    return static_cast<uint8_t>(kind);
}

uint8_t commandMask(TransportCommandKind kind)
{
    return static_cast<uint8_t>(1u << commandIndex(kind));
}

bool queueCommand(AppState &state, TransportCommandKind kind, uint32_t transactionId, uint32_t nowMs,
                  uint32_t assetMask = 0, uint8_t targetPosition = 0)
{
    const uint8_t index = commandIndex(kind);
    const uint8_t mask = commandMask(kind);
    if (!state.authorityOnline || state.commandCount == 4 || (state.pendingCommandMask & mask) != 0) return false;

    TransportCommand &command = state.commandQueue[state.commandTail];
    command = TransportCommand{};
    command.kind = kind;
    command.requestId = state.nextRequestId++;
    command.stateVersion = state.stateVersion;
    command.transactionId = transactionId;
    command.clientTimeMs = nowMs;
    command.assetMask = assetMask;
    command.targetPosition = targetPosition;
    state.commandTail = static_cast<uint8_t>((state.commandTail + 1) % 4);
    ++state.commandCount;
    state.pendingCommandMask |= mask;
    state.pendingRequestIds[index] = command.requestId;
    return true;
}

void clearPendingRequest(AppState &state, uint32_t requestId)
{
    if (requestId == 0) return;
    for (uint8_t index = 0; index < 6; ++index) {
        if (state.pendingRequestIds[index] != requestId) continue;
        state.pendingRequestIds[index] = 0;
        state.pendingCommandMask &= static_cast<uint8_t>(~(1u << index));
        return;
    }
}

void clearPendingKind(AppState &state, TransportCommandKind kind)
{
    const uint8_t index = commandIndex(kind);
    state.pendingRequestIds[index] = 0;
    state.pendingCommandMask &= static_cast<uint8_t>(~commandMask(kind));
}

void clearStaleSubmissions(AppState &state)
{
    state.pendingCommandMask = 0;
    for (uint32_t &requestId : state.pendingRequestIds) requestId = 0;
    state.modal.submitting = false;
}

bool isDebtMortgageCompletion(const AppState &state, const TransportEvent &event)
{
    const uint8_t mortgageIndex = commandIndex(TransportCommandKind::MortgageBatchRequest);
    return state.modal.kind == ModalKind::DebtMortgageConfirm && state.modal.submitting &&
           event.requestId != 0 && event.requestId == state.debt.submittedMortgageRequestId &&
           event.requestId == state.pendingRequestIds[mortgageIndex] &&
           event.transactionId == state.debt.transactionId &&
           event.assetMask == state.debt.submittedMortgageMask;
}

bool isVoluntaryMortgageCompletion(const AppState &state, const TransportEvent &event)
{
    const uint8_t mortgageIndex = commandIndex(TransportCommandKind::MortgageBatchRequest);
    return state.modal.kind == ModalKind::VoluntaryMortgage && state.modal.submitting &&
           event.requestId != 0 && event.requestId == state.pendingRequestIds[mortgageIndex] &&
           (state.modal.transactionId == 0 || event.transactionId == state.modal.transactionId) &&
           event.assetMask == (static_cast<uint32_t>(1u) << state.selectedAsset);
}

bool isGenericModalCompletion(const AppState &state, const TransportEvent &event)
{
    if (event.kind != TransportEventKind::CommandCompleted ||
        event.requestId == 0 || !state.modal.submitting) {
        return false;
    }
    if (state.modal.kind == ModalKind::CollectRent) {
        return event.requestId ==
            state.pendingRequestIds[commandIndex(TransportCommandKind::ClaimRent)];
    }
    if (state.modal.kind == ModalKind::TradeCreate) {
        return event.requestId ==
            state.pendingRequestIds[commandIndex(TransportCommandKind::TradeCreate)];
    }
    return false;
}

bool isActiveModalRequest(const AppState &state, uint32_t requestId)
{
    if (requestId == 0 || !state.modal.submitting) return false;
    switch (state.modal.kind) {
        case ModalKind::CollectRent:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::ClaimRent)];
        case ModalKind::TradeCreate:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::TradeCreate)];
        case ModalKind::ForcedPayment:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)];
        case ModalKind::VoluntaryMortgage:
            return requestId ==
                state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
        case ModalKind::DebtMortgageConfirm:
            return requestId == state.debt.submittedMortgageRequestId &&
                   requestId == state.pendingRequestIds[
                       commandIndex(TransportCommandKind::MortgageBatchRequest)];
        case ModalKind::None:
            return false;
    }
    return false;
}

bool isForcedPaymentCompletion(const AppState &state, const TransportEvent &event)
{
    return event.kind == TransportEventKind::PaymentCompleted &&
           state.modal.kind == ModalKind::ForcedPayment && state.modal.submitting &&
           event.requestId != 0 &&
           event.requestId ==
               state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)] &&
           (state.modal.transactionId == 0 ||
            event.transactionId == state.modal.transactionId);
}

bool isMatchingBankruptcyResolution(const AppState &state, const TransportEvent &event)
{
    return event.kind == TransportEventKind::BankruptcyResolved && state.debt.bankruptcyPending &&
           state.debt.transactionId != 0 &&
           event.transactionId == state.debt.transactionId;
}

void showModal(AppState &state, ModalKind kind, bool cancelAllowed, const char *title,
               const char *counterparty, const char *purpose, int32_t amount, uint32_t transactionId,
               uint32_t deadlineMs)
{
    state.modal = ModalState{};
    state.modal.kind = kind;
    state.modal.focus = ModalFocus::Confirm;
    state.modal.cancelAllowed = cancelAllowed;
    state.modal.transactionId = transactionId;
    state.modal.deadlineMs = deadlineMs;
    state.modal.amount = amount;
    state.modal.title = title;
    state.modal.counterparty = counterparty;
    state.modal.purpose = purpose;
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

void enterBankruptcy(AppState &state)
{
    dismissModal(state);
    replaceForcedPage(state, ScreenPage::Bankruptcy);
}

void back(AppState &state)
{
    if (state.modal.kind != ModalKind::None) {
        if (state.modal.cancelAllowed && !state.modal.submitting &&
            state.modal.focus == ModalFocus::Cancel) {
            dismissModal(state);
        }
        return;
    }
    if (!appCanNavigateBack(state)) return;
    clearInlineEditing(state);
    state.nav.current = state.nav.stack[--state.nav.depth];
    restoreListAnchor(state);
    syncLegacyState(state);
    touchRevision(state);
}

void submitModal(AppState &state, uint32_t nowMs)
{
    if (state.modal.submitting) return;
    switch (state.modal.kind) {
        case ModalKind::CollectRent:
            if (queueCommand(state, TransportCommandKind::ClaimRent, state.modal.transactionId, nowMs)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::ForcedPayment:
            if (state.modal.focus == ModalFocus::ResolveAssets) {
                showModal(state, ModalKind::DebtMortgageConfirm, false, "Resolve assets", "City Bank",
                          "Mortgage assets", state.modal.amount, state.modal.transactionId, 0);
            } else if (queueCommand(state, TransportCommandKind::PayNow, state.modal.transactionId, nowMs)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::VoluntaryMortgage:
            if (queueCommand(state, TransportCommandKind::MortgageBatchRequest, state.modal.transactionId, nowMs,
                             static_cast<uint32_t>(1u << state.selectedAsset))) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::TradeCreate:
            if (queueCommand(state, TransportCommandKind::TradeCreate, state.modal.transactionId, nowMs)) {
                state.modal.submitting = true;
            }
            return;
        case ModalKind::DebtMortgageConfirm:
            if (appDebtCanConfirm(state) &&
                queueCommand(state, TransportCommandKind::MortgageBatchRequest, state.debt.transactionId, nowMs,
                             state.debt.selectedMask)) {
                state.modal.submitting = true;
                state.debt.submittedMortgageRequestId =
                    state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
                state.debt.submittedMortgageMask = state.debt.selectedMask;
            }
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
        if (page == ScreenPage::DebtAssets && appDebtCanConfirm(state)) {
            showModal(state, ModalKind::DebtMortgageConfirm, false, "Confirm mortgages", "City Bank",
                      "Mortgage selected assets", appDebtSelectedProceeds(state), state.debt.transactionId, 0);
            return;
        }
        back(state);
        return;
    }

    switch (page) {
        case ScreenPage::Home: {
            if (state.homePhase == HomePhase::MyTurn && focus == 0) {
                if (queueCommand(state, TransportCommandKind::RollRequest, 0, nowMs)) {
                    showToast(state, "骰子请求已发送", nowMs);
                }
                return;
            }
            const uint8_t menuFocus = state.homePhase == HomePhase::MyTurn ? focus - 1 : focus;
            if (menuFocus == 0) pushPage(state, ScreenPage::Assets);
            else if (menuFocus == 1) pushPage(state, ScreenPage::Players);
            else if (menuFocus == 2) {
                state.tradeEntryMode = TradeEntryMode::HomeEditable;
                pushPage(state, ScreenPage::Trade);
            }
            else pushPage(state, ScreenPage::More);
            return;
        }
        case ScreenPage::Assets:
            state.selectedAsset = focus;
            pushPage(state, ScreenPage::AssetDetail);
            return;
        case ScreenPage::AssetDetail:
            showModal(state, ModalKind::VoluntaryMortgage, true, "确认抵押", "城市银行",
                      kAssets[state.selectedAsset].name, kAssets[state.selectedAsset].value / 2, 0, 0);
            return;
        case ScreenPage::Players:
            state.selectedPlayer = focus;
            pushPage(state, ScreenPage::PlayerDetail);
            return;
        case ScreenPage::PlayerDetail:
            state.tradeReceiver = state.selectedPlayer;
            state.tradeEntryMode = TradeEntryMode::PlayerLocked;
            pushPage(state, ScreenPage::Trade);
            return;
        case ScreenPage::Trade:
            {
                const InlineEditField field = focusedTradeField(state);
                if (field != InlineEditField::None) {
                    state.inlineEditField = state.inlineEditField == field
                                                ? InlineEditField::None
                                                : field;
                    touchRevision(state);
                } else if (focus == tradeSubmitFocus(state)) {
                    showModal(state, ModalKind::TradeCreate, true, "发送交易",
                              kPlayers[state.tradeReceiver].name,
                              "现金转账", state.tradeAmount, 0, 0);
                }
            }
            return;
        case ScreenPage::More:
            if (focus == 0) openDemoLab(state);
            else showToast(state, "Gridopoly Player Console", nowMs);
            return;
        case ScreenPage::DemoLab:
            if (focus == 0) { state.homePhase = HomePhase::Waiting; state.turnsUntilYou = 3; resetHome(state, 3); }
            else if (focus == 1) { state.homePhase = HomePhase::NextPlayer; state.turnsUntilYou = 1; resetHome(state, 3); }
            else if (focus == 2) { state.homePhase = HomePhase::MyTurn; state.turnsUntilYou = 0; resetHome(state, 4); }
            else if (focus == 3) showModal(state, ModalKind::CollectRent, true, "等待收租", "砾川",
                                            "霓虹港湾地租", 120, 0, nowMs + 20000);
            else if (focus == 4) showModal(state, ModalKind::ForcedPayment, false, "即将付款", "岑蓝",
                                            "天穹广场地租", 175, 0, nowMs + 10000);
            else replaceForcedPage(state, ScreenPage::DebtAssets);
            return;
        case ScreenPage::Debt:
            pushPage(state, ScreenPage::Assets);
            return;
        case ScreenPage::DebtAssets:
            if (appDebtAssetEligible(state, focus)) {
                state.debt.selectedMask ^= static_cast<uint32_t>(1u) << focus;
                touchRevision(state);
            }
            return;
        case ScreenPage::DiceStage:
        case ScreenPage::MoveGuide:
        case ScreenPage::Bankruptcy:
            return;
    }
}

void beginPress(AppState &state, uint32_t nowMs)
{
    state.buttonHeld = true;
    state.buttonDownMs = nowMs;
    state.holdActionConsumed = false;
    if (state.modal.kind != ModalKind::None && !state.modal.submitting &&
        (state.modal.focus == ModalFocus::Confirm ||
         state.modal.focus == ModalFocus::ResolveAssets)) {
        state.modal.holding = true;
        state.modal.holdStartMs = nowMs;
    }
}

void endPress(AppState &state, uint32_t nowMs)
{
    if (!state.buttonHeld) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    bool consumed = state.holdActionConsumed;
    state.buttonHeld = false;
    if (state.modal.kind != ModalKind::None) {
        const bool canSubmit = state.modal.holding && !state.modal.submitting &&
            (state.modal.focus == ModalFocus::Confirm ||
             state.modal.focus == ModalFocus::ResolveAssets);
        if (!consumed && canSubmit && heldMs >= kConfirmHoldMs) {
            state.holdActionConsumed = true;
            consumed = true;
            submitModal(state, nowMs);
        }
        state.modal.holding = false;
        if (!consumed && heldMs <= kShortPressMaxMs && state.modal.cancelAllowed &&
            state.modal.focus == ModalFocus::Cancel) {
            dismissModal(state);
        }
        return;
    }
    if (consumed) return;
    if (heldMs <= kShortPressMaxMs) activate(state, nowMs);
    else if (heldMs >= kBackPressMs && heldMs < kHomePressMs && isOrdinaryPage(state.nav.current.page)) back(state);
}

void moveFocus(AppState &state, int16_t delta)
{
    if (state.modal.kind != ModalKind::None) {
        if (delta != 0 && state.modal.cancelAllowed && !state.modal.submitting) {
            state.modal.focus = state.modal.focus == ModalFocus::Confirm ? ModalFocus::Cancel : ModalFocus::Confirm;
            touchRevision(state);
        }
        return;
    }
    if (state.nav.current.page == ScreenPage::Trade &&
        state.inlineEditField != InlineEditField::None) {
        if (delta == 0) return;
        if (state.inlineEditField == InlineEditField::TradeReceiver) {
            if (appTradeReceiverLocked(state)) return;
            int32_t receiver = static_cast<int32_t>(state.tradeReceiver) + delta;
            receiver %= kPlayerCount;
            if (receiver < 0) receiver += kPlayerCount;
            state.tradeReceiver = static_cast<uint8_t>(receiver);
        } else if (state.inlineEditField == InlineEditField::TradeAmount) {
            int64_t amount = static_cast<int64_t>(state.tradeAmount) +
                             static_cast<int64_t>(delta) * 50;
            if (amount < 50) amount = 50;
            if (amount > 500) amount = 500;
            state.tradeAmount = static_cast<int32_t>(amount);
        }
        touchRevision(state);
        return;
    }
    const uint8_t count = appFocusCount(state);
    if (count == 0 || delta == 0 || !isInteractiveListPage(state.nav.current.page)) return;

    const int32_t requested = static_cast<int32_t>(state.nav.current.focus) + delta;
    int32_t next = requested;
    if (state.nav.current.page == ScreenPage::Home) {
        while (next < 0) next += count;
        next %= count;
    } else {
        if (next < 0) next = 0;
        if (next >= count) next = count - 1;
        if (next == state.nav.current.focus && next != requested) {
            pulseListBoundary(state, delta);
            return;
        }
    }
    setFocus(state, static_cast<uint8_t>(next));
}

void selectListItem(AppState &state, int16_t item, uint32_t nowMs)
{
    if (!isInteractiveListPage(state.nav.current.page) || state.nav.current.page == ScreenPage::Home) return;
    const uint8_t content = appPageContentCount(state);
    if (item < 0 || item >= content) return;
    if (state.nav.current.page == ScreenPage::Trade &&
        state.inlineEditField != InlineEditField::None &&
        state.nav.current.focus != static_cast<uint8_t>(item)) {
        clearInlineEditing(state);
        setFocus(state, static_cast<uint8_t>(item));
        return;
    }
    if (state.nav.current.focus == static_cast<uint8_t>(item)) activate(state, nowMs);
    else setFocus(state, static_cast<uint8_t>(item));
}

void selectFooter(AppState &state, uint32_t nowMs)
{
    if (state.nav.current.page == ScreenPage::Home ||
        !isInteractiveListPage(state.nav.current.page)) return;
    const uint8_t footerFocus = contentCount(state, state.nav.current.page);
    if (state.nav.current.focus != footerFocus) {
        clearInlineEditing(state);
        setFocus(state, footerFocus);
        return;
    }
    activate(state, nowMs);
}

} // namespace

void appInit(AppState &state, uint32_t nowMs)
{
    state = AppState{};
    state.toast = "旋转浏览 · 按下选择";
    state.toastUntilMs = nowMs + 1500;
    syncLegacyState(state);
}

bool appCanNavigateBack(const AppState &state)
{
    return isOrdinaryPage(state.nav.current.page) && state.nav.depth != 0;
}

bool appDebtAssetEligible(const AppState &state, uint8_t assetIndex)
{
    if (assetIndex >= kAssetCount) return false;
    return (legalDebtAssetMask(state) & (static_cast<uint32_t>(1u) << assetIndex)) != 0;
}

bool appDebtAssetSelected(const AppState &state, uint8_t assetIndex)
{
    return appDebtAssetEligible(state, assetIndex) &&
           (state.debt.selectedMask & (static_cast<uint32_t>(1u) << assetIndex)) != 0;
}

int32_t appDebtShortfall(const AppState &state)
{
    const int64_t difference = static_cast<int64_t>(state.debt.amountDue) -
                               static_cast<int64_t>(state.debt.cashBefore);
    return difference <= 0 ? 0 : saturateInt32(difference);
}

int32_t appDebtSelectedProceeds(const AppState &state)
{
    int64_t proceeds = 0;
    const uint32_t mask = state.debt.selectedMask & legalDebtAssetMask(state);
    for (uint8_t index = 0; index < kAssetCount; ++index) {
        if ((mask & (static_cast<uint32_t>(1u) << index)) != 0) {
            proceeds += kAssets[index].mortgageValue;
        }
    }
    return saturateInt32(proceeds);
}

int32_t appDebtPostMortgageBalance(const AppState &state)
{
    const int64_t balance = static_cast<int64_t>(state.debt.cashBefore) +
                            static_cast<int64_t>(appDebtSelectedProceeds(state));
    return saturateInt32(balance);
}

bool appDebtCanConfirm(const AppState &state)
{
    return appDebtSelectedProceeds(state) >= appDebtShortfall(state);
}

uint8_t appPageContentCount(const AppState &state)
{
    return contentCount(state, state.nav.current.page);
}

uint8_t appFocusCount(const AppState &state)
{
    const uint8_t content = appPageContentCount(state);
    if (state.nav.current.page == ScreenPage::Home) return content;
    return isInteractiveListPage(state.nav.current.page) ? content + 1 : 1;
}

bool appFocusIsFooter(const AppState &state)
{
    return state.nav.current.page != ScreenPage::Home &&
           isInteractiveListPage(state.nav.current.page) &&
           state.nav.current.focus == appPageContentCount(state);
}

bool appInlineFieldEditing(const AppState &state, InlineEditField field)
{
    return field != InlineEditField::None && state.inlineEditField == field;
}

bool appTradeReceiverLocked(const AppState &state)
{
    return state.tradeEntryMode == TradeEntryMode::PlayerLocked;
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
            if (event.value < 0 || event.value >= contentCount(state, ScreenPage::Home)) return;
            {
                const uint8_t actionFocus = static_cast<uint8_t>(event.value);
                if (state.nav.current.focus != actionFocus) {
                    setFocus(state, actionFocus);
                    return;
                }
            }
            activate(state, nowMs);
            return;
        case UiEventKind::SelectListItem:
            selectListItem(state, event.value, nowMs);
            return;
        case UiEventKind::SelectFooter:
            selectFooter(state, nowMs);
            return;
        case UiEventKind::Back:
            if (state.modal.kind != ModalKind::None && state.modal.cancelAllowed &&
                !state.modal.submitting) {
                dismissModal(state);
                return;
            }
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
        beginPress(state, event.timestampMs);
        return;
    }
    if (event.kind == InputKind::ButtonUp) {
        endPress(state, event.timestampMs);
        return;
    }
    if (event.kind != InputKind::Rotate) return;
    moveFocus(state, event.delta);
}

bool appPollCommand(AppState &state, TransportCommand &command)
{
    if (state.commandCount == 0) return false;
    command = state.commandQueue[state.commandHead];
    state.commandHead = static_cast<uint8_t>((state.commandHead + 1) % 4);
    --state.commandCount;
    return true;
}

void appHandleTransportEvent(AppState &state, const TransportEvent &event, uint32_t nowMs)
{
    const bool debtMortgageCompletion = isDebtMortgageCompletion(state, event);
    const bool voluntaryMortgageCompletion = isVoluntaryMortgageCompletion(state, event);
    const bool genericModalCompletion = isGenericModalCompletion(state, event);
    const bool forcedPaymentCompletion = isForcedPaymentCompletion(state, event);
    const bool matchingBankruptcyResolution = isMatchingBankruptcyResolution(state, event);
    const bool matchingActiveModalRequest = isActiveModalRequest(state, event.requestId);
    if (state.debt.bankruptcyResolved) return;
    if (state.debt.bankruptcyPending && !matchingBankruptcyResolution) return;
    if (event.kind == TransportEventKind::MortgageBatchCompleted &&
        !debtMortgageCompletion && !voluntaryMortgageCompletion) return;
    if (event.kind == TransportEventKind::CommandCompleted && !genericModalCompletion) return;
    if (event.kind == TransportEventKind::PaymentCompleted && !forcedPaymentCompletion) return;
    if (event.kind == TransportEventKind::BankruptcyResolved && !matchingBankruptcyResolution) return;
    if (event.stateVersion != 0 && event.stateVersion < state.stateVersion) return;
    if (event.stateVersion > state.stateVersion) state.stateVersion = event.stateVersion;
    if (event.kind != TransportEventKind::None && clearInlineEditing(state)) {
        touchRevision(state);
    }

    const bool matchesPayNow = event.requestId != 0 &&
                              event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::PayNow)];
    const bool matchesMortgageRequest = event.requestId != 0 &&
        event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::MortgageBatchRequest)];
    const bool matchesAnyPendingRequest = event.requestId != 0 &&
        (matchesPayNow || matchesMortgageRequest ||
         event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::RollRequest)] ||
         event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::ClaimRent)] ||
         event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::TradeCreate)] ||
         event.requestId == state.pendingRequestIds[commandIndex(TransportCommandKind::MoveManualConfirmRequest)]);
    if (event.kind == TransportEventKind::CommandRejected && !matchesAnyPendingRequest) return;

    switch (event.kind) {
        case TransportEventKind::None:
            return;
        case TransportEventKind::ConnectionLost:
            state.authorityOnline = false;
            touchRevision(state);
            return;
        case TransportEventKind::StateSnapshotApplied:
            state.money = event.cash;
            state.position = event.playerPosition;
            state.commandHead = 0;
            state.commandTail = 0;
            state.commandCount = 0;
            clearStaleSubmissions(state);
            dismissModal(state);
            state.authorityOnline = true;
            touchRevision(state);
            return;
        case TransportEventKind::RollResult:
            clearPendingRequest(state, event.requestId);
            if (event.playerPosition != 0) state.position = event.playerPosition;
            touchRevision(state);
            return;
        case TransportEventKind::MoveGuidanceStarted:
            return;
        case TransportEventKind::RfidPositionConfirmed:
            clearPendingRequest(state, event.requestId);
            state.position = event.observedPosition != 0 ? event.observedPosition : event.targetPosition;
            touchRevision(state);
            return;
        case TransportEventKind::RfidPositionRejected:
            clearPendingRequest(state, event.requestId);
            return;
        case TransportEventKind::PaymentRequired:
            showModal(state, ModalKind::ForcedPayment, false, "Payment required", event.targetName,
                      "Authority payment", event.amount, event.transactionId, event.deadlineMs);
            return;
        case TransportEventKind::PaymentCompleted:
            clearPendingKind(state, TransportCommandKind::PayNow);
            state.money = event.cash;
            dismissModal(state);
            touchRevision(state);
            return;
        case TransportEventKind::DebtResolutionRequired:
            {
                const bool refresh = state.nav.current.page == ScreenPage::DebtAssets &&
                                     event.transactionId == state.debt.transactionId;
                const uint32_t previousSelection = state.debt.selectedMask;
                if (!refresh) state.debt = DebtState{};
                state.debt.transactionId = event.transactionId;
                state.debt.amountDue = event.amount;
                state.debt.cashBefore = event.cash;
                state.debt.eligibleMask = event.assetMask;
                state.debt.selectedMask = refresh ? previousSelection & legalDebtAssetMask(state) : 0;
                if (!refresh && debtEligibleProceeds(state) < appDebtShortfall(state)) {
                    state.debt.bankruptcyPending = true;
                    enterBankruptcy(state);
                    return;
                }
                if (refresh && state.debt.selectedMask != previousSelection) {
                    showToast(state,
                              "\xE8\xB5\x84\xE4\xBA\xA7\xE7\x8A\xB6\xE6\x80\x81\xE5\xB7\xB2\xE5\x8F\x98\xE5\x8C\x96\xEF\xBC\x8C\xE8\xAF\xB7\xE9\x87\x8D\xE6\x96\xB0\xE9\x80\x89\xE6\x8B\xA9",
                              nowMs);
                }
                dismissModal(state);
                replaceForcedPage(state, ScreenPage::DebtAssets);
            }
            return;
        case TransportEventKind::MortgageBatchCompleted:
            if (debtMortgageCompletion) {
                clearPendingKind(state, TransportCommandKind::MortgageBatchRequest);
                state.debt.submittedMortgageRequestId = 0;
                state.debt.submittedMortgageMask = 0;
                state.money = event.cash;
                showModal(state, ModalKind::ForcedPayment, false, "Payment required", "City Bank",
                          "Authority payment", state.debt.amountDue, state.debt.transactionId, nowMs + 10000);
            } else if (voluntaryMortgageCompletion) {
                clearPendingKind(state, TransportCommandKind::MortgageBatchRequest);
                state.money = event.cash;
                dismissModal(state);
                showToast(state, "Mortgage complete", nowMs);
            }
            touchRevision(state);
            return;
        case TransportEventKind::BankruptcyResolved:
            state.money = event.cash;
            state.debt.bankruptcyPending = false;
            state.debt.bankruptcyResolved = true;
            enterBankruptcy(state);
            touchRevision(state);
            return;
        case TransportEventKind::CommandCompleted:
            {
                const ModalKind completedKind = state.modal.kind;
                clearPendingRequest(state, event.requestId);
                dismissModal(state);
                showToast(state, completedKind == ModalKind::CollectRent
                                     ? "Rent claimed"
                                     : "Trade sent",
                          nowMs);
            }
            return;
        case TransportEventKind::CommandRejected:
            clearPendingRequest(state, event.requestId);
            if (matchesPayNow && event.error == TransportError::InsufficientCash &&
                state.modal.kind == ModalKind::ForcedPayment) {
                state.modal.submitting = false;
                state.modal.insufficient = true;
                state.modal.focus = ModalFocus::ResolveAssets;
                state.modal.deadlineMs = 0;
                touchRevision(state);
                return;
            }
            if (matchesMortgageRequest && state.modal.kind == ModalKind::DebtMortgageConfirm) {
                state.debt.submittedMortgageRequestId = 0;
                state.debt.submittedMortgageMask = 0;
            }
            if (matchingActiveModalRequest && state.modal.submitting) {
                state.modal.submitting = false;
                touchRevision(state);
            }
            return;
    }
}

void appHandleTouch(AppState &state, TouchAction action, uint32_t nowMs)
{
    if (action == TouchAction::PressDown) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldDown, 0}, nowMs); return; }
    if (action == TouchAction::PressUp) { appHandleUiEvent(state, UiEvent{UiEventKind::HoldUp, 0}, nowMs); return; }
    if (action == TouchAction::Back) {
        appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        return;
    }
    if (state.modal.kind != ModalKind::None) return;

    const uint16_t raw = static_cast<uint16_t>(action);
    if (raw >= static_cast<uint16_t>(TouchAction::ListItem0) &&
        raw < static_cast<uint16_t>(TouchAction::ListItem0) + 16) {
        appHandleUiEvent(
            state,
            UiEvent{UiEventKind::SelectListItem,
                    static_cast<int16_t>(raw - static_cast<uint16_t>(TouchAction::ListItem0))},
            nowMs
        );
        return;
    }
    if (action == TouchAction::ListPrevious) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, nowMs);
        return;
    }
    if (action == TouchAction::ListNext) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, nowMs);
        return;
    }
    if (action == TouchAction::Back) {
        appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        return;
    }
    if (action == TouchAction::Footer) {
        const UiEventKind kind = state.nav.current.page == ScreenPage::DebtAssets
                                     ? UiEventKind::SelectFooter
                                     : UiEventKind::Back;
        appHandleUiEvent(state, UiEvent{kind, 0}, nowMs);
        return;
    }
    if (raw >= 100 && raw <= 104) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, static_cast<int16_t>(raw - 100)}, nowMs); return; }
    if (raw >= 200 && raw <= 204) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 200)}, nowMs); return; }
    if (raw >= 300 && raw <= 304) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 300)}, nowMs); return; }
    if (raw >= 400 && raw <= 403) {
        if (action == TouchAction::TradeBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        else {
            int16_t focus = static_cast<int16_t>(raw - 400);
            if (appTradeReceiverLocked(state)) {
                if (action == TouchAction::TradeReceiver) return;
                --focus;
            }
            appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, focus}, nowMs);
        }
        return;
    }
    if (raw >= 500 && raw <= 501) { appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 500)}, nowMs); return; }
    if (raw >= 600 && raw <= 606) {
        if (action == TouchAction::DemoBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
        else appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, static_cast<int16_t>(raw - 600)}, nowMs);
        return;
    }
    if (action == TouchAction::DetailPrimary) {
        if (state.nav.current.page == ScreenPage::Home && state.homePhase == HomePhase::MyTurn) {
            setFocus(state, 0);
            activate(state, nowMs);
            return;
        }
        if (state.nav.current.page == ScreenPage::AssetDetail ||
            state.nav.current.page == ScreenPage::PlayerDetail) {
            setFocus(state, 0);
            activate(state, nowMs);
            return;
        }
        appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 0}, nowMs);
        return;
    }
    if (action == TouchAction::DetailBack) appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, nowMs);
}

void appTick(AppState &state, uint32_t nowMs)
{
    syncLegacyState(state);
    if (state.toastUntilMs != 0 && hasReachedDeadline(nowMs, state.toastUntilMs)) {
        state.toastUntilMs = 0;
        touchRevision(state);
    }

    if (state.modal.kind != ModalKind::None && state.modal.deadlineMs != 0 &&
        hasReachedDeadline(nowMs, state.modal.deadlineMs)) {
        if (state.modal.kind == ModalKind::CollectRent) {
            if (state.modal.submitting) return;
            dismissModal(state);
            showToast(state, "收租超时，已放弃", nowMs);
            return;
        }
        if (state.modal.kind == ModalKind::ForcedPayment && !state.modal.submitting) {
            submitModal(state, nowMs);
        }
        return;
    }

    if (!state.buttonHeld || state.holdActionConsumed) return;
    const uint32_t heldMs = nowMs - state.buttonDownMs;
    if (state.modal.kind != ModalKind::None) {
        if (state.modal.holding && heldMs >= kConfirmHoldMs && !state.modal.submitting &&
            (state.modal.focus == ModalFocus::Confirm ||
             state.modal.focus == ModalFocus::ResolveAssets)) {
            state.holdActionConsumed = true;
            submitModal(state, nowMs);
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
    if (state.modal.kind == ModalKind::None) return 0;
    if (state.modal.submitting) return 1000;
    if (!state.modal.holding) return 0;
    const uint32_t elapsed = nowMs - state.modal.holdStartMs;
    return static_cast<uint16_t>(elapsed >= kConfirmHoldMs ? 1000 : elapsed * 1000 / kConfirmHoldMs);
}

uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs)
{
    if (state.modal.kind == ModalKind::None || state.modal.deadlineMs == 0 ||
        hasReachedDeadline(nowMs, state.modal.deadlineMs)) return 0;
    return state.modal.deadlineMs - nowMs;
}
