#include "app_state.h"
#include <cstdio>

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
    std::printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
