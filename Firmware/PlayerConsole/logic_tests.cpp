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

} // namespace

bool runLogicTests(Stream &out)
{
    bool ok = true;
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
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == 1980, "1.2s confirms rent");

    state.nav.current = NavigationEntry{ScreenPage::DemoLab, 4, 0};
    shortPress(state, 7000, 7200);
    ok &= expect(out, state.modal.kind == ModalKind::Payment, "demo opens payment modal");
    appTick(state, 17200);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == 1805, "payment auto-runs after 10s");

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
    appHandleUiEvent(state, UiEvent{UiEventKind::Back, 0}, 560);
    ok &= expect(out, state.nav.current.page == ScreenPage::Home && state.nav.current.focus == 2,
                 "trade back restores home trade focus");

    appHandleUiEvent(state, UiEvent{UiEventKind::SelectHomeAction, 3}, 570);
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
    transport.begin(0);
    bool paymentQueueFilled = true;
    for (uint32_t requestId = 100; requestId < 108; ++requestId) {
        paymentQueueFilled &= transport.send(
            TransportCommand{TransportCommandKind::PayNow, requestId, 41, 0, 0}, 0);
    }
    ok &= expect(out, paymentQueueFilled, "payment commands fill the scheduled queue");
    ok &= expect(out, !transport.send(
                      TransportCommand{TransportCommandKind::PayNow, 108, 41, 0, 0}, 0),
                 "full scheduled queue rejects payment command");
    TransportCommand blockedRoll{TransportCommandKind::RollRequest, 109, 41, 0, 0};
    ok &= expect(out, !transport.send(blockedRoll, 0),
                 "full scheduled queue atomically rejects roll");
    transport.tick(100);
    uint8_t paymentEventCount = 0;
    while (transport.poll(event)) {
        paymentEventCount += event.kind == TransportEventKind::PaymentCompleted ? 1 : 0;
    }
    ok &= expect(out, paymentEventCount == 8, "full queue retains all scheduled payments");
    TransportCommand recoveredRoll{TransportCommandKind::RollRequest, 110, 41, 0, 0};
    ok &= expect(out, transport.send(recoveredRoll, 100),
                 "atomic roll rejection leaves later roll available");
    transport.tick(220);
    ok &= expect(out, transport.poll(event) &&
                      event.kind == TransportEventKind::RollResult && event.requestId == 110,
                 "later roll is not stranded after capacity rejection");

    transport.begin(0);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 201, 41, 0, 0}, 0);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 202, 41, 0, 0}, 200);
    transport.tick(100);
    transport.poll(event);
    transport.send(TransportCommand{TransportCommandKind::PayNow, 203, 41, 0, 0}, 400);
    transport.tick(500);
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
