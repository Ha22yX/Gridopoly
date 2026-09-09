#pragma once
#include "app_state.h"
#include "frame_presentation_tracker.h"
#include "test_fixture.h"
#include <GridopolyCore.h>
#include <GridopolyProtocol.h>
#include <cstring>
// The same state-machine scenarios execute on the native host and device.
template<class Check> bool runMovementCueAppTests(Check check)
{
    bool ok = true;
    const TransportCommand legacy{TransportCommandKind::MortgageBatchRequest, 700, 42, 90, 0, 0x55};
    ok &= check(legacy.requestId == 700 && legacy.stateVersion == 42 &&
        legacy.transactionId == 90 && legacy.assetMask == 0x55 && legacy.roomId == 0,
        "cue: local metadata preserves legacy aggregate command fields");
    FramePresentationTracker frames;
    const uint32_t first = frames.request();
    frames.hardwareFrameBoundary();
    ok &= check(!frames.completed(first), "frame: old scan cannot present a newly requested page");
    frames.bufferSwitchCompleted(true);
    ok &= check(!frames.completed(first), "frame: framebuffer switch alone is not presentation");
    frames.hardwareFrameBoundary();
    ok &= check(!frames.completed(first), "frame: DMA completion alone may leave final rows in flight");
    frames.hardwareFrameBoundary();
    ok &= check(frames.completed(first), "frame: following frame boundary confirms complete physical scan");
    const uint32_t second = frames.request();
    ok &= check(!frames.completed(second), "frame: older receipt cannot acknowledge a newer page");
    frames.bufferSwitchCompleted(false);
    frames.hardwareFrameBoundary();
    frames.hardwareFrameBoundary();
    ok &= check(!frames.completed(second),
        "frame: rejected framebuffer switch cannot acknowledge the new page");
    TestFixture<AppState> storage;
    if (!storage) return check(false, "cue: app fixture allocation");
    AppState &state = *storage;
    auto fresh = [&]() {
        appInit(state, 0);
        state.authorityRoomId = 123;
        state.stateVersion = 100;
        state.selfSeatId = state.activePlayerId = 1;
        state.authorityOnline = state.boardCatalogCompatible = true;
        state.authorityPhase = AuthorityPhase::AwaitMoveConfirm;
        state.rollOrigin = 3;
        state.rollTarget = 7;
        state.moveArrivalPending = true;
        state.availableActions = 1u << 1;
        state.page = state.nav.current.page = ScreenPage::DiceStage;
        state.rollAnimating = true;
    };
    fresh();
    TransportCommand command{};
    appNotifyFramePresented(state, 10);
    ok &= check(!appPollCommand(state, command), "cue: dice frame never opens gate");
    state.page = state.nav.current.page = ScreenPage::MoveGuide;
    state.rollAnimating = false;
    ok &= check(!appPollCommand(state, command), "cue: page entry without presentation never sends");
    appNotifyFramePresented(state, 20);
    ok &= check(appPollCommand(state, command) &&
        command.kind == TransportCommandKind::MovementCueReadyRequest &&
        command.roomId == 123 && command.stateVersion == 100 &&
        command.assetIndex == 0xFF && command.argument == 7,
        "cue: first presented guide sends exact Action17 metadata");
    const uint32_t request = command.requestId;
    appNotifyFramePresented(state, 21);
    ok &= check(!appPollCommand(state, command), "cue: repeated frame sends only once");
    state.rollResolved = state.rollPresentationComplete = state.rollRevealPresented = true;
    TransportEvent resync{};
    resync.kind = TransportEventKind::StateSnapshotApplied;
    resync.roomId = 123; resync.stateVersion = 100; resync.resync = true;
    resync.phase = AuthorityPhase::AwaitMoveConfirm;
    resync.selfSeatId = resync.activePlayerId = resync.decisionPlayerId = 1;
    resync.playerCount = 2; resync.boardSize = 24;
    resync.playerPosition = 3; resync.pendingTarget = resync.targetPosition = 7;
    resync.availableActions = 1u << 1;
    appHandleTransportEvent(state, resync, 22);
    resync.kind = TransportEventKind::AuthoritySnapshotApplied;
    const auto *board = gridopoly::core::BoardCatalog::findBySize(24);
    resync.boardIdHash = gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(board->id), std::strlen(board->id));
    resync.assetCount = board->assetCount;
    resync.pendingMoveFlags = 1; resync.pendingMovePlayerId = 1;
    resync.pendingMoveOrigin = 3; resync.pendingMoveDieA = resync.pendingMoveDieB = 2;
    appHandleTransportEvent(state, resync, 23);
    ok &= check(!appPollCommand(state, command) && state.movementCueRequestId == request &&
        state.movementCueFramePresented, "cue: same-key full resync keeps the first-frame receipt and request");
    TransportEvent loss{};
    loss.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(state, loss, 22);
    state.authorityOnline = true;
    appNotifyFramePresented(state, 23);
    ok &= check(state.movementCueRequestId == request && !appPollCommand(state, command),
        "cue: reconnect preserves request for transport retry");
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 30}, 30);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 60}, 60);
    ok &= check(appPollCommand(state, command) &&
        command.kind == TransportCommandKind::MoveManualConfirmRequest,
        "cue: manual arrival remains independent");
    TransportEvent ack{};
    ack.kind = TransportEventKind::CommandCompleted;
    ack.requestId = request;
    ack.stateVersion = 100;
    appHandleTransportEvent(state, ack, 70);
    ok &= check(state.movementCueAcknowledged && state.stateVersion == 100,
        "cue: ActionResult completes without version advance");
    appNotifyFramePresented(state, 80);
    ok &= check(!appPollCommand(state, command), "cue: acknowledged move does not replay");
    state.authorityRoomId = 124;
    ok &= check(!appPollCommand(state, command) && !state.movementCueAcknowledged &&
        !state.movementCueFramePresented, "cue: new room requires new presentation");
    appNotifyFramePresented(state, 90);
    ok &= check(appPollCommand(state, command), "cue: new room can begin");
    state.rollTarget = 8;
    ok &= check(!appPollCommand(state, command) && !state.movementCueFramePresented,
        "cue: new target invalidates old presentation");
    appNotifyFramePresented(state, 100);
    ok &= check(appPollCommand(state, command), "cue: new target can begin");
    state.authorityPhase = AuthorityPhase::TurnEnd;
    ok &= check(!appPollCommand(state, command) && state.movementCueRoomId == 0 &&
        state.movementCueRequestId == 0, "cue: leaving movement clears lifecycle");
    fresh();
    state.page = state.nav.current.page = ScreenPage::MoveGuide;
    state.rollAnimating = false;
    appNotifyFramePresented(state, 101);
    appPollCommand(state, command);
    const uint32_t staleRequest = command.requestId;
    ++state.stateVersion;
    ok &= check(appPollCommand(state, command) && command.stateVersion == 101 &&
        command.requestId != staleRequest && state.movementCueFramePresented,
        "cue: newer version preserves presentation and creates fresh request");
    ack.requestId = staleRequest;
    appHandleTransportEvent(state, ack, 102);
    ok &= check(!state.movementCueAcknowledged, "cue: old ack cannot complete new request");
    const uint32_t retiredRequest = state.movementCueRequestId;
    TransportEvent retry{};
    retry.kind = TransportEventKind::MovementCueRetryRequested;
    retry.requestId = retiredRequest; retry.roomId = state.authorityRoomId;
    appHandleTransportEvent(state, retry, 200);
    ok &= check(!appPollCommand(state, command), "cue: recovery renewal is rate limited");
    appTick(state, 649);
    ok &= check(!appPollCommand(state, command), "cue: renewal does not busy-loop before retry window");
    appTick(state, 650);
    ok &= check(appPollCommand(state, command) && command.requestId != retiredRequest &&
        command.stateVersion == 101 && state.movementCueFramePresented,
        "cue: explicit recovery creates a fresh request at the same version without replaying presentation");
    fresh();
    state.page = state.nav.current.page = ScreenPage::MoveGuide;
    state.rollAnimating = false;
    state.rollResolved = state.rollPresentationComplete = true;
    TransportEvent arrived{};
    arrived.kind = TransportEventKind::StateSnapshotApplied;
    arrived.roomId = 123; arrived.stateVersion = 101;
    arrived.phase = AuthorityPhase::AwaitPurchase;
    arrived.selfSeatId = arrived.activePlayerId = arrived.decisionPlayerId = 1;
    arrived.playerCount = 2; arrived.boardSize = 24;
    arrived.playerPosition = 7; arrived.pendingTarget = 0xFF;
    arrived.availableActions = (1u << 2) | (1u << 3);
    appHandleTransportEvent(state, arrived, 1000);
    const uint32_t continueAt = state.arrivalContinueAtMs;
    arrived.resync = true;
    appHandleTransportEvent(state, arrived, 1001);
    ok &= check(state.nav.current.page == ScreenPage::MoveGuide &&
        state.moveArrivalConfirmed && continueAt != 0 && state.arrivalContinueAtMs == continueAt,
        "arrival: same-position resync preserves confirmed page and original deadline");
    arrived.playerPosition = 8;
    appHandleTransportEvent(state, arrived, 1002);
    ok &= check(!state.moveArrivalConfirmed && state.nav.current.page == ScreenPage::Purchase,
        "arrival: changed authoritative position cannot preserve an old confirmation");
    return ok;
}
