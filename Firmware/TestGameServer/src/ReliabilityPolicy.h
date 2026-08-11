#pragma once

#include <cstdint>
#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::server {

enum class InboundDisposition : std::uint8_t {
  Accept,
  ReplayCachedAction,
  Resync,
};

enum class PlayerDetailDisposition : std::uint8_t {
  Generate,
  ReplayCached,
  RejectRequestIdCollision,
  Resync,
};

struct HeartbeatResponsePlan {
  bool sendAck{};
  bool requestSync{};
  bool fullResync{};
};

constexpr std::uint32_t kPeerLivenessTimeoutMs = 15000;
constexpr std::uint32_t kPeerSeatReservationMs = 60000;
constexpr std::uint32_t kPairAcceptRetryIntervalMs = 250;
constexpr std::uint32_t kPairEncryptionPromotionDelayMs = 900;
constexpr std::uint8_t kPairAcceptAttemptCount = 3;

constexpr bool shouldRetryPairAccept(bool encrypted, std::uint32_t promoteAtMs,
                                     std::uint8_t attempts, std::uint32_t lastAttemptAtMs,
                                     std::uint32_t nowMs) {
  return !encrypted && promoteAtMs != 0 && attempts < kPairAcceptAttemptCount &&
         static_cast<std::int32_t>(nowMs - promoteAtMs) < 0 &&
         nowMs - lastAttemptAtMs >= kPairAcceptRetryIntervalMs;
}

constexpr bool isPriorityResponse(gridopoly::protocol::MessageType type) {
  return type == gridopoly::protocol::MessageType::PairAccept ||
         type == gridopoly::protocol::MessageType::ActionResult ||
         type == gridopoly::protocol::MessageType::PlayerDetailResponse ||
         type == gridopoly::protocol::MessageType::TradeResponse ||
         type == gridopoly::protocol::MessageType::IdentitySnapshot ||
         type == gridopoly::protocol::MessageType::Ack;
}

constexpr std::uint8_t transmissionAttemptLimit(gridopoly::protocol::MessageType type) {
  if (isPriorityResponse(type)) return 3;
  if (type == gridopoly::protocol::MessageType::Discover) return 1;
  return 2;
}

constexpr bool isNewerSequence(std::uint32_t candidate, std::uint32_t reference) {
  return candidate != 0 && static_cast<std::int32_t>(candidate - reference) > 0;
}

constexpr InboundDisposition classifyInbound(gridopoly::protocol::MessageType type,
                                             std::uint32_t sequence,
                                             std::uint32_t lastAcceptedSequence,
                                             bool hasCachedAction,
                                             std::uint32_t cachedActionSequence) {
  if (sequence == 0) return InboundDisposition::Resync;
  if (isNewerSequence(sequence, lastAcceptedSequence)) return InboundDisposition::Accept;
  if (type == gridopoly::protocol::MessageType::ActionRequest && hasCachedAction &&
      sequence == cachedActionSequence) {
    return InboundDisposition::ReplayCachedAction;
  }
  return InboundDisposition::Resync;
}

constexpr PlayerDetailDisposition classifyPlayerDetailInbound(
    std::uint32_t sequence, std::uint32_t lastAcceptedSequence,
    std::uint32_t requestId, std::uint8_t targetPlayerId,
    std::uint32_t expectedStateVersion, bool hasCachedResponse,
    std::uint32_t cachedRequestId, std::uint8_t cachedTargetPlayerId,
    std::uint32_t cachedExpectedStateVersion) {
  if (sequence == 0 || requestId == 0 || targetPlayerId == 0) {
    return PlayerDetailDisposition::Resync;
  }
  if (hasCachedResponse && requestId == cachedRequestId) {
    if (targetPlayerId != cachedTargetPlayerId ||
        expectedStateVersion != cachedExpectedStateVersion) {
      return PlayerDetailDisposition::RejectRequestIdCollision;
    }
    return PlayerDetailDisposition::ReplayCached;
  }
  return isNewerSequence(sequence, lastAcceptedSequence)
             ? PlayerDetailDisposition::Generate
             : PlayerDetailDisposition::Resync;
}

constexpr bool heartbeatRequestsFullResync(std::uint32_t appliedStateVersion,
                                           std::uint32_t authorityStateVersion,
                                           std::uint32_t appliedEventSequence,
                                           std::uint32_t latestEventSequence,
                                           std::uint8_t flags) {
  return (flags & 1u) != 0 || appliedStateVersion != authorityStateVersion ||
         appliedEventSequence > latestEventSequence;
}

constexpr HeartbeatResponsePlan planHeartbeatResponse(bool hasCumulativeState,
                                                       std::uint32_t appliedStateVersion,
                                                       std::uint32_t authorityStateVersion,
                                                       std::uint32_t appliedEventSequence,
                                                       std::uint32_t latestEventSequence,
                                                       std::uint8_t flags) {
  if (!hasCumulativeState) return {true, true, false};
  const bool fullResync = heartbeatRequestsFullResync(
      appliedStateVersion, authorityStateVersion, appliedEventSequence, latestEventSequence, flags);
  const bool synchronized = flags == 0 && appliedStateVersion == authorityStateVersion &&
                            appliedEventSequence == latestEventSequence;
  return {true, !synchronized, fullResync};
}

constexpr HeartbeatResponsePlan planIdentityHeartbeatResponse(
    std::uint32_t appliedIdentityStateVersion,
    std::uint32_t authorityStateVersion,
    std::uint8_t flags) {
  // Identity is the sole required projection before Active. Gameplay events
  // and their cursor are intentionally outside this readiness decision.
  return planHeartbeatResponse(true, appliedIdentityStateVersion,
                               authorityStateVersion, 0, 0, flags);
}

}  // namespace gridopoly::server
