#include "TileDebugAssignments.h"

#include <gridopoly/core/BoardCatalog.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

using namespace gridopoly::core;
using namespace gridopoly::pi;

namespace {

const TileDebugTile& findTile(const TileDebugSnapshot& snapshot, const char* id) {
  for (const auto& tile : snapshot.tiles) {
    if (tile.tileId == id) return tile;
  }
  assert(false && "tile not found");
  return snapshot.tiles.front();
}

const TileModuleDebugState& assignmentFor(const TileDebugSnapshot& snapshot,
                                          const char* moduleId) {
  for (const auto& assignment : snapshot.assignments) {
    if (assignment.moduleId == moduleId) return assignment;
  }
  assert(false && "assignment not found");
  return snapshot.assignments.front();
}

const TileDebugModule& moduleFor(const TileDebugSnapshot& snapshot,
                                const char* moduleId) {
  for (const auto& module : snapshot.modules) {
    if (module.moduleId == moduleId) return module;
  }
  assert(false && "module not found");
  return snapshot.modules.front();
}

GameState makeState(std::uint8_t boardSize) {
  GameState state{};
  state.board = BoardCatalog::findBySize(boardSize);
  assert(state.board != nullptr);
  state.playerCount = 6;
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    state.players[index].id = static_cast<std::uint8_t>(index + 1u);
  }
  std::strcpy(state.players[0].name, "Kicofy");
  std::strcpy(state.players[1].name, "Bot 1");
  std::strcpy(state.players[2].name, "Bot 2");
  std::strcpy(state.players[3].name, "Bot 3");
  std::strcpy(state.players[4].name, "Bot 4");
  std::strcpy(state.players[5].name, "Bot 5");
  state.stateVersion = 77;
  for (std::uint8_t index = 0; index < state.board->assetCount; ++index) {
    state.assets[index].ownerId = kNoPlayer;
  }
  return state;
}

std::uint8_t assetIndexFor(const GameState& state, const char* tileId) {
  assert(state.board != nullptr);
  for (std::uint8_t index = 0; index < state.board->tileCount; ++index) {
    const auto& tile = state.board->tiles[index];
    if (std::strcmp(tile.id, tileId) == 0) {
      assert(tile.assetIndex != kNoAsset);
      return tile.assetIndex;
    }
  }
  assert(false && "asset tile not found");
  return kNoAsset;
}

void verifyCatalogAndColors() {
  std::uint64_t now = 1787540000000ull;
  TileDebugAssignments service([&now] { return now; });
  for (const auto size : {16u, 24u, 32u, 40u}) {
    auto state = makeState(static_cast<std::uint8_t>(size));
    const auto catalog = service.snapshot(size, state);
    assert(catalog.boardSize == size);
    assert(catalog.tiles.size() == size);
    assert(catalog.players.size() == 6);
    for (const auto& tile : catalog.tiles) {
      assert(!tile.tileId.empty());
      assert(!tile.displayName.empty());
      assert(!tile.kind.empty());
      assert(tile.accentRgb != 0);
      assert(!tile.artworkKey.empty());
      const auto& definition = state.board->tiles[tile.mapIndex];
      const auto expectedPrice = definition.assetIndex == kNoAsset
          ? 0 : state.board->assets[definition.assetIndex].economy.price;
      assert(tile.purchasePrice == expectedPrice);
    }
  }

  auto state = makeState(32);
  const auto snapshot = service.snapshot(9001, state);
  const std::array<std::uint32_t, 6> expectedColors{
      0x58A7EBu, 0xEF7168u, 0x52DCB7u, 0xF2C453u, 0xC28AE8u, 0xEA8A55u,
  };
  const std::array<std::uint16_t, 6> expectedRgb565{
      0x5D3Du, 0xEB8Du, 0x56F6u, 0xF62Au, 0xC45Du, 0xEC4Au,
  };
  for (std::size_t index = 0; index < expectedColors.size(); ++index) {
    assert(snapshot.players[index].playerId == index + 1u);
    assert(snapshot.players[index].rgb == expectedColors[index]);
    const auto rgb = expectedColors[index];
    const auto converted = static_cast<std::uint16_t>(
        ((rgb >> 8u) & 0xF800u) | ((rgb >> 5u) & 0x07E0u) | ((rgb >> 3u) & 0x001Fu));
    assert(converted == expectedRgb565[index]);
  }
  const auto& a1 = findTile(snapshot, "A1");
  assert(a1.mapIndex == 1);
  assert(a1.displayName == "Rivet Row");
  assert(a1.kind == "PROPERTY");
  assert(a1.accentRgb == 0xC97852u);
  assert(a1.artworkKey == "a1-rivet-row");
  assert(a1.purchasePrice == 60);
  assert(findTile(snapshot, "CARD-CE-1").artworkKey == "cover-chance");
  assert(findTile(snapshot, "CARD-CF-1").artworkKey == "cover-community-fund");
  assert(findTile(snapshot, "CORNER-START").purchasePrice == 0);
}

void verifyAutomaticAndManualAssignments() {
  std::uint64_t now = 1000;
  TileDebugAssignments service([&now] { return now; });
  auto state = makeState(32);
  const auto originalVersion = state.stateVersion;
  const auto originalOwner = state.assets[0].ownerId;

  auto first = service.heartbeat(9001, state, "module-a", "device-a");
  assert(first.result && first.result.changed);
  assert(first.assigned);
  assert(first.source == TileDebugAssignmentSource::Auto);
  assert(first.leaseMs == 15000);
  assert(first.assignment.mapIndex == 0);
  assert(first.assignment.tileId == "CORNER-START");
  assert(first.assignment.ownerPlayerId == 0);

  const auto firstRevision = first.serverRevision;
  now += 2000;
  const auto renewed = service.heartbeat(9001, state, "module-a", "device-a");
  assert(renewed.result && !renewed.result.changed);
  assert(renewed.serverRevision == firstRevision);
  auto view = service.snapshot(9001, state);
  assert(moduleFor(view, "module-a").online);
  assert(moduleFor(view, "module-a").leaseRemainingMs == 15000);

  auto second = service.heartbeat(9001, state, "module-b", "device-b");
  assert(second.result && second.assigned);
  assert(second.assignment.mapIndex == 1);

  auto manual = service.set(9001, state, "module-a", "device-a", "B3");
  assert(manual && manual.changed);
  view = service.snapshot(9001, state);
  const auto& manualAssignment = assignmentFor(view, "module-a");
  assert(manualAssignment.mapIndex == 9);
  assert(manualAssignment.source == TileDebugAssignmentSource::Manual);
  assert(manualAssignment.ownerPlayerId == 0);
  assert(manualAssignment.ownerDisplayName.empty());
  assert(manualAssignment.ownerRgb == 0);

  const auto third = service.heartbeat(9001, state, "module-c", "device-c");
  assert(third.result && third.assigned);
  assert(third.assignment.mapIndex == 0);
  assert(service.set(9001, state, "module-b", "device-b", "B3").code ==
         TileDebugResultCode::TileConflict);
  assert(service.set(9001, state, "missing", "missing", "A2").code ==
         TileDebugResultCode::ModuleOffline);
  assert(service.set(9001, state, "module-a", "wrong-device", "A2").code ==
         TileDebugResultCode::DeviceMismatch);

  const auto cleared = service.clear(9001, state, "module-a");
  assert(cleared && cleared.changed);
  view = service.snapshot(9001, state);
  assert(!moduleFor(view, "module-a").assigned);
  assert(view.modules.size() == 3);
  const auto reassigned = service.heartbeat(9001, state, "module-a", "device-a");
  assert(reassigned.result && reassigned.result.changed);
  assert(reassigned.assignment.mapIndex == 2);
  assert(reassigned.source == TileDebugAssignmentSource::Auto);

  const auto promoted = service.set(9001, state, "module-a", "device-a",
                                    reassigned.assignment.tileId);
  assert(promoted && promoted.changed);
  const auto promotedRevision = promoted.serverRevision;
  const auto duplicate = service.set(9001, state, "module-a", "device-a",
                                     reassigned.assignment.tileId);
  assert(duplicate && !duplicate.changed);
  assert(duplicate.serverRevision == promotedRevision);

  assert(service.heartbeat(9001, state, "bad id", "device").result.code ==
         TileDebugResultCode::InvalidModuleId);
  assert(service.heartbeat(9001, state, "module", "bad/device").result.code ==
         TileDebugResultCode::InvalidDeviceId);
  assert(service.heartbeat(9001, state, "module-d", "device-b").result.code ==
         TileDebugResultCode::DeviceConflict);
  assert(service.set(9001, state, "module-a", "device-a", "NOPE").code ==
         TileDebugResultCode::TileNotFound);

  assert(state.stateVersion == originalVersion);
  assert(state.assets[0].ownerId == originalOwner);
}

void verifyAuthoritativeOwnershipRefresh() {
  std::uint64_t now = 3000;
  TileDebugAssignments service([&now] { return now; });
  auto state = makeState(32);
  const auto originalVersion = state.stateVersion;

  assert(service.heartbeat(500, state, "owner-a", "device-a").result);
  assert(service.heartbeat(500, state, "owner-b", "device-b").result);
  assert(service.set(500, state, "owner-a", "device-a", "B3"));
  auto view = service.snapshot(500, state);
  const auto baseRevision = view.revision;
  assert(assignmentFor(view, "owner-a").ownerPlayerId == 0);
  assert(assignmentFor(view, "owner-b").ownerPlayerId == 0);

  state.assets[assetIndexFor(state, "B3")].ownerId = 2;
  state.assets[assetIndexFor(state, "A1")].ownerId = 2;
  ++state.stateVersion;
  ++now;
  view = service.snapshot(500, state);
  assert(view.revision == baseRevision + 1);
  const auto authorityRevision = view.revision;
  const auto authorityUpdatedAt = view.updatedAtMs;
  for (const auto* moduleId : {"owner-a", "owner-b"}) {
    const auto& assignment = assignmentFor(view, moduleId);
    assert(assignment.ownerPlayerId == 2);
    assert(assignment.ownerDisplayName == "Bot 1");
    assert(assignment.ownerRgb == 0xEF7168u);
    assert(assignment.revision == authorityRevision);
    assert(assignment.updatedAtMs == authorityUpdatedAt);
  }

  ++now;
  const auto unchanged = service.snapshot(500, state);
  assert(unchanged.revision == authorityRevision);

  std::strcpy(state.players[1].name, "BANKER");
  ++state.stateVersion;
  ++now;
  const auto renamed = service.heartbeat(500, state, "owner-a", "device-a");
  assert(renamed.result && renamed.result.changed);
  assert(renamed.serverRevision == authorityRevision + 1);
  assert(renamed.assignment.ownerDisplayName == "BANKER");
  view = service.snapshot(500, state);
  assert(assignmentFor(view, "owner-b").ownerDisplayName == "BANKER");
  assert(assignmentFor(view, "owner-b").revision == renamed.serverRevision);

  state.assets[assetIndexFor(state, "B3")].ownerId = kNoPlayer;
  state.assets[assetIndexFor(state, "A1")].ownerId = 7;
  ++state.stateVersion;
  ++now;
  view = service.snapshot(500, state);
  assert(view.revision == renamed.serverRevision + 1);
  for (const auto* moduleId : {"owner-a", "owner-b"}) {
    const auto& assignment = assignmentFor(view, moduleId);
    assert(assignment.ownerPlayerId == 0);
    assert(assignment.ownerDisplayName.empty());
    assert(assignment.ownerRgb == 0);
  }

  const auto ownerRevision = view.revision;
  state.players[0].cash += 123;
  ++state.stateVersion;
  ++now;
  view = service.snapshot(500, state);
  assert(view.revision == ownerRevision);
  assert(state.stateVersion == originalVersion + 4);
}

void verifyLeaseBoundaryAndReclamation() {
  std::uint64_t now = 100;
  TileDebugAssignments service([&now] { return now; });
  auto state = makeState(16);
  const auto registered = service.heartbeat(42, state, "lease-a", "lease-device");
  assert(registered.result && registered.assignment.mapIndex == 0);
  const auto registrationRevision = registered.serverRevision;

  now = 15099;
  auto view = service.snapshot(42, state);
  assert(view.modules.size() == 1);
  assert(view.modules.front().leaseRemainingMs == 1);
  assert(view.revision == registrationRevision);

  now = 15100;
  view = service.snapshot(42, state);
  assert(view.modules.empty());
  assert(view.assignments.empty());
  assert(view.revision == registrationRevision + 1);
  assert(service.set(42, state, "lease-a", "lease-device", "A1").code ==
         TileDebugResultCode::ModuleOffline);

  const auto replacement = service.heartbeat(42, state, "lease-b", "new-device");
  assert(replacement.result && replacement.assignment.mapIndex == 0);
}

void verifyRoomReassignmentOrder() {
  std::uint64_t now = 2000;
  TileDebugAssignments service([&now] { return now; });
  auto state32 = makeState(32);
  assert(service.heartbeat(100, state32, "first", "device-1").result);
  assert(service.heartbeat(100, state32, "second", "device-2").result);
  assert(service.heartbeat(100, state32, "third", "device-3").result);
  assert(service.set(100, state32, "first", "device-1", "B3"));
  assert(service.set(100, state32, "second", "device-2", "C1"));
  const auto before = service.snapshot(100, state32).revision;

  auto state24 = makeState(24);
  const auto changed = service.snapshot(101, state24);
  assert(changed.revision == before + 1);
  assert(changed.modules.size() == 3);
  assert(changed.assignments.size() == 3);
  assert(assignmentFor(changed, "first").mapIndex == 0);
  assert(assignmentFor(changed, "second").mapIndex == 1);
  assert(assignmentFor(changed, "third").mapIndex == 2);
  for (const auto& assignment : changed.assignments) {
    assert(assignment.source == TileDebugAssignmentSource::Auto);
    assert(assignment.ownerPlayerId == 0);
    assert(assignment.revision == changed.revision);
  }
  assert(moduleFor(changed, "first").registrationOrder <
         moduleFor(changed, "second").registrationOrder);
  assert(moduleFor(changed, "second").registrationOrder <
         moduleFor(changed, "third").registrationOrder);
}

void verifyConcurrentRegistration() {
  std::uint64_t now = 5000;
  auto state = makeState(16);
  TileDebugAssignments service([&now] { return now; });
  std::array<TileDebugHeartbeatResponse, 2> responses{};
  std::thread left([&] {
    responses[0] = service.heartbeat(700, state, "concurrent-a", "device-a");
  });
  std::thread right([&] {
    responses[1] = service.heartbeat(700, state, "concurrent-b", "device-b");
  });
  left.join();
  right.join();
  assert(responses[0].result && responses[1].result);
  assert(responses[0].assigned && responses[1].assigned);
  assert(responses[0].assignment.mapIndex != responses[1].assignment.mapIndex);
  assert((responses[0].assignment.mapIndex == 0 || responses[0].assignment.mapIndex == 1));
  assert((responses[1].assignment.mapIndex == 0 || responses[1].assignment.mapIndex == 1));

  TileDebugAssignments duplicateDeviceService([&now] { return now; });
  std::array<TileDebugHeartbeatResponse, 2> duplicateDevice{};
  std::thread first([&] {
    duplicateDevice[0] = duplicateDeviceService.heartbeat(
        701, state, "device-owner-a", "same-device");
  });
  std::thread second([&] {
    duplicateDevice[1] = duplicateDeviceService.heartbeat(
        701, state, "device-owner-b", "same-device");
  });
  first.join();
  second.join();
  const auto successCount = static_cast<int>(static_cast<bool>(duplicateDevice[0].result)) +
      static_cast<int>(static_cast<bool>(duplicateDevice[1].result));
  const auto conflictCount =
      static_cast<int>(duplicateDevice[0].result.code == TileDebugResultCode::DeviceConflict) +
      static_cast<int>(duplicateDevice[1].result.code == TileDebugResultCode::DeviceConflict);
  assert(successCount == 1);
  assert(conflictCount == 1);
  assert(duplicateDeviceService.snapshot(701, state).modules.size() == 1);
}

void verifyTagAggregationAndMovementCues() {
  std::uint64_t now = 9000;
  TileDebugAssignments service([&now] { return now; });
  auto state = makeState(16);

  TileTagReport originTags{};
  originTags.readerState = TileTagReaderState::Stable;
  originTags.revision = 7;
  originTags.tags = {0x8EFA259Du, 0x8EFA259Du};
  const auto origin = service.heartbeat(
      800, state, "tag-origin", "tag-device-a", &originTags);
  assert(origin.result && origin.assigned && origin.assignment.mapIndex == 0);
  assert(origin.tagReaderState == TileTagReaderState::Stable);
  assert(origin.stableTags.size() == 1);
  assert(origin.stableTags.front() == 0x8EFA259Du);

  TileTagReport destinationTags{};
  destinationTags.readerState = TileTagReaderState::Stable;
  destinationTags.revision = 3;
  destinationTags.tags = {0x7A563412u, 0x8EFA259Du};
  const auto destination = service.heartbeat(
      800, state, "tag-destination", "tag-device-b", &destinationTags);
  assert(destination.result && destination.assigned &&
         destination.assignment.mapIndex == 1);

  const auto tags = service.tagSnapshot(800, state);
  assert(tags.tags.size() == 2);
  const auto shared = std::find_if(tags.tags.begin(), tags.tags.end(),
                                   [](const TileDetectedTag& tag) {
    return tag.uid == 0x8EFA259Du;
  });
  assert(shared != tags.tags.end());
  assert(shared->currentlySeen);
  assert(shared->sightings.size() == 2);
  assert(shared->sightings[0].moduleId == "tag-destination");
  assert(shared->sightings[0].tileId == "A1");
  assert(shared->sightings[1].moduleId == "tag-origin");
  assert(shared->sightings[1].tileId == "CORNER-START");

  state.phase = GamePhase::AwaitMoveConfirm;
  state.pendingMove = {true, 1, 0, 1, 1, 2, false};
  ++state.stateVersion;
  const auto blockedOriginCue = service.movementCue(800, state, "tag-origin", false);
  const auto blockedDestinationCue = service.movementCue(
      800, state, "tag-destination", false);
  assert(blockedOriginCue.mode == TileMovementCueMode::None);
  assert(blockedDestinationCue.mode == TileMovementCueMode::None);

  const auto originCue = service.movementCue(800, state, "tag-origin", true);
  const auto destinationCue = service.movementCue(800, state, "tag-destination", true);
  assert(originCue.mode == TileMovementCueMode::Departure);
  assert(originCue.playerId == 1);
  assert(originCue.revision == state.stateVersion);
  assert(destinationCue.mode == TileMovementCueMode::Destination);
  assert(destinationCue.playerId == 1);
  assert(destinationCue.revision == state.stateVersion);

  const auto destinationHeartbeat = service.heartbeat(
      800, state, "tag-destination", "tag-device-b", &destinationTags, true);
  assert(destinationHeartbeat.movementCue.mode == TileMovementCueMode::Destination);
  assert(destinationHeartbeat.movementCue.playerId == 1);

  state.pendingMove = {};
  state.phase = GamePhase::AwaitPurchase;
  ++state.stateVersion;
  const auto clearedCue = service.movementCue(800, state, "tag-destination", true);
  assert(clearedCue.mode == TileMovementCueMode::None);
  assert(clearedCue.playerId == 0);
  assert(clearedCue.revision == state.stateVersion);

  // A module lease expiration removes both its assignment and all retained
  // sightings, preventing an unplugged reader from keeping a Tag selectable.
  now += TileDebugAssignments::kLeaseMs;
  const auto expired = service.tagSnapshot(800, state);
  assert(expired.tags.empty());
  assert(service.snapshot(800, state).modules.empty());
}

}  // namespace

int main() {
  assert(std::strcmp(tileDebugAssignmentSourceName(TileDebugAssignmentSource::None), "none") == 0);
  assert(std::strcmp(tileDebugAssignmentSourceName(TileDebugAssignmentSource::Auto), "auto") == 0);
  assert(std::strcmp(tileDebugAssignmentSourceName(TileDebugAssignmentSource::Manual), "manual") == 0);
  verifyCatalogAndColors();
  verifyAutomaticAndManualAssignments();
  verifyAuthoritativeOwnershipRefresh();
  verifyLeaseBoundaryAndReclamation();
  verifyRoomReassignmentOrder();
  verifyConcurrentRegistration();
  verifyTagAggregationAndMovementCues();
  std::cout << "GRIDOPOLY_TILE_DEBUG_ASSIGNMENT_TESTS_PASS\n";
  return 0;
}
