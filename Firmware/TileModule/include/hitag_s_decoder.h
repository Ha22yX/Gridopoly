#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gridopoly::tile::hitag_s {

struct Pulse {
  std::uint16_t duration_us = 0;
  bool level = false;
};

struct Uid {
  // HITAG S transmits the least-significant byte first: UID0, UID1, UID2,
  // UID3. Keep that wire order here and reverse it only for human display.
  std::uint8_t wire_bytes[4]{};
};

enum class DecodeStatus : std::uint8_t {
  NoResponse,
  Present,
  Ambiguous,
  Collision,
};

struct DecodeResult {
  DecodeStatus status = DecodeStatus::NoResponse;
  Uid uid{};
  std::uint16_t quarter_us = 0;
  std::size_t consumed_pulses = 0;
  std::size_t start_pulse = 0;
  bool product_id_valid = false;
  std::uint8_t decoded_bits = 0;
  std::uint8_t collision_bit = 0xFFU;
};

inline bool equalUid(const Uid &left, const Uid &right) {
  return std::memcmp(left.wire_bytes, right.wire_bytes,
                     sizeof(left.wire_bytes)) == 0;
}

inline bool hasHitagSProductIdentifier(const Uid &uid) {
  const std::uint8_t pid1 = uid.wire_bytes[3] >> 4U;
  const std::uint8_t pid0 = uid.wire_bytes[3] & 0x0FU;
  return pid1 >= 0x07U && pid1 <= 0x0FU && pid0 != 0x05U && pid0 != 0x06U;
}

inline void formatUid(const Uid &uid, char output[9]) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::size_t output_index = 0;
  for (int wire_index = 3; wire_index >= 0; --wire_index) {
    const std::uint8_t value = uid.wire_bytes[wire_index];
    output[output_index++] = kHex[value >> 4U];
    output[output_index++] = kHex[value & 0x0FU];
  }
  output[8] = '\0';
}

inline std::uint8_t uidBit(const Uid &uid, std::size_t bit_index) {
  if (bit_index >= 32U) {
    return 0U;
  }
  return static_cast<std::uint8_t>(
      (uid.wire_bytes[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U);
}

inline void setUidBit(Uid &uid, std::size_t bit_index, std::uint8_t value) {
  if (bit_index >= 32U) {
    return;
  }
  const std::uint8_t mask = static_cast<std::uint8_t>(
      1U << (7U - (bit_index % 8U)));
  if (value != 0U) {
    uid.wire_bytes[bit_index / 8U] |= mask;
  } else {
    uid.wire_bytes[bit_index / 8U] &= static_cast<std::uint8_t>(~mask);
  }
}

inline bool equalUidBits(const Uid &left, const Uid &right,
                         std::size_t bit_count) {
  for (std::size_t bit = 0; bit < bit_count && bit < 32U; ++bit) {
    if (uidBit(left, bit) != uidBit(right, bit)) {
      return false;
    }
  }
  return true;
}

namespace detail {

inline std::uint32_t absoluteDifference(std::uint32_t left,
                                        std::uint32_t right) {
  return left > right ? left - right : right - left;
}

inline bool nearDuration(std::uint32_t measured, std::uint32_t expected,
                         std::uint32_t tolerance) {
  return absoluteDifference(measured, expected) <= tolerance;
}

}  // namespace detail

inline DecodeResult decodeAdvancedBits(const Pulse *raw_pulses,
                                       std::size_t raw_pulse_count,
                                       std::size_t expected_bit_count) {
  constexpr std::size_t kMaximumPulses = 1024;
  constexpr std::size_t kAdvancedSofPulses = 12;
  constexpr std::uint16_t kNominalShortUs = 128;
  constexpr std::uint16_t kMinimumShortUs = 80;
  // The first SOF edge after receiver settle can stretch slightly; COM12's
  // clean capture measured 184 us while the 12-pulse mean remained 132 us.
  constexpr std::uint16_t kMaximumShortUs = 200;
  constexpr std::uint16_t kGlitchLimitUs = 40;

  DecodeResult result;
  if (raw_pulses == nullptr || expected_bit_count == 0U ||
      expected_bit_count > 32U || raw_pulse_count < kAdvancedSofPulses + 2U) {
    return result;
  }

  // Suppress a short opposite-level spike by joining the equal-level pulses
  // on either side. Buffers live in static storage to protect the ESP32 loop
  // task's limited stack; this decoder has one owner and is never called in ISR.
  static Pulse pulses[kMaximumPulses]{};
  std::size_t pulse_count = 0;
  for (std::size_t index = 0;
       index < raw_pulse_count && pulse_count < kMaximumPulses;) {
    const Pulse &pulse = raw_pulses[index];
    if (pulse.duration_us < kGlitchLimitUs && pulse_count != 0U &&
        index + 1U < raw_pulse_count &&
        pulses[pulse_count - 1U].level == raw_pulses[index + 1U].level) {
      const std::uint32_t merged =
          static_cast<std::uint32_t>(pulses[pulse_count - 1U].duration_us) +
          pulse.duration_us + raw_pulses[index + 1U].duration_us;
      pulses[pulse_count - 1U].duration_us = static_cast<std::uint16_t>(
          merged > 0xFFFFU ? 0xFFFFU : merged);
      index += 2U;
      continue;
    }
    pulses[pulse_count++] = pulse;
    ++index;
  }

  bool pid_found = false;
  bool pid_ambiguous = false;
  Uid pid_uid{};
  std::uint16_t pid_quarter_us = 0;
  std::size_t pid_consumed = 0;
  std::size_t pid_start = 0;
  bool fallback_found = false;
  bool fallback_ambiguous = false;
  Uid fallback_uid{};
  std::uint16_t fallback_quarter_us = 0;
  std::size_t fallback_consumed = 0;
  std::size_t fallback_start = 0;
  bool collision_found = false;
  Uid collision_prefix{};
  std::uint8_t collision_bit = 0;
  std::uint16_t collision_quarter_us = 0;
  std::size_t collision_consumed = 0;
  std::size_t collision_start = 0;

  for (std::size_t start = 0;
       start + kAdvancedSofPulses + 2U <= pulse_count; ++start) {
    std::uint32_t sof_sum_us = 0;
    bool sof_valid = true;
    for (std::size_t pulse = 0; pulse < kAdvancedSofPulses; ++pulse) {
      const std::uint16_t duration = pulses[start + pulse].duration_us;
      if (duration < kMinimumShortUs || duration > kMaximumShortUs) {
        sof_valid = false;
        break;
      }
      sof_sum_us += duration;
    }
    if (!sof_valid) {
      continue;
    }

    const std::uint16_t short_us = static_cast<std::uint16_t>(
        (sof_sum_us + kAdvancedSofPulses / 2U) / kAdvancedSofPulses);
    if (detail::absoluteDifference(short_us, kNominalShortUs) > 48U) {
      continue;
    }
    const std::uint32_t short_tolerance = short_us * 35U / 100U;
    const std::uint32_t long_us = short_us * 2U;
    const std::uint32_t long_tolerance = long_us * 30U / 100U;

    std::size_t cursor = start + kAdvancedSofPulses;
    Uid uid{};
    bool data_valid = true;
    for (std::size_t bit_index = 0; bit_index < expected_bit_count; ++bit_index) {
      const bool final_bit = bit_index + 1U == expected_bit_count;
      bool one = cursor + 4U <= pulse_count;
      if (one) {
        for (std::size_t half = 0; half < 4U; ++half) {
          const std::uint16_t duration = pulses[cursor + half].duration_us;
          // The final AC2K half-bit can merge with receiver idle. Ignore its
          // upper bound even when later receiver-recovery noise adds edges.
          const bool trailing_run = final_bit && half == 3U;
          const bool matches = trailing_run
                                   ? duration + short_tolerance >= short_us
                                   : detail::nearDuration(duration, short_us,
                                                          short_tolerance);
          if (!matches) {
            one = false;
            break;
          }
        }
      }

      bool zero = cursor + 2U <= pulse_count;
      if (zero) {
        for (std::size_t half = 0; half < 2U; ++half) {
          const std::uint16_t duration = pulses[cursor + half].duration_us;
          const bool trailing_run = final_bit && half == 1U;
          const bool matches = trailing_run
                                   ? duration + long_tolerance >= long_us
                                   : detail::nearDuration(duration, long_us,
                                                          long_tolerance);
          if (!matches) {
            zero = false;
            break;
          }
        }
      }

      if (one == zero) {
        const std::uint32_t collision_long_us = short_us * 3U;
        const std::uint32_t collision_long_tolerance = short_us * 55U / 100U;
        const bool collision_pattern =
            cursor + 2U <= pulse_count &&
            ((detail::nearDuration(pulses[cursor].duration_us,
                                   collision_long_us,
                                   collision_long_tolerance) &&
              detail::nearDuration(pulses[cursor + 1U].duration_us, short_us,
                                   short_tolerance)) ||
             (detail::nearDuration(pulses[cursor].duration_us, short_us,
                                   short_tolerance) &&
              detail::nearDuration(pulses[cursor + 1U].duration_us,
                                   collision_long_us,
                                   collision_long_tolerance)));
        // Prefer the earliest valid SOF. Later AC data can resemble another
        // SOF and previously produced a deeper but fabricated UID collision.
        if (collision_pattern && !collision_found) {
          collision_found = true;
          collision_prefix = uid;
          collision_bit = static_cast<std::uint8_t>(bit_index);
          collision_quarter_us = short_us;
          collision_consumed = cursor + 2U - start;
          collision_start = start;
        }
        data_valid = false;
        break;
      }
      const std::uint8_t bit = one ? 1U : 0U;
      setUidBit(uid, bit_index, bit);
      cursor += one ? 4U : 2U;
    }
    if (!data_valid) {
      continue;
    }

    if (expected_bit_count == 32U && hasHitagSProductIdentifier(uid)) {
      if (!pid_found) {
        pid_found = true;
        pid_uid = uid;
        pid_quarter_us = short_us;
        pid_consumed = cursor - start;
        pid_start = start;
      } else if (!equalUid(pid_uid, uid)) {
        pid_ambiguous = true;
      }
    } else if (!fallback_found) {
      fallback_found = true;
      fallback_uid = uid;
      fallback_quarter_us = short_us;
      fallback_consumed = cursor - start;
      fallback_start = start;
    } else if (!equalUidBits(fallback_uid, uid, expected_bit_count)) {
      fallback_ambiguous = true;
    }
  }

  if (pid_found) {
    if (pid_ambiguous) {
      result.status = DecodeStatus::Ambiguous;
      return result;
    }
    result.status = DecodeStatus::Present;
    result.uid = pid_uid;
    result.quarter_us = pid_quarter_us;
    result.consumed_pulses = pid_consumed;
    result.start_pulse = pid_start;
    result.product_id_valid = true;
    result.decoded_bits = static_cast<std::uint8_t>(expected_bit_count);
  } else if (fallback_found) {
    if (fallback_ambiguous) {
      result.status = DecodeStatus::Ambiguous;
      return result;
    }
    result.status = DecodeStatus::Present;
    result.uid = fallback_uid;
    result.quarter_us = fallback_quarter_us;
    result.consumed_pulses = fallback_consumed;
    result.start_pulse = fallback_start;
    result.decoded_bits = static_cast<std::uint8_t>(expected_bit_count);
  } else if (collision_found) {
    result.status = DecodeStatus::Collision;
    result.uid = collision_prefix;
    result.quarter_us = collision_quarter_us;
    result.consumed_pulses = collision_consumed;
    result.start_pulse = collision_start;
    result.decoded_bits = collision_bit;
    result.collision_bit = collision_bit;
  }
  return result;
}

inline DecodeResult decodeAdvancedUid(const Pulse *raw_pulses,
                                      std::size_t raw_pulse_count) {
  return decodeAdvancedBits(raw_pulses, raw_pulse_count, 32U);
}

}  // namespace gridopoly::tile::hitag_s
