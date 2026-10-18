#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

#include "../../Server/RaspberryPi/src/AuthorityService.h"
#include "avatar_v2_fixture.h"

#ifdef assert
#undef assert
#endif
#define assert(condition)                                                            \
  do {                                                                               \
    if (!(condition)) {                                                              \
      std::cerr << "ASSERTION FAILED: " #condition " at " << __FILE__ << ':'         \
                << __LINE__ << '\n';                                                  \
      std::abort();                                                                  \
    }                                                                                \
  } while (false)

#ifndef GRIDOPOLY_SOURCE_DIR
#error GRIDOPOLY_SOURCE_DIR is required
#endif

namespace {

using namespace gridopoly::core;
using namespace gridopoly::pi;
using namespace gridopoly::protocol;

IdentityRequest avatarRequest(std::uint8_t player, std::uint32_t requestId,
                              std::uint32_t stateVersion, std::uint16_t seatRevision,
                              AvatarRecipe recipe) {
  IdentityRequest request{};
  request.operation = IdentityOperation::ConfirmAvatar;
  request.playerId = player;
  request.requestId = requestId;
  request.expectedStateVersion = stateVersion;
  request.expectedSeatRevision = seatRevision;
  request.avatarCatalogVersion = recipe.avatarCatalogVersion;
  request.recipe = recipe;
  return request;
}

IdentityRequest nameRequest(std::uint8_t player, std::uint32_t requestId,
                            std::uint32_t stateVersion, std::uint16_t seatRevision,
                            const char* name) {
  IdentityRequest request{};
  request.operation = IdentityOperation::ConfirmName;
  request.playerId = player;
  request.requestId = requestId;
  request.expectedStateVersion = stateVersion;
  request.expectedSeatRevision = seatRevision;
  request.nameLength = static_cast<std::uint8_t>(std::strlen(name));
  std::memcpy(request.name.data(), name, request.nameLength);
  return request;
}

bool waitForAvatar(AuthorityService& authority, std::uint8_t seatId,
                   IdentitySnapshot& snapshot) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    authority.tick();
    authority.makeIdentitySnapshot(seatId, snapshot);
    if ((snapshot.seats[seatId - 1].flags & IdentitySeatAvatarFinal) != 0) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

}  // namespace

int main() {
  const auto source = std::filesystem::path(GRIDOPOLY_SOURCE_DIR);
  const auto temporary = std::filesystem::temp_directory_path() /
      "gridopoly-identity-authority-tests";
  std::error_code error;
  std::filesystem::remove_all(temporary, error);
  std::filesystem::create_directories(temporary, error);
  assert(!error);

  std::atomic<std::uint64_t> now{1900000000000ull};
  AuthorityIdentityOptions options{};
  options.identityPath = temporary / "identity.bin";
  options.avatarComponentRoot =
      source / "Assets/GridCity/Avatars/V1/runtime/components-v1";
  options.avatarAssetRoot = temporary / "assets";
  options.epochClock = [&now]() { return now.load(); };
  options.avatarComponentV2Root = temporary / "components-v2";
  avatar_v2_fixture::catalog(options.avatarComponentV2Root);

  const auto statePath = temporary / "state.bin";
  const auto metadataPath = temporary / "authority.meta";
  AuthorityService authority(statePath, metadataPath, 0x12345678u,
                             std::chrono::milliseconds(1200), options);
  assert(authority.initialize());
  const auto initialRoom = authority.roomId();
  assert(!authority.newGame(16, 250, 8));
  assert(authority.roomId() == initialRoom);
  assert(authority.newGame(16, 2, 2));
  const auto room = authority.roomId();
  auto game = authority.stateCopy();
  assert(game.phase == GamePhase::Lobby && game.playerCount == 4);
  assert(game.players[0].controller == ControllerKind::RealConsole);
  assert(game.players[1].controller == ControllerKind::RealConsole);
  assert(game.players[2].controller == ControllerKind::Bot);
  assert(game.players[3].controller == ControllerKind::Bot);
  assert(authority.isHumanSeat(1) && authority.isHumanSeat(2));
  assert(!authority.isHumanSeat(3) && !authority.activateConsoleSeat(3, "intruder"));

  IdentitySnapshot first{};
  assert(authority.makeIdentitySnapshot(1, first, true));
  assert(first.roomPhase == IdentityRoomPhase::AvatarSetup);
  assert(first.selfStage == IdentitySeatStage::AvatarSetup);
  assert(first.playerCount == 4 && first.requiredHumanMask == 0x03);
  assert(first.seats[0].recipe.avatarCatalogVersion == 0);
  assert(first.seats[2].flags & IdentitySeatBot);
  RosterSnapshot unpublishedRoster{};
  assert(authority.makeRosterSnapshot(unpublishedRoster));
  assert(unpublishedRoster.displayNames[0][0] == '\0');
  assert(unpublishedRoster.displayNames[1][0] == '\0');

  IdentitySnapshot response{};
  const auto beforeAvatarVersion = authority.stateVersion();
  auto request = avatarRequest(1, 1001, authority.stateVersion(),
                               first.seats[0].seatRevision, {1, 2, 5, 3, 4, 6});
  authority.handleIdentityRequest(1, request, response);
  assert(response.result == IdentityResultCode::Ok);
  assert(response.selfStage == IdentitySeatStage::AvatarGenerating);
  assert(response.seats[0].recipe.avatarCatalogVersion == 0);
  assert(response.stateVersion > beforeAvatarVersion);
  assert(response.stateVersion == authority.stateVersion());
  const auto generatingRevision = authority.identityRevision();
  const auto generatingStateVersion = authority.stateVersion();

  IdentitySnapshot replay{};
  authority.handleIdentityRequest(1, request, replay);
  assert(replay.result == IdentityResultCode::Ok);
  assert((replay.flags & IdentitySnapshotFlagReplay) != 0);
  assert(authority.identityRevision() == generatingRevision);
  assert(authority.stateVersion() == generatingStateVersion);
  auto collision = request;
  collision.recipe.hairPresetId = 3;
  authority.handleIdentityRequest(1, collision, response);
  assert(response.result == IdentityResultCode::RequestIdConflict);
  assert(authority.identityRevision() == generatingRevision);

  assert(waitForAvatar(authority, 1, first));
  assert(first.selfStage == IdentitySeatStage::NameSetup);
  assert(first.seats[0].avatarRevision == 1);
  assert(first.seats[0].avatarContentHash64 != 0);
  assert(first.seats[0].recipe.hairPresetId == 2);
  assert(first.stateVersion > generatingStateVersion);

  auto staleAvatar = avatarRequest(2, 2000, authority.stateVersion() - 1,
                                   first.seats[1].seatRevision, {1, 4, 6, 5, 3, 2});
  authority.handleIdentityRequest(2, staleAvatar, response);
  assert(response.result == IdentityResultCode::StateVersionStale);
  staleAvatar.requestId = 2001;
  staleAvatar.expectedStateVersion = authority.stateVersion();
  staleAvatar.expectedSeatRevision = static_cast<std::uint16_t>(
      first.seats[1].seatRevision + 1);
  authority.handleIdentityRequest(2, staleAvatar, response);
  if (response.result != IdentityResultCode::SeatRevisionStale) {
    std::cerr << "seat stale result=" << static_cast<unsigned>(response.result)
              << " expectedState=" << staleAvatar.expectedStateVersion
              << " actualState=" << authority.stateVersion()
              << " expectedSeat=" << staleAvatar.expectedSeatRevision << '\n';
  }
  assert(response.result == IdentityResultCode::SeatRevisionStale);

  auto alice = nameRequest(1, 1002, authority.stateVersion(),
                           first.seats[0].seatRevision, "Alice");
  authority.handleIdentityRequest(1, alice, response);
  assert(response.result == IdentityResultCode::Ok);
  assert(response.selfStage == IdentitySeatStage::Ready);
  assert(authority.stateCopy().phase == GamePhase::Lobby);
  assert(authority.makeRosterSnapshot(unpublishedRoster));
  assert(std::string(unpublishedRoster.displayNames[0].data()) == "Alice");
  assert(unpublishedRoster.displayNames[1][0] == '\0');

  IdentitySnapshot second{};
  assert(authority.makeIdentitySnapshot(2, second, false));
  auto secondAvatar = avatarRequest(2, 2002, authority.stateVersion(),
                                    second.seats[1].seatRevision, {1, 4, 6, 5, 3, 2});
  authority.handleIdentityRequest(2, secondAvatar, response);
  assert(response.result == IdentityResultCode::Ok);
  assert(waitForAvatar(authority, 2, second));
  auto duplicate = nameRequest(2, 2003, authority.stateVersion(),
                               second.seats[1].seatRevision, "aLiCe");
  authority.handleIdentityRequest(2, duplicate, response);
  assert(response.result == IdentityResultCode::DuplicateName);
  auto botCollision = nameRequest(2, 2004, authority.stateVersion(),
                                  second.seats[1].seatRevision, "BOT 1");
  authority.handleIdentityRequest(2, botCollision, response);
  assert(response.result == IdentityResultCode::DuplicateName);
  auto bob = nameRequest(2, 2005, authority.stateVersion(),
                         second.seats[1].seatRevision, "  Bob  ");
  authority.handleIdentityRequest(2, bob, response);
  assert(response.result == IdentityResultCode::Ok);

  IdentitySnapshot countdown{};
  for (int attempt = 0; attempt < 200; ++attempt) {
    authority.tick();
    authority.makeIdentitySnapshot(1, countdown, false);
    if (countdown.roomPhase == IdentityRoomPhase::Countdown) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(countdown.roomPhase == IdentityRoomPhase::Countdown);
  assert(countdown.countdownDeadlineEpochMs == now + 5000);
  assert(countdown.readyMask == 0x0F);
  assert(authority.stateCopy().phase == GamePhase::Lobby);
  const auto countdownDeadline = countdown.countdownDeadlineEpochMs;
  const auto countdownVersion = authority.stateVersion();
  const auto countdownRevision = authority.identityRevision();
  now += 2000;
  IdentitySnapshot bobReplay{};
  authority.handleIdentityRequest(2, bob, bobReplay);
  assert((bobReplay.flags & IdentitySnapshotFlagReplay) != 0);
  assert(bobReplay.serverEpochMs == now);
  assert(bobReplay.countdownDeadlineEpochMs == countdownDeadline);
  assert(authority.stateVersion() == countdownVersion);
  assert(authority.identityRevision() == countdownRevision);
  authority.setConsoleConnected(2, false);
  now += 2999;
  authority.tick();
  assert(authority.stateCopy().phase == GamePhase::Lobby);
  now += 1;
  authority.tick();
  assert(authority.stateCopy().phase == GamePhase::AwaitRoll);
  assert(authority.identityPhase() == IdentityRoomPhase::Active);

  RosterSnapshot roster{};
  assert(authority.makeRosterSnapshot(roster));
  assert(std::string(roster.displayNames[0].data()) == "Alice");
  assert(std::string(roster.displayNames[1].data()) == "Bob");
  assert(std::string(roster.displayNames[2].data()) == "Bot 1");
  assert(std::string(roster.displayNames[3].data()) == "Bot 2");
  assert(authority.flush());

  const auto activeVersion = authority.stateVersion();
  const auto activeIdentityRevision = authority.identityRevision();
  {
    auto restoreOptions = options;
    restoreOptions.avatarCatalogVersion = 2;
    AuthorityService restored(statePath, metadataPath, 0,
                              std::chrono::milliseconds(1200), restoreOptions);
    assert(restored.initialize());
    assert(restored.roomId() == room);
    assert(restored.stateVersion() >= activeVersion);
    assert(restored.identityRevision() == activeIdentityRevision);
    assert(restored.identityPhase() == IdentityRoomPhase::Active);
    assert(restored.isHumanSeat(1) && !restored.isHumanSeat(3));
    IdentitySnapshot restoredSnapshot{};
    assert(restored.makeIdentitySnapshot(1, restoredSnapshot, true));
    assert(restoredSnapshot.roomPhase == IdentityRoomPhase::Active);
    assert(restoredSnapshot.avatarCatalogVersion == 1);
    assert(restoredSnapshot.seats[0].recipe.avatarCatalogVersion == 1);
    assert(restoredSnapshot.seats[0].avatarContentHash64 ==
           countdown.seats[0].avatarContentHash64);
  }

  assert(authority.newGame(24, 3, 1));
  assert(authority.roomId() != room);
  IdentitySnapshot fresh{};
  assert(authority.makeIdentitySnapshot(1, fresh, false));
  assert(fresh.roomPhase == IdentityRoomPhase::AvatarSetup);
  assert(fresh.selfStage == IdentitySeatStage::AvatarSetup);
  assert(fresh.requiredHumanMask == 0x07 && fresh.nameFinalMask == 0x08);
  assert((fresh.seats[0].flags & IdentitySeatNameFinal) == 0);

  auto v2Options = options;
  v2Options.identityPath = temporary / "v2-identity.bin";
  v2Options.avatarCatalogVersion = 2;
  std::uint32_t v2Room = 0;
  std::uint64_t v2Hash = 0;
  {
    AuthorityService v2Authority(temporary / "v2-state.bin", temporary / "v2-meta.bin", 0,
                                 std::chrono::milliseconds(1200), v2Options);
    assert(v2Authority.initialize());
    assert(v2Authority.newGame(16, 1, 1));
    IdentitySnapshot v2Snapshot{};
    assert(waitForAvatar(v2Authority, 2, v2Snapshot));
    assert(v2Authority.makeIdentitySnapshot(1, v2Snapshot));
    assert(v2Snapshot.avatarCatalogVersion == 2);
    auto request = avatarRequest(1, 1001, v2Snapshot.stateVersion,
        v2Snapshot.seats[0].seatRevision, {1,1,1,1,8,1});
    v2Authority.handleIdentityRequest(1, request, v2Snapshot);
    assert(v2Snapshot.result == IdentityResultCode::CatalogMismatch);
    request = avatarRequest(1, 1002, v2Snapshot.stateVersion,
        v2Snapshot.seats[0].seatRevision, {2,1,1,1,8,1});
    v2Authority.handleIdentityRequest(1, request, v2Snapshot);
    assert(v2Snapshot.result == IdentityResultCode::Ok);
    assert(waitForAvatar(v2Authority, 1, v2Snapshot));
    assert(v2Snapshot.seats[0].recipe.avatarCatalogVersion == 2);
    v2Room = v2Authority.roomId();
    v2Hash = v2Snapshot.seats[0].avatarContentHash64;
    assert(v2Authority.flush());
  }
  {
    v2Options.avatarCatalogVersion = 1;
    AuthorityService restored(temporary / "v2-state.bin", temporary / "v2-meta.bin", 0,
                              std::chrono::milliseconds(1200), v2Options);
    assert(restored.initialize());
    IdentitySnapshot snapshot{};
    assert(restored.makeIdentitySnapshot(1, snapshot));
    assert(restored.roomId() == v2Room && snapshot.avatarCatalogVersion == 2);
    assert(snapshot.seats[0].recipe.avatarCatalogVersion == 2 &&
           snapshot.seats[0].avatarContentHash64 == v2Hash);
    assert(restored.newGame(16, 1, 1));
    assert(restored.makeIdentitySnapshot(1, snapshot));
    assert(snapshot.avatarCatalogVersion == 1);
  }
  {
    v2Options.avatarCatalogVersion = 2;
    v2Options.avatarComponentV2Root = temporary / "missing-catalog";
    v2Options.identityPath = temporary / "missing-identity.bin";
    AuthorityService unavailable(temporary / "missing-state.bin", temporary / "missing-meta.bin", 0,
                                 std::chrono::milliseconds(1200), v2Options);
    assert(!unavailable.initialize());
  }

  std::cout << "GRIDOPOLY_IDENTITY_AUTHORITY_TESTS_PASS\n";
  return 0;
}
