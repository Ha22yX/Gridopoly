#include "logic_tests.h"

#include "app_config.h"
#include "app_state.h"
#include "demo_transport.h"

namespace {

bool expect(Stream &out, bool condition, const char *name)
{
    out.printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

void press(AppState &state, uint32_t downMs, uint32_t upMs)
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
    ok &= expect(out, state.page == ScreenPage::Home && state.focus == 0, "starts on home");

    appHandleInput(state, InputEvent{InputKind::Rotate, -1, 10}, 10);
    ok &= expect(out, state.focus == 3, "focus wraps left");
    appHandleInput(state, InputEvent{InputKind::Rotate, 2, 20}, 20);
    ok &= expect(out, state.focus == 1, "focus advances by delta");

    press(state, 100, 300);
    ok &= expect(out, state.page == ScreenPage::Players, "short press activates focus");
    press(state, 400, 1000);
    ok &= expect(out, state.page == ScreenPage::Players, "500-799ms dead band does nothing");
    press(state, 1100, 1950);
    ok &= expect(out, state.page == ScreenPage::Home, "800ms hold returns home");

    state.page = ScreenPage::Home;
    press(state, 2000, 5100);
    ok &= expect(out, state.page == ScreenPage::DemoLab, "3s hold opens demo lab");

    state.focus = 3;
    press(state, 5200, 5400);
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent, "demo opens rent modal");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 5500}, 5500);
    appTick(state, 6699);
    ok &= expect(out, state.modal.kind == ModalKind::CollectRent, "hold is not early");
    appTick(state, 6700);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == 1980, "1.2s confirms rent");

    state.page = ScreenPage::DemoLab;
    state.focus = 4;
    press(state, 7000, 7200);
    ok &= expect(out, state.modal.kind == ModalKind::Payment, "demo opens payment modal");
    appTick(state, 17200);
    ok &= expect(out, state.modal.kind == ModalKind::None && state.money == 1805, "payment auto-runs after 10s");

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

    DemoTransport duplicateTransport;
    duplicateTransport.begin(0);
    TransportCommand duplicateRoll{TransportCommandKind::RollRequest, 8, 41, 0, 0};
    ok &= expect(out, duplicateTransport.send(duplicateRoll, 10), "demo accepts first duplicate-id roll");
    ok &= expect(out, duplicateTransport.send(duplicateRoll, 11), "demo accepts duplicate-id roll");
    duplicateTransport.tick(130);
    ok &= expect(out, duplicateTransport.poll(event) &&
                      event.kind == TransportEventKind::RollResult &&
                      event.requestId == 8 && event.dieA == 3 && event.dieB == 4,
                 "duplicate-id roll preserves deterministic dice");
    duplicateTransport.tick(160);
    ok &= expect(out, duplicateTransport.poll(event) &&
                      event.kind == TransportEventKind::MoveGuidanceStarted &&
                      event.targetPosition == 24 && !duplicateTransport.poll(event),
                 "duplicate-id roll creates no second transaction");

    ok &= expect(out, kMechanicalCorrectionDeg == 60 && kFirmwareRotationDeg == 0,
                 "M4 pin 3 is mechanically aligned at six o'clock");
    return ok;
}
