#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../../libraries/GridopolyCore/src/gridopoly/assets/DevelopmentArtworkCatalog.h"
#include "../../libraries/GridopolyCore/src/gridopoly/assets/RemoteArtworkPolicy.h"

namespace gridopoly::tile::development {

namespace policy = gridopoly::assets;
constexpr std::size_t kImageBytes = policy::kTileDevelopmentImageBytes;
constexpr unsigned kReadyLimit = 5, kSlotCount = kReadyLimit + 1;
constexpr unsigned kNoSlot = kSlotCount;

struct Key {
  const policy::DevelopmentProperty *property = nullptr;
  std::uint8_t level = 0;
  static Key from(const char *artwork_key, std::uint8_t level, bool property) {
    const auto *entry = property && policy::validDevelopmentLevel(level)
                            ? policy::developmentProperty(artwork_key) : nullptr;
    return {entry, entry ? level : std::uint8_t{0}};
  }
  bool valid() const { return property && policy::validDevelopmentLevel(level); }
  std::uint32_t crc() const {
    return policy::developmentCrc32(property, level, policy::DevelopmentTarget::Tile);
  }
  bool operator==(const Key &other) const {
    if (!valid() || !other.valid()) return !valid() && !other.valid();
    return level == other.level && std::strcmp(property->key, other.property->key) == 0;
  }
  bool operator!=(const Key &other) const { return !(*this == other); }
};

enum class TransferState : std::uint8_t {
  Complete, HttpError, LengthError, Truncated, Timeout, CrcError, Cancelled,
  EncodingError, NoMemory, QueueError
};
struct TransferResult {
  TransferState state = TransferState::HttpError;
  int http_status = 0;
  int content_length = -1;
  std::size_t received = 0;
  std::uint32_t crc = 0, retry_after_seconds = 0;
};
struct Request {
  Key key{};
  std::uint32_t generation = 0;
  unsigned slot = kNoSlot;
  std::uint8_t *bytes = nullptr;
};

// All methods/leases belong to the display task. The worker receives only a
// staging Request and must return it before that slot can be reused. No mutex
// is held while drawing or doing HTTP. Destruction requires a stopped worker.
class Cache {
 public:
  using Allocate = std::uint8_t *(*)(std::size_t);
  using Release = void (*)(void *);
  class Lease {
   public:
    Lease() = default;
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    Lease(Lease &&other) noexcept : owner_(other.owner_), slot_(other.slot_) {
      other.owner_ = nullptr;
    }
    Lease &operator=(Lease &&other) noexcept {
      if (this != &other) {
        reset(); owner_ = other.owner_; slot_ = other.slot_; other.owner_ = nullptr;
      }
      return *this;
    }
    ~Lease() { reset(); }
    const std::uint16_t *pixels() const {
      return owner_ ? reinterpret_cast<const std::uint16_t *>(owner_->slots_[slot_].bytes)
                    : nullptr;
    }
    void reset() {
      if (owner_) { --owner_->slots_[slot_].pins; owner_ = nullptr; }
    }
   private:
    friend class Cache;
    Lease(Cache *owner, unsigned slot) : owner_(owner), slot_(slot) {}
    Cache *owner_ = nullptr;
    unsigned slot_ = kNoSlot;
  };

  Cache(Allocate allocate, Release release) : allocate_(allocate), release_(release) {}
  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;
  ~Cache() { for (auto &slot : slots_) if (slot.bytes) release_(slot.bytes); }

  void select(Key key, std::uint32_t now, std::uint32_t entropy) {
    if (key == selected_) return;  // Repeated heartbeats never restart the timer.
    selected_ = key;
    ++generation_;
    selection_deadline_ = now + policy::initialArtworkDelayMs(entropy);
    // A new assignment or L0 must not bypass a server Retry-After/backoff.
  }
  void online(bool value, std::uint32_t now, std::uint32_t entropy) {
    if (value && !online_)
      selection_deadline_ = now + policy::initialArtworkDelayMs(entropy);
    online_ = value;
  }
  Lease acquire(Key key) {
    const unsigned slot = find(key);
    if (slot == kNoSlot) return {};
    ++slots_[slot].pins;
    slots_[slot].used = ++use_sequence_;
    return Lease(this, slot);
  }
  bool next(std::uint32_t now, std::uint32_t entropy, Request &request) {
    if (!online_ || busy_ || !selected_.valid() || find(selected_) != kNoSlot ||
        !policy::artworkDeadlineReached(now, selection_deadline_) ||
        (cooling_ && !policy::artworkDeadlineReached(now, retry_deadline_))) return false;
    unsigned slot = kNoSlot;
    for (unsigned i = 0; i < kSlotCount; ++i)
      if (!slots_[i].ready && slots_[i].pins == 0) { slot = i; break; }
    if (slot == kNoSlot) return false;
    if (!slots_[slot].bytes) slots_[slot].bytes = allocate_(kImageBytes);
    if (!slots_[slot].bytes) {
      failed(now, entropy, 0); ++allocation_failures_; return false;
    }
    busy_ = true;
    active_ = {selected_, generation_, slot, slots_[slot].bytes};
    request = active_;
    ++requests_;
    return true;
  }
  bool finish(const Request &request, const TransferResult &result,
              std::uint32_t now, std::uint32_t entropy) {
    if (!busy_ || request.slot != active_.slot || request.generation != active_.generation ||
        request.bytes != active_.bytes || request.key != active_.key) return false;
    busy_ = false;
    const bool complete = result.state == TransferState::Complete &&
                          result.http_status == 200 && result.content_length == static_cast<int>(kImageBytes) &&
                          result.received == kImageBytes && result.crc == request.key.crc();
    if (!complete) {
      // Retain throttling even when a failed request belongs to an old selection.
      failed(now, entropy, result.retry_after_seconds);
      return false;
    }
    if (request.generation != generation_ || request.key != selected_) {
      ++discarded_; return false;  // Includes A -> B -> A during one download.
    }
    if (readyCount() >= kReadyLimit) {
      unsigned victim = kNoSlot;
      for (unsigned i = 0; i < kSlotCount; ++i) {
        if (slots_[i].ready && slots_[i].pins == 0 &&
            (victim == kNoSlot || slots_[i].used < slots_[victim].used)) victim = i;
      }
      if (victim == kNoSlot) { failed(now, entropy, 0); return false; }
      slots_[victim].ready = false;
    }
    Slot &slot = slots_[request.slot];
    slot.key = request.key;
    slot.used = ++use_sequence_;
    slot.ready = true;  // Publication happens only here, on the display owner.
    failures_ = 0; cooling_ = false; ++published_;
    return true;
  }
  std::uint32_t generation() const { return generation_; }
  bool busy() const { return busy_; }
  unsigned readyCount() const {
    unsigned count = 0; for (const auto &slot : slots_) if (slot.ready) ++count; return count;
  }
  std::size_t allocatedBytes() const {
    std::size_t bytes = 0; for (const auto &slot : slots_) if (slot.bytes) bytes += kImageBytes;
    return bytes;
  }
  std::uint32_t requests() const { return requests_; }
  std::uint32_t published() const { return published_; }
  std::uint32_t discarded() const { return discarded_; }
  std::uint32_t allocationFailures() const { return allocation_failures_; }
  std::uint8_t failures() const { return failures_; }

 private:
  struct Slot {
    std::uint8_t *bytes = nullptr;
    Key key{};
    bool ready = false;
    unsigned pins = 0;
    std::uint64_t used = 0;
  } slots_[kSlotCount];
  unsigned find(Key key) const {
    if (key.valid()) for (unsigned i = 0; i < kSlotCount; ++i)
      if (slots_[i].ready && slots_[i].key == key) return i;
    return kNoSlot;
  }
  void failed(std::uint32_t now, std::uint32_t entropy, std::uint32_t retry_after) {
    if (failures_ < 255) ++failures_;
    retry_deadline_ = now + policy::retryArtworkDelayMs(failures_, entropy, retry_after);
    cooling_ = true;
  }
  Allocate allocate_;
  Release release_;
  Key selected_{};
  Request active_{};
  bool online_ = false, busy_ = false, cooling_ = false;
  std::uint32_t generation_ = 0, selection_deadline_ = 0, retry_deadline_ = 0;
  std::uint32_t requests_ = 0, published_ = 0, discarded_ = 0, allocation_failures_ = 0;
  std::uint64_t use_sequence_ = 0;
  std::uint8_t failures_ = 0;
};
}  // namespace gridopoly::tile::development
