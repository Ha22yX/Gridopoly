#include "logic_tests.h"

#include "app_config.h"
#include "app_state.h"
#include "demo_data.h"
#include "demo_transport.h"

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

} // namespace

bool runLogicTests(Stream &out)
{
    bool ok = true;
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
    appTick(state, deadlineWrapStartMs + 200u);
    ok &= expect(out, state.toastUntilMs != 0,
                 "toast deadline remains pending before millis wrap");
    appTick(state, deadlineWrapStartMs + 2500u);
    ok &= expect(out, state.toastUntilMs == 0,
                 "toast deadline expires after millis wrap");

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
