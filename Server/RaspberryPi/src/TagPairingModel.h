#pragma once

#include <gridopoly/protocol/Protocol.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gridopoly::pi {

struct TagPairReaderObservation {
  std::string moduleId, deviceId, bootId;
  std::uint8_t mapIndex{0xFF};
  bool stable{}, fault{}, overflow{}, hasFreshness{}, fresh{};
  std::uint32_t scanSequence{}, ageMs{}, tagRevision{}, roomId{}, sessionId{};
  std::vector<std::uint32_t> tags;
};

inline const char* tagPairStateName(gridopoly::protocol::TagPairState state) {
  static constexpr const char* names[] = {"disabled", "waiting_reader", "clear_required",
      "awaiting_tag", "success_remove", "already_bound", "multiple_tags", "reader_offline",
      "timed_out", "cancelled", "complete"};
  const auto n = static_cast<unsigned>(state);
  return n < sizeof(names) / sizeof(names[0]) ? names[n] : "disabled";
}

// Pure authority workflow. The caller serializes access and commits UID changes
// before a success is visible. Reader inventory and scan identity are distinct:
// unchanged contents can be a new scan; repeated HTTP inventory cannot be one.
class TagPairingModel {
 public:
  using State = gridopoly::protocol::TagPairState;
  using Result = gridopoly::protocol::TagPairResultCode;
  using Tags = std::array<std::uint32_t, 6>;
  using Commit = std::function<bool(std::uint8_t, std::uint32_t)>;
  using Random = std::function<std::uint32_t()>;
  using Remove = std::function<bool(const std::string&)>;
  static constexpr std::uint64_t kFreshMs = 5000;
  static constexpr std::uint64_t kSessionMs = 60000;
  static constexpr std::uint64_t kSuccessMs = 1000;

  void begin(std::uint32_t room, std::uint8_t humans, const Tags& tags, Random random) {
    room_ = room; required_ = humans > 0 && humans <= 4
        ? static_cast<std::uint8_t>((1u << humans) - 1u) : 0;
    random_ = std::move(random); revision_ = 1;
    sessionCounter_ = random_ ? random_() : 1;
    session_ = 0; active_ = 0; reader_.clear(); previous_.clear();
    deadline_ = 0; successUntil_ = 0; pending_.clear(); cache_.clear(); readers_.clear();
    readerBoot_.clear(); readerDevice_.clear(); fence_ = 0; emptySeen_ = false;
    state_ = required_ ? State::WaitingReader : State::Disabled;
    lastResult_ = Result::Ok;
    if (required_) nextPlayer(tags);
  }
  void restoreRemovals(const std::array<std::string, 4>& origins, std::uint8_t mask, const Tags& tags) {
    for (unsigned i = 0; i < 4; ++i) if ((mask & (1u << i)) && tags[i] && !origins[i].empty())
      pending_[origins[i]] = {nextSessionId(), static_cast<std::uint8_t>(i + 1), tags[i]};
  }
  bool enabled() const { return required_ != 0; }
  std::uint32_t revision() const { return revision_; }
  State state() const { return state_; }
  bool canEdit(std::uint8_t player, const Tags& tags) const {
    return !enabled() || (player > 0 && player <= 4 &&
        (required_ & (1u << (player - 1))) && tags[player - 1] != 0);
  }

  void observe(const TagPairReaderObservation& report, std::uint64_t now,
               const Tags& tags, const Commit& commit, const Remove& remove = {}) {
    if (report.moduleId.empty() || report.moduleId.size() > 32 || report.deviceId.empty() ||
        report.bootId.empty() || !report.hasFreshness || report.tags.size() > 6 ||
        (report.roomId != 0 && report.roomId != room_)) return;
    if (!readers_.count(report.moduleId) && readers_.size() >= 64) return;
    tick(now, tags);
    auto& r = readers_[report.moduleId];
    const bool newBoot = r.report.bootId != report.bootId || r.report.deviceId != report.deviceId;
    if (!newBoot && r.hasScan &&
        static_cast<std::uint32_t>(report.scanSequence - r.report.scanSequence) >= 0x80000000u)
      return;
    const bool newScan = newBoot || !r.hasScan || report.scanSequence != r.report.scanSequence;
    if (newBoot && report.moduleId == reader_ && active_ && accepting())
      setState(State::ReaderOffline);
    r.seenAt = now;
    const bool usable = report.stable && !report.fault && !report.overflow && report.fresh &&
        report.ageMs < kFreshMs;
    // Incomplete scans and removal debounce are normal reader reports. Keep the
    // last good scan's finite lease, without treating the transient as evidence
    // or extending that lease. A different device/boot cannot inherit it.
    if (newBoot) r.freshUntil = 0;
    if (!usable) r.usable = false;
    if (newScan) {
      r.report = report; r.hasScan = true; r.usable = usable;
      if (usable) r.freshUntil = now + kFreshMs - report.ageMs;
    }
    if (newScan && report.overflow && report.moduleId == reader_ &&
        report.roomId == room_ && report.sessionId == session_ && accepting()) {
      emptySeen_ = false; setState(State::MultipleTags);
    }
    // A different session echo attached to cached inventory is not new evidence.
    if (!newScan || !usable || r.freshUntil <= now) { tick(now, tags); return; }
    const auto pending = pending_.find(report.moduleId);
    if (pending != pending_.end() && report.roomId == room_ &&
        report.sessionId == pending->second.session && report.tags.empty()) {
      if (!remove || remove(report.moduleId)) { pending_.erase(pending); bump(); }
    }
    if (report.moduleId != reader_ || report.roomId != room_ || report.sessionId != session_ ||
        report.bootId != readerBoot_ || report.deviceId != readerDevice_ ||
        static_cast<std::uint32_t>(report.scanSequence - fence_) == 0 ||
        static_cast<std::uint32_t>(report.scanSequence - fence_) >= 0x80000000u ||
        !accepting()) { tick(now, tags); return; }
    if (now >= deadline_) { setState(State::TimedOut); return; }
    fence_ = report.scanSequence;
    if (report.tags.empty()) {
      // A reused reader may still carry a previous player's durable removal
      // fence. Only this new, authenticated session's fresh empty scan clears it.
      if (remove && !remove(report.moduleId)) {
        emptySeen_ = false; lastResult_ = Result::PersistFailed;
        setState(State::ClearRequired); return;
      }
      if (pending_.erase(report.moduleId)) bump();
      emptySeen_ = true; lastResult_ = Result::Ok; setState(State::AwaitingTag);
    } else if (!emptySeen_) {
      setState(State::ClearRequired);
    } else if (report.tags.size() != 1 || report.tags.front() == 0) {
      emptySeen_ = false; setState(State::MultipleTags);
    } else {
      const auto uid = report.tags.front();
      if (std::find(tags.begin(), tags.end(), uid) != tags.end()) {
        emptySeen_ = false; setState(State::AlreadyBound);
      } else if (!commit(active_, uid)) {
        emptySeen_ = false; lastResult_ = Result::PersistFailed; setState(State::ClearRequired);
      } else {
        lastResult_ = Result::Ok;
        pending_[reader_] = {session_, active_, uid};
        successUntil_ = now + kSuccessMs; setState(State::SuccessRemove);
      }
    }
    tick(now, tags);
  }

  void tick(std::uint64_t now, const Tags& tags) {
    if (!enabled()) return;
    if (state_ == State::SuccessRemove && now >= successUntil_) nextPlayer(tags);
    if (accepting()) {
      const auto r = readers_.find(reader_);
      if (r == readers_.end() || r->second.freshUntil <= now ||
          r->second.report.bootId != readerBoot_ || r->second.report.deviceId != readerDevice_)
        setState(State::ReaderOffline);
      else if (now >= deadline_) setState(State::TimedOut);
    }
    if (state_ == State::ReaderOffline) {
      const bool available = std::any_of(readers_.begin(), readers_.end(),
          [now](const auto& r) { return r.second.usable && r.second.freshUntil > now; });
      if (available) {
        previous_ = reader_; reader_.clear(); newSession(); setState(State::WaitingReader);
      }
    }
    if (state_ == State::WaitingReader) selectReader(now);
  }

  gridopoly::protocol::TagPairSnapshot snapshot(std::uint8_t self, const Tags& tags,
                                               std::uint64_t now) const {
    using namespace gridopoly::protocol;
    TagPairSnapshot out;
    out.state = state_; out.result = lastResult_; out.roomId = room_; out.revision = revision_;
    out.sessionId = session_; out.selfPlayerId = self; out.activePlayerId = active_;
    out.requiredHumanMask = required_; out.flags = enabled() ? TagPairFlagEnabled : 0;
    for (unsigned i = 0; i < 4; ++i) if ((required_ & (1u << i)) && tags[i])
      out.boundMask |= static_cast<std::uint8_t>(1u << i);
    if (self > 0 && self <= 4 && (out.boundMask & (1u << (self - 1))))
      out.flags |= TagPairFlagSelfBound | TagPairFlagCanEditIdentity;
    out.tagUid = active_ ? tags[active_ - 1] : 0;
    out.expiresInMs = accepting() && deadline_ > now
        ? static_cast<std::uint32_t>(deadline_ - now) : 0;
    std::strncpy(out.moduleId.data(), reader_.c_str(), out.moduleId.size() - 1);
    const auto r = readers_.find(reader_);
    if (r != readers_.end()) {
      out.readerMapIndex = r->second.report.mapIndex;
      out.readerTagRevision = r->second.report.tagRevision;
    }
    return out;
  }

  // Per-module cues preserve the previous player's removal prompt while a new
  // reader is already pairing the next player. An active reuse takes display
  // priority, but retains the old cue until a fresh empty scan is committed.
  gridopoly::protocol::TagPairSnapshot readerSnapshot(const std::string& module,
                                                      const Tags& tags, std::uint64_t now) const {
    auto out = snapshot(0, tags, now);
    if (reader_ == module && (accepting() || state_ == State::SuccessRemove)) {
      out.expiresInMs = static_cast<std::uint32_t>(kFreshMs);
      return out;
    }
    const auto pending = pending_.find(module);
    const auto r = readers_.find(module);
    if (pending != pending_.end() && r != readers_.end() && now >= r->second.seenAt &&
        now - r->second.seenAt < kFreshMs) {
      out.state = State::SuccessRemove; out.sessionId = pending->second.session;
      out.activePlayerId = pending->second.player; out.tagUid = pending->second.uid;
      out.expiresInMs = static_cast<std::uint32_t>(kFreshMs);
      out.moduleId.fill(0); std::strncpy(out.moduleId.data(), module.c_str(), 32);
      return out;
    }
    out.state = State::Disabled; out.expiresInMs = 0; out.moduleId.fill(0);
    return out;
  }

  gridopoly::protocol::TagPairSnapshot request(std::uint8_t source,
      const gridopoly::protocol::TagPairRequest& req, const Tags& tags, std::uint64_t now) {
    using namespace gridopoly::protocol;
    tick(now, tags);
    auto reply = [&](Result result, bool replay = false) {
      auto out = snapshot(source, tags, now); out.result = result; out.requestId = req.requestId;
      if (replay) out.flags |= TagPairFlagReplay;
      return out;
    };
    std::array<std::uint8_t, kTagPairRequestSize> canonical{}; std::size_t bytes = 0;
    if (!encodeTagPairRequest(req, canonical.data(), canonical.size(), bytes))
      return reply(Result::InvalidRequest);
    if (req.roomId != room_) return reply(Result::RoomStale);
    if (source != req.playerId || source == 0 || source > 4 || !(required_ & (1u << (source - 1))))
      return reply(Result::Unauthorized);
    if (req.operation == TagPairOperation::Query) return reply(Result::Ok);
    const std::uint64_t key = (static_cast<std::uint64_t>(source) << 32) | req.requestId;
    const auto previous = cache_.find(key);
    if (previous != cache_.end()) return reply(previous->second.bytes == canonical
        ? previous->second.result : Result::RequestIdConflict, previous->second.bytes == canonical);
    Result result = Result::Ok;
    if (source != active_ || state_ == State::SuccessRemove || state_ == State::Complete)
      result = Result::NotAllowed;
    else if (req.sessionId != session_) result = Result::SessionStale;
    else if (req.expectedRevision != revision_) result = Result::RevisionStale;
    else if (cache_.size() >= 256) result = Result::NotAllowed;
    else if (req.operation == TagPairOperation::Cancel) {
      setState(State::Cancelled);
    } else {
      previous_ = reader_; reader_.clear(); newSession();
      lastResult_ = Result::Ok; setState(State::WaitingReader); selectReader(now);
    }
    if (cache_.size() < 256) cache_[key] = {canonical, result};
    return reply(result);
  }

 private:
  struct Reader {
    TagPairReaderObservation report;
    std::uint64_t seenAt{}, freshUntil{};
    bool hasScan{}, usable{};
  };
  struct Removal { std::uint32_t session{}; std::uint8_t player{}; std::uint32_t uid{}; };
  struct Cached { std::array<std::uint8_t, gridopoly::protocol::kTagPairRequestSize> bytes;
                  Result result{}; };
  std::map<std::string, Reader> readers_;
  std::map<std::string, Removal> pending_;
  std::map<std::uint64_t, Cached> cache_;
  Random random_;
  std::uint32_t room_{}, revision_{1}, session_{}, sessionCounter_{}, fence_{};
  std::uint8_t required_{}, active_{};
  State state_{State::Disabled}; Result lastResult_{Result::Ok};
  std::string reader_, previous_, readerBoot_, readerDevice_;
  std::uint64_t deadline_{}, successUntil_{};
  bool emptySeen_{};
  std::uint32_t nextSessionId() { if (++sessionCounter_ == 0) ++sessionCounter_; return sessionCounter_; }
  void newSession() { session_ = nextSessionId(); }
  void bump() { if (++revision_ == 0) ++revision_; }
  void setState(State state) { if (state_ != state) { state_ = state; bump(); } }
  bool accepting() const {
    return state_ == State::ClearRequired || state_ == State::AwaitingTag ||
        state_ == State::AlreadyBound || state_ == State::MultipleTags;
  }
  void nextPlayer(const Tags& tags) {
    previous_ = reader_; reader_.clear(); active_ = 0; newSession();
    for (unsigned i = 0; i < 4; ++i) if ((required_ & (1u << i)) && !tags[i]) {
      active_ = static_cast<std::uint8_t>(i + 1); break;
    }
    setState(active_ ? State::WaitingReader : State::Complete);
  }
  void selectReader(std::uint64_t now) {
    std::vector<std::string> choices;
    for (const auto& pair : readers_) if (pair.second.usable && pair.second.freshUntil > now)
      choices.push_back(pair.first);
    if (choices.empty()) return;
    // A genuinely empty reader is preferable even if it was used last time.
    std::vector<std::string> empty;
    for (const auto& id : choices) if (readers_.at(id).report.tags.empty()) empty.push_back(id);
    if (!empty.empty()) choices.swap(empty);
    std::vector<std::string> free;
    for (const auto& id : choices) if (!pending_.count(id)) free.push_back(id);
    if (!free.empty()) choices.swap(free);
    if (choices.size() > 1) choices.erase(std::remove(choices.begin(), choices.end(), previous_), choices.end());
    reader_ = choices[(random_ ? random_() : 0) % choices.size()];
    const auto& r = readers_.at(reader_);
    readerBoot_ = r.report.bootId; readerDevice_ = r.report.deviceId;
    fence_ = r.report.scanSequence; emptySeen_ = false;
    newSession(); deadline_ = now + kSessionMs;
    setState(State::ClearRequired); bump();
  }
};

}  // namespace gridopoly::pi
