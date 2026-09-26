#include "app_state.h"
#include "tag_pair_view.h"
#include "tag_pair_wire.h"
#include "player_device_identity.h"
#include <GridopolyCore.h>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <algorithm>

namespace {
int failed = 0, checks = 0;
void check(bool value, const char *name) {
    ++checks; if (!value) ++failed;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}
TransportIdentityPayload seats;
TransportEvent identity(uint8_t self, uint8_t stage = 7, uint32_t room = 100) {
    TransportEvent event{};
    event.kind = TransportEventKind::IdentitySnapshotReceived;
    event.roomId = room; event.selfSeatId = self; event.stateVersion = 20;
    event.identityRevision = 3; event.identitySeatCount = 4;
    event.identityHumanMask = 15; event.identityOnlineMask = 15;
    event.identitySelfStage = stage;
    event.identityPhase = stage == 7 ? TransportIdentityPhase::TagPairing :
        (stage == 3 ? TransportIdentityPhase::AwaitName : TransportIdentityPhase::AwaitAvatar);
    event.identity = &seats;
    return event;
}
TransportTagPairSnapshot pairing(uint8_t self, uint32_t revision, uint8_t active = 1) {
    TransportTagPairSnapshot snapshot{};
    snapshot.selfPlayerId = self; snapshot.activePlayerId = active;
    snapshot.revision = revision; snapshot.sessionId = 77; snapshot.flags = kTagPairEnabled;
    snapshot.state = TransportTagPairState::AwaitingTag; snapshot.requiredHumanMask = 15;
    snapshot.readerMapIndex = 0; std::strcpy(snapshot.moduleId, "tile-reader");
    return snapshot;
}
void applyPair(AppState &app, const TransportTagPairSnapshot &pair, uint32_t now,
               uint32_t room = 100, uint32_t request = 0, bool newEpoch = false, bool resync = false) {
    TransportEvent event{}; event.kind = TransportEventKind::TagPairSnapshotReceived;
    event.roomId = room; event.selfSeatId = pair.selfPlayerId; event.tagPair = &pair;
    event.requestId = request; event.tagPairEpochReset = newEpoch; event.resync = resync;
    appHandleTransportEvent(app, event, now);
}
TransportEvent game(uint8_t self, AuthorityPhase phase, bool full, uint8_t winner = 0) {
    TransportEvent event{};
    event.kind = full ? TransportEventKind::AuthoritySnapshotApplied : TransportEventKind::StateSnapshotApplied;
    event.roomId = 500; event.stateVersion = 30; event.selfSeatId = self;
    event.playerCount = 4; event.activePlayerId = 1; event.decisionPlayerId = 1;
    event.phase = phase; event.winnerPlayerId = winner; event.cash = 800; event.boardSize = 24;
    const auto *board = gridopoly::core::BoardCatalog::findBySize(24);
    event.boardIdHash = gridopoly::protocol::crc32(reinterpret_cast<const uint8_t *>(board->id), std::strlen(board->id));
    for (uint8_t i = 0; i < 4; ++i) {
        event.players[i].playerId = i + 1; event.players[i].cash = 800; event.players[i].flags = 4;
    }
    return event;
}

void holdConfirm(AppState &app, uint32_t now) {
    appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, now}, now);
    appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, now + 1300}, now + 1300);
}

void projectCore(AppState &app, const gridopoly::core::GameEngine &engine,
                 uint32_t revision, uint32_t now, bool resync = false) {
    const auto &source = engine.state();
    auto event = game(1, static_cast<AuthorityPhase>(source.phase), false);
    event.stateVersion = revision; event.boardSize = source.board->tileCount;
    event.boardIdHash = gridopoly::protocol::crc32(
        reinterpret_cast<const uint8_t *>(source.board->id), std::strlen(source.board->id));
    event.availableActions = engine.actionsFor(1); event.decisionPlayerId = engine.decisionPlayerId();
    event.activePlayerId = source.activePlayerId; event.cash = source.players[0].cash;
    event.playerCount = source.playerCount; event.assetCount = source.board->assetCount;
    event.debtFlags = source.pendingDebt.active ? 1 : 0;
    event.debtDebtorId = source.pendingDebt.debtorId; event.debtAmount = source.pendingDebt.amount;
    for (uint8_t i = 0; i < source.playerCount; ++i) {
        event.players[i].cash = source.players[i].cash;
        event.players[i].flags = source.players[i].bankrupt ? 6 : 4;
    }
    for (uint8_t i = 0; i < event.assetCount; ++i) {
        event.assets[i].ownerId = source.assets[i].ownerId;
        event.assets[i].buildingLevel = source.assets[i].buildingLevel;
        event.assets[i].flags = source.assets[i].mortgaged ? 1 : 0;
    }
    event.resync = resync;
    appHandleTransportEvent(app, event, now);
    event.kind = TransportEventKind::AuthoritySnapshotApplied;
    appHandleTransportEvent(app, event, now + 1);
}

void joiningChecks() {
    AppState app{}; appInit(app, 0, true);
    check(app.page == ScreenPage::WaitingForGame && appAwaitingGame(app) &&
          app.money == 0 && app.playerCount == 0 && !app.authorityOnline &&
          appPageContentCount(app) == 0 && !appCanNavigateBack(app),
          "production first frame waits for authority without demo cash, position or actions");
    AppState diagnostic{}; appInit(diagnostic, 0);
    check(diagnostic.page == ScreenPage::Home && !appAwaitingGame(diagnostic) && diagnostic.money == 1860,
          "existing self-test fixtures keep their explicit demo Home instead of benchmarking waiting page");
    appHandleTouch(app, TouchAction::HomeAssets, 10);
    appHandleTouch(app, TouchAction::DemoTurn, 11);
    appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, 12}, 12);
    appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, 4012}, 4012);
    TransportCommand command{};
    check(app.page == ScreenPage::WaitingForGame && !appPollCommand(app, command),
          "waiting screen ignores stale home, demo and long-hold inputs");
    TransportEvent join{}; join.kind = TransportEventKind::JoinStatusChanged;
    join.joinStatus = TransportJoinStatus::WaitingForNewGame;
    appHandleTransportEvent(app, join, 4100); appTick(app, 20000);
    check(app.page == ScreenPage::WaitingForGame && app.joinStatus == TransportJoinStatus::WaitingForNewGame &&
          app.toastUntilMs == 0 && !app.hasAuthoritativeState,
          "authenticated refusal is a persistent new-game page rather than an expiring disconnect toast");
    join.joinStatus = TransportJoinStatus::Connecting; appHandleTransportEvent(app, join, 20001);
    check(app.joinStatus == TransportJoinStatus::WaitingForNewGame,
          "ordinary discovery retry cannot erase the actionable new-game refusal");
    join.joinStatus = TransportJoinStatus::WaitingForNewGame; appHandleTransportEvent(app, join, 20002);
    check(app.page == ScreenPage::WaitingForGame && !appPollCommand(app, command),
          "repeated admission refusal remains stable and cannot enqueue game actions");
    join.joinStatus = TransportJoinStatus::Syncing; appHandleTransportEvent(app, join, 20003);
    check(app.page == ScreenPage::WaitingForGame && app.joinStatus == TransportJoinStatus::Syncing && app.money == 0,
          "PairAccept alone waits for real player state without revealing placeholder gameplay");
    auto actual = game(2, AuthorityPhase::AwaitRoll, false);
    actual.cash = 1117; actual.playerPosition = 8;
    appHandleTransportEvent(app, actual, 20004);
    check(app.hasAuthoritativeState && !appAwaitingGame(app) && app.page == ScreenPage::Home &&
          app.money == 1117 && app.position == 8 && app.authorityOnline,
          "first real state projection replaces waiting with authoritative cash and location");
    TransportEvent lost{}; lost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(app, lost, 21000);
    check(app.page == ScreenPage::Home && app.money == 1117 && app.position == 8 && !app.authorityOnline,
          "previously joined screen preserves real business page during temporary disconnect");
    join.joinStatus = TransportJoinStatus::WaitingForNewGame; appHandleTransportEvent(app, join, 21001);
    check(app.page == ScreenPage::Home && !appAwaitingGame(app) && app.money == 1117,
          "admission retry after a real session never replaces retained business state with onboarding");
    actual.stateVersion = 31; actual.resync = true; actual.cash = 1120;
    appHandleTransportEvent(app, actual, 21002);
    check(app.authorityOnline && app.joinStatus == TransportJoinStatus::Inactive && app.money == 1120,
          "authenticated resync restores original gameplay recovery semantics");

    AppState setup{}; appInit(setup, 0, true);
    applyPair(setup, pairing(1, 1), 30000);
    check(appAwaitingGame(setup), "early Tag snapshot cannot expose a placeholder game before seat context");
    appHandleTransportEvent(setup, identity(1), 30001);
    applyPair(setup, pairing(1, 2), 30002);
    check(setup.hasAuthoritativeState && !appAwaitingGame(setup) && setup.page == ScreenPage::TagPairing,
          "legitimate identity then Tag pairing leaves onboarding for the token instructions");
    AppState disconnected{}; appInit(disconnected, 0, true);
    appHandleTransportEvent(disconnected, join, 31000);
    appHandleTransportEvent(disconnected, lost, 31001);
    check(disconnected.joinStatus == TransportJoinStatus::Connecting && disconnected.page == ScreenPage::WaitingForGame,
          "real link loss while awaiting a new game shows connecting without revealing demo state");
    AppState activeRoom{}; appInit(activeRoom, 0, true);
    auto activeIdentity = identity(1, 6); activeIdentity.identityPhase = TransportIdentityPhase::Active;
    appHandleTransportEvent(activeRoom, activeIdentity, 32000);
    check(appAwaitingGame(activeRoom) && activeRoom.page == ScreenPage::WaitingForGame,
          "active-room identity alone cannot open Home before cash and position arrive");
    auto completePair = pairing(1, 1); completePair.state = TransportTagPairState::Complete;
    completePair.flags |= kTagPairSelfBound | kTagPairCanEditIdentity;
    applyPair(activeRoom, completePair, 32001);
    check(activeRoom.page == ScreenPage::WaitingForGame,
          "early completed Tag binding cannot bypass waiting for real gameplay state");
    actual.roomId = 100; actual.selfSeatId = 1;
    appHandleTransportEvent(activeRoom, actual, 32002);
    applyPair(activeRoom, completePair, 32003);
    check(activeRoom.page == ScreenPage::Home && activeRoom.money == 1120 &&
          activeRoom.identity.tagPairSuccessUntilMs == 0,
          "restoring an active game does not invent a take-token-back success interruption");
    AppState fullOnly{}; appInit(fullOnly, 0, true);
    auto fullState = game(3, AuthorityPhase::TurnEnd, true);
    fullState.players[2].cash = 411; fullState.players[2].position = 9;
    appHandleTransportEvent(fullOnly, fullState, 33000);
    check(!appAwaitingGame(fullOnly) && fullOnly.page == ScreenPage::Home &&
          fullOnly.money == 411 && fullOnly.position == 9,
          "full authority arriving before compact snapshot also opens Home with actual player values");
}

void buildingSaleChecks() {
    using namespace gridopoly::core;
    const auto *board = BoardCatalog::findBySize(32);
    GameEngine engine; engine.reset(*board, 31);
    engine.addPlayer("P1", ControllerKind::RealConsole);
    engine.addPlayer("P2", ControllerKind::RealConsole); engine.start();
    auto &model = engine.mutableStateForRestore();
    model.players[0].cash = 2000;
    uint8_t selected = 0;
    while (board->assets[selected].kind != TileKind::Property) ++selected;
    const uint8_t group = board->assets[selected].group;
    uint16_t consumed = 0; int32_t total = 0;
    for (uint8_t i = 0; i < board->assetCount; ++i) {
        if (board->assets[i].kind != TileKind::Property) continue;
        if (board->assets[i].group == group) {
            model.assets[i].ownerId = 1; model.assets[i].buildingLevel = 5;
            total += 5 * (board->assets[i].economy.buildingCost / 2);
        } else if (consumed < board->buildingStock) {
            model.assets[i].ownerId = 2;
            model.assets[i].buildingLevel = static_cast<uint8_t>(
                std::min<int>(4, board->buildingStock - consumed));
            consumed += model.assets[i].buildingLevel;
        }
    }
    AppState app{}; appInit(app, 0); projectCore(app, engine, 100, 3000, true);
    app.selectedAsset = selected;
    check(appBankBuildingStock(app) == engine.availableBuildings() &&
          appBankBuildingStock(app) == 0 && appBankLandmarkStock(app) == engine.availableLandmarks(),
          "client bank stock matches real Core across whole board including landmarks");
    check(!engine.canSellBuilding(1, selected) && engine.canLiquidateBuildingGroup(1, selected) &&
          !appAssetDetailActionEnabled(app, AssetDetailAction::SellBuilding) &&
          appAssetDetailActionEnabled(app, AssetDetailAction::SellBuildingGroup),
          "empty bank disables single landmark downgrade but explicit whole group remains available");
    check(appBuildingGroupSaleProceeds(app, selected) == total,
          "group preview counts all five landmark investment levels at half each cost");
    auto noBuild = app;
    for (uint8_t i = 0; i < board->assetCount; ++i)
        if (board->assets[i].group == group) noBuild.authorityAssets[i].buildingLevel = 0;
    noBuild.availableActions |= ActionBuild;
    check(!appAssetDetailActionEnabled(noBuild, AssetDetailAction::Build),
          "global build permission cannot enable a property when ordinary building inventory is empty");
    auto noLandmark = app; uint8_t usedLandmarks = 0;
    for (uint8_t i = 0; i < board->assetCount; ++i) {
        if (board->assets[i].kind != TileKind::Property) continue;
        if (board->assets[i].group == group) noLandmark.authorityAssets[i].buildingLevel = 4;
        else if (usedLandmarks < board->landmarkStock) {
            noLandmark.authorityAssets[i].buildingLevel = 5; ++usedLandmarks;
        } else noLandmark.authorityAssets[i].buildingLevel = 0;
    }
    noLandmark.availableActions |= ActionBuild;
    check(appBankLandmarkStock(noLandmark) == 0 &&
          !appAssetDetailActionEnabled(noLandmark, AssetDetailAction::Build),
          "landmark upgrade is disabled when all landmark pieces are already on board");
    auto incomplete = app; --incomplete.authorityAssetCount;
    check(appBankBuildingStock(incomplete) == 0 && !appBuildingGroupSaleEligible(incomplete, selected),
          "incomplete board projection cannot fabricate stock or a complete group sale quote");
    app.nav.current = NavigationEntry{ScreenPage::AssetDetail, 3, 0}; app.page = ScreenPage::AssetDetail;
    appHandleTouch(app, TouchAction::DetailRefresh, 3010);
    check(app.modal.kind == ModalKind::SellBuildingGroupConfirm && app.modal.cancelAllowed &&
          app.modal.amount == total && std::strstr(app.modal.purpose, "ALL BUILDINGS") != nullptr,
          "explicit group action opens cancellable preview naming every building in the color group");
    appHandleInput(app, InputEvent{InputKind::Rotate, 1, 3020}, 3020);
    appHandleInput(app, InputEvent{InputKind::ButtonDown, 0, 3021}, 3021);
    appHandleInput(app, InputEvent{InputKind::ButtonUp, 0, 3040}, 3040);
    TransportCommand command{};
    check(app.modal.kind == ModalKind::None && !appPollCommand(app, command),
          "cancelling whole group sale emits no command");
    appHandleTouch(app, TouchAction::DetailRefresh, 3100); holdConfirm(app, 3110);
    check(appPollCommand(app, command) && command.kind == TransportCommandKind::SellBuildingRequest &&
          command.argument == 1 && command.assetIndex == selected && app.modal.submitting,
          "group confirmation sends exactly explicit SellBuilding argument one");
    const uint32_t saleRequest = command.requestId;
    holdConfirm(app, 4500);
    check(!appPollCommand(app, command), "repeat confirmation cannot submit group sale twice");
    const int32_t cashBefore = model.players[0].cash;
    check(static_cast<bool>(engine.liquidateBuildingGroup(1, selected)) &&
          model.players[0].cash == cashBefore + total, "preview agrees with actual Core atomic liquidation result");
    projectCore(app, engine, 101, 5900);
    TransportEvent completed{}; completed.kind = TransportEventKind::CommandCompleted;
    completed.requestId = saleRequest; completed.stateVersion = 101;
    appHandleTransportEvent(app, completed, 5902);
    check(appAssetBuildingLevel(app, selected) == 0 && appBuildingGroupSaleProceeds(app, selected) == 0 &&
          app.money == cashBefore + total && app.modal.kind == ModalKind::None,
          "authoritative post-sale projection clears all levels and restores final cash");

    // Restore a legal all-landmark group and exercise its forced debt entry.
    for (uint8_t i = 0; i < board->assetCount; ++i)
        if (board->assets[i].kind == TileKind::Property && board->assets[i].group == group)
            model.assets[i].buildingLevel = 5;
    model.phase = GamePhase::AwaitDebt; model.pendingDebt.active = true;
    model.pendingDebt.debtorId = 1; model.pendingDebt.amount = total; model.players[0].cash = 0;
    projectCore(app, engine, 102, 6000);
    check(app.page == ScreenPage::DebtAssets && appBuildingGroupSaleEligible(app, selected),
          "debt with insufficient physical bank stock still offers raising funds by group sale");
    appHandleTouch(app, TouchAction::ListItem0, 6010);
    appHandleTouch(app, TouchAction::ListItem0, 6020);
    check(app.page == ScreenPage::AssetDetail && app.selectedAsset == selected,
          "debt asset row opens explicit sell-one and sell-group choices");
    projectCore(app, engine, 103, 6030);
    check(app.page == ScreenPage::AssetDetail && appCanNavigateBack(app),
          "ordinary debt heartbeat preserves sale choices and return to debt list");
    appHandleTouch(app, TouchAction::DetailRefresh, 6040);
    holdConfirm(app, 6050);
    check(appPollCommand(app, command) && command.argument == 1 && app.modal.submitting,
          "forced debt flow submits the same explicit whole group action");

    AppState single{}; appInit(single, 0);
    model.phase = GamePhase::AwaitRoll; model.pendingDebt = {}; model.players[0].cash = 2000;
    for (uint8_t i = 0; i < board->assetCount; ++i)
        if (board->assets[i].group != group) model.assets[i].buildingLevel = 0;
    projectCore(single, engine, 104, 7500, true); single.selectedAsset = selected;
    single.nav.current = NavigationEntry{ScreenPage::AssetDetail, 2, 0}; single.page = ScreenPage::AssetDetail;
    appHandleTouch(single, TouchAction::DetailTertiary, 7510);
    check(appPollCommand(single, command) && command.argument == 0,
          "existing single-level sale retains argument zero and never silently liquidates group");
    AppState changed{}; appInit(changed, 0); projectCore(changed, engine, 105, 7600, true);
    changed.selectedAsset = selected; changed.nav.current = NavigationEntry{ScreenPage::AssetDetail, 3, 0};
    changed.page = ScreenPage::AssetDetail; appHandleTouch(changed, TouchAction::DetailRefresh, 7610);
    model.assets[selected].buildingLevel = 4; projectCore(changed, engine, 106, 7620);
    holdConfirm(changed, 7630);
    check(!appPollCommand(changed, command) && changed.modal.amount == appBuildingGroupSaleProceeds(changed, selected),
          "changed group proceeds require reviewing updated amount before confirmation");
    model.assets[selected].ownerId = 2; projectCore(changed, engine, 107, 9000);
    check(!appBuildingGroupSaleEligible(changed, selected), "partial or foreign ownership disables whole-group sale");
}
}

int main() {
    buildingSaleChecks();
    for (uint8_t index = 0; index < 4; ++index) {
        seats.seats[index].playerId = index + 1; seats.seats[index].seatRevision = 1;
        seats.seats[index].flags = gridopoly::protocol::IdentitySeatPresent |
            gridopoly::protocol::IdentitySeatHuman | gridopoly::protocol::IdentitySeatConnected;
    }
    joiningChecks();
    char discovery[17]{}; playerDeviceDiscoveryName(0xdcd102d9b4dcULL, discovery);
    check(std::strcmp(discovery, "GP-dcb4d902d1dc") == 0 && discovery[16] == 0,
          "discovery carries all MAC bytes in canonical order");
    std::unordered_set<uint32_t> deviceIds;
    for (uint32_t suffix = 0; suffix < 65536; ++suffix)
        deviceIds.insert(playerDeviceId((static_cast<uint64_t>(suffix) << 32) | 0x02d9b4dcULL));
    check(deviceIds.size() == 65536 && deviceIds.count(0) == 0,
          "65536 screens with formerly identical truncated ID remain distinct and nonzero");
    check(playerDeviceId(0xdcd102d9b4dcULL) == playerDeviceId(0xabcdDCD102D9B4DCULL),
          "stable ID ignores bits outside 48-bit hardware MAC");

    AppState screens[4];
    for (uint8_t index = 0; index < 4; ++index) {
        appInit(screens[index], 0);
        appHandleTransportEvent(screens[index], identity(index + 1), 100);
        applyPair(screens[index], pairing(index + 1, 1), 110);
        check(screens[index].page == ScreenPage::TagPairing && appTagPairBlocksIdentity(screens[index]),
              "each unpaired human screen stays in the pairing flow");
    }
    auto &first = screens[0]; auto &second = screens[1];
    check(std::strcmp(tagPairView(first).instruction, "PLACE YOUR TOKEN") == 0 &&
          std::strcmp(tagPairView(second).title, "WAIT YOUR TURN") == 0,
          "only the selected player sees placement instructions");
    check(!appRequestTagPair(second, TransportTagPairOperation::Retry, 120),
          "queued player cannot restart another player's session");
    check(appRequestTagPair(first, TransportTagPairOperation::Retry, 120), "active player can retry");
    TransportCommand command{};
    check(appPollCommand(first, command) && command.kind == TransportCommandKind::TagPairRequest &&
          command.roomId == 100 && command.tagPairSessionId == 77 && command.tagPairRevision == 1 &&
          command.targetPlayerId == 1, "retry binds room, session, revision and own seat");
    check(!appRequestTagPair(first, TransportTagPairOperation::Cancel, 125), "pending retry cannot be double-submitted");
    auto pair = pairing(1, 2); pair.state = TransportTagPairState::AlreadyBound;
    applyPair(first, pair, 130, 100, command.requestId);
    check(appTagPairCanManage(first) && std::strcmp(tagPairView(first).title, "TOKEN ALREADY USED") == 0,
          "duplicate UID clears pending response and requests another token");
    for (auto state : {TransportTagPairState::MultipleTags, TransportTagPairState::ReaderOffline,
                       TransportTagPairState::TimedOut, TransportTagPairState::Cancelled,
                       TransportTagPairState::ClearRequired}) {
        ++pair.revision; pair.state = state; applyPair(first, pair, 140);
        check(first.page == ScreenPage::TagPairing && appTagPairBlocksIdentity(first),
              "reader, timeout and cancellation states never open identity editing");
    }
    auto staleIdentity = identity(1, 3);
    staleIdentity.identityAvatarReadyMask = 1;
    appHandleTransportEvent(first, staleIdentity, 150);
    appUpdateAvatarPreloadProgress(first, 30, 30, true);
    check(first.page == ScreenPage::TagPairing && !appIdentityAppendCharacter(first, 'X'),
          "out-of-order identity, preload and handwriting callbacks cannot bypass pairing");
    appHandleTouch(first, TouchAction::IdentityConfirm, 151);
    check(!appPollCommand(first, command), "stale confirm touch sends no avatar command while unpaired");

    pair.revision = 10; pair.state = TransportTagPairState::SuccessRemove;
    pair.flags |= kTagPairSelfBound | kTagPairCanEditIdentity; pair.boundMask = 1;
    applyPair(first, pair, 200);
    appHandleTransportEvent(first, identity(1, 1), 201);
    check(first.page == ScreenPage::TagPairing &&
          std::strcmp(tagPairView(first).instruction, "TAKE YOUR TOKEN BACK") == 0,
          "successful binding presents a bounded take-back message");
    appTick(first, 1399);
    check(first.page == ScreenPage::TagPairing, "success message persists for its display interval");
    appTick(first, 1400);
    check(first.page == ScreenPage::AvatarLoading && !appTagPairBlocksIdentity(first),
          "bound player enters identity without waiting for another player's pairing");
    appUpdateAvatarPreloadProgress(first, 30, 30, true);
    first.identity.draftRecipe.hairPresetId = 4; first.identity.draftDirty = true;
    first.identity.draftName[0] = 'A'; first.identity.draftName[1] = 0;
    pair.revision = 11; pair.activePlayerId = 2; pair.sessionId = 78;
    pair.state = TransportTagPairState::AwaitingTag;
    applyPair(first, pair, 1500);
    check(first.page == ScreenPage::AvatarSetup && first.identity.draftRecipe.hairPresetId == 4 &&
          first.identity.draftName[0] == 'A', "next player's pairing preserves local customization draft");
    auto old = pairing(1, 9); applyPair(first, old, 1501);
    check(first.identity.tagPair.revision == 11 && !appTagPairBlocksIdentity(first),
          "older pairing revision cannot revoke a newer bound state");
    auto alien = pairing(1, 999); applyPair(first, alien, 1502, 99);
    check(first.authorityRoomId == 100 && first.identity.tagPair.revision == 11,
          "old-room pairing projection cannot replace the current room");
    auto wrongSeat = pairing(2, 999); applyPair(first, wrongSeat, 1503);
    check(first.identity.tagPair.revision == 11, "another screen's projection is rejected");

    TransportEvent lost{}; lost.kind = TransportEventKind::ConnectionLost;
    appHandleTransportEvent(second, lost, 1600);
    check(std::strcmp(tagPairView(second).title, "RECONNECTING") == 0 &&
          !appRequestTagPair(second, TransportTagPairOperation::Retry, 1601),
          "disconnected pairing screen disables mutation and explains recovery");
    auto recovery = pairing(2, 12, 2); applyPair(second, recovery, 1700);
    check(second.authorityOnline && second.page == ScreenPage::TagPairing && appTagPairCanManage(second),
          "authenticated pairing projection restores the current session after reconnect");
    recovery.revision = 40; applyPair(second, recovery, 1701);
    recovery.revision = 1; recovery.sessionId = 900;
    applyPair(second, recovery, 1702, 100, 0, false, true);
    check(second.identity.tagPair.revision == 40,
          "wire resync alone cannot roll back pairing revision in the same authenticated epoch");
    applyPair(second, recovery, 1703, 100, 0, true, true);
    check(second.identity.tagPair.revision == 1 && second.identity.tagPair.sessionId == 900,
          "new authenticated session resync rebuilds pairing after same-room server restart");
    auto disabled = pairing(2, 2, 2); disabled.flags = 0; disabled.state = TransportTagPairState::Disabled;
    applyPair(second, disabled, 1704);
    auto staleEditing = identity(2, 1); staleEditing.identityRevision = 4;
    appHandleTransportEvent(second, staleEditing, 1705);
    check(appTagPairBlocksIdentity(second) && second.page == ScreenPage::TagPairing,
          "once required in a room, missing enabled flag cannot bypass pairing via stale editing stage");
    check(!tagPairRevisionIsOlder(1, 0xfffffffeu) && tagPairRevisionIsOlder(0xfffffffeu, 1),
          "pairing revision comparison tolerates uint32 wrap without accepting old sequence");
    auto freshRoom = identity(1, 7, 101); freshRoom.stateVersion = 1;
    appHandleTransportEvent(first, freshRoom, 1800);
    check(first.identity.tagPairRequired && !first.identity.tagPairSnapshotValid &&
          first.identity.draftName[0] == 0 && first.page == ScreenPage::TagPairing,
          "new room clears binding, pending state and old identity draft");

    gridopoly::protocol::TagPairSnapshot wire{};
    wire.selfPlayerId = 3; wire.activePlayerId = 2; wire.roomId = 100; wire.revision = 14;
    wire.sessionId = 78; wire.state = gridopoly::protocol::TagPairState::MultipleTags;
    wire.flags = gridopoly::protocol::TagPairFlagEnabled; wire.requiredHumanMask = 15;
    wire.readerTagRevision = 88; wire.readerMapIndex = 5;
    std::strcpy(wire.moduleId.data(), "reader-abc");
    uint8_t bytes[gridopoly::protocol::kTagPairSnapshotSize]{}; size_t written = 0;
    gridopoly::protocol::TagPairSnapshot decoded{};
    const bool encoded = gridopoly::protocol::encodeTagPairSnapshot(wire, bytes, sizeof(bytes), written);
    check(encoded && gridopoly::protocol::decodeTagPairSnapshot(bytes, written, decoded),
          "frozen 88-byte pairing snapshot round trips through real protocol codec");
    const auto mapped = tagPairFromWire(decoded);
    check(mapped.state == TransportTagPairState::MultipleTags && mapped.revision == 14 &&
          mapped.readerTagRevision == 88 && mapped.readerMapIndex == 5 &&
          std::strcmp(mapped.moduleId, "reader-abc") == 0, "wire adapter preserves reader and session presentation data");

    const uint8_t resultSeats[] = {1, 2};
    for (uint8_t self : resultSeats) {
        AppState player{}; appInit(player, 0);
        auto compact = game(self, AuthorityPhase::GameOver, false);
        compact.resync = true;
        appHandleTransportEvent(player, compact, 2000);
        check(player.page == ScreenPage::GameOver && player.homePhase == HomePhase::Waiting,
              "compact terminal push suppresses stale YOUR TURN even before winner projection");
        auto full = game(self, AuthorityPhase::GameOver, true, 2); full.resync = true;
        appHandleTransportEvent(player, full, 2001);
        check(player.page == ScreenPage::GameOver && player.winnerPlayerId == 2 &&
              player.availableActions == 0 && appPageContentCount(player) == 0,
              "winner and loser both restore final result after full reconnect");
        appHandleTouch(player, TouchAction::HomeAssets, 2002);
        appHandleInput(player, InputEvent{InputKind::ButtonDown, 0, 2003}, 2003);
        appHandleInput(player, InputEvent{InputKind::ButtonUp, 0, 2020}, 2020);
        check(!appPollCommand(player, command), "terminal page cannot enqueue roll or another gameplay action");
        auto next = identity(self, 7, 501); next.stateVersion = 1;
        appHandleTransportEvent(player, next, 2100); appTick(player, 2101);
        check(player.winnerPlayerId == 0 && player.page == ScreenPage::TagPairing &&
              !appSelfBankrupt(player), "new game clears winner and spectator state");
    }
    AppState spectator{}; appInit(spectator, 0);
    auto bankrupt = game(1, AuthorityPhase::AwaitRoll, false); bankrupt.players[0].flags |= 2;
    bankrupt.activePlayerId = 2;
    appHandleTransportEvent(spectator, bankrupt, 2200);
    check(spectator.page == ScreenPage::Spectator && appSelfBankrupt(spectator),
          "bankrupt player observes remaining turns with no roll prompt");
    auto finish = game(1, AuthorityPhase::GameOver, true, 2);
    finish.players[0].flags |= 2; finish.stateVersion = 31;
    appHandleTransportEvent(spectator, finish, 2300);
    check(spectator.page == ScreenPage::GameOver && spectator.winnerPlayerId == 2,
          "bankrupt spectator still receives ordinary final winner updates");
    std::printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
