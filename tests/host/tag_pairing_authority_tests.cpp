#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "../../Server/RaspberryPi/src/AuthorityService.h"

#ifndef GRIDOPOLY_SOURCE_DIR
#error GRIDOPOLY_SOURCE_DIR is required
#endif

namespace {
using namespace gridopoly::core;
using namespace gridopoly::pi;
using namespace gridopoly::protocol;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

std::filesystem::path testRoot;

std::uint32_t deviceHash(const std::array<std::uint8_t, 6>& mac) {
  std::uint32_t hash = 2166136261u;
  for (const auto octet : mac) { hash ^= octet; hash *= 16777619u; }
  return hash ? hash : 1;
}

struct Device {
  std::uint32_t id{};
  PairRequest pair{};
};

Device device(const std::array<std::uint8_t, 6>& mac) {
  Device out; out.id = deviceHash(mac);
  out.pair.deviceNonce = 123; out.pair.capabilities = kCapabilityWifiUdp | kCapabilityTagPairing;
  constexpr char hex[] = "0123456789ABCDEF";
  std::memcpy(out.pair.displayName, "GP-", 3);
  for (unsigned i = 0; i < 6; ++i) {
    out.pair.displayName[3 + i * 2] = hex[mac[i] >> 4];
    out.pair.displayName[4 + i * 2] = hex[mac[i] & 15];
  }
  return out;
}

Device device(std::uint8_t index) { return device({0xDC, 0xB4, 0xD9, 0x02, 0xD1, index}); }

std::string deviceJson(const std::string& json, std::uint32_t id) {
  const auto start = json.find("{\"deviceId\":" + std::to_string(id) + ',');
  CHECK(start != std::string::npos);
  const auto end = json.find('}', start);
  CHECK(end != std::string::npos);
  return json.substr(start, end - start + 1);
}

std::vector<char> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Fixture {
  std::filesystem::path directory;
  std::atomic<std::uint64_t> now{1900000000000ull};
  AuthorityIdentityOptions options;
  std::unique_ptr<AuthorityService> authority;

  explicit Fixture(const char* name) : directory(testRoot / name) {
    CHECK(directory.parent_path() == testRoot && !std::filesystem::exists(directory));
    CHECK(std::filesystem::create_directories(directory));
    options.identityPath = directory / "identity.bin";
    options.avatarComponentRoot = std::filesystem::path(GRIDOPOLY_SOURCE_DIR) /
        "Assets/GridCity/Avatars/V1/runtime/components-v1";
    options.avatarAssetRoot = directory / "avatars";
    options.epochClock = [this] { return now.load(); };
    open();
  }
  void open() {
    authority = std::make_unique<AuthorityService>(directory / "state.bin",
        directory / "authority.meta", 0x76543210u, std::chrono::milliseconds(1200), options);
    CHECK(authority->initialize());
  }
  void restart() { CHECK(authority->flush()); authority.reset(); open(); }
  void advance(std::uint64_t milliseconds) { now += milliseconds; authority->tick(); }
  bool discover(const Device& value) { return authority->observeConsoleDevice(value.id, value.pair); }
  std::vector<Device> automatic(unsigned humans) {
    std::vector<Device> devices;
    for (unsigned i = 0; i < humans; ++i) {
      devices.push_back(device(static_cast<std::uint8_t>(i + 1)));
      CHECK(discover(devices.back()));
    }
    CHECK(authority->newAutoGame(32));
    std::sort(devices.begin(), devices.end(), [](const Device& a, const Device& b) { return a.id < b.id; });
    return devices;
  }
  TagPairSnapshot snapshot(std::uint8_t self = 1) { return authority->makeTagPairSnapshot(self); }
  TagPairReaderObservation report(std::uint32_t sequence, std::vector<std::uint32_t> tags = {}) {
    const auto current = snapshot();
    TagPairReaderObservation out;
    out.moduleId = "reader-A"; out.deviceId = "tile-A"; out.bootId = "boot-A";
    out.stable = true; out.hasFreshness = true; out.fresh = true; out.mapIndex = 8;
    out.scanSequence = sequence; out.tagRevision = sequence; out.tags = std::move(tags);
    out.roomId = current.roomId; out.sessionId = current.sessionId;
    return out;
  }
  void scan(std::uint32_t sequence, std::vector<std::uint32_t> tags = {}) {
    authority->observeTagReader(authority->roomId(), report(sequence, std::move(tags)));
  }
  void bindFirst(std::uint32_t uid) {
    scan(1); scan(2); scan(3, {uid});
    CHECK(snapshot().state == TagPairState::SuccessRemove && snapshot().boundMask == 1);
  }
};

IdentityRequest avatarRequest(AuthorityService& authority, std::uint8_t player, std::uint32_t id) {
  IdentitySnapshot current;
  CHECK(authority.makeIdentitySnapshot(player, current));
  IdentityRequest out;
  out.operation = IdentityOperation::ConfirmAvatar; out.playerId = player; out.requestId = id;
  out.expectedStateVersion = current.stateVersion;
  out.expectedSeatRevision = current.seats[player - 1].seatRevision;
  out.recipe = {1, 2, 5, 3, 4, 6}; out.avatarCatalogVersion = 1;
  return out;
}

IdentitySnapshot waitForAvatar(AuthorityService& authority, std::uint8_t player) {
  IdentitySnapshot out;
  for (unsigned attempt = 0; attempt < 500; ++attempt) {
    authority.tick(); CHECK(authority.makeIdentitySnapshot(player, out));
    if (out.seats[player - 1].flags & IdentitySeatAvatarFinal) return out;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  throw std::runtime_error("avatar worker did not complete within five seconds");
}

IdentitySnapshot finishFirstIdentityAndStart(Fixture& fixture) {
  auto& authority = *fixture.authority;
  auto avatar = avatarRequest(authority, 1, 8001);
  IdentitySnapshot identity;
  authority.handleIdentityRequest(1, avatar, identity);
  CHECK(identity.result == IdentityResultCode::Ok);
  identity = waitForAvatar(authority, 1);
  IdentityRequest name;
  name.operation = IdentityOperation::ConfirmName; name.playerId = 1; name.requestId = 8002;
  name.expectedStateVersion = identity.stateVersion;
  name.expectedSeatRevision = identity.seats[0].seatRevision;
  name.nameLength = 5; std::strcpy(name.name.data(), "Alice");
  authority.handleIdentityRequest(1, name, identity);
  CHECK(identity.result == IdentityResultCode::Ok);
  fixture.advance(1000);
  CHECK(authority.identityPhase() == IdentityRoomPhase::Countdown);
  fixture.advance(5000);
  CHECK(authority.identityPhase() == IdentityRoomPhase::Active);
  CHECK(authority.makeIdentitySnapshot(1, identity));
  CHECK(identity.selfStage == IdentitySeatStage::Active);
  CHECK((identity.seats[0].flags & (IdentitySeatAvatarFinal | IdentitySeatNameFinal)) ==
      (IdentitySeatAvatarFinal | IdentitySeatNameFinal));
  return identity;
}

void discoveryChecksFullMacAndCapability() {
  Fixture f("discovery");
  const auto valid = device({0xDC, 0xB4, 0xD9, 0x02, 0xD1, 0xDC});
  CHECK(f.discover(valid));
  CHECK(deviceJson(f.authority->playerDevicesJson(), valid.id).find("\"eligible\":true") != std::string::npos);
  auto lower = valid;
  for (auto& c : lower.pair.displayName) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
  lower.pair.displayName[0] = 'G'; lower.pair.displayName[1] = 'P';
  ++lower.pair.deviceNonce;
  CHECK(f.discover(lower));
  CHECK(deviceJson(f.authority->playerDevicesJson(), valid.id).find(valid.pair.displayName) != std::string::npos);
  auto invalid = device(50);
  CHECK(!f.authority->observeConsoleDevice(invalid.id ^ 0x100u, invalid.pair));
  invalid.pair.displayName[3] = 'Z'; CHECK(!f.discover(invalid));
  invalid = device(50); invalid.pair.displayName[14] = 0; CHECK(!f.discover(invalid));
  invalid = device(50); invalid.pair.displayName[0] = 'X'; CHECK(!f.discover(invalid));
  CHECK(!f.authority->observeConsoleDevice(0, valid.pair));
  Device legacy; legacy.id = 42; legacy.pair.capabilities = kCapabilityWifiUdp;
  std::strcpy(legacy.pair.displayName, "Legacy Screen");
  CHECK(f.discover(legacy));
  const auto row = deviceJson(f.authority->playerDevicesJson(), 42);
  CHECK(row.find("\"online\":true") != std::string::npos);
  CHECK(row.find("\"eligible\":false") != std::string::npos);
  CHECK(f.authority->newAutoGame(32));
  CHECK(f.authority->frozenSeatForDevice(valid.id) == 1 && f.authority->frozenSeatForDevice(42) == 0);
  CHECK(f.snapshot().requiredHumanMask == 1);
}

void hashCollisionCannotStealFrozenSeat() {
  Fixture f("collision");
  // Two distinct six-byte identities with the same FNV-1a32 value.
  const auto first = device({0x02, 0xD4, 0x2D, 0xBD, 0xA1, 0x39});
  const auto second = device({0x02, 0xD4, 0xC9, 0xC4, 0xDF, 0xC6});
  CHECK(first.id == 0x92E6619Fu && first.id == second.id);
  CHECK(std::strcmp(first.pair.displayName, second.pair.displayName) != 0);
  CHECK(f.discover(first) && f.authority->newAutoGame(32));
  CHECK(f.authority->frozenSeatForDevice(first.id) == 1);
  CHECK(!f.discover(second));
  CHECK(f.authority->frozenSeatForDevice(first.id) == 0);
  CHECK(!f.discover(first)); // Quarantined collision cannot oscillate between claimants.
  CHECK(deviceJson(f.authority->playerDevicesJson(), first.id).find("\"eligible\":false") != std::string::npos);
  const auto room = f.authority->roomId();
  CHECK(!f.authority->newAutoGame(32) && f.authority->roomId() == room);
  CHECK(f.authority->stateCopy().players[0].controller == ControllerKind::RealConsole);
}

void zeroFiveAndWrongBoardRejectWithoutChangingRoom() {
  Fixture f("rejected-counts");
  const auto room = f.authority->roomId();
  const auto version = f.authority->stateVersion();
  const auto initialMetadata = readBytes(f.directory / "authority.meta");
  CHECK(!f.authority->newAutoGame(32));
  CHECK(f.authority->roomId() == room && f.authority->stateVersion() == version);
  std::vector<Device> devices;
  for (std::uint8_t i = 1; i <= 5; ++i) { devices.push_back(device(i)); CHECK(f.discover(devices.back())); }
  CHECK(!f.authority->newAutoGame(32));
  CHECK(f.authority->roomId() == room && f.authority->stateVersion() == version);
  const auto unsupported = f.authority->newAutoGame(23);
  CHECK(!unsupported && unsupported.code == gridopoly::core::ErrorCode::InvalidArgument);
  CHECK(f.authority->roomId() == room && f.authority->stateVersion() == version);
  CHECK(readBytes(f.directory / "authority.meta") == initialMetadata);
  f.now += 15000; // The exact online lease boundary is ineligible.
  CHECK(!f.authority->newAutoGame(32) && f.authority->roomId() == room);
  for (unsigned i = 0; i < 4; ++i) f.authority->touchConsoleDevice(devices[i].id);
  CHECK(f.authority->newAutoGame(32));
  CHECK(f.authority->roomId() != room && f.snapshot().requiredHumanMask == 15);
}

void oneToFourConsolesProduceFourStableSeats() {
  for (unsigned count = 1; count <= 4; ++count) {
    const auto name = "seat-count-" + std::to_string(count);
    Fixture f(name.c_str());
    const auto devices = f.automatic(count);
    const auto state = f.authority->stateCopy();
    CHECK(state.phase == GamePhase::Lobby && state.playerCount == 4 && state.board->tileCount == 32);
    CHECK(f.authority->automaticPlayers());
    CHECK(f.snapshot().requiredHumanMask == (1u << count) - 1u);
    for (unsigned i = 0; i < 4; ++i) {
      CHECK(state.players[i].controller == (i < count ? ControllerKind::RealConsole : ControllerKind::Bot));
      CHECK(f.authority->isHumanSeat(static_cast<std::uint8_t>(i + 1)) == (i < count));
      if (i < count) CHECK(f.authority->frozenSeatForDevice(devices[i].id) == i + 1);
    }
    const auto room = f.authority->roomId();
    f.now += 20000;
    for (unsigned i = 0; i < count; ++i) {
      auto rebooted = devices[i]; rebooted.pair.deviceNonce += 99;
      CHECK(f.discover(rebooted));
      CHECK(f.authority->frozenSeatForDevice(rebooted.id) == i + 1);
      f.authority->setConsoleConnected(static_cast<std::uint8_t>(i + 1), true);
    }
    const auto newcomer = device(250); CHECK(f.discover(newcomer));
    CHECK(f.authority->frozenSeatForDevice(newcomer.id) == 0);
    CHECK(f.authority->roomId() == room && f.authority->stateCopy().playerCount == 4);
  }
}

void tagGateAllowsBoundPlayersToEditDuringNextPairing() {
  Fixture f("identity-gate"); f.automatic(2);
  IdentitySnapshot identity;
  CHECK(f.authority->makeIdentitySnapshot(1, identity));
  CHECK(identity.selfStage == IdentitySeatStage::TagPairing);
  auto avatar = avatarRequest(*f.authority, 1, 100);
  f.authority->handleIdentityRequest(1, avatar, identity);
  CHECK(identity.result == IdentityResultCode::NotAllowed && identity.selfStage == IdentitySeatStage::TagPairing);
  CHECK((identity.seats[0].flags & IdentitySeatAvatarGenerating) == 0);
  IdentityRequest query;
  query.operation = IdentityOperation::Query; query.playerId = 1; query.requestId = 101;
  f.authority->handleIdentityRequest(1, query, identity);
  CHECK(identity.result == IdentityResultCode::Ok && identity.selfStage == IdentitySeatStage::TagPairing);
  CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
  CHECK(f.authority->tagPairingBlocksArrival("unassigned-reader"));
  f.bindFirst(0xABC001u);
  CHECK((f.snapshot().flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)) ==
      (TagPairFlagSelfBound | TagPairFlagCanEditIdentity));
  CHECK(f.authority->makeIdentitySnapshot(1, identity));
  CHECK(identity.selfStage == IdentitySeatStage::AvatarSetup);
  avatar = avatarRequest(*f.authority, 1, 102);
  f.authority->handleIdentityRequest(1, avatar, identity);
  CHECK(identity.result == IdentityResultCode::Ok);
  f.advance(1000);
  CHECK(f.snapshot().activePlayerId == 2 && f.snapshot().boundMask == 1);
  identity = waitForAvatar(*f.authority, 1);
  CHECK(identity.selfStage == IdentitySeatStage::NameSetup);
  IdentityRequest name;
  name.operation = IdentityOperation::ConfirmName; name.playerId = 1; name.requestId = 103;
  name.expectedStateVersion = identity.stateVersion; name.expectedSeatRevision = identity.seats[0].seatRevision;
  std::strcpy(name.name.data(), "Alice"); name.nameLength = 5;
  f.authority->handleIdentityRequest(1, name, identity);
  CHECK(identity.result == IdentityResultCode::Ok && identity.selfStage == IdentitySeatStage::Ready);
  CHECK(f.authority->makeIdentitySnapshot(2, identity) && identity.selfStage == IdentitySeatStage::TagPairing);
  CHECK(f.authority->identityPhase() == IdentityRoomPhase::AvatarSetup);
  CHECK(f.authority->stateCopy().phase == GamePhase::Lobby);
  CHECK(!f.authority->confirmTaggedArrival(1, 0xABC001u, 8, f.authority->stateVersion()));
  CHECK(f.authority->stateCopy().phase == GamePhase::Lobby);
}

void heartbeatRoomFenceAndPairRevisionAreIndependentOfGameVersion() {
  Fixture f("room-and-version"); f.automatic(4);
  const auto room = f.authority->roomId();
  const auto gameVersion = f.authority->stateVersion();
  const auto identityRevision = f.authority->identityRevision();
  auto report = f.report(1, {700});
  f.authority->observeTagReader(room + 1, report);
  CHECK(f.snapshot().state == TagPairState::WaitingReader);
  f.authority->observeTagReader(room, report);
  CHECK(f.snapshot().state == TagPairState::ClearRequired);
  CHECK(f.authority->identityRevision() > identityRevision);
  const auto pairRevision = f.authority->tagPairingRevision();
  report = f.report(2); ++report.roomId;
  f.authority->observeTagReader(room, report);
  f.scan(3, {700}); CHECK(f.snapshot().boundMask == 0);
  f.scan(4); f.scan(5, {700});
  CHECK(f.snapshot().boundMask == 1 && f.authority->tagPairingRevision() > pairRevision);
  CHECK(f.authority->stateVersion() == gameVersion); // Tag observation is not a game action.
  const auto beforeNewRoom = f.snapshot();
  CHECK(f.authority->newAutoGame(32));
  f.authority->observeTagReader(room, report);
  CHECK(f.authority->roomId() != room && f.snapshot().boundMask == 0);
  TagPairRequest old{TagPairOperation::Retry, 1, 909, room,
                     beforeNewRoom.sessionId, beforeNewRoom.revision};
  TagPairSnapshot response;
  f.authority->handleTagPairRequest(1, old, response);
  CHECK(response.result == TagPairResultCode::RoomStale);
}

void metadataFailureRollsBackUidAndRevision() {
  Fixture f("failed-persistence"); f.automatic(1);
  f.scan(1); f.scan(2);
  const auto previous = f.authority->playerTagBindings();
  const auto durable = readBytes(f.directory / "authority.meta");
  const auto blockedTemporary = f.directory / "authority.meta.tmp";
  CHECK(std::filesystem::create_directory(blockedTemporary));
  f.scan(3, {0xBEEFu});
  CHECK(f.snapshot().result == TagPairResultCode::PersistFailed);
  CHECK(f.snapshot().boundMask == 0);
  CHECK(f.authority->playerTagBindings().revision == previous.revision);
  CHECK(f.authority->playerTagBindings().playerTagUids == previous.playerTagUids);
  CHECK(readBytes(f.directory / "authority.meta") == durable);
  CHECK(std::filesystem::remove(blockedTemporary));
  f.scan(4, {0xBEEFu}); CHECK(f.snapshot().boundMask == 0);
  f.scan(5); f.scan(6, {0xBEEFu});
  CHECK(f.snapshot().boundMask == 1 && f.authority->tagBindingRevision() != previous.revision);
  CHECK(readBytes(f.directory / "authority.meta") != durable);
  f.restart();
  CHECK(f.authority->playerTagBindings().playerTagUids[0] == 0xBEEFu);
}

void automaticGameRejectsManualBindingBypass() {
  Fixture f("manual-binding-gate"); f.automatic(2);
  const auto initial = f.authority->playerTagBindings();
  const auto metadata = readBytes(f.directory / "authority.meta");
  auto result = f.authority->setPlayerTagBinding(1, 0x55AAu, initial.revision);
  CHECK(result.code == PlayerTagBindingResultCode::NotAllowed && !result.changed);
  CHECK(f.snapshot().boundMask == 0);
  CHECK(f.authority->playerTagBindings().revision == initial.revision);
  CHECK(readBytes(f.directory / "authority.meta") == metadata);
  f.bindFirst(0x55AAu);
  const auto bound = f.authority->playerTagBindings();
  result = f.authority->clearPlayerTagBinding(1, bound.revision);
  CHECK(result.code == PlayerTagBindingResultCode::NotAllowed && !result.changed);
  result = f.authority->setPlayerTagBinding(2, 0x55AAu, bound.revision);
  CHECK(result.code == PlayerTagBindingResultCode::NotAllowed && !result.changed);
  result = f.authority->setPlayerTagBinding(2, 0xAA55u, bound.revision);
  CHECK(result.code == PlayerTagBindingResultCode::NotAllowed && !result.changed);
  CHECK(f.authority->playerTagBindings().playerTagUids == bound.playerTagUids);
  CHECK(f.authority->playerTagBindings().revision == bound.revision);
  CHECK(f.snapshot().boundMask == 1);
}

void metadataRestartRestoresFrozenIdentityAndUidButNotSession() {
  Fixture f("restart"); const auto devices = f.automatic(2);
  f.bindFirst(0x1234ABCDu); f.advance(1000);
  const auto before = f.snapshot(2);
  const auto bindingRevision = f.authority->tagBindingRevision();
  const auto oldRequest = TagPairRequest{TagPairOperation::Retry, 2, 501, before.roomId,
                                       before.sessionId, before.revision};
  f.restart();
  CHECK(f.authority->automaticPlayers() && f.authority->roomId() == before.roomId);
  CHECK(f.authority->stateCopy().playerCount == 4);
  CHECK(f.authority->tagBindingRevision() == bindingRevision);
  CHECK(f.authority->playerTagBindings().playerTagUids[0] == 0x1234ABCDu);
  const auto restored = f.authority->makeTagPairSnapshot(1, true);
  CHECK(restored.boundMask == 1 && restored.activePlayerId == 2);
  CHECK(restored.flags & TagPairFlagResync);
  CHECK(restored.flags & TagPairFlagCanEditIdentity);
  CHECK(restored.sessionId != before.sessionId && restored.moduleId[0] == 0);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
  TagPairSnapshot response;
  f.authority->handleTagPairRequest(2, oldRequest, response);
  CHECK(response.result == TagPairResultCode::SessionStale);
  for (unsigned i = 0; i < devices.size(); ++i) {
    CHECK(f.authority->frozenSeatForDevice(devices[i].id) == i + 1);
    const auto offline = deviceJson(f.authority->playerDevicesJson(), devices[i].id);
    CHECK(offline.find("\"online\":false") != std::string::npos);
    auto reconnected = devices[i]; reconnected.pair.deviceNonce += 1;
    CHECK(f.discover(reconnected));
    CHECK(f.authority->frozenSeatForDevice(reconnected.id) == i + 1);
  }
  auto staleReport = f.report(4, {555}); staleReport.sessionId = before.sessionId;
  f.authority->observeTagReader(before.roomId, staleReport);
  CHECK(f.snapshot().boundMask == 1);
  f.scan(5, {555}); CHECK(f.snapshot().boundMask == 1);
  f.scan(6); f.scan(7, {555});
  CHECK(f.snapshot().boundMask == 3 && f.authority->playerTagBindings().playerTagUids[0] == 0x1234ABCDu);
}

void pendingRemovalSurvivesRestartUntilFreshNewSessionEmpty() {
  for (bool zeroSession : {false, true}) {
    Fixture f(zeroSession ? "pending-restart-zero" : "pending-restart-old"); f.automatic(1);
    f.bindFirst(0xCAFE1234u);
    const auto oldCue = f.authority->moduleTagPairSnapshot("reader-A");
    const auto identity = finishFirstIdentityAndStart(f);
    const auto binding = f.authority->playerTagBindings();
    const auto room = f.authority->roomId();
    f.restart();
    CHECK(f.authority->roomId() == room && f.authority->identityPhase() == IdentityRoomPhase::Active);
    CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
    IdentitySnapshot restored;
    CHECK(f.authority->makeIdentitySnapshot(1, restored));
    CHECK(restored.selfStage == IdentitySeatStage::Active);
    CHECK(std::string(f.authority->stateCopy().players[0].name) == "Alice");
    CHECK(restored.seats[0].avatarContentHash64 == identity.seats[0].avatarContentHash64);
    CHECK((restored.seats[0].flags & (IdentitySeatAvatarFinal | IdentitySeatNameFinal)) ==
        (IdentitySeatAvatarFinal | IdentitySeatNameFinal));

    auto report = f.report(1, {0xCAFE1234u});
    report.sessionId = zeroSession ? 0 : oldCue.sessionId;
    f.authority->observeTagReader(room, report);
    const auto newCue = f.authority->moduleTagPairSnapshot("reader-A");
    CHECK(newCue.state == TagPairState::SuccessRemove && newCue.activePlayerId == 1);
    CHECK(newCue.sessionId != 0 && newCue.sessionId != oldCue.sessionId);
    CHECK(newCue.tagUid == 0xCAFE1234u && newCue.expiresInMs > 0);
    CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
    const auto durable = readBytes(f.directory / "authority.meta");

    report.scanSequence = 2; report.tags.clear(); report.sessionId = oldCue.sessionId;
    f.authority->observeTagReader(room, report);
    CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::SuccessRemove);
    CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
    report.scanSequence = 3; report.sessionId = newCue.sessionId; report.fresh = false;
    f.authority->observeTagReader(room, report);
    // A missing fresh scan can hide the physical cue; it must not release the arrival fence.
    CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
    CHECK(readBytes(f.directory / "authority.meta") == durable);
    report.scanSequence = 4; report.tags = {0xCAFE1234u}; report.fresh = true;
    f.authority->observeTagReader(room, report);
    CHECK(f.authority->moduleTagPairSnapshot("reader-A").sessionId == newCue.sessionId);
    CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::SuccessRemove);
    report.scanSequence = 5; report.tags.clear();
    f.authority->observeTagReader(room, report);
    CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
    CHECK(!f.authority->tagPairingBlocksArrival("reader-A"));
    CHECK(f.authority->playerTagBindings().playerTagUids == binding.playerTagUids);
    CHECK(f.authority->tagBindingRevision() == binding.revision);
    f.restart();
    report = f.report(1, {0xCAFE1234u}); report.sessionId = 0;
    f.authority->observeTagReader(room, report);
    CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
    CHECK(!f.authority->tagPairingBlocksArrival("reader-A"));
    CHECK(f.authority->makeIdentitySnapshot(1, restored));
    CHECK(restored.selfStage == IdentitySeatStage::Active);
    CHECK(std::string(f.authority->stateCopy().players[0].name) == "Alice");
    CHECK(restored.seats[0].avatarContentHash64 == identity.seats[0].avatarContentHash64);
    CHECK(f.authority->playerTagBindings().playerTagUids == binding.playerTagUids);
  }
}

void failedRemovalPersistenceRetainsFenceUntilAnotherFreshScan() {
  Fixture f("pending-removal-write-failure"); f.automatic(1);
  f.bindFirst(0xDDCCBBAAu);
  const auto oldSession = f.authority->moduleTagPairSnapshot("reader-A").sessionId;
  finishFirstIdentityAndStart(f);
  f.restart();
  auto report = f.report(1, {0xDDCCBBAAu}); report.sessionId = 0;
  f.authority->observeTagReader(f.authority->roomId(), report);
  const auto restoredCue = f.authority->moduleTagPairSnapshot("reader-A");
  CHECK(restoredCue.state == TagPairState::SuccessRemove && restoredCue.sessionId != oldSession);
  const auto binding = f.authority->playerTagBindings();
  const auto durable = readBytes(f.directory / "authority.meta");
  const auto blockedTemporary = f.directory / "authority.meta.tmp";
  CHECK(std::filesystem::create_directory(blockedTemporary));
  report.scanSequence = 2; report.sessionId = restoredCue.sessionId; report.tags.clear();
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::SuccessRemove);
  CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
  CHECK(readBytes(f.directory / "authority.meta") == durable);
  CHECK(std::filesystem::remove(blockedTemporary));
  f.authority->observeTagReader(f.authority->roomId(), report); // Same scan cannot retry the disk commit.
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::SuccessRemove);
  CHECK(readBytes(f.directory / "authority.meta") == durable);
  report.scanSequence = 3; report.sessionId = oldSession;
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
  report.scanSequence = 4; report.sessionId = restoredCue.sessionId; report.fresh = false;
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->tagPairingBlocksArrival("reader-A"));
  CHECK(readBytes(f.directory / "authority.meta") == durable);
  report.scanSequence = 5; report.tags = {0xDDCCBBAAu}; report.fresh = true;
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::SuccessRemove);
  report.scanSequence = 6; report.tags.clear();
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
  CHECK(!f.authority->tagPairingBlocksArrival("reader-A"));
  CHECK(readBytes(f.directory / "authority.meta") != durable);
  CHECK(f.authority->playerTagBindings().playerTagUids == binding.playerTagUids);
  CHECK(f.authority->tagBindingRevision() == binding.revision);
  f.restart();
  report = f.report(1, {0xDDCCBBAAu}); report.sessionId = 0;
  f.authority->observeTagReader(f.authority->roomId(), report);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
  CHECK(!f.authority->tagPairingBlocksArrival("reader-A"));
  CHECK(f.authority->playerTagBindings().playerTagUids == binding.playerTagUids);
}

void reusedReaderClearsPriorRemovalBeforeRetryingAnotherReader() {
  Fixture f("reused-reader-switch"); f.automatic(2);
  f.bindFirst(0xAA1111u);
  const auto firstSession = f.snapshot().sessionId;
  const auto metadataPath = f.directory / "authority.meta";
  const auto beforeEmpty = readBytes(metadataPath);
  f.advance(1000);
  auto second = f.snapshot(2);
  CHECK(second.activePlayerId == 2 && std::string(second.moduleId.data()) == "reader-A");
  CHECK(second.sessionId != firstSession && second.state == TagPairState::ClearRequired);
  f.scan(4);
  CHECK(f.snapshot(2).state == TagPairState::AwaitingTag && f.snapshot().boundMask == 1);
  const bool firstRemovalSaved = readBytes(metadataPath) != beforeEmpty;

  // Deliberately old mtime exposes redundant replacement writes without sleeps
  // or dependence on the filesystem's timestamp resolution.
  const auto marker = std::filesystem::last_write_time(metadataPath) - std::chrono::hours(24);
  std::filesystem::last_write_time(metadataPath, marker);
  f.scan(4); f.scan(5);
  const bool repeatedEmptyDidNotWrite = std::filesystem::last_write_time(metadataPath) == marker;
  auto reportB = f.report(1);
  reportB.moduleId = "reader-B"; reportB.deviceId = "tile-B"; reportB.bootId = "boot-B";
  reportB.sessionId = 0;
  f.authority->observeTagReader(f.authority->roomId(), reportB);
  second = f.snapshot(2);
  TagPairRequest retry{TagPairOperation::Retry, 2, 9100, second.roomId,
                       second.sessionId, second.revision};
  TagPairSnapshot response;
  f.authority->handleTagPairRequest(2, retry, response);
  CHECK(response.result == TagPairResultCode::Ok && std::string(response.moduleId.data()) == "reader-B");
  CHECK(response.sessionId != second.sessionId);
  reportB.sessionId = response.sessionId; reportB.scanSequence = 2;
  f.authority->observeTagReader(f.authority->roomId(), reportB);
  CHECK(f.snapshot(2).state == TagPairState::AwaitingTag);
  const bool unusedReaderEmptyDidNotWrite = std::filesystem::last_write_time(metadataPath) == marker;
  reportB.scanSequence = 3; reportB.tags = {0xBB2222u};
  f.authority->observeTagReader(f.authority->roomId(), reportB);
  CHECK(f.snapshot(2).state == TagPairState::SuccessRemove && f.snapshot().boundMask == 3);
  f.advance(1000);
  CHECK(f.snapshot().state == TagPairState::Complete);
  reportB.scanSequence = 4; reportB.tags.clear();
  f.authority->observeTagReader(f.authority->roomId(), reportB);
  CHECK(f.authority->moduleTagPairSnapshot("reader-B").state == TagPairState::Disabled);

  for (std::uint8_t player = 1; player <= 2; ++player) {
    auto avatar = avatarRequest(*f.authority, player, 9200 + player * 2);
    IdentitySnapshot identity;
    f.authority->handleIdentityRequest(player, avatar, identity);
    CHECK(identity.result == IdentityResultCode::Ok);
    identity = waitForAvatar(*f.authority, player);
    IdentityRequest name;
    name.operation = IdentityOperation::ConfirmName; name.playerId = player;
    name.requestId = 9201 + player * 2; name.expectedStateVersion = identity.stateVersion;
    name.expectedSeatRevision = identity.seats[player - 1].seatRevision;
    const char* text = player == 1 ? "Alice" : "Bob";
    name.nameLength = static_cast<std::uint8_t>(std::strlen(text)); std::strcpy(name.name.data(), text);
    f.authority->handleIdentityRequest(player, name, identity);
    CHECK(identity.result == IdentityResultCode::Ok);
  }
  CHECK(f.authority->identityPhase() == IdentityRoomPhase::Countdown);
  f.advance(5000);
  CHECK(f.authority->identityPhase() == IdentityRoomPhase::Active);
  const bool blockedBeforeRestart = f.authority->tagPairingBlocksArrival("reader-A");
  CHECK(!f.authority->tagPairingBlocksArrival("reader-B"));
  const auto bindings = f.authority->playerTagBindings();
  f.restart();
  const bool blockedAfterRestart = f.authority->tagPairingBlocksArrival("reader-A");
  auto reportA = f.report(1); reportA.sessionId = 0;
  f.authority->observeTagReader(f.authority->roomId(), reportA);
  const auto restoredCue = f.authority->moduleTagPairSnapshot("reader-A");
  CHECK(f.authority->playerTagBindings().playerTagUids == bindings.playerTagUids);
  CHECK(f.authority->identityPhase() == IdentityRoomPhase::Active);
  if (!firstRemovalSaved || blockedBeforeRestart || blockedAfterRestart ||
      restoredCue.state != TagPairState::Disabled || !repeatedEmptyDidNotWrite || !unusedReaderEmptyDidNotWrite) {
    std::cerr << "REUSED_READER firstRemovalSaved=" << firstRemovalSaved
              << " blockedBeforeRestart=" << blockedBeforeRestart
              << " blockedAfterRestart=" << blockedAfterRestart
              << " restoredCue=" << tagPairStateName(restoredCue.state)
              << " repeatedEmptyDidNotWrite=" << repeatedEmptyDidNotWrite
              << " unusedReaderEmptyDidNotWrite=" << unusedReaderEmptyDidNotWrite << '\n';
  }
  CHECK(firstRemovalSaved);
  CHECK(!blockedBeforeRestart && !blockedAfterRestart && restoredCue.state == TagPairState::Disabled);
  CHECK(repeatedEmptyDidNotWrite && unusedReaderEmptyDidNotWrite);
}

void reusedReaderCancellationRetainsPreviousPlayersRemovalCue() {
  Fixture f("reused-reader-cancel"); f.automatic(2);
  f.bindFirst(0x11AABBCCu);
  const auto previousCue = f.authority->moduleTagPairSnapshot("reader-A");
  const auto metadataPath = f.directory / "authority.meta";
  const auto pendingBytes = readBytes(metadataPath);
  f.advance(1000);
  auto second = f.snapshot(2);
  CHECK(second.activePlayerId == 2 && std::string(second.moduleId.data()) == "reader-A");
  CHECK(second.state == TagPairState::ClearRequired);
  TagPairRequest cancel{TagPairOperation::Cancel, 2, 9400, second.roomId,
                        second.sessionId, second.revision};
  TagPairSnapshot response;
  f.authority->handleTagPairRequest(2, cancel, response);
  CHECK(response.result == TagPairResultCode::Ok && response.state == TagPairState::Cancelled);
  auto removal = f.authority->moduleTagPairSnapshot("reader-A");
  CHECK(removal.state == TagPairState::SuccessRemove && removal.activePlayerId == 1);
  CHECK(removal.sessionId == previousCue.sessionId && removal.tagUid == 0x11AABBCCu);
  auto reportB = f.report(1);
  reportB.moduleId = "reader-B"; reportB.deviceId = "tile-B"; reportB.bootId = "boot-B";
  reportB.sessionId = 0;
  f.authority->observeTagReader(f.authority->roomId(), reportB);
  second = f.snapshot(2);
  CHECK(second.state == TagPairState::Cancelled);
  TagPairRequest retry{TagPairOperation::Retry, 2, 9401, second.roomId,
                       second.sessionId, second.revision};
  f.authority->handleTagPairRequest(2, retry, response);
  CHECK(response.result == TagPairResultCode::Ok && std::string(response.moduleId.data()) == "reader-B");
  removal = f.authority->moduleTagPairSnapshot("reader-A");
  CHECK(removal.state == TagPairState::SuccessRemove && removal.activePlayerId == 1);
  CHECK(removal.sessionId == previousCue.sessionId);
  CHECK(readBytes(metadataPath) == pendingBytes);
  CHECK(f.authority->playerTagBindings().playerTagUids[0] == 0x11AABBCCu);
  CHECK(f.authority->playerTagBindings().playerTagUids[1] == 0);
  auto takeBack = f.report(4); takeBack.sessionId = previousCue.sessionId;
  f.authority->observeTagReader(f.authority->roomId(), takeBack);
  CHECK(f.authority->moduleTagPairSnapshot("reader-A").state == TagPairState::Disabled);
  CHECK(readBytes(metadataPath) != pendingBytes);
  CHECK(f.snapshot().boundMask == 1);
}

void reusedReaderFailedEmptyWriteRequiresAnotherNewScan() {
  Fixture f("reused-reader-empty-write-failure"); f.automatic(2);
  f.bindFirst(0x331122u); f.advance(1000);
  CHECK(f.snapshot(2).state == TagPairState::ClearRequired && f.snapshot().activePlayerId == 2);
  const auto metadataPath = f.directory / "authority.meta";
  const auto durable = readBytes(metadataPath);
  const auto blockedTemporary = f.directory / "authority.meta.tmp";
  CHECK(std::filesystem::create_directory(blockedTemporary));
  f.scan(4);
  CHECK(f.snapshot(2).state == TagPairState::ClearRequired);
  CHECK(f.snapshot(2).result == TagPairResultCode::PersistFailed);
  CHECK(f.snapshot().boundMask == 1 && readBytes(metadataPath) == durable);
  CHECK(std::filesystem::remove(blockedTemporary));
  f.scan(4); // Restoring the disk does not turn the failed cached scan into new evidence.
  CHECK(f.snapshot(2).state == TagPairState::ClearRequired);
  CHECK(readBytes(metadataPath) == durable);
  f.scan(5, {0x442233u});
  CHECK(f.snapshot().boundMask == 1 && f.authority->playerTagBindings().playerTagUids[1] == 0);
  CHECK(readBytes(metadataPath) == durable);
  f.scan(6);
  CHECK(f.snapshot(2).state == TagPairState::AwaitingTag && f.snapshot(2).result == TagPairResultCode::Ok);
  CHECK(readBytes(metadataPath) != durable);
  const auto marker = std::filesystem::last_write_time(metadataPath) - std::chrono::hours(24);
  std::filesystem::last_write_time(metadataPath, marker);
  f.scan(6); f.scan(7);
  CHECK(std::filesystem::last_write_time(metadataPath) == marker);
  f.scan(8, {0x442233u});
  CHECK(f.snapshot(2).state == TagPairState::SuccessRemove && f.snapshot().boundMask == 3);
  CHECK(f.authority->playerTagBindings().playerTagUids[0] == 0x331122u);
  CHECK(f.authority->playerTagBindings().playerTagUids[1] == 0x442233u);
}

void jsonIncludesAuthoritativePairingFrozenSeatsAndWinner() {
  Fixture f("json-contract"); const auto devices = f.automatic(1);
  f.bindFirst(700);
  const auto sync = f.authority->syncJson("127.0.0.1");
  const auto state = f.authority->stateJson("127.0.0.1");
  CHECK(sync == state);
  CHECK(sync.find("\"winnerPlayerId\":0") != std::string::npos);
  CHECK(sync.find("\"tagPairing\":") != std::string::npos);
  CHECK(sync.find("\"frozenDevices\":[{\"deviceId\":" + std::to_string(devices[0].id)) != std::string::npos);
  CHECK(sync.find("\"seatId\":1") != std::string::npos);
  const auto pairing = f.authority->tagPairingJson();
  for (const char* field : {"enabled", "roomId", "revision", "sessionId", "state", "result",
      "requestId", "replay", "activePlayerId", "playerName", "moduleId", "readerMapIndex",
      "readerTagRevision", "expiresInMs", "requiredHumanMask", "boundMask", "identityEditableMask",
      "tagUid", "canRetry", "canCancel"})
    CHECK(pairing.find(std::string("\"") + field + "\":") != std::string::npos);
  CHECK(pairing.find("\"state\":\"success_remove\"") != std::string::npos);
  CHECK(pairing.find("\"identityEditableMask\":1") != std::string::npos);
  CHECK(pairing.find("\"canRetry\":false") != std::string::npos);
  CHECK(pairing.find("\"tagUid\":700") != std::string::npos);
}

void legacyExplicitGameRetainsAvatarSetupWithoutTagGate() {
  Fixture f("legacy");
  CHECK(f.authority->newGame(16, 2, 2));
  CHECK(!f.authority->automaticPlayers());
  CHECK(f.snapshot().state == TagPairState::Disabled && !(f.snapshot().flags & TagPairFlagEnabled));
  IdentitySnapshot identity;
  CHECK(f.authority->makeIdentitySnapshot(1, identity) && identity.selfStage == IdentitySeatStage::AvatarSetup);
  auto request = avatarRequest(*f.authority, 1, 601);
  f.authority->handleIdentityRequest(1, request, identity);
  CHECK(identity.result == IdentityResultCode::Ok);
  CHECK(!f.authority->tagPairingBlocksArrival("reader-A"));
  auto binding = f.authority->setPlayerTagBinding(1, 0x8811u, f.authority->tagBindingRevision());
  CHECK(binding && binding.changed);
  binding = f.authority->clearPlayerTagBinding(1, binding.revision);
  CHECK(binding && binding.changed && f.authority->playerTagBindings().playerTagUids[0] == 0);
}

}  // namespace

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  const auto process =
#if defined(_WIN32)
      ::_getpid();
#else
      ::getpid();
#endif
  const auto temporary = std::filesystem::temp_directory_path();
  testRoot = temporary / ("gridopoly-tag-pairing-authority-" + std::to_string(process) + '-' +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(std::filesystem::equivalent(testRoot.parent_path(), temporary) && !std::filesystem::exists(testRoot));
  CHECK(std::filesystem::create_directory(testRoot));
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"full MAC and capability discovery", discoveryChecksFullMacAndCapability},
      {"FNV collision seat quarantine", hashCollisionCannotStealFrozenSeat},
      {"invalid player counts do not reset room", zeroFiveAndWrongBoardRejectWithoutChangingRoom},
      {"one to four real players and frozen reconnect", oneToFourConsolesProduceFourStableSeats},
      {"identity editing while other players pair", tagGateAllowsBoundPlayersToEditDuringNextPairing},
      {"room fence and independent revisions", heartbeatRoomFenceAndPairRevisionAreIndependentOfGameVersion},
      {"durable metadata failure rollback", metadataFailureRollsBackUidAndRevision},
      {"automatic game rejects manual UID bypass", automaticGameRejectsManualBindingBypass},
      {"same room metadata restart", metadataRestartRestoresFrozenIdentityAndUidButNotSession},
      {"pending removal persists through server restart", pendingRemovalSurvivesRestartUntilFreshNewSessionEmpty},
      {"failed removal write preserves arrival fence", failedRemovalPersistenceRetainsFenceUntilAnotherFreshScan},
      {"reused reader clears prior removal before switching", reusedReaderClearsPriorRemovalBeforeRetryingAnotherReader},
      {"reused reader cancellation retains previous removal cue", reusedReaderCancellationRetainsPreviousPlayersRemovalCue},
      {"reused reader failed write requires new empty scan", reusedReaderFailedEmptyWriteRequiresAnotherNewScan},
      {"Web JSON contract", jsonIncludesAuthoritativePairingFrozenSeatsAndWinner},
      {"legacy explicit game compatibility", legacyExplicitGameRetainsAvatarSetupWithoutTagGate},
  };
  unsigned failed = 0;
  for (const auto& test : tests) {
    std::cout << "RUN " << test.first << '\n';
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << "tag_pairing_authority_tests: " << std::size(tests) - failed << '/' << std::size(tests) << " passed\n";
  // Only remove this process's newly-created test directory, never a repository path.
  CHECK(std::filesystem::equivalent(testRoot.parent_path(), temporary));
  std::error_code cleanupError;
  std::filesystem::remove_all(testRoot, cleanupError);
  if (cleanupError) std::cerr << "Test fixture cleanup: " << cleanupError.message() << '\n';
  return failed ? 1 : 0;
}
