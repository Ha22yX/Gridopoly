#include "app_state.h"
#include "input_dispatch.h"
#include <cstdio>
#include <vector>

int main() {
    int failed = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failed;
    };
    AppState app{};
    appInit(app, 0);
    app.identity.avatarAssetsReady = true;
    TransportIdentityPayload payload{};
    payload.seats[0].playerId = 1;
    payload.seats[0].seatRevision = 1;
    payload.seats[0].recipe = app.identity.draftRecipe;
    TransportEvent snapshot{};
    snapshot.kind = TransportEventKind::IdentitySnapshotReceived;
    snapshot.roomId = 108; snapshot.selfSeatId = 1; snapshot.stateVersion = 1;
    snapshot.identityRevision = 1; snapshot.identitySeatCount = 4;
    snapshot.identityHumanMask = 1; snapshot.identityOnlineMask = 15;
    snapshot.identityPhase = TransportIdentityPhase::AwaitAvatar;
    snapshot.identitySelfStage = 2; snapshot.identity = &payload;
    appHandleTransportEvent(app, snapshot, 10);
    app.identity.avatarAssetsReady = true;
    appHandleTransportEvent(app, snapshot, 11);
    check(app.page == ScreenPage::AvatarSetup, "real identity projection enters Avatar Setup");
    appHandleInput(app, InputEvent{InputKind::Rotate, 2, 20}, 20);
    check(app.focus == 2, "clockwise logical steps select the third avatar row");
    appHandleTransportEvent(app, snapshot, 1000);
    check(app.focus == 2, "unchanged identity heartbeat preserves selected avatar row");
    appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, 1010}, 1010);
    appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, 1040}, 1040);
    const auto before = app.identity.draftRecipe;
    appHandleInput(app, InputEvent{InputKind::Rotate, 1, 1050}, 1050);
    appHandleTransportEvent(app, snapshot, 2000);
    check(app.focus == 2 && app.identity.editingValue &&
          app.identity.draftRecipe.facePresetId != before.facePresetId &&
          app.identity.draftRecipe.hairPresetId == before.hairPresetId,
          "editing and identity heartbeat cannot redirect a face change to hair");
    // A slow frame must not turn a ready burst into one frame per detent.
    app.focus = app.nav.current.focus = 0;
    app.identity.editingValue = false;
    const auto beforeBurst = app.identity.draftRecipe;
    std::vector<InputEvent> pending{
        {InputKind::Rotate, 1, 2100}, {InputKind::Rotate, 1, 2110},
        {InputKind::Rotate, 1, 2120}, {InputKind::Rotate, -1, 2130},
        {InputKind::ButtonDown, 0, 2140}, {InputKind::ButtonUp, 0, 2170},
        {InputKind::Rotate, 1, 2180}};
    size_t cursor = 0;
    auto poll = [&](InputEvent &event) {
        if (cursor == pending.size()) return false;
        event = pending[cursor++]; return true;
    };
    auto consume = [&](const InputEvent &event) { appHandleInput(app, event, 2300); };
    const auto firstBatch = dispatchInputBatch(poll, consume);
    check(firstBatch == 6 && cursor == 6 && app.focus == 2 && app.identity.editingValue,
          "queued forward/reverse steps and short click reach the right row in one batch");
    check(app.identity.draftRecipe.facePresetId == beforeBurst.facePresetId,
          "button release yields for page/handwriting service before later rotations");
    dispatchInputBatch(poll, consume);
    check(app.identity.draftRecipe.facePresetId != beforeBurst.facePresetId &&
          app.identity.draftRecipe.hairPresetId == beforeBurst.hairPresetId,
          "rotation after click edits the selected field, preserving input order");
    pending.assign(40, InputEvent{InputKind::Rotate, 1, 2400}); cursor = 0;
    check(dispatchInputBatch(poll, consume) == 16 && cursor == 16,
          "input burst is bounded so rendering and transport get a service opportunity");
    check(dispatchInputBatch(poll, consume) == 16 &&
          dispatchInputBatch(poll, consume) == 8 && cursor == pending.size(),
          "bounded dispatch preserves the unconsumed tail without dropping steps");
    pending.assign(15, InputEvent{InputKind::Rotate, 1, 2400});
    pending.push_back({InputKind::ButtonDown, 0, 2410});
    pending.push_back({InputKind::ButtonUp, 0, 2440}); cursor = 0;
    check(dispatchInputBatch(poll, consume) == 16 && app.buttonHeld && cursor == 16,
          "batch remains strictly bounded even when the last event is a press");
    appTick(app, 6000, cursor != pending.size());
    dispatchInputBatch(poll, consume);
    check(!app.buttonHeld && !app.holdActionConsumed,
          "already queued release completes without an elapsed-time hold at the boundary");
    for (bool earlyPress : {false, true}) {
        for (bool modal : {false, true}) {
            appInit(app, 0);
            if (modal) app.modal.kind = ModalKind::VoluntaryMortgage;
            app.toastUntilMs = 5500;
            pending.assign(earlyPress ? 0 : 15, InputEvent{InputKind::Rotate, 1, 990});
            pending.push_back({InputKind::ButtonDown, 0, 1000});
            for (int i = 0; i < (earlyPress ? 40 : 1); ++i)
                pending.push_back({InputKind::Rotate, 1, static_cast<uint32_t>(1001 + i)});
            pending.push_back({InputKind::ButtonUp, 0, 1100}); cursor = 0;
            bool noFalseHold = true, bounded = true;
            while (cursor != pending.size()) {
                bounded &= dispatchInputBatch(poll, consume) <= 16;
                appTick(app, 6000, cursor != pending.size());
                noFalseHold &= !app.holdActionConsumed && app.commandCount == 0 &&
                    app.page != ScreenPage::DemoLab;
            }
            check(noFalseHold && !app.buttonHeld && bounded,
                  earlyPress ? (modal ? "early press and multi-batch rotation never submit a delayed short modal click"
                                     : "early press and multi-batch rotation never convert a short click into Home hold")
                             : (modal ? "Down16 Rotate17 Up18 never submit a delayed short modal click"
                                     : "Down16 Rotate17 Up18 never convert a short click into Home hold"));
            check(app.toastUntilMs == 0,
                  "non-input deadlines continue while an input backlog is being drained");
        }
    }
    appInit(app, 0);
    appHandleInput(app, {InputKind::ButtonDown, 0, 1000}, 1000);
    appTick(app, 3999);
    check(!app.holdActionConsumed, "live Home hold does not fire before 3000ms");
    appTick(app, 4000);
    check(app.holdActionConsumed && app.page == ScreenPage::DemoLab,
          "real sustained Home hold still fires once the input queue is empty");
    appInit(app, 0); app.modal.kind = ModalKind::VoluntaryMortgage;
    appHandleInput(app, {InputKind::ButtonDown, 0, 1000}, 1000);
    appTick(app, 2199);
    check(!app.holdActionConsumed, "live modal hold does not fire before 1200ms");
    appTick(app, 2200);
    check(app.holdActionConsumed && app.modal.submitting,
          "real sustained modal confirmation still submits once the input queue is empty");
    std::printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
