#pragma once

#include <cstdint>

namespace gridopoly::tile::tag_presence {

// A clean miss is stronger evidence than an isolated successful frame. This
// makes real removal settle after two scans while a single weak-frame miss is
// retained, and prevents a fringe/ghost read from restarting a long timer.
constexpr std::uint8_t kMissingEvidenceThreshold = 3U;
constexpr std::uint8_t kMissingEvidenceGain = 2U;
constexpr std::uint8_t kPresentEvidenceRecovery = 1U;

inline std::uint8_t updateMissingEvidence(std::uint8_t current,
                                          bool observed) {
  if (observed) {
    return current > kPresentEvidenceRecovery
               ? static_cast<std::uint8_t>(current -
                                           kPresentEvidenceRecovery)
               : 0U;
  }
  const std::uint16_t increased =
      static_cast<std::uint16_t>(current) + kMissingEvidenceGain;
  return static_cast<std::uint8_t>(
      increased > kMissingEvidenceThreshold ? kMissingEvidenceThreshold
                                             : increased);
}

inline bool removalConfirmed(std::uint8_t evidence) {
  return evidence >= kMissingEvidenceThreshold;
}

}  // namespace gridopoly::tile::tag_presence
