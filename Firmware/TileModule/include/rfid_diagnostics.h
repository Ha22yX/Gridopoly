#pragma once

#include <cstddef>
#include <cstdint>

namespace gridopoly::tile::rfid_diagnostics {

enum class AntennaPolicy { Strict, ProbeOnce };
constexpr bool antennaAccepted(bool antenna_ok, AntennaPolicy policy) {
  return antenna_ok || policy == AntennaPolicy::ProbeOnce;
}
constexpr bool captureAccepted(bool config_ok, bool antenna_ok, bool power_ok,
                               bool overflow, bool field_off,
                               std::uint32_t field_ms,
                               AntennaPolicy policy = AntennaPolicy::Strict) {
  return config_ok && antennaAccepted(antenna_ok, policy) && power_ok &&
         !overflow && field_off && field_ms <= 350;
}

class ProbeBudget {
 public:
  static constexpr std::uint32_t kBudgetMs = 6000, kCooldownMs = 60000;
  static constexpr unsigned kMaximumAttempts = 12;
  bool claim(std::uint32_t now) {
    if (active_ || (used_ && static_cast<std::uint32_t>(now - started_) < kCooldownMs))
      return false;
    used_ = active_ = true;
    started_ = now;
    attempts_ = 0;
    return true;
  }
  bool hasTime(std::uint32_t now, std::uint32_t needed_ms) const {
    const auto elapsed = static_cast<std::uint32_t>(now - started_);
    return active_ && elapsed < kBudgetMs && needed_ms <= kBudgetMs - elapsed;
  }
  bool next(std::uint32_t now) {
    if (attempts_ >= kMaximumAttempts || !hasTime(now, 350)) return false;
    ++attempts_;
    return true;
  }
  static constexpr std::uint32_t restMs(std::uint32_t field_ms) {
    return field_ms * 4U > 150U ? field_ms * 4U : 150U;
  }
  void finish() { active_ = false; }
  unsigned attempts() const { return attempts_; }
 private:
  std::uint32_t started_ = 0;
  unsigned attempts_ = 0;
  bool used_ = false, active_ = false;
};

// -1 means not measured at this stage, never a fabricated PASS/FAIL.
struct Record {
  const char *stage = "unknown";
  int digital = -1, config = -1, sampling_ok = -1, antenna = -1;
  int field_off = -1, overflow = -1, safe = -1, power_valid = -1, ina_configured = -1;
  int sampling = -1, field_ms = -1, decode = -1;
  std::uint8_t page_mask = 0, pages[4]{};
  float voltage = 0, current = 0, baseline = 0;
  std::uint16_t edges = 0, pulses = 0;
  std::uint32_t first_edge_us = 0;
  std::uint8_t bits = 0, collision = 0;
  char uid[9] = "--------";
};

class Trace {
 public:
  static constexpr std::uint32_t kWindowMs = 30000;
  static constexpr std::uint32_t kIntervalMs = 1000;
  static constexpr std::size_t kCapacity = 24;

  void enable(std::uint32_t now) {
    enabled_ = true;
    started_ = now;
    have_last_ = false;
  }
  void disable() { enabled_ = capturing_ = false; }
  bool enabled(std::uint32_t now) {
    if (enabled_ && static_cast<std::uint32_t>(now - started_) >= kWindowMs)
      disable();
    return enabled_;
  }
  bool begin(std::uint32_t now, bool one_shot = false) {
    capturing_ = false;
    count_ = dropped_ = 0;
    if (!one_shot && (!enabled(now) ||
        (have_last_ && static_cast<std::uint32_t>(now - last_) < kIntervalMs)))
      return false;
    have_last_ = capturing_ = true;
    last_ = now;
    ++sequence_;
    return true;
  }
  bool capturing() const { return capturing_; }
  void add(const Record &record) {
    if (!capturing_) return;
    if (count_ < kCapacity) records_[count_++] = record;
    else {
      // Preserve the final safety result even when an inventory is very busy.
      records_[kCapacity - 1] = record;
      ++dropped_;
    }
  }
  void finish() { capturing_ = false; }
  std::size_t size() const { return count_; }
  std::size_t dropped() const { return dropped_; }
  const Record &at(std::size_t index) const { return records_[index]; }
  std::uint32_t sequence() const { return sequence_; }

 private:
  Record records_[kCapacity]{};
  std::size_t count_ = 0, dropped_ = 0;
  std::uint32_t started_ = 0, last_ = 0, sequence_ = 0;
  bool enabled_ = false, capturing_ = false, have_last_ = false;
};

}  // namespace gridopoly::tile::rfid_diagnostics
