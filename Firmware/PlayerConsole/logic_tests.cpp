#include "logic_tests.h"

#include "app_config.h"
#include "app_state.h"
#include "demo_data.h"
#include "demo_transport.h"
#include "ui_layout.h"
#include "ui_carousel.h"
#include "ui_motion.h"
#include "ui_primitives.h"

#include <limits.h>
#include <string.h>

namespace {

bool expect(Stream &out, bool condition, const char *name)
{
    out.printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

void shortPress(AppState &state, uint32_t downMs, uint32_t upMs)
{
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, downMs}, downMs);
    appTick(state, upMs);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, upMs}, upMs);
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
    state.nav.current = NavigationEntry{ScreenPage::Trade, 2, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}

void openDebtFixture(AppState &state, int32_t amountDue, int32_t cash,
                     uint32_t eligibleMask)
{
    appInit(state, 0);
    TransportEvent event{};
    event.kind = TransportEventKind::DebtResolutionRequired;
    event.transactionId = 90;
    event.stateVersion = 2;
    event.amount = amountDue;
    event.cash = cash;
    event.assetMask = eligibleMask;
    appHandleTransportEvent(state, event, 0);
}

void focusDebtAsset(AppState &state, uint8_t index)
{
    state.nav.current.focus = index;
    state.debtListIndex = index;
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

} // namespace

bool runLogicTests(Stream &out)
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
    ok &= expect(out, uiRectInsideCircle(kModalRect),
                 "modal corners stay inside round safe area");
    ok &= expect(out, !uiRectInsideCircle(UiRect{0, 0, 32, 32}),
                 "corner geometry rejects rectangles outside round safe area");
    ok &= expect(out, uiCarouselPose(0).centerX == 240 &&
                      uiCarouselPose(0).opacity == 255 &&
                      uiCarouselPose(1).opacity == 87 &&
                      uiCarouselPose(2).opacity == 15,
                 "carousel uses approved opacity hierarchy");
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
    ok &= expect(out, waitingActionCount == 4 && nextActionCount == 4 &&
                      waitingActions[0] == HomeAction::Assets &&
                      waitingActions[1] == HomeAction::Players &&
                      waitingActions[2] == HomeAction::Trade &&
                      waitingActions[3] == HomeAction::More &&
                      nextActions[0] == HomeAction::Assets &&
                      nextActions[1] == HomeAction::Players &&
                      nextActions[2] == HomeAction::Trade &&
                      nextActions[3] == HomeAction::More,
                 "waiting and next expose Assets Players Trade More in order");
    ok &= expect(out, turnActionCount == 5 &&
                      turnActions[0] == HomeAction::Dice &&
                      turnActions[1] == HomeAction::Assets &&
                      turnActions[2] == HomeAction::Players &&
                      turnActions[3] == HomeAction::Trade &&
                      turnActions[4] == HomeAction::More,
                 "my turn exposes Dice Assets Players Trade More in order");
    ok &= expect(out, uiCenterListTrackY(3, 220, 58) == 46,
                 "center list track locks selected row");
    ok &= expect(out, uiListProgressPermille(0, 8) == 0 &&
                      uiListProgressPermille(7, 8) == 1000 &&
                      uiListProgressPermille(2, 1) == 0,
                 "list progress spans complete item range");
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
                      static_cast<uint8_t>(ModalKind::DebtMortgageConfirm) == 5,
                 "exact modal kind names compile in contract order");
    AppState state{};
    appInit(state, 0);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 0,
                 "starts on home");

    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 10}, 10);
    ok &= expect(out, state.nav.current.focus == 3, "home focus wraps left");
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
    ok &= expect(out, state.nav.current.page == ScreenPage::PlayerDetail,
                 "player row opens detail");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 530);
    ok &= expect(out, state.nav.current.page == ScreenPage::Players &&
                      state.nav.current.focus == 3 && state.playerListIndex == 3,
                 "player detail back restores players focus and anchor");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 540);

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 550);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 2}, 551);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 560);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 2,
                 "trade back restores home trade focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 570);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 571);
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 580);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 3,
                 "more back restores home more focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 590);
    appHandleUiEvent(state, UiEvent{UiEventKind::ActivateFocused, 0}, 600);
    ok &= expect(out, state.nav.current.page == ScreenPage::DemoLab, "more opens demo lab");
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 610);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 3,
                 "demo lab back restores home more focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 620);
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 0}, 621);
    appHandleUiEvent(state, UiEvent{UiEventKind::ListPrevious, 0}, 630);
    ok &= expect(out, state.nav.current.focus == 0 && state.assetListIndex == 0,
                 "list previous clamps at first row");
    for (uint8_t i = 0; i < kAssetCount + 2; ++i) {
        appHandleUiEvent(state, UiEvent{UiEventKind::ListNext, 0}, 640 + i);
    }
    ok &= expect(out, appFocusIsFooter(state) && state.assetListIndex == kAssetCount - 1,
                 "list next clamps at footer without moving list anchor");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 1}, 650);
    ok &= expect(out, state.nav.current.page == ScreenPage::Assets && state.nav.current.focus == 1,
                 "touching an unfocused list row selects it");
    appHandleUiEvent(state, UiEvent{UiEventKind::SelectListItem, 1}, 660);
    ok &= expect(out, state.nav.current.page == ScreenPage::AssetDetail,
                 "touching the selected list row activates it");

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
    ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                      appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::RollRequest,
                 "my turn centered Dice tap activates the roll action");

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
    state.nav.current = NavigationEntry{ScreenPage::Home, 4, 0};
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
    TransportEvent payment{TransportEventKind::PaymentRequired};
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
    TransportEvent insufficient{TransportEventKind::CommandRejected};
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
    TransportEvent completed{TransportEventKind::PaymentCompleted};
    completed.requestId = command.requestId;
    completed.transactionId = 90;
    completed.cash = 240;
    appHandleTransportEvent(state, completed, 13100);
    ok &= expect(out, state.money == 240 && state.modal.kind == ModalKind::None,
                 "payment completion event is the only cash mutation");

    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    appHandleTransportEvent(state, TransportEvent{TransportEventKind::ConnectionLost}, 10);
    shortPress(state, 20, 100);
    ok &= expect(out, !state.authorityOnline && !appPollCommand(state, command),
                 "offline authority blocks dangerous commands");
    state.modal.kind = ModalKind::TradeCreate;
    state.modal.submitting = true;
    TransportEvent snapshot{TransportEventKind::StateSnapshotApplied};
    snapshot.stateVersion = 9;
    snapshot.cash = 1775;
    snapshot.playerPosition = 17;
    appHandleTransportEvent(state, snapshot, 110);
    shortPress(state, 120, 200);
    ok &= expect(out, state.authorityOnline && state.money == 1775 && state.position == 17 &&
                      state.stateVersion == 9 && !state.modal.submitting &&
                      appPollCommand(state, command) && command.kind == TransportCommandKind::RollRequest,
                 "snapshot restores authority fields before commands unlock");

    TransportEvent stale{TransportEventKind::PaymentCompleted};
    stale.stateVersion = 8;
    stale.cash = 1;
    appHandleTransportEvent(state, stale, 210);
    ok &= expect(out, state.money == 1775 && state.stateVersion == 9,
                 "lower state version is ignored");
    TransportEvent newer{TransportEventKind::PaymentCompleted};
    newer.stateVersion = 10;
    newer.cash = 1600;
    appHandleTransportEvent(state, newer, 220);
    appHandleTransportEvent(state, newer, 230);
    ok &= expect(out, state.money == 1600 && state.stateVersion == 10,
                 "higher state version applies authoritative cash once");

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
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == rentCash,
                 "unclaimed rent expires after twenty seconds without cash mutation");

    DemoTransport transport;
    transport.begin(0);
    TransportCommand roll{TransportCommandKind::RollRequest, 7, 41, 0, 0};
    ok &= expect(out, transport.send(roll, 10), "demo accepts roll request");
    TransportEvent event{};
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
    const AppState ordinaryDebt = state;
    TransportEvent unsolicitedBankruptcy{};
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
    appHandleInput(state, InputEvent{InputKind::Rotate, 1, 1010}, 1010);
    shortPress(state, 1020, 1100);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.nav.current.page == ScreenPage::DebtAssets &&
                      !appCanNavigateBack(state),
                 "debt confirmation cancel returns only to forced debt assets");
    focusDebtAsset(state, kAssetCount);
    shortPress(state, 1110, 1200);
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 1210}, 1210);
    appTick(state, 2410);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2410}, 2410);
    ok &= expect(out, appPollCommand(state, command) &&
                      command.kind == TransportCommandKind::MortgageBatchRequest &&
                      command.transactionId == 90 && command.assetMask == 0x55 && !appPollCommand(state, command),
                 "debt confirmation hold queues exactly one selected mortgage batch");
    TransportEvent mortgageCompleted{};
    mortgageCompleted.kind = TransportEventKind::MortgageBatchCompleted;
    mortgageCompleted.requestId = command.requestId;
    mortgageCompleted.transactionId = 90;
    mortgageCompleted.stateVersion = 3;
    mortgageCompleted.cash = 735;
    mortgageCompleted.assetMask = 0x55;
    const AppState submittedDebt = state;
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
    const AppState acceptedMortgage = state;
    appHandleTransportEvent(state, mortgageCompleted, 2600);
    ok &= expect(out, debtAuthorityStateUnchanged(state, acceptedMortgage),
                 "duplicate mortgage completion leaves accepted debt unchanged");

    openDebtFixture(state, /*amountDue=*/500, /*cash=*/240, /*eligibleMask=*/0x01);
    ok &= expect(out, state.nav.current.page == ScreenPage::Bankruptcy && !appCanNavigateBack(state) &&
                      appFocusCount(state) == 1,
                 "unresolvable debt enters terminal bankruptcy without back");
    const AppState pendingBankruptcy = state;
    TransportEvent wrongBankruptcy{};
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
    TransportEvent unrelatedBankruptcyEvent{};
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
    const AppState terminalBankruptcy = state;
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
        TransportEventKind::CommandRejected,
    };
    bool terminalEventsIgnored = true;
    for (TransportEventKind kind : terminalEventKinds) {
        TransportEvent eventAfterBankruptcy{};
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
    DemoTransport voluntaryTransport;
    voluntaryTransport.setScenario(DemoScenario::DebtMortgage);
    voluntaryTransport.begin(0);
    ok &= expect(out, voluntaryTransport.send(voluntaryMortgage, 0),
                 "demo accepts voluntary mortgage command");
    voluntaryTransport.tick(150);
    TransportEvent voluntaryCompleted{};
    ok &= expect(out, voluntaryTransport.poll(voluntaryCompleted) &&
                      voluntaryCompleted.kind == TransportEventKind::MortgageBatchCompleted &&
                      voluntaryCompleted.requestId == voluntaryMortgage.requestId &&
                      voluntaryCompleted.transactionId != 0 && voluntaryCompleted.assetMask == 0x01,
                 "demo assigns transaction to voluntary mortgage completion");
    const AppState submittedVoluntaryMortgage = state;
    TransportEvent wrongVoluntaryMask = voluntaryCompleted;
    wrongVoluntaryMask.assetMask = 0x02;
    appHandleTransportEvent(state, wrongVoluntaryMask, 4020);
    ok &= expect(out, debtAuthorityStateUnchanged(state, submittedVoluntaryMortgage),
                 "wrong voluntary mortgage mask leaves submission unchanged");
    appHandleTransportEvent(state, voluntaryCompleted, 4030);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == voluntaryCompleted.cash,
                 "assigned voluntary mortgage completion applies cash and dismisses once");
    const AppState acceptedVoluntaryMortgage = state;
    appHandleTransportEvent(state, voluntaryCompleted, 4040);
    ok &= expect(out, debtAuthorityStateUnchanged(state, acceptedVoluntaryMortgage),
                 "duplicate voluntary mortgage completion is inert");

    openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
    focusDebtAsset(state, 0);
    shortPress(state, 2600, 2700);
    TransportEvent refreshedDebt{};
    refreshedDebt.kind = TransportEventKind::DebtResolutionRequired;
    refreshedDebt.transactionId = 90;
    refreshedDebt.stateVersion = 3;
    refreshedDebt.amount = 680;
    refreshedDebt.cash = 240;
    refreshedDebt.assetMask = 0x54;
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
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.transactionId == paymentTransactionId && !transport.poll(event),
                 "payment cash auto-completes exactly at its deadline once");

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
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      event.transactionId == latePaymentTransactionId && !transport.poll(event),
                 "payment deadline wins over a late PayNow response");

    const uint32_t paymentWrapStartMs = 0xFFFFFF00u;
    transport.begin(paymentWrapStartMs);
    transport.tick(paymentWrapStartMs);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentRequired &&
                      event.deadlineMs == paymentWrapStartMs + 10000u,
                 "payment cash deadline wraps from scenario start");
    transport.tick(paymentWrapStartMs + 9999u);
    ok &= expect(out, !transport.poll(event), "wrapped payment deadline is not early");
    transport.tick(paymentWrapStartMs + 10000u);
    ok &= expect(out, transport.poll(event) && event.kind == TransportEventKind::PaymentCompleted &&
                      !transport.poll(event),
                 "wrapped payment deadline completes exactly once");

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
