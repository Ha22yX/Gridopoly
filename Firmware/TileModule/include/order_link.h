#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gridopoly::tile::order {
inline constexpr std::uint32_t kPeriodUs = 6'000'000;
inline constexpr std::uint32_t kFreshUs = 15'000'000;
inline constexpr std::uint32_t kTickUs = 1'000;
inline constexpr std::uint32_t kMaximumTickGapUs = 6'000;
inline constexpr std::uint32_t kStuckLowUs = 150'000;
inline constexpr std::size_t kFrameBytes = 12;
using Frame = std::array<std::uint8_t, kFrameBytes>;

struct Beacon {
  std::uint64_t boot_id = 0;
  std::uint16_t sequence = 0;
};

inline bool newerSequence(std::uint16_t next, std::uint16_t previous) {
  const auto delta = static_cast<std::uint16_t>(next - previous);
  return delta != 0 && delta < 0x8000U;
}
inline std::uint16_t crc16(const std::uint8_t *data, std::size_t size) {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8U;
    for (int bit = 0; bit < 8; ++bit)
      crc = static_cast<std::uint16_t>((crc & 0x8000U) ? (crc << 1U) ^ 0x1021U
                                                                    : crc << 1U);
  }
  return crc;
}
inline Frame encode(Beacon beacon) {
  Frame frame{};
  for (std::size_t i = 0; i < 8; ++i)
    frame[i] = static_cast<std::uint8_t>(beacon.boot_id >> ((7U - i) * 8U));
  frame[8] = static_cast<std::uint8_t>(beacon.sequence >> 8U);
  frame[9] = static_cast<std::uint8_t>(beacon.sequence);
  const auto crc = crc16(frame.data(), 10);
  frame[10] = static_cast<std::uint8_t>(crc >> 8U);
  frame[11] = static_cast<std::uint8_t>(crc);
  return frame;
}
inline bool decode(const Frame &frame, Beacon &beacon) {
  const auto expected = static_cast<std::uint16_t>((frame[10] << 8U) | frame[11]);
  if (crc16(frame.data(), 10) != expected) return false;
  Beacon parsed{};
  for (std::size_t i = 0; i < 8; ++i) parsed.boot_id = (parsed.boot_id << 8U) | frame[i];
  parsed.sequence = static_cast<std::uint16_t>((frame[8] << 8U) | frame[9]);
  if (parsed.boot_id == 0) return false;
  beacon = parsed;
  return true;
}

// Return value is GPIO10 gate drive: true pulls the downstream wire LOW.
// The transmitter never waits for HTTP or for any downstream acknowledgement.
class Transmitter {
 public:
  void begin(std::uint64_t boot, std::uint32_t now) {
    boot_ = boot;
    sequence_ = 0;
    phase_ = Phase::Waiting;
    frame_started_ = now - (kPeriodUs - 100'000U);
    drive_ = false;
  }
  void abort(std::uint32_t now) {
    phase_ = Phase::Waiting;
    frame_started_ = now;
    drive_ = false;
  }
  bool tick(std::uint32_t now) {
    if (boot_ == 0) return false;
    if (phase_ == Phase::Waiting) {
      if (static_cast<std::uint32_t>(now - frame_started_) >= kPeriodUs) {
        frame_started_ = now;
        frame_ = encode({boot_, ++sequence_});
        bit_ = 0;
        enter(Phase::SyncLow, now, 90'000, true);
      }
      return drive_;
    }
    if (static_cast<std::uint32_t>(now - phase_started_) < duration_) return drive_;
    switch (phase_) {
      case Phase::SyncLow: enter(Phase::SyncGap, now, 45'000, false); break;
      case Phase::SyncGap: startBit(now); break;
      case Phase::BitLow: enter(Phase::BitGap, now, 15'000, false); break;
      case Phase::BitGap:
        if (++bit_ == kFrameBytes * 8U) {
          phase_ = Phase::Waiting;
          drive_ = false;
        } else startBit(now);
        break;
      case Phase::Waiting: break;
    }
    return drive_;
  }
  std::uint16_t sequence() const { return sequence_; }
 private:
  enum class Phase { Waiting, SyncLow, SyncGap, BitLow, BitGap };
  void enter(Phase phase, std::uint32_t now, std::uint32_t duration, bool drive) {
    phase_ = phase; phase_started_ = now; duration_ = duration; drive_ = drive;
  }
  void startBit(std::uint32_t now) {
    const bool one = (frame_[bit_ / 8U] & (0x80U >> (bit_ % 8U))) != 0;
    enter(Phase::BitLow, now, one ? 30'000 : 15'000, true);
  }
  Frame frame_{};
  std::uint64_t boot_ = 0;
  std::uint16_t sequence_ = 0;
  std::uint16_t bit_ = 0;
  Phase phase_ = Phase::Waiting;
  bool drive_ = false;
  std::uint32_t frame_started_ = 0, phase_started_ = 0, duration_ = 0;
};

class Receiver {
 public:
  void resetCapture(std::uint32_t now, bool high) {
    initialized_ = true; high_ = high; level_started_ = now;
    collecting_ = false; previous_high_us_ = 0;
  }
  void tick(std::uint32_t now, bool high) {
    if (!initialized_) resetCapture(now, high);
    if (high != high_) {
      const auto duration = static_cast<std::uint32_t>(now - level_started_);
      if (high) onLow(duration, now); else onHigh(duration);
      high_ = high; level_started_ = now;
    }
    if (!high && static_cast<std::uint32_t>(now - level_started_) > kStuckLowUs) {
      stuck_low_ = true; valid_ = false; collecting_ = false;
    } else if (high) stuck_low_ = false;
    if (valid_ && static_cast<std::uint32_t>(now - received_) >= kFreshUs) valid_ = false;
    if (collecting_ && static_cast<std::uint32_t>(now - frame_started_) > 5'000'000U)
      collecting_ = false;
  }
  bool valid() const { return valid_; }
  bool receiving() const { return collecting_; }
  bool stuckLow() const { return stuck_low_; }
  Beacon upstream() const { return upstream_; }
  std::uint32_t ageMs(std::uint32_t now) const {
    return valid_ ? static_cast<std::uint32_t>(now - received_) / 1000U : 0;
  }
  std::uint32_t accepted() const { return accepted_; }
  std::uint32_t rejected() const { return rejected_; }
 private:
  static bool between(std::uint32_t n, std::uint32_t lo, std::uint32_t hi) {
    return n >= lo && n <= hi;
  }
  void onHigh(std::uint32_t duration) {
    previous_high_us_ = duration;
    if (!collecting_) return;
    const bool valid_gap = sync_gap_ ? between(duration, 30'000, 60'000)
                                    : between(duration, 9'000, 25'000);
    if (!valid_gap) { collecting_ = false; ++rejected_; }
    sync_gap_ = false;
  }
  void onLow(std::uint32_t duration, std::uint32_t now) {
    if (between(duration, 70'000, 115'000) && previous_high_us_ >= 60'000) {
      collecting_ = true; sync_gap_ = true; bit_ = 0; frame_ = {};
      frame_started_ = now;
      return;
    }
    if (!collecting_) return;
    const bool zero = between(duration, 10'000, 21'000);
    const bool one = between(duration, 24'000, 39'000);
    if (!zero && !one) { collecting_ = false; ++rejected_; return; }
    if (one) frame_[bit_ / 8U] |= static_cast<std::uint8_t>(0x80U >> (bit_ % 8U));
    if (++bit_ != kFrameBytes * 8U) return;
    collecting_ = false;
    Beacon beacon;
    if (!decode(frame_, beacon)) { ++rejected_; return; }
    // Retain the last sequence after timeout too; replay must not revive it.
    if (beacon.boot_id == upstream_.boot_id &&
        !newerSequence(beacon.sequence, upstream_.sequence)) return;
    upstream_ = beacon; received_ = now; valid_ = true; ++accepted_;
  }
  Frame frame_{};
  Beacon upstream_{};
  bool initialized_ = false, high_ = true, collecting_ = false;
  bool sync_gap_ = false, stuck_low_ = false, valid_ = false;
  std::uint16_t bit_ = 0;
  std::uint32_t level_started_ = 0, previous_high_us_ = 0, frame_started_ = 0;
  std::uint32_t received_ = 0, accepted_ = 0, rejected_ = 0;
};

// Shared production timing guard; tests drive the exact same state machine
// without a scheduler or GPIO. A late tick always releases before retrying.
class Link {
 public:
  void begin(std::uint64_t boot, std::uint32_t now, bool input_high) {
    tx_.begin(boot, now);
    rx_ = Receiver{};
    rx_.resetCapture(now, input_high);
    last_tick_ = now; timing_drops_ = 0;
  }
  bool tick(std::uint32_t now, bool input_high) {
    if (static_cast<std::uint32_t>(now - last_tick_) > kMaximumTickGapUs) {
      tx_.abort(now);
      rx_.resetCapture(now, input_high);
      ++timing_drops_;
    }
    last_tick_ = now;
    rx_.tick(now, input_high);
    return tx_.tick(now);
  }
  const Transmitter &transmitter() const { return tx_; }
  const Receiver &receiver() const { return rx_; }
  std::uint32_t timingDrops() const { return timing_drops_; }
 private:
  Transmitter tx_;
  Receiver rx_;
  std::uint32_t last_tick_ = 0, timing_drops_ = 0;
};
}  // namespace gridopoly::tile::order
