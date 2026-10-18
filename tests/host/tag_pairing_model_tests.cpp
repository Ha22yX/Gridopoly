#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../../Server/RaspberryPi/src/TagPairingModel.h"

namespace {
using namespace gridopoly::pi;
using namespace gridopoly::protocol;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

TagPairReaderObservation reader(const std::string& module, std::uint32_t sequence,
    std::vector<std::uint32_t> tags = {}, std::uint32_t room = 77,
    std::uint32_t session = 0) {
  TagPairReaderObservation out;
  out.moduleId = module; out.deviceId = "device-" + module; out.bootId = "boot-" + module;
  out.mapIndex = 8; out.stable = true; out.hasFreshness = true; out.fresh = true;
  out.scanSequence = sequence; out.tags = std::move(tags); out.tagRevision = sequence;
  out.roomId = room; out.sessionId = session;
  return out;
}

struct Fixture {
  TagPairingModel model;
  TagPairingModel::Tags tags{};
  std::uint64_t now{1000};
  std::uint32_t random{100};
  unsigned attempts{};
  bool persist{true};
  explicit Fixture(std::uint8_t humans = 2) { begin(humans); }
  void begin(std::uint8_t humans, std::uint32_t room = 77) {
    model.begin(room, humans, tags, [this] { return random++; });
  }
  TagPairSnapshot snapshot(std::uint8_t self = 1) const { return model.snapshot(self, tags, now); }
  void send(TagPairReaderObservation report) {
    model.observe(report, now, tags, [this](std::uint8_t player, std::uint32_t uid) {
      ++attempts;
      if (!persist) return false;
      CHECK(player >= 1 && player <= 4 && tags[player - 1] == 0);
      tags[player - 1] = uid;
      return true;
    });
  }
  void scan(const std::string& module, std::uint32_t sequence,
            std::vector<std::uint32_t> inventory = {}) {
    const auto state = snapshot();
    send(reader(module, sequence, std::move(inventory), state.roomId, state.sessionId));
  }
  void advance(std::uint64_t ms) { now += ms; model.tick(now, tags); }
  TagPairRequest request(TagPairOperation operation, std::uint32_t id, std::uint8_t player = 1) const {
    const auto state = snapshot();
    return {operation, player, id, state.roomId, state.sessionId, state.revision};
  }
  TagPairSnapshot run(const TagPairRequest& request, std::uint8_t source = 1) {
    return model.request(source, request, tags, now);
  }
};

void requestWireContract() {
  static_assert(kTagPairRequestSize == 20);
  CHECK(static_cast<unsigned>(MessageType::TagPairRequest) == 0x2B);
  CHECK(static_cast<unsigned>(IdentitySeatStage::TagPairing) == 7);
  TagPairRequest request{TagPairOperation::Retry, 3, 0x04030201u, 0x08070605u,
                        0x0C0B0A09u, 0x100F0E0Du};
  const std::array<std::uint8_t, 20> expected = {
      1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  std::array<std::uint8_t, 21> wire{};
  wire.back() = 0xA5;
  std::size_t count = 999;
  CHECK(encodeTagPairRequest(request, wire.data(), 20, count));
  CHECK(count == 20 && wire.back() == 0xA5);
  CHECK(std::equal(expected.begin(), expected.end(), wire.begin()));
  TagPairRequest decoded{};
  CHECK(decodeTagPairRequest(wire.data(), 20, decoded));
  CHECK(decoded.requestId == request.requestId && decoded.roomId == request.roomId);
  CHECK(decoded.sessionId == request.sessionId && decoded.expectedRevision == request.expectedRevision);
  for (std::size_t size = 0; size < 20; ++size)
    CHECK(!decodeTagPairRequest(wire.data(), size, decoded));
  CHECK(!decodeTagPairRequest(wire.data(), 21, decoded));
  CHECK(!decodeTagPairRequest(nullptr, 20, decoded));
  CHECK(!encodeTagPairRequest(request, wire.data(), 19, count) && count == 0);
  CHECK(!encodeTagPairRequest(request, nullptr, 20, count));
  for (const auto index : {0u, 1u, 2u, 3u}) {
    auto bad = expected; bad[index] = 0xFF;
    CHECK(!decodeTagPairRequest(bad.data(), bad.size(), decoded));
  }
  request.requestId = 0;
  CHECK(!encodeTagPairRequest(request, wire.data(), 20, count));
  request.operation = TagPairOperation::Query; request.sessionId = 0;
  CHECK(encodeTagPairRequest(request, wire.data(), 20, count));
  request.roomId = 0;
  CHECK(!encodeTagPairRequest(request, wire.data(), 20, count));
}

void snapshotWireContract() {
  static_assert(kTagPairSnapshotSize == 88);
  CHECK(static_cast<unsigned>(MessageType::TagPairSnapshot) == 0x2C);
  TagPairSnapshot snapshot;
  snapshot.state = TagPairState::SuccessRemove; snapshot.result = TagPairResultCode::PersistFailed;
  snapshot.selfPlayerId = 1; snapshot.requestId = 0x04030201; snapshot.roomId = 0x08070605;
  snapshot.revision = 0x0C0B0A09; snapshot.sessionId = 0x100F0E0D;
  snapshot.expiresInMs = 0x14131211; snapshot.tagUid = 0x18171615;
  snapshot.activePlayerId = 2; snapshot.requiredHumanMask = 3; snapshot.boundMask = 1;
  snapshot.flags = TagPairFlagEnabled | TagPairFlagSelfBound | TagPairFlagCanEditIdentity;
  std::strcpy(snapshot.moduleId.data(), "reader-A"); std::strcpy(snapshot.playerName.data(), "Alice");
  snapshot.readerTagRevision = 0x1C1B1A19; snapshot.readerMapIndex = 24;
  std::array<std::uint8_t, 89> wire{}; wire.back() = 0xA5; std::size_t count = 0;
  CHECK(encodeTagPairSnapshot(snapshot, wire.data(), 88, count));
  CHECK(count == 88 && wire.back() == 0xA5);
  CHECK(wire[0] == 1 && wire[1] == 4 && wire[2] == 8 && wire[3] == 1);
  for (unsigned i = 4; i < 28; ++i) CHECK(wire[i] == i - 3);
  CHECK(wire[28] == 2 && wire[29] == 3 && wire[30] == 1 && wire[31] == 7);
  CHECK(std::memcmp(wire.data() + 32, "reader-A", 8) == 0);
  CHECK(std::memcmp(wire.data() + 64, "Alice", 5) == 0);
  CHECK(wire[80] == 25 && wire[83] == 28 && wire[84] == 24);
  CHECK(wire[85] == 0 && wire[86] == 0 && wire[87] == 0);
  TagPairSnapshot decoded;
  CHECK(decodeTagPairSnapshot(wire.data(), 88, decoded));
  CHECK(decoded.tagUid == snapshot.tagUid && decoded.readerMapIndex == 24);
  CHECK(decoded.moduleId == snapshot.moduleId && decoded.playerName == snapshot.playerName);
  CHECK(decoded.revision == snapshot.revision && decoded.sessionId == snapshot.sessionId);
  for (std::size_t size = 0; size < 88; ++size)
    CHECK(!decodeTagPairSnapshot(wire.data(), size, decoded));
  CHECK(!decodeTagPairSnapshot(wire.data(), 89, decoded));
  CHECK(!decodeTagPairSnapshot(nullptr, 88, decoded));
  CHECK(!encodeTagPairSnapshot(snapshot, wire.data(), 87, count) && count == 0);
  CHECK(!encodeTagPairSnapshot(snapshot, nullptr, 88, count));
  for (const auto index : {0u, 1u, 2u, 3u, 28u, 29u, 30u, 31u, 85u, 86u, 87u}) {
    auto bad = wire; bad[index] = 0xFF;
    CHECK(!decodeTagPairSnapshot(bad.data(), 88, decoded));
  }
  auto bad = wire; bad[84] = 32;
  CHECK(!decodeTagPairSnapshot(bad.data(), 88, decoded));
  bad = wire; bad[41] = 'x'; // Noncanonical text after its NUL cannot hide on the wire.
  CHECK(!decodeTagPairSnapshot(bad.data(), 88, decoded));
  snapshot.moduleId.fill('M'); snapshot.moduleId.back() = 0;
  snapshot.playerName.fill('N'); snapshot.playerName.back() = 0;
  snapshot.readerMapIndex = 0xFF;
  CHECK(encodeTagPairSnapshot(snapshot, wire.data(), 88, count));
  CHECK(decodeTagPairSnapshot(wire.data(), 88, decoded));
  CHECK(decoded.moduleId == snapshot.moduleId && decoded.playerName == snapshot.playerName);
  snapshot.moduleId.back() = 'x';
  CHECK(!encodeTagPairSnapshot(snapshot, wire.data(), 88, count));
}

void existingTagNeedsSessionEmptyAndNewScan() {
  Fixture f;
  f.send(reader("A", 10, {101}));
  CHECK(f.snapshot().state == TagPairState::ClearRequired && f.attempts == 0);
  const auto session = f.snapshot().sessionId;
  CHECK(session != 0);
  f.scan("A", 11, {101});
  CHECK(f.tags[0] == 0 && f.snapshot().state == TagPairState::ClearRequired);
  f.scan("A", 11, {}); // Cached inventory cannot acquire an empty fence.
  f.scan("A", 12, {101});
  CHECK(f.tags[0] == 0);
  f.scan("A", 13, {});
  CHECK(f.snapshot().state == TagPairState::AwaitingTag);
  f.scan("A", 13, {101});
  CHECK(f.tags[0] == 0);
  f.scan("A", 14, {101});
  CHECK(f.tags[0] == 101 && f.attempts == 1);
  CHECK(f.snapshot().state == TagPairState::SuccessRemove);
  CHECK(f.model.canEdit(1, f.tags) && !f.model.canEdit(2, f.tags));
  CHECK((f.snapshot().flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)) ==
      (TagPairFlagSelfBound | TagPairFlagCanEditIdentity));
  f.scan("A", 15, {101});
  CHECK(f.attempts == 1 && f.snapshot().sessionId == session);
}

void wrongRoomSessionReaderAndOutOfOrderCannotBind() {
  Fixture f;
  f.send(reader("A", 100));
  const auto current = f.snapshot();
  f.send(reader("A", 101, {}, 76, current.sessionId));
  f.send(reader("A", 102, {111}, 77, current.sessionId));
  CHECK(f.attempts == 0);
  f.send(reader("A", 103, {}, 77, current.sessionId + 1));
  f.scan("A", 104, {111});
  CHECK(f.attempts == 0);
  f.scan("B", 200, {}); f.scan("B", 201, {111});
  CHECK(f.attempts == 0 && std::string(f.snapshot().moduleId.data()) == "A");
  f.scan("A", 105, {});
  f.scan("A", 104, {111});
  CHECK(f.attempts == 0);
  f.scan("A", 106, {111});
  CHECK(f.tags[0] == 111 && f.attempts == 1);
}

void untrustworthyInventoryCannotDriveBinding() {
  for (unsigned failure = 0; failure < 6; ++failure) {
    Fixture f;
    auto report = reader("A", 1);
    if (failure == 0) report.hasFreshness = false;
    if (failure == 1) report.fresh = false;
    if (failure == 2) report.stable = false;
    if (failure == 3) report.fault = true;
    if (failure == 4) report.overflow = true;
    if (failure == 5) report.ageMs = 5000;
    f.send(report);
    CHECK(f.snapshot().state == TagPairState::WaitingReader && f.attempts == 0);
    f.send(reader("A", 2));
    CHECK(f.snapshot().state == TagPairState::ClearRequired);
    const auto session = f.snapshot().sessionId;
    report.scanSequence = 3; report.roomId = 77; report.sessionId = session;
    report.tags = {222}; f.send(report);
    CHECK(f.attempts == 0 && f.tags[0] == 0);
  }
}

void sameScanCannotRenewFreshnessAndRecoveryNeedsNewSession() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2);
  const auto before = f.snapshot();
  f.now += 4999; f.scan("A", 2);
  CHECK(f.snapshot().state == TagPairState::AwaitingTag);
  f.advance(1);
  CHECK(f.snapshot().state == TagPairState::ReaderOffline);
  f.scan("A", 2); // Replayed HTTP heartbeat at the boundary remains stale.
  CHECK(f.snapshot().state == TagPairState::ReaderOffline);
  f.send(reader("A", 3, {222}, 77, before.sessionId));
  CHECK(f.snapshot().state == TagPairState::ClearRequired);
  CHECK(f.snapshot().sessionId != before.sessionId && f.attempts == 0);
  f.scan("A", 4, {222}); CHECK(f.attempts == 0);
  f.scan("A", 5); f.scan("A", 6, {222});
  CHECK(f.tags[0] == 222);
}

void bootChangeAndDeviceReplacementInvalidateEmptyFence() {
  for (bool replaceDevice : {false, true}) {
    Fixture f;
    f.send(reader("A", 10)); f.scan("A", 11);
    const auto old = f.snapshot();
    auto replacement = reader("A", 1, {333}, 77, old.sessionId);
    if (replaceDevice) replacement.deviceId = "replacement-device";
    else replacement.bootId = "replacement-boot";
    f.send(replacement);
    CHECK(f.attempts == 0 && f.snapshot().sessionId != old.sessionId);
    replacement.sessionId = f.snapshot().sessionId; replacement.scanSequence = 2;
    f.send(replacement); CHECK(f.attempts == 0);
    replacement.scanSequence = 3; replacement.tags.clear(); f.send(replacement);
    replacement.scanSequence = 4; replacement.tags = {333}; f.send(replacement);
    CHECK(f.tags[0] == 333);
  }
}

void scanSequenceWrapUsesSerialOrder() {
  Fixture f;
  f.send(reader("A", 0xFFFFFFFEu)); f.scan("A", 0xFFFFFFFFu);
  f.scan("A", 0xFFFFFFFDu, {44}); CHECK(f.attempts == 0);
  f.scan("A", 0x7FFFFFFFu, {44}); CHECK(f.attempts == 0); // Ambiguous half range.
  f.scan("A", 0, {44}); CHECK(f.tags[0] == 44);
}

void multipleTagsAndBoundUidRequireAnotherClear() {
  Fixture f;
  f.tags[0] = 555; f.begin(2);
  CHECK(f.snapshot().activePlayerId == 2);
  f.send(reader("A", 1)); f.scan("A", 2); f.scan("A", 3, {600, 601});
  CHECK(f.snapshot().state == TagPairState::MultipleTags && f.attempts == 0);
  f.scan("A", 4, {600}); CHECK(f.attempts == 0);
  f.scan("A", 5); f.scan("A", 6, {555});
  CHECK(f.snapshot().state == TagPairState::AlreadyBound);
  CHECK(f.tags[0] == 555 && f.tags[1] == 0 && f.attempts == 0);
  f.scan("A", 7, {600}); CHECK(f.attempts == 0);
  f.scan("A", 8); f.scan("A", 9, {600});
  CHECK(f.tags[0] == 555 && f.tags[1] == 600 && f.attempts == 1);
}

void persistenceFailureDoesNotPublishBinding() {
  Fixture f;
  f.persist = false;
  f.send(reader("A", 1)); f.scan("A", 2); f.scan("A", 3, {700});
  CHECK(f.attempts == 1 && f.tags[0] == 0);
  CHECK(f.snapshot().result == TagPairResultCode::PersistFailed);
  CHECK(f.snapshot().boundMask == 0 && !f.model.canEdit(1, f.tags));
  CHECK(f.model.readerSnapshot("A", f.tags, f.now).state != TagPairState::SuccessRemove);
  f.persist = true; f.scan("A", 4, {700}); CHECK(f.attempts == 1);
  f.scan("A", 5); f.scan("A", 6, {700});
  CHECK(f.attempts == 2 && f.tags[0] == 700 && f.snapshot().result == TagPairResultCode::Ok);
}

void nextPlayerProceedsWhilePriorReaderShowsRemoval() {
  Fixture f;
  f.send(reader("A", 1)); f.send(reader("B", 1));
  f.scan("A", 2); f.scan("A", 3, {800});
  const auto first = f.snapshot();
  f.advance(999); CHECK(f.snapshot().activePlayerId == 1);
  f.advance(1);
  CHECK(f.snapshot().activePlayerId == 2 && std::string(f.snapshot().moduleId.data()) == "B");
  CHECK(f.snapshot().sessionId != first.sessionId && f.model.canEdit(1, f.tags));
  const auto pending = f.model.readerSnapshot("A", f.tags, f.now);
  CHECK(pending.state == TagPairState::SuccessRemove && pending.sessionId == first.sessionId);
  CHECK(pending.activePlayerId == 1 && pending.tagUid == 800 && pending.expiresInMs > 0);
  f.scan("B", 2); f.scan("B", 3, {801});
  CHECK(f.tags[1] == 801);
  f.advance(1000);
  CHECK(f.snapshot().state == TagPairState::Complete && f.snapshot().boundMask == 3);
  f.send(reader("A", 4, {}, 77, first.sessionId + 1));
  CHECK(f.model.readerSnapshot("A", f.tags, f.now).state == TagPairState::SuccessRemove);
  f.send(reader("A", 5, {}, 77, first.sessionId));
  CHECK(f.model.readerSnapshot("A", f.tags, f.now).state == TagPairState::Disabled);
}

void singleReaderRequiresRemovalBeforeNextPlayerCanBind() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2); f.scan("A", 3, {900});
  const auto first = f.snapshot(); f.advance(1000);
  CHECK(f.snapshot().activePlayerId == 2 && f.snapshot().state == TagPairState::ClearRequired);
  CHECK(f.snapshot().sessionId != first.sessionId);
  f.send(reader("A", 4, {}, 77, first.sessionId)); f.scan("A", 5, {901});
  CHECK(f.tags[1] == 0);
  f.scan("A", 6); f.scan("A", 7, {901});
  CHECK(f.tags[0] == 900 && f.tags[1] == 901);
}

void readerSelectionPrefersEmptyThenDifferentPrevious() {
  Fixture f;
  f.send(reader("A", 1, {10})); f.send(reader("B", 1));
  CHECK(f.run(f.request(TagPairOperation::Retry, 100)).result == TagPairResultCode::Ok);
  CHECK(std::string(f.snapshot().moduleId.data()) == "B");
  auto retry = f.request(TagPairOperation::Retry, 1);
  CHECK(f.run(retry).result == TagPairResultCode::Ok);
  CHECK(std::string(f.snapshot().moduleId.data()) == "B"); // Empty previous beats occupied alternative.
  f.send(reader("A", 2));
  retry = f.request(TagPairOperation::Retry, 2); f.run(retry);
  CHECK(std::string(f.snapshot().moduleId.data()) == "A"); // Both empty: avoid previous.
  retry = f.request(TagPairOperation::Retry, 3); f.run(retry);
  CHECK(std::string(f.snapshot().moduleId.data()) == "B");
}

void cancelRetryAreIdempotentAndAuthorized() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2);
  auto cancel = f.request(TagPairOperation::Cancel, 100);
  auto response = f.run(cancel, 2);
  CHECK(response.result == TagPairResultCode::Unauthorized);
  auto other = f.request(TagPairOperation::Cancel, 101, 2);
  CHECK(f.run(other, 2).result == TagPairResultCode::NotAllowed);
  response = f.run(cancel);
  CHECK(response.result == TagPairResultCode::Ok && response.state == TagPairState::Cancelled);
  const auto revision = f.model.revision();
  f.scan("A", 3, {1000}); CHECK(f.tags[0] == 0);
  response = f.run(cancel);
  CHECK(response.result == TagPairResultCode::Ok && (response.flags & TagPairFlagReplay));
  CHECK(f.model.revision() == revision);
  auto conflict = cancel; conflict.operation = TagPairOperation::Retry;
  CHECK(f.run(conflict).result == TagPairResultCode::RequestIdConflict);
  auto retry = f.request(TagPairOperation::Retry, 102);
  const auto session = retry.sessionId;
  response = f.run(retry);
  CHECK(response.result == TagPairResultCode::Ok && response.sessionId != session);
  const auto nextSession = response.sessionId;
  response = f.run(retry);
  CHECK((response.flags & TagPairFlagReplay) && response.sessionId == nextSession);
  auto stale = f.request(TagPairOperation::Cancel, 103); --stale.expectedRevision;
  CHECK(f.run(stale).result == TagPairResultCode::RevisionStale);
  stale = f.request(TagPairOperation::Cancel, 104); stale.sessionId = session;
  CHECK(f.run(stale).result == TagPairResultCode::SessionStale);
  stale = f.request(TagPairOperation::Cancel, 105); --stale.roomId;
  CHECK(f.run(stale).result == TagPairResultCode::RoomStale);
  auto query = f.request(TagPairOperation::Query, 0); query.sessionId = 0; query.expectedRevision = 0;
  CHECK(f.run(query).result == TagPairResultCode::Ok);
  CHECK(f.snapshot().sessionId == nextSession);
}

void sessionTimeoutDoesNotBindAndRetryStartsFresh() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2);
  const auto old = f.snapshot();
  std::uint32_t sequence = 2;
  for (unsigned i = 0; i < 59; ++i) { f.now += 1000; f.scan("A", ++sequence); }
  CHECK(f.snapshot().state == TagPairState::AwaitingTag);
  f.now += 1000; f.scan("A", ++sequence, {1234});
  CHECK(f.snapshot().state == TagPairState::TimedOut && f.attempts == 0);
  const auto retry = f.request(TagPairOperation::Retry, 200);
  const auto response = f.run(retry);
  CHECK(response.result == TagPairResultCode::Ok && response.sessionId != old.sessionId);
  f.scan("A", ++sequence, {1234}); CHECK(f.attempts == 0);
  f.scan("A", ++sequence); f.scan("A", ++sequence, {1234});
  CHECK(f.tags[0] == 1234);
}

void cancelledAndTimedOutStayStoppedAcrossReaderReplacement() {
  for (bool timedOut : {false, true}) {
    for (bool replaceDevice : {false, true}) {
      Fixture f;
      f.send(reader("A", 1)); f.scan("A", 2);
      if (timedOut) {
        std::uint32_t sequence = 2;
        for (unsigned second = 0; second < 60; ++second) {
          f.now += 1000; f.scan("A", ++sequence);
        }
      } else {
        const auto cancel = f.request(TagPairOperation::Cancel, 700);
        CHECK(f.run(cancel).result == TagPairResultCode::Ok);
      }
      const auto stopped = f.snapshot();
      CHECK(stopped.state == (timedOut ? TagPairState::TimedOut : TagPairState::Cancelled));

      auto replacement = reader("A", 1, {}, stopped.roomId, 0);
      if (replaceDevice) replacement.deviceId = "replacement-device";
      else replacement.bootId = "replacement-boot";
      f.send(replacement);
      CHECK(f.snapshot().state == stopped.state && f.snapshot().sessionId == stopped.sessionId);
      // Neither a matching echo nor a delayed echo can reopen the empty fence.
      replacement.sessionId = stopped.sessionId;
      replacement.scanSequence = 2; replacement.tags = {0x778899u}; f.send(replacement);
      replacement.scanSequence = 3; replacement.tags.clear(); f.send(replacement);
      replacement.scanSequence = 4; replacement.tags = {0x778899u}; f.send(replacement);
      replacement.sessionId = stopped.sessionId + 1;
      replacement.scanSequence = 5; replacement.tags.clear(); f.send(replacement);
      replacement.scanSequence = 6; replacement.tags = {0x778899u}; f.send(replacement);
      f.advance(1000);
      CHECK(f.snapshot().state == stopped.state && f.snapshot().sessionId == stopped.sessionId);
      CHECK(f.snapshot().revision == stopped.revision && f.attempts == 0 && f.tags[0] == 0);

      const auto retry = f.request(TagPairOperation::Retry, 701);
      const auto resumed = f.run(retry);
      CHECK(resumed.result == TagPairResultCode::Ok && resumed.state == TagPairState::ClearRequired);
      CHECK(resumed.sessionId != stopped.sessionId);
      replacement.sessionId = stopped.sessionId;
      replacement.scanSequence = 7; replacement.tags.clear(); f.send(replacement);
      replacement.sessionId = resumed.sessionId;
      replacement.scanSequence = 8; replacement.tags = {0x778899u}; f.send(replacement);
      CHECK(f.attempts == 0 && f.tags[0] == 0);
      replacement.scanSequence = 9; replacement.tags.clear(); f.send(replacement);
      replacement.scanSequence = 10; replacement.tags = {0x778899u}; f.send(replacement);
      CHECK(f.attempts == 1 && f.tags[0] == 0x778899u);
      CHECK(f.snapshot().state == TagPairState::SuccessRemove);
    }
  }
}

void sameRoomRestartUsesNewSessionAndKeepsConfirmedPlayers() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2); f.scan("A", 3, {1444});
  f.advance(1000);
  const auto old = f.snapshot();
  const auto oldRequest = f.request(TagPairOperation::Cancel, 300, 2);
  TagPairingModel restarted;
  restarted.begin(77, 2, f.tags, [] { return 0x8ABCu; });
  const auto restored = restarted.snapshot(1, f.tags, f.now);
  CHECK(restored.roomId == 77 && restored.activePlayerId == 2 && restored.boundMask == 1);
  CHECK(restored.sessionId != old.sessionId && restarted.canEdit(1, f.tags));
  CHECK(restarted.request(2, oldRequest, f.tags, f.now).result == TagPairResultCode::SessionStale);
  CHECK(f.tags[0] == 1444 && f.tags[1] == 0);
  f.begin(2, 78);
  CHECK(f.run(oldRequest, 2).result == TagPairResultCode::RoomStale);
  CHECK(f.snapshot().activePlayerId == 2 && f.tags[0] == 1444);
}

void transientInventoryKeepsReaderWithoutBecomingEvidence() {
  for (const bool sameScan : {false, true}) {
    Fixture f;
    f.send(reader("A", 1)); f.scan("A", 2); f.send(reader("B", 1));
    const auto before = f.snapshot();
    // V0.37 can retain a Present/NoTag inventory while a collision scan is
    // incomplete or a one-scan loss is still inside removal debounce.
    auto transient = reader("A", sameScan ? 2 : 3, {9876}, 77, before.sessionId);
    transient.fresh = false;
    f.advance(100); f.send(transient);
    CHECK(f.snapshot().state == TagPairState::AwaitingTag);
    CHECK(f.snapshot().sessionId == before.sessionId && f.snapshot().revision == before.revision);
    CHECK(std::string(f.snapshot().moduleId.data()) == "A" && f.attempts == 0);
    transient.scanSequence = 4; transient.stable = false; f.send(transient);
    CHECK(f.snapshot().sessionId == before.sessionId && f.attempts == 0);
    auto cached = reader("A", 4, {9876}, 77, before.sessionId);
    f.send(cached); // Re-labelling this cached scan is still not new evidence.
    CHECK(f.attempts == 0);
    f.scan("A", 5, {9876});
    CHECK(f.tags[0] == 9876 && f.attempts == 1);
    CHECK(f.snapshot().sessionId == before.sessionId && f.snapshot().revision != before.revision);
  }
}

void transientDoesNotRenewLeaseOrOfferAnotherReader() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2); f.send(reader("B", 1));
  const auto before = f.snapshot();
  auto invalid = reader("B", 2); invalid.fresh = false; f.send(invalid);
  f.advance(4000);
  invalid = reader("A", 3, {999}, 77, before.sessionId); invalid.fresh = false; f.send(invalid);
  CHECK(f.snapshot().sessionId == before.sessionId);
  const auto retry = f.run(f.request(TagPairOperation::Retry, 800));
  CHECK(retry.state == TagPairState::WaitingReader); // Neither transient reader is selectable.
  f.advance(1000);
  CHECK(f.attempts == 0);

  Fixture g;
  g.send(reader("A", 1)); g.scan("A", 2);
  const auto old = g.snapshot();
  g.now += 5000; // No intervening tick: arrival must expire the old empty fence first.
  g.send(reader("A", 3, {777}, 77, old.sessionId));
  CHECK(g.attempts == 0 && g.snapshot().sessionId != old.sessionId);
  CHECK(g.snapshot().state == TagPairState::ClearRequired);
}

void overflowRequiresClearWithoutRandomReaderChange() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2); f.send(reader("B", 1));
  const auto before = f.snapshot();
  auto overflow = reader("A", 3, {11,22}, 77, before.sessionId); overflow.overflow = true;
  f.send(overflow);
  CHECK(f.snapshot().state == TagPairState::MultipleTags && f.snapshot().sessionId == before.sessionId);
  f.scan("A", 4, {11}); CHECK(f.attempts == 0);
  f.scan("A", 5); f.scan("A", 6, {11}); CHECK(f.tags[0] == 11);
}

void newRoomDiscardsOldReaderDiscovery() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2);
  const auto old = f.snapshot();
  f.begin(2, 78); f.advance(1);
  CHECK(f.snapshot().state == TagPairState::WaitingReader);
  f.send(reader("A", 3, {}, 77, old.sessionId));
  CHECK(f.snapshot().state == TagPairState::WaitingReader);
  f.send(reader("A", 4, {}, 78, old.sessionId));
  CHECK(f.snapshot().state == TagPairState::ClearRequired);
  f.scan("A", 5, {456}); CHECK(f.attempts == 0);
  f.scan("A", 6); f.scan("A", 7, {456}); CHECK(f.tags[0] == 456);
}

void transientRemovalDoesNotClearPendingOrBoundFlags() {
  Fixture f;
  f.send(reader("A", 1)); f.scan("A", 2); f.scan("A", 3, {1444});
  const auto old = f.snapshot(); f.send(reader("B", 1)); f.advance(1000);
  auto empty = reader("A", 4, {}, 77, old.sessionId); empty.fresh = false; f.send(empty);
  CHECK(f.model.readerSnapshot("A", f.tags, f.now).state == TagPairState::SuccessRemove);
  const auto self = f.snapshot(1);
  CHECK((self.flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)) ==
      (TagPairFlagSelfBound | TagPairFlagCanEditIdentity));
  empty.scanSequence = 5; empty.fresh = true; f.send(empty);
  CHECK(f.model.readerSnapshot("A", f.tags, f.now).state == TagPairState::Disabled);
}

}  // namespace

int main() {
  const std::pair<const char*, std::function<void()>> tests[] = {
      {"production transient keeps active reader", transientInventoryKeepsReaderWithoutBecomingEvidence},
      {"transient lease and selection boundaries", transientDoesNotRenewLeaseOrOfferAnotherReader},
      {"overflow keeps reader but requires clear", overflowRequiresClearWithoutRandomReaderChange},
      {"new room reader discovery fence", newRoomDiscardsOldReaderDiscovery},
      {"transient removal and bound flags", transientRemovalDoesNotClearPendingOrBoundFlags},
      {"request wire contract", requestWireContract}, {"snapshot wire contract", snapshotWireContract},
      {"preexisting tag and empty scan fence", existingTagNeedsSessionEmptyAndNewScan},
      {"room/session/reader/order fences", wrongRoomSessionReaderAndOutOfOrderCannotBind},
      {"untrustworthy inventory", untrustworthyInventoryCannotDriveBinding},
      {"freshness expiry and automatic recovery", sameScanCannotRenewFreshnessAndRecoveryNeedsNewSession},
      {"reader reboot/replacement", bootChangeAndDeviceReplacementInvalidateEmptyFence},
      {"scan sequence wrap", scanSequenceWrapUsesSerialOrder},
      {"multiple tags and duplicate UID", multipleTagsAndBoundUidRequireAnotherClear},
      {"failed persistence", persistenceFailureDoesNotPublishBinding},
      {"parallel next player/removal", nextPlayerProceedsWhilePriorReaderShowsRemoval},
      {"single reader removal fence", singleReaderRequiresRemovalBeforeNextPlayerCanBind},
      {"reader selection priorities", readerSelectionPrefersEmptyThenDifferentPrevious},
      {"request authorization/idempotence", cancelRetryAreIdempotentAndAuthorized},
      {"session timeout/retry", sessionTimeoutDoesNotBindAndRetryStartsFresh},
      {"cancel/timeout survive reader reboot and replacement", cancelledAndTimedOutStayStoppedAcrossReaderReplacement},
      {"same room restart", sameRoomRestartUsesNewSessionAndKeepsConfirmedPlayers},
  };
  unsigned failed = 0;
  for (const auto& test : tests) {
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << "tag_pairing_model_tests: " << std::size(tests) - failed << '/' << std::size(tests) << " passed\n";
  return failed ? 1 : 0;
}
