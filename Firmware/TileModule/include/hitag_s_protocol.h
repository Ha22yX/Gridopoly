#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hitag_s_decoder.h"

namespace gridopoly::tile::hitag_s {

constexpr std::size_t kMaximumInventoryTags = 6U;
constexpr std::size_t kMaximumReaderFrameBits = 45U;

struct BitBuffer {
  std::uint8_t bytes[6]{};
  std::uint8_t bit_count = 0;
};

struct UidPrefix {
  Uid uid{};
  std::uint8_t bit_count = 0;
};

struct UidSet {
  Uid uids[kMaximumInventoryTags]{};
  std::uint8_t count = 0;
  bool overflow = false;
};

inline std::uint8_t bitAt(const std::uint8_t *bytes, std::size_t bit_index) {
  return static_cast<std::uint8_t>(
      (bytes[bit_index / 8U] >> (7U - (bit_index % 8U))) & 1U);
}

inline bool appendBit(BitBuffer &buffer, std::uint8_t bit) {
  if (buffer.bit_count >= kMaximumReaderFrameBits) {
    return false;
  }
  if (bit != 0U) {
    buffer.bytes[buffer.bit_count / 8U] |= static_cast<std::uint8_t>(
        1U << (7U - (buffer.bit_count % 8U)));
  }
  ++buffer.bit_count;
  return true;
}

inline std::uint8_t crc8HitagBits(const std::uint8_t *bytes,
                                 std::size_t bit_count) {
  std::uint8_t crc = 0xFFU;
  for (std::size_t bit = 0; bit < bit_count; ++bit) {
    const bool feedback = ((crc & 0x80U) != 0U) ^
                          (bitAt(bytes, bit) != 0U);
    crc = static_cast<std::uint8_t>(crc << 1U);
    if (feedback) {
      crc ^= 0x1DU;
    }
  }
  return crc;
}

inline bool buildAnticollisionCommand(const UidPrefix &prefix,
                                      BitBuffer &command) {
  command = {};
  if (prefix.bit_count == 0U || prefix.bit_count >= 32U) {
    return false;
  }
  for (int bit = 4; bit >= 0; --bit) {
    if (!appendBit(command,
                   static_cast<std::uint8_t>((prefix.bit_count >> bit) & 1U))) {
      return false;
    }
  }
  for (std::size_t bit = 0; bit < prefix.bit_count; ++bit) {
    if (!appendBit(command, uidBit(prefix.uid, bit))) {
      return false;
    }
  }
  const std::uint8_t crc = crc8HitagBits(command.bytes, command.bit_count);
  for (int bit = 7; bit >= 0; --bit) {
    if (!appendBit(command, static_cast<std::uint8_t>((crc >> bit) & 1U))) {
      return false;
    }
  }
  return true;
}

inline bool extendPrefix(const UidPrefix &base, const Uid &response_prefix,
                         std::uint8_t response_bit_count,
                         std::uint8_t branch_bit, UidPrefix &extended) {
  const std::size_t next_count = static_cast<std::size_t>(base.bit_count) +
                                 response_bit_count + 1U;
  if (next_count > 32U) {
    return false;
  }
  extended = base;
  for (std::size_t bit = 0; bit < response_bit_count; ++bit) {
    setUidBit(extended.uid, extended.bit_count++, uidBit(response_prefix, bit));
  }
  setUidBit(extended.uid, extended.bit_count++, branch_bit);
  return true;
}

inline Uid combinePrefixAndSuffix(const UidPrefix &prefix, const Uid &suffix,
                                  std::uint8_t suffix_bit_count) {
  Uid result = prefix.uid;
  for (std::size_t bit = 0;
       bit < suffix_bit_count && prefix.bit_count + bit < 32U; ++bit) {
    setUidBit(result, prefix.bit_count + bit, uidBit(suffix, bit));
  }
  return result;
}

inline int compareUid(const Uid &left, const Uid &right) {
  for (std::size_t bit = 0; bit < 32U; ++bit) {
    const std::uint8_t left_bit = uidBit(left, bit);
    const std::uint8_t right_bit = uidBit(right, bit);
    if (left_bit != right_bit) {
      return left_bit < right_bit ? -1 : 1;
    }
  }
  return 0;
}

inline bool addUniqueUid(UidSet &set, const Uid &uid) {
  for (std::size_t index = 0; index < set.count; ++index) {
    if (equalUid(set.uids[index], uid)) {
      return true;
    }
  }
  if (set.count >= kMaximumInventoryTags) {
    set.overflow = true;
    return false;
  }
  std::size_t insert_at = set.count;
  while (insert_at != 0U && compareUid(uid, set.uids[insert_at - 1U]) < 0) {
    set.uids[insert_at] = set.uids[insert_at - 1U];
    --insert_at;
  }
  set.uids[insert_at] = uid;
  ++set.count;
  return true;
}

inline bool containsUid(const UidSet &set, const Uid &uid) {
  for (std::size_t index = 0; index < set.count; ++index) {
    if (equalUid(set.uids[index], uid)) {
      return true;
    }
  }
  return false;
}

inline bool equalUidSet(const UidSet &left, const UidSet &right) {
  if (left.count != right.count || left.overflow != right.overflow) {
    return false;
  }
  for (std::size_t index = 0; index < left.count; ++index) {
    if (!equalUid(left.uids[index], right.uids[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace gridopoly::tile::hitag_s
