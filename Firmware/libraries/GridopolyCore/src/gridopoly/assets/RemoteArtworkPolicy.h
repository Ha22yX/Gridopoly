#pragma once
#include <cstddef>
#include <cstdint>

namespace gridopoly { namespace assets {

// Callers seed entropy from device identity AND fresh boot/random entropy.
// No filesystem/NVS writes: images and retry state live only in RAM.
inline std::uint32_t artworkEntropy(std::uint32_t seed, const char* key,
                                    std::uint8_t level, std::uint32_t attempt = 0) {
  std::uint32_t value = 2166136261u ^ seed;
  if (key) while (*key) { value = (value ^ static_cast<std::uint8_t>(*key++)) * 16777619u; }
  value = (value ^ level) * 16777619u;
  value = (value ^ attempt) * 16777619u;
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  return value ^ (value >> 16);
}

inline std::uint32_t initialArtworkDelayMs(std::uint32_t entropy) {
  return 250u + entropy % 1751u;
}

// failures=1 is the first failure. Retry-After is a minimum, not a replacement
// for jitter; a crowded group should not reconnect at the same exact second.
inline std::uint32_t retryArtworkDelayMs(std::uint8_t failures,
                                        std::uint32_t entropy,
                                        std::uint32_t retryAfterSeconds = 0) {
  const auto exponent = failures > 5 ? 5u : (failures > 0 ? failures - 1u : 0u);
  auto base = 1000u << exponent;
  if (base > 30000u) base = 30000u;
  const auto serverMinimum = (retryAfterSeconds > 120u ? 120u : retryAfterSeconds) * 1000u;
  if (serverMinimum > base) base = serverMinimum;
  return base + entropy % 1001u;
}

inline bool artworkDeadlineReached(std::uint32_t now, std::uint32_t deadline) {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

// The server emits delta-seconds. Invalid values (including HTTP dates) use
// normal backoff; clamp before arithmetic to avoid an untrusted overflow.
inline std::uint32_t artworkRetryAfterSeconds(const char* value) {
  if (!value) return 0;
  while (*value == ' ' || *value == '\t') ++value;
  if (*value < '0' || *value > '9') return 0;
  std::uint32_t result = 0;
  while (*value >= '0' && *value <= '9') {
    result = result >= 120u ? 120u : result * 10u + static_cast<unsigned>(*value - '0');
    if (result > 120u) result = 120u;
    ++value;
  }
  while (*value == ' ' || *value == '\t') ++value;
  return *value == '\0' ? result : 0u;
}

static constexpr std::uint32_t kArtworkCrc32Initial = 0xFFFFFFFFu;
inline std::uint32_t artworkCrc32Update(std::uint32_t state,
                                       const std::uint8_t* bytes, std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    state ^= bytes[index];
    for (unsigned bit = 0; bit < 8; ++bit)
      state = (state >> 1) ^ ((state & 1u) ? 0xEDB88320u : 0u);
  }
  return state;
}
inline std::uint32_t artworkCrc32Finish(std::uint32_t state) { return state ^ 0xFFFFFFFFu; }
inline std::uint32_t artworkCrc32(const std::uint8_t* bytes, std::size_t length) {
  return artworkCrc32Finish(artworkCrc32Update(kArtworkCrc32Initial, bytes, length));
}

} }  // namespace gridopoly::assets
