#include "Protocol.h"

#include <cstring>

namespace gridopoly::protocol {
namespace {

void put16(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value);
  out[1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value);
  out[1] = static_cast<std::uint8_t>(value >> 8);
  out[2] = static_cast<std::uint8_t>(value >> 16);
  out[3] = static_cast<std::uint8_t>(value >> 24);
}

void put64(std::uint8_t* out, std::uint64_t value) {
  put32(out, static_cast<std::uint32_t>(value));
  put32(out + 4, static_cast<std::uint32_t>(value >> 32));
}

std::uint16_t get16(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) | (static_cast<std::uint16_t>(in[1]) << 8);
}

std::uint32_t get32(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* in) {
  return static_cast<std::uint64_t>(get32(in)) |
      (static_cast<std::uint64_t>(get32(in + 4)) << 32);
}

bool zeroRecipe(const AvatarRecipe& value) {
  return value.avatarCatalogVersion == 0 && value.hairPresetId == 0 &&
      value.hairColorId == 0 && value.facePresetId == 0 &&
      value.skinToneId == 0 && value.outfitPresetId == 0;
}

bool validV1Recipe(const AvatarRecipe& value) {
  return value.avatarCatalogVersion == kAvatarCatalogVersionV1 &&
      value.hairPresetId >= 1 && value.hairPresetId <= 10 &&
      value.hairColorId >= 1 && value.hairColorId <= 20 &&
      value.facePresetId >= 1 && value.facePresetId <= 10 &&
      value.skinToneId >= 1 && value.skinToneId <= 8 &&
      value.outfitPresetId >= 1 && value.outfitPresetId <= 10;
}

bool validIdentityRequestValue(const IdentityRequest& value) {
  if (value.playerId == 0 || value.playerId > 6 || value.requestId == 0 ||
      value.nameLength > 16) {
    return false;
  }
  for (std::size_t index = value.nameLength; index < value.name.size(); ++index) {
    if (value.name[index] != '\0') return false;
  }
  switch (value.operation) {
    case IdentityOperation::Query:
      return value.expectedStateVersion == 0 && value.expectedSeatRevision == 0 &&
          value.avatarCatalogVersion == 0 && zeroRecipe(value.recipe) && value.nameLength == 0;
    case IdentityOperation::ConfirmAvatar:
      return value.expectedStateVersion != 0 && value.expectedSeatRevision != 0 &&
          value.avatarCatalogVersion == kAvatarCatalogVersionV1 &&
          value.recipe.avatarCatalogVersion == value.avatarCatalogVersion &&
          validV1Recipe(value.recipe) && value.nameLength == 0;
    case IdentityOperation::ConfirmName:
      return value.expectedStateVersion != 0 && value.expectedSeatRevision != 0 &&
          value.avatarCatalogVersion == 0 && zeroRecipe(value.recipe) && value.nameLength != 0;
    default: return false;
  }
}

bool validIdentitySnapshotValue(const IdentitySnapshot& value) {
  const auto roomPhase = static_cast<std::uint8_t>(value.roomPhase);
  const auto selfStage = static_cast<std::uint8_t>(value.selfStage);
  const auto result = static_cast<std::uint8_t>(value.result);
  const auto operation = static_cast<std::uint8_t>(value.operationEcho);
  if (roomPhase < 1 || roomPhase > 3 || selfStage < 1 || selfStage > 6 || result > 11 ||
      operation > 3 || value.playerCount == 0 || value.playerCount > value.seats.size() ||
      value.selfPlayerId == 0 || value.selfPlayerId > value.playerCount ||
      value.avatarCatalogVersion != kAvatarCatalogVersionV1 ||
      (value.flags & ~(IdentitySnapshotFlagReplay | IdentitySnapshotFlagResync)) != 0 ||
      ((value.requestId == 0) != (value.operationEcho == IdentityOperation::None))) {
    return false;
  }
  const auto validMask = static_cast<std::uint8_t>((1u << value.playerCount) - 1u);
  if (((value.requiredHumanMask | value.avatarFinalMask | value.nameFinalMask |
        value.readyMask | value.onlineMask) & static_cast<std::uint8_t>(~validMask)) != 0) {
    return false;
  }
  for (std::size_t index = 0; index < value.seats.size(); ++index) {
    const auto& seat = value.seats[index];
    if (index >= value.playerCount) {
      if (seat.playerId != 0 || seat.flags != 0 || seat.seatColorId != 0 ||
          seat.seatRevision != 0 || seat.avatarRevision != 0 ||
          seat.avatarContentHash64 != 0 || !zeroRecipe(seat.recipe)) {
        return false;
      }
      continue;
    }
    if (seat.playerId != index + 1 || (seat.flags & IdentitySeatPresent) == 0 ||
        ((seat.flags & IdentitySeatHuman) != 0) == ((seat.flags & IdentitySeatBot) != 0) ||
        seat.seatColorId == 0 || seat.seatRevision == 0) {
      return false;
    }
    const bool final = (seat.flags & IdentitySeatAvatarFinal) != 0;
    if (final != (seat.avatarRevision != 0 && seat.avatarContentHash64 != 0 &&
                  validV1Recipe(seat.recipe))) {
      return false;
    }
    if (!final && (seat.avatarRevision != 0 || seat.avatarContentHash64 != 0 ||
                   !zeroRecipe(seat.recipe))) {
      return false;
    }
  }
  return true;
}

bool roomFor(std::size_t need, std::size_t capacity) { return need <= capacity; }

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t length) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool encodeFrame(const Header& header, const std::uint8_t* payload, std::size_t payloadLength,
                 std::uint8_t* output, std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || payloadLength > kMaxPayloadSize || !roomFor(kHeaderSize + payloadLength, capacity) ||
      (payloadLength != 0 && payload == nullptr)) {
    return false;
  }
  put32(output + 0, kMagic);
  output[4] = kVersion;
  output[5] = static_cast<std::uint8_t>(header.type);
  put16(output + 6, header.flags);
  put32(output + 8, header.sequence);
  put32(output + 12, header.acknowledgement);
  put32(output + 16, header.roomId);
  put32(output + 20, header.deviceId);
  put16(output + 24, static_cast<std::uint16_t>(payloadLength));
  put16(output + 26, static_cast<std::uint16_t>(kHeaderSize));
  const auto payloadCrc = crc32(payload, payloadLength);
  put32(output + 28, payloadCrc);
  if (payloadLength != 0) std::memcpy(output + kHeaderSize, payload, payloadLength);
  written = kHeaderSize + payloadLength;
  return true;
}

bool decodeFrame(const std::uint8_t* input, std::size_t length, DecodedFrame& output) {
  if (input == nullptr || length < kHeaderSize || length > kMaxFrameSize || get32(input) != kMagic ||
      input[4] != kVersion || get16(input + 26) != kHeaderSize) {
    return false;
  }
  const auto payloadLength = get16(input + 24);
  if (length != kHeaderSize + payloadLength || payloadLength > kMaxPayloadSize) return false;
  if (crc32(input + kHeaderSize, payloadLength) != get32(input + 28)) return false;
  output.header.type = static_cast<MessageType>(input[5]);
  output.header.flags = get16(input + 6);
  output.header.sequence = get32(input + 8);
  output.header.acknowledgement = get32(input + 12);
  output.header.roomId = get32(input + 16);
  output.header.deviceId = get32(input + 20);
  output.header.payloadLength = payloadLength;
  output.header.payloadCrc32 = get32(input + 28);
  output.payload = input + kHeaderSize;
  return true;
}

bool encodeStateSnapshot(const StateSnapshot& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written) {
  constexpr std::size_t baseSize = 44;
  constexpr std::size_t playerSize = 7;
  if (value.playerCount > value.players.size()) return false;
  const auto need = baseSize + value.playerCount * playerSize;
  if (output == nullptr || !roomFor(need, capacity)) return false;
  output[0] = 2;
  output[1] = value.seatId;
  output[2] = value.phase;
  output[3] = value.activePlayerId;
  put16(output + 4, value.round);
  output[6] = value.boardSize;
  output[7] = value.selfPosition;
  put32(output + 8, static_cast<std::uint32_t>(value.selfCash));
  put32(output + 12, value.availableActions);
  output[16] = value.playerCount;
  output[17] = value.tileAssetIndex;
  output[18] = value.tileOwnerId;
  output[19] = value.tileBuildingLevel;
  output[20] = value.tileFlags;
  output[21] = value.pendingTarget;
  put32(output + 22, value.stateVersion);
  output[26] = value.decisionPlayerId;
  output[27] = value.debtCreditorId;
  output[28] = value.debtAssetIndex;
  output[29] = value.auctionAssetIndex;
  put32(output + 30, static_cast<std::uint32_t>(value.debtAmount));
  put32(output + 34, static_cast<std::uint32_t>(value.auctionCurrentBid));
  put32(output + 38, static_cast<std::uint32_t>(value.auctionMinimumBid));
  output[42] = value.auctionHighestBidderId;
  output[43] = 0;
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    output[offset++] = value.players[i].playerId;
    output[offset++] = value.players[i].position;
    put32(output + offset, static_cast<std::uint32_t>(value.players[i].cash));
    offset += 4;
    output[offset++] = value.players[i].flags;
  }
  written = offset;
  return true;
}

bool decodeStateSnapshot(const std::uint8_t* input, std::size_t length, StateSnapshot& value) {
  constexpr std::size_t baseSize = 44;
  constexpr std::size_t playerSize = 7;
  if (input == nullptr || length < baseSize || input[0] != 2 || input[16] > value.players.size() ||
      length != baseSize + input[16] * playerSize) return false;
  value.seatId = input[1];
  value.phase = input[2];
  value.activePlayerId = input[3];
  value.round = get16(input + 4);
  value.boardSize = input[6];
  value.selfPosition = input[7];
  value.selfCash = static_cast<std::int32_t>(get32(input + 8));
  value.availableActions = get32(input + 12);
  value.playerCount = input[16];
  value.tileAssetIndex = input[17];
  value.tileOwnerId = input[18];
  value.tileBuildingLevel = input[19];
  value.tileFlags = input[20];
  value.pendingTarget = input[21];
  value.stateVersion = get32(input + 22);
  value.decisionPlayerId = input[26];
  value.debtCreditorId = input[27];
  value.debtAssetIndex = input[28];
  value.auctionAssetIndex = input[29];
  value.debtAmount = static_cast<std::int32_t>(get32(input + 30));
  value.auctionCurrentBid = static_cast<std::int32_t>(get32(input + 34));
  value.auctionMinimumBid = static_cast<std::int32_t>(get32(input + 38));
  value.auctionHighestBidderId = input[42];
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    value.players[i].playerId = input[offset++];
    value.players[i].position = input[offset++];
    value.players[i].cash = static_cast<std::int32_t>(get32(input + offset));
    offset += 4;
    value.players[i].flags = input[offset++];
  }
  return true;
}

bool encodeAuthoritySnapshot(const AuthoritySnapshot& value, std::uint8_t* output, std::size_t capacity,
                             std::size_t& written) {
  constexpr std::size_t baseSize = 80;
  constexpr std::size_t playerSize = 9;
  constexpr std::size_t assetSize = 3;
  written = 0;
  if (value.playerCount > value.players.size() || value.assetCount > value.assets.size()) return false;
  const auto need = baseSize + value.playerCount * playerSize + value.assetCount * assetSize;
  if (output == nullptr || need > kMaxPayloadSize || !roomFor(need, capacity)) return false;
  output[0] = 3;
  output[1] = value.phase;
  output[2] = value.activePlayerId;
  output[3] = value.decisionPlayerId;
  output[4] = value.winnerPlayerId;
  output[5] = value.boardSize;
  output[6] = value.playerCount;
  output[7] = value.assetCount;
  put16(output + 8, value.round);
  put32(output + 10, value.stateVersion);
  put32(output + 14, value.lastEventSequence);
  put32(output + 18, value.boardIdHash);
  output[22] = value.pendingMoveFlags;
  output[23] = value.pendingMovePlayerId;
  output[24] = value.pendingMoveOrigin;
  output[25] = value.pendingMoveTarget;
  output[26] = value.pendingMoveDieA;
  output[27] = value.pendingMoveDieB;
  output[28] = value.pendingPurchaseFlags;
  output[29] = value.pendingPurchasePlayerId;
  output[30] = value.pendingPurchaseAssetIndex;
  output[31] = value.debtFlags;
  output[32] = value.debtDebtorId;
  output[33] = value.debtCreditorId;
  output[34] = value.debtAssetIndex;
  output[35] = value.debtPaymentEvent;
  output[36] = value.debtContinuation;
  output[37] = value.debtDieA;
  output[38] = value.debtDieB;
  output[39] = 0;
  put32(output + 40, static_cast<std::uint32_t>(value.debtAmount));
  output[44] = value.auctionFlags;
  output[45] = value.auctionAssetIndex;
  output[46] = value.auctionLandingPlayerId;
  output[47] = value.auctionCurrentBidderId;
  output[48] = value.auctionHighestBidderId;
  output[49] = value.auctionPassedMask;
  output[50] = value.auctionReadyMask;
  output[51] = value.auctionRequiredReadyMask;
  put32(output + 52, static_cast<std::uint32_t>(value.auctionCurrentBid));
  put32(output + 56, value.auctionGeneration);
  output[60] = value.pendingCardFlags;
  output[61] = value.pendingCardPlayerId;
  output[62] = value.pendingCardDeckId;
  output[63] = value.pendingCardIndex;
  put16(output + 64, value.pendingCardInstanceId);
  put16(output + 66, value.pendingCardCatalogId);
  put16(output + 68, value.pendingCardEffectId);
  put32(output + 70, static_cast<std::uint32_t>(value.pendingCardDisplayAmount));
  output[74] = value.pendingCardTargetPlayerId;
  output[75] = value.pendingCardTargetPosition;
  put32(output + 76, value.pendingCardDrawEventSequence);
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    const auto& player = value.players[i];
    output[offset++] = player.playerId;
    output[offset++] = player.position;
    put32(output + offset, static_cast<std::uint32_t>(player.cash));
    offset += 4;
    output[offset++] = player.flags;
    output[offset++] = player.failedHoldRolls;
    output[offset++] = player.doublesStreak;
  }
  for (std::uint8_t i = 0; i < value.assetCount; ++i) {
    output[offset++] = value.assets[i].ownerId;
    output[offset++] = value.assets[i].buildingLevel;
    output[offset++] = value.assets[i].flags;
  }
  written = offset;
  return true;
}

bool decodeAuthoritySnapshot(const std::uint8_t* input, std::size_t length, AuthoritySnapshot& value) {
  constexpr std::size_t playerSize = 9;
  constexpr std::size_t assetSize = 3;
  if (input == nullptr || length < 56 || (input[0] != 1 && input[0] != 2 && input[0] != 3)) {
    return false;
  }
  const std::size_t baseSize = input[0] == 3 ? 80 : (input[0] == 2 ? 60 : 56);
  if (length < baseSize || input[6] > value.players.size() || input[7] > value.assets.size() ||
      length != baseSize + input[6] * playerSize + input[7] * assetSize) {
    return false;
  }
  value = AuthoritySnapshot{};
  value.phase = input[1];
  value.activePlayerId = input[2];
  value.decisionPlayerId = input[3];
  value.winnerPlayerId = input[4];
  value.boardSize = input[5];
  value.playerCount = input[6];
  value.assetCount = input[7];
  value.round = get16(input + 8);
  value.stateVersion = get32(input + 10);
  value.lastEventSequence = get32(input + 14);
  value.boardIdHash = get32(input + 18);
  value.pendingMoveFlags = input[22];
  value.pendingMovePlayerId = input[23];
  value.pendingMoveOrigin = input[24];
  value.pendingMoveTarget = input[25];
  value.pendingMoveDieA = input[26];
  value.pendingMoveDieB = input[27];
  value.pendingPurchaseFlags = input[28];
  value.pendingPurchasePlayerId = input[29];
  value.pendingPurchaseAssetIndex = input[30];
  value.debtFlags = input[31];
  value.debtDebtorId = input[32];
  value.debtCreditorId = input[33];
  value.debtAssetIndex = input[34];
  value.debtPaymentEvent = input[35];
  value.debtContinuation = input[36];
  value.debtDieA = input[37];
  value.debtDieB = input[38];
  value.debtAmount = static_cast<std::int32_t>(get32(input + 40));
  value.auctionFlags = input[44];
  value.auctionAssetIndex = input[45];
  value.auctionLandingPlayerId = input[46];
  value.auctionCurrentBidderId = input[47];
  value.auctionHighestBidderId = input[48];
  value.auctionPassedMask = input[49];
  if (input[0] >= 2) {
    value.auctionReadyMask = input[50];
    value.auctionRequiredReadyMask = input[51];
    value.auctionGeneration = get32(input + 56);
  }
  value.auctionCurrentBid = static_cast<std::int32_t>(get32(input + 52));
  if (input[0] == 3) {
    value.pendingCardFlags = input[60];
    value.pendingCardPlayerId = input[61];
    value.pendingCardDeckId = input[62];
    value.pendingCardIndex = input[63];
    value.pendingCardInstanceId = get16(input + 64);
    value.pendingCardCatalogId = get16(input + 66);
    value.pendingCardEffectId = get16(input + 68);
    value.pendingCardDisplayAmount = static_cast<std::int32_t>(get32(input + 70));
    value.pendingCardTargetPlayerId = input[74];
    value.pendingCardTargetPosition = input[75];
    value.pendingCardDrawEventSequence = get32(input + 76);
  }
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    auto& player = value.players[i];
    player.playerId = input[offset++];
    player.position = input[offset++];
    player.cash = static_cast<std::int32_t>(get32(input + offset));
    offset += 4;
    player.flags = input[offset++];
    player.failedHoldRolls = input[offset++];
    player.doublesStreak = input[offset++];
  }
  for (std::uint8_t i = 0; i < value.assetCount; ++i) {
    value.assets[i].ownerId = input[offset++];
    value.assets[i].buildingLevel = input[offset++];
    value.assets[i].flags = input[offset++];
  }
  return true;
}

bool encodeRosterSnapshot(const RosterSnapshot& value, std::uint8_t* output, std::size_t capacity,
                          std::size_t& written) {
  constexpr std::size_t baseSize = 6;
  constexpr std::size_t playerSize = 18;
  written = 0;
  if (value.playerCount > value.playerIds.size()) return false;
  const auto need = baseSize + value.playerCount * playerSize;
  if (output == nullptr || !roomFor(need, capacity)) return false;
  output[0] = 1;
  put32(output + 1, value.stateVersion);
  output[5] = value.playerCount;
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    output[offset++] = value.playerIds[i];
    std::memcpy(output + offset, value.displayNames[i].data(), 17);
    offset += 17;
  }
  written = offset;
  return true;
}

bool decodeRosterSnapshot(const std::uint8_t* input, std::size_t length, RosterSnapshot& value) {
  constexpr std::size_t baseSize = 6;
  constexpr std::size_t playerSize = 18;
  if (input == nullptr || length < baseSize || input[0] != 1 || input[5] > value.playerIds.size() ||
      length != baseSize + input[5] * playerSize) return false;
  value = RosterSnapshot{};
  value.stateVersion = get32(input + 1);
  value.playerCount = input[5];
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.playerCount; ++i) {
    value.playerIds[i] = input[offset++];
    std::memcpy(value.displayNames[i].data(), input + offset, 17);
    value.displayNames[i][16] = '\0';
    offset += 17;
  }
  return true;
}

bool encodeGameEventBatch(const GameEventBatch& value, std::uint8_t* output, std::size_t capacity,
                          std::size_t& written) {
  constexpr std::size_t baseSize = 6;
  constexpr std::size_t eventSize = 16;
  written = 0;
  if (value.eventCount > value.events.size()) return false;
  const auto need = baseSize + value.eventCount * eventSize;
  if (output == nullptr || need > kMaxPayloadSize || !roomFor(need, capacity)) return false;
  output[0] = 1;
  output[1] = value.eventCount;
  put32(output + 2, value.stateVersion);
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.eventCount; ++i) {
    const auto& event = value.events[i];
    put32(output + offset, event.sequence);
    offset += 4;
    output[offset++] = event.kind;
    output[offset++] = event.actorId;
    output[offset++] = event.targetId;
    output[offset++] = event.assetIndex;
    put32(output + offset, static_cast<std::uint32_t>(event.amount));
    offset += 4;
    put32(output + offset, event.detail);
    offset += 4;
  }
  written = offset;
  return true;
}

bool decodeGameEventBatch(const std::uint8_t* input, std::size_t length, GameEventBatch& value) {
  constexpr std::size_t baseSize = 6;
  constexpr std::size_t eventSize = 16;
  if (input == nullptr || length < baseSize || input[0] != 1 || input[1] > value.events.size() ||
      length != baseSize + input[1] * eventSize) return false;
  value = GameEventBatch{};
  value.eventCount = input[1];
  value.stateVersion = get32(input + 2);
  std::size_t offset = baseSize;
  for (std::uint8_t i = 0; i < value.eventCount; ++i) {
    auto& event = value.events[i];
    event.sequence = get32(input + offset);
    offset += 4;
    event.kind = input[offset++];
    event.actorId = input[offset++];
    event.targetId = input[offset++];
    event.assetIndex = input[offset++];
    event.amount = static_cast<std::int32_t>(get32(input + offset));
    offset += 4;
    event.detail = get32(input + offset);
    offset += 4;
  }
  return true;
}

bool encodePlayerCardEvent(const PlayerCardEvent& value, std::uint8_t* output,
                           std::size_t capacity, std::size_t& written) {
  written = 0;
  const bool drawn = value.stage == PlayerCardStage::Drawn &&
      value.domainEventType == kDomainEventCardDrawn && value.outcome == 0;
  const bool applied = value.stage == PlayerCardStage::EffectApplied &&
      value.domainEventType == kDomainEventCardEffectApplied &&
      value.outcome >= 1 && value.outcome <= 4;
  if ((!drawn && !applied) || output == nullptr || !roomFor(kPlayerCardEventSize, capacity) ||
      value.playerId == 0 || value.playerId > 6 || value.deckId < 1 || value.deckId > 2 ||
      value.cardIndex > 7 || value.cardInstanceId == 0 || value.cardInstanceId == 0xFFFFu ||
      value.cardCatalogId == 0 || value.cardCatalogId == 0xFFFFu ||
      value.effectId == 0 || value.effectId == 0xFFFFu) {
    return false;
  }
  output[0] = 1;
  output[1] = static_cast<std::uint8_t>(value.stage);
  put16(output + 2, value.domainEventType);
  put32(output + 4, value.stateVersion);
  put32(output + 8, value.eventSequence);
  output[12] = value.playerId;
  output[13] = value.deckId;
  output[14] = value.cardIndex;
  output[15] = value.flags;
  put16(output + 16, value.cardInstanceId);
  put16(output + 18, value.cardCatalogId);
  put16(output + 20, value.effectId);
  put32(output + 22, static_cast<std::uint32_t>(value.amount));
  output[26] = value.targetPlayerId;
  output[27] = value.targetPosition;
  output[28] = value.outcome;
  output[29] = 0;
  output[30] = 0;
  output[31] = 0;
  written = kPlayerCardEventSize;
  return true;
}

bool decodePlayerCardEvent(const std::uint8_t* input, std::size_t length,
                           PlayerCardEvent& value) {
  if (input == nullptr || length != kPlayerCardEventSize || input[0] != 1 ||
      input[29] != 0 || input[30] != 0 || input[31] != 0) {
    return false;
  }
  PlayerCardEvent decoded{};
  decoded.stage = static_cast<PlayerCardStage>(input[1]);
  decoded.domainEventType = get16(input + 2);
  decoded.stateVersion = get32(input + 4);
  decoded.eventSequence = get32(input + 8);
  decoded.playerId = input[12];
  decoded.deckId = input[13];
  decoded.cardIndex = input[14];
  decoded.flags = input[15];
  decoded.cardInstanceId = get16(input + 16);
  decoded.cardCatalogId = get16(input + 18);
  decoded.effectId = get16(input + 20);
  decoded.amount = static_cast<std::int32_t>(get32(input + 22));
  decoded.targetPlayerId = input[26];
  decoded.targetPosition = input[27];
  decoded.outcome = input[28];
  std::array<std::uint8_t, kPlayerCardEventSize> scratch{};
  std::size_t ignored = 0;
  if (!encodePlayerCardEvent(decoded, scratch.data(), scratch.size(), ignored)) return false;
  value = decoded;
  return true;
}

bool encodeHeartbeat(const Heartbeat& value, std::uint8_t* output, std::size_t capacity,
                     std::size_t& written) {
  written = 0;
  if (output == nullptr || capacity < 12) return false;
  output[0] = 1;
  output[1] = value.flags;
  output[2] = 0;
  output[3] = 0;
  put32(output + 4, value.appliedStateVersion);
  put32(output + 8, value.appliedEventSequence);
  written = 12;
  return true;
}

bool decodeHeartbeat(const std::uint8_t* input, std::size_t length, Heartbeat& value) {
  if (input == nullptr || length != 12 || input[0] != 1) return false;
  value.flags = input[1];
  value.appliedStateVersion = get32(input + 4);
  value.appliedEventSequence = get32(input + 8);
  return true;
}

bool encodeActionRequest(const ActionRequest& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written) {
  if (output == nullptr || capacity < 12) return false;
  output[0] = 1;
  output[1] = static_cast<std::uint8_t>(value.action);
  output[2] = value.playerId;
  output[3] = value.assetIndex;
  put32(output + 4, static_cast<std::uint32_t>(value.argument));
  put32(output + 8, value.expectedStateVersion);
  written = 12;
  return true;
}

bool decodeActionRequest(const std::uint8_t* input, std::size_t length, ActionRequest& value) {
  if (input == nullptr || length != 12 || input[0] != 1) return false;
  value.action = static_cast<ActionCode>(input[1]);
  value.playerId = input[2];
  value.assetIndex = input[3];
  value.argument = static_cast<std::int32_t>(get32(input + 4));
  value.expectedStateVersion = get32(input + 8);
  return true;
}

bool encodePlayerDetailRequest(const PlayerDetailRequest& value, std::uint8_t* output,
                               std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || capacity < kPlayerDetailRequestSize || value.requestId == 0 ||
      value.targetPlayerId == 0) {
    return false;
  }
  output[0] = 1;
  output[1] = value.targetPlayerId;
  output[2] = 0;
  output[3] = 0;
  put32(output + 4, value.requestId);
  put32(output + 8, value.expectedStateVersion);
  written = kPlayerDetailRequestSize;
  return true;
}

bool decodePlayerDetailRequest(const std::uint8_t* input, std::size_t length,
                               PlayerDetailRequest& value) {
  if (input == nullptr || length != kPlayerDetailRequestSize || input[0] != 1 ||
      input[1] == 0 || input[2] != 0 || input[3] != 0 || get32(input + 4) == 0) {
    return false;
  }
  value = PlayerDetailRequest{};
  value.targetPlayerId = input[1];
  value.requestId = get32(input + 4);
  value.expectedStateVersion = get32(input + 8);
  return true;
}

bool encodePlayerDetailResponse(const PlayerDetailResponse& value, std::uint8_t* output,
                                std::size_t capacity, std::size_t& written) {
  written = 0;
  if (value.requestId == 0 || value.targetPlayerId == 0 ||
      value.assetCount > value.assets.size() || value.ledgerCount > value.ledger.size()) {
    return false;
  }
  const auto need = kPlayerDetailResponseBaseSize + value.assetCount * kPlayerDetailAssetSize +
                    value.ledgerCount * kPlayerDetailLedgerEntrySize;
  if (output == nullptr || need > kMaxPayloadSize || !roomFor(need, capacity)) return false;
  output[0] = 1;
  output[1] = value.flags;
  output[2] = value.targetPlayerId;
  output[3] = value.position;
  put32(output + 4, value.requestId);
  put32(output + 8, value.stateVersion);
  put32(output + 12, static_cast<std::uint32_t>(value.cash));
  output[16] = value.assetCount;
  output[17] = value.ledgerCount;
  output[18] = value.totalOwnedAssets;
  output[19] = 0;
  std::size_t offset = kPlayerDetailResponseBaseSize;
  for (std::uint8_t i = 0; i < value.assetCount; ++i) {
    output[offset++] = value.assets[i].assetIndex;
    output[offset++] = value.assets[i].state;
  }
  for (std::uint8_t i = 0; i < value.ledgerCount; ++i) {
    const auto& entry = value.ledger[i];
    put32(output + offset, entry.sequence);
    offset += 4;
    put32(output + offset, static_cast<std::uint32_t>(entry.amount));
    offset += 4;
    output[offset++] = entry.kind;
    output[offset++] = entry.counterpartyId;
    output[offset++] = entry.assetIndex;
    output[offset++] = entry.flags;
  }
  written = offset;
  return true;
}

bool decodePlayerDetailResponse(const std::uint8_t* input, std::size_t length,
                                PlayerDetailResponse& value) {
  if (input == nullptr || length < kPlayerDetailResponseBaseSize || input[0] != 1 ||
      input[2] == 0 || input[19] != 0 || get32(input + 4) == 0 ||
      input[16] > value.assets.size() || input[17] > value.ledger.size()) {
    return false;
  }
  const auto expected = kPlayerDetailResponseBaseSize + input[16] * kPlayerDetailAssetSize +
                        input[17] * kPlayerDetailLedgerEntrySize;
  if (length != expected || length > kMaxPayloadSize) return false;
  value = PlayerDetailResponse{};
  value.flags = input[1];
  value.targetPlayerId = input[2];
  value.position = input[3];
  value.requestId = get32(input + 4);
  value.stateVersion = get32(input + 8);
  value.cash = static_cast<std::int32_t>(get32(input + 12));
  value.assetCount = input[16];
  value.ledgerCount = input[17];
  value.totalOwnedAssets = input[18];
  std::size_t offset = kPlayerDetailResponseBaseSize;
  for (std::uint8_t i = 0; i < value.assetCount; ++i) {
    value.assets[i].assetIndex = input[offset++];
    value.assets[i].state = input[offset++];
  }
  for (std::uint8_t i = 0; i < value.ledgerCount; ++i) {
    auto& entry = value.ledger[i];
    entry.sequence = get32(input + offset);
    offset += 4;
    entry.amount = static_cast<std::int32_t>(get32(input + offset));
    offset += 4;
    entry.kind = input[offset++];
    entry.counterpartyId = input[offset++];
    entry.assetIndex = input[offset++];
    entry.flags = input[offset++];
  }
  return true;
}

bool encodeTradeRequest(const TradeRequest& value, std::uint8_t* output,
                        std::size_t capacity, std::size_t& written) {
  written = 0;
  const auto operation = static_cast<std::uint8_t>(value.operation);
  const auto totalAssets = static_cast<std::size_t>(value.selfAssetCount) +
      value.counterpartyAssetCount;
  const bool offerOperation = value.operation == TradeOperation::Create ||
      value.operation == TradeOperation::Update;
  const bool existingOperation = value.operation == TradeOperation::Update ||
      value.operation == TradeOperation::Confirm || value.operation == TradeOperation::Reject ||
      value.operation == TradeOperation::Cancel;
  if (output == nullptr || operation < static_cast<std::uint8_t>(TradeOperation::Query) ||
      operation > static_cast<std::uint8_t>(TradeOperation::Cancel) || value.requestId == 0 ||
      value.selfAssetCount > value.selfAssets.size() ||
      value.counterpartyAssetCount > value.counterpartyAssets.size() ||
      totalAssets > kMaxTradeAssetsTotal || value.selfGivesCash < 0 ||
      value.counterpartyGivesCash < 0 ||
      (value.operation != TradeOperation::Query && value.expectedStateVersion == 0) ||
      (offerOperation && value.targetPlayerId == 0) ||
      (value.operation == TradeOperation::Create &&
       (value.tradeId != 0 || value.expectedRevision != 0)) ||
      (existingOperation && (value.tradeId == 0 || value.expectedRevision == 0)) ||
      (!offerOperation && (totalAssets != 0 || value.selfGivesCash != 0 ||
                           value.counterpartyGivesCash != 0))) {
    return false;
  }
  std::array<bool, kMaxTradeAssetsTotal> seen{};
  for (std::uint8_t i = 0; i < value.selfAssetCount; ++i) {
    const auto asset = value.selfAssets[i];
    if (asset >= seen.size() || seen[asset]) return false;
    seen[asset] = true;
  }
  for (std::uint8_t i = 0; i < value.counterpartyAssetCount; ++i) {
    const auto asset = value.counterpartyAssets[i];
    if (asset >= seen.size() || seen[asset]) return false;
    seen[asset] = true;
  }
  const auto need = kTradeRequestBaseSize + totalAssets;
  if (need > kMaxPayloadSize || !roomFor(need, capacity)) return false;
  output[0] = 1;
  output[1] = operation;
  output[2] = value.targetPlayerId;
  output[3] = value.selfAssetCount;
  output[4] = value.counterpartyAssetCount;
  output[5] = 0;
  put16(output + 6, value.expectedRevision);
  put32(output + 8, value.requestId);
  put32(output + 12, value.expectedStateVersion);
  put32(output + 16, value.tradeId);
  put32(output + 20, static_cast<std::uint32_t>(value.selfGivesCash));
  put32(output + 24, static_cast<std::uint32_t>(value.counterpartyGivesCash));
  output[28] = 0;
  output[29] = 0;
  output[30] = 0;
  output[31] = 0;
  std::size_t offset = kTradeRequestBaseSize;
  for (std::uint8_t i = 0; i < value.selfAssetCount; ++i) output[offset++] = value.selfAssets[i];
  for (std::uint8_t i = 0; i < value.counterpartyAssetCount; ++i) {
    output[offset++] = value.counterpartyAssets[i];
  }
  written = offset;
  return true;
}

bool decodeTradeRequest(const std::uint8_t* input, std::size_t length,
                        TradeRequest& value) {
  if (input == nullptr || length < kTradeRequestBaseSize || input[0] != 1 || input[5] != 0 ||
      input[28] != 0 || input[29] != 0 || input[30] != 0 || input[31] != 0) {
    return false;
  }
  TradeRequest decoded{};
  decoded.operation = static_cast<TradeOperation>(input[1]);
  decoded.targetPlayerId = input[2];
  decoded.selfAssetCount = input[3];
  decoded.counterpartyAssetCount = input[4];
  decoded.expectedRevision = get16(input + 6);
  decoded.requestId = get32(input + 8);
  decoded.expectedStateVersion = get32(input + 12);
  decoded.tradeId = get32(input + 16);
  decoded.selfGivesCash = static_cast<std::int32_t>(get32(input + 20));
  decoded.counterpartyGivesCash = static_cast<std::int32_t>(get32(input + 24));
  const auto totalAssets = static_cast<std::size_t>(decoded.selfAssetCount) +
      decoded.counterpartyAssetCount;
  if (decoded.selfAssetCount > decoded.selfAssets.size() ||
      decoded.counterpartyAssetCount > decoded.counterpartyAssets.size() ||
      totalAssets > kMaxTradeAssetsTotal || length != kTradeRequestBaseSize + totalAssets) {
    return false;
  }
  std::size_t offset = kTradeRequestBaseSize;
  for (std::uint8_t i = 0; i < decoded.selfAssetCount; ++i) decoded.selfAssets[i] = input[offset++];
  for (std::uint8_t i = 0; i < decoded.counterpartyAssetCount; ++i) {
    decoded.counterpartyAssets[i] = input[offset++];
  }
  std::array<std::uint8_t, kMaxTradeRequestSize> scratch{};
  std::size_t encoded = 0;
  if (!encodeTradeRequest(decoded, scratch.data(), scratch.size(), encoded) || encoded != length ||
      std::memcmp(scratch.data(), input, length) != 0) {
    return false;
  }
  value = decoded;
  return true;
}

bool encodeTradeResponse(const TradeResponse& value, std::uint8_t* output,
                         std::size_t capacity, std::size_t& written) {
  written = 0;
  const auto operation = static_cast<std::uint8_t>(value.operation);
  const auto result = static_cast<std::uint8_t>(value.result);
  const auto status = static_cast<std::uint8_t>(value.status);
  const auto totalAssets = static_cast<std::size_t>(value.selfAssetCount) +
      value.counterpartyAssetCount;
  if (output == nullptr || operation < static_cast<std::uint8_t>(TradeOperation::Query) ||
      operation > static_cast<std::uint8_t>(TradeOperation::Cancel) ||
      result > static_cast<std::uint8_t>(TradeResultCode::RequestIdConflict) ||
      status > static_cast<std::uint8_t>(TradeStatus::Invalidated) || value.selfPlayerId == 0 ||
      (value.requestId == 0 && (value.flags & TradeResponseFlagResync) == 0) ||
      value.selfGivesCash < 0 || value.counterpartyGivesCash < 0 ||
      value.selfAssetCount > value.selfAssets.size() ||
      value.counterpartyAssetCount > value.counterpartyAssets.size() ||
      totalAssets > kMaxTradeAssetsTotal) {
    return false;
  }
  std::array<bool, kMaxTradeAssetsTotal> seen{};
  for (std::uint8_t i = 0; i < value.selfAssetCount; ++i) {
    const auto asset = value.selfAssets[i];
    if (asset >= seen.size() || seen[asset]) return false;
    seen[asset] = true;
  }
  for (std::uint8_t i = 0; i < value.counterpartyAssetCount; ++i) {
    const auto asset = value.counterpartyAssets[i];
    if (asset >= seen.size() || seen[asset]) return false;
    seen[asset] = true;
  }
  const auto need = kTradeResponseBaseSize + totalAssets;
  if (need > kMaxPayloadSize || !roomFor(need, capacity)) return false;
  output[0] = 1;
  output[1] = operation;
  output[2] = result;
  output[3] = status;
  output[4] = value.flags;
  output[5] = value.selfPlayerId;
  output[6] = value.counterpartyId;
  output[7] = value.selfAssetCount;
  output[8] = value.counterpartyAssetCount;
  output[9] = 0;
  put16(output + 10, value.revision);
  put32(output + 12, value.requestId);
  put32(output + 16, value.stateVersion);
  put32(output + 20, value.tradeId);
  put32(output + 24, value.expiresInMs);
  put32(output + 28, static_cast<std::uint32_t>(value.selfGivesCash));
  put32(output + 32, static_cast<std::uint32_t>(value.counterpartyGivesCash));
  output[36] = value.confirmedMask;
  output[37] = value.originatorId;
  output[38] = 0;
  output[39] = 0;
  std::size_t offset = kTradeResponseBaseSize;
  for (std::uint8_t i = 0; i < value.selfAssetCount; ++i) output[offset++] = value.selfAssets[i];
  for (std::uint8_t i = 0; i < value.counterpartyAssetCount; ++i) {
    output[offset++] = value.counterpartyAssets[i];
  }
  written = offset;
  return true;
}

bool decodeTradeResponse(const std::uint8_t* input, std::size_t length,
                         TradeResponse& value) {
  if (input == nullptr || length < kTradeResponseBaseSize || input[0] != 1 || input[9] != 0 ||
      input[38] != 0 || input[39] != 0) {
    return false;
  }
  TradeResponse decoded{};
  decoded.operation = static_cast<TradeOperation>(input[1]);
  decoded.result = static_cast<TradeResultCode>(input[2]);
  decoded.status = static_cast<TradeStatus>(input[3]);
  decoded.flags = input[4];
  decoded.selfPlayerId = input[5];
  decoded.counterpartyId = input[6];
  decoded.selfAssetCount = input[7];
  decoded.counterpartyAssetCount = input[8];
  decoded.revision = get16(input + 10);
  decoded.requestId = get32(input + 12);
  decoded.stateVersion = get32(input + 16);
  decoded.tradeId = get32(input + 20);
  decoded.expiresInMs = get32(input + 24);
  decoded.selfGivesCash = static_cast<std::int32_t>(get32(input + 28));
  decoded.counterpartyGivesCash = static_cast<std::int32_t>(get32(input + 32));
  decoded.confirmedMask = input[36];
  decoded.originatorId = input[37];
  const auto totalAssets = static_cast<std::size_t>(decoded.selfAssetCount) +
      decoded.counterpartyAssetCount;
  if (decoded.selfAssetCount > decoded.selfAssets.size() ||
      decoded.counterpartyAssetCount > decoded.counterpartyAssets.size() ||
      totalAssets > kMaxTradeAssetsTotal || length != kTradeResponseBaseSize + totalAssets) {
    return false;
  }
  std::size_t offset = kTradeResponseBaseSize;
  for (std::uint8_t i = 0; i < decoded.selfAssetCount; ++i) decoded.selfAssets[i] = input[offset++];
  for (std::uint8_t i = 0; i < decoded.counterpartyAssetCount; ++i) {
    decoded.counterpartyAssets[i] = input[offset++];
  }
  std::array<std::uint8_t, kMaxTradeResponseSize> scratch{};
  std::size_t encoded = 0;
  if (!encodeTradeResponse(decoded, scratch.data(), scratch.size(), encoded) || encoded != length ||
      std::memcmp(scratch.data(), input, length) != 0) {
    return false;
  }
  value = decoded;
  return true;
}

bool encodeIdentityRequest(const IdentityRequest& value, std::uint8_t* output,
                           std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || !roomFor(kIdentityRequestSize, capacity) ||
      !validIdentityRequestValue(value)) {
    return false;
  }
  std::memset(output, 0, kIdentityRequestSize);
  output[0] = 1;
  output[1] = static_cast<std::uint8_t>(value.operation);
  output[2] = value.playerId;
  put32(output + 4, value.requestId);
  put32(output + 8, value.expectedStateVersion);
  put16(output + 12, value.expectedSeatRevision);
  put16(output + 14, value.avatarCatalogVersion);
  output[16] = value.recipe.hairPresetId;
  output[17] = value.recipe.hairColorId;
  output[18] = value.recipe.facePresetId;
  output[19] = value.recipe.skinToneId;
  output[20] = value.recipe.outfitPresetId;
  output[21] = value.nameLength;
  std::memcpy(output + 22, value.name.data(), value.name.size());
  written = kIdentityRequestSize;
  return true;
}

bool decodeIdentityRequest(const std::uint8_t* input, std::size_t length,
                           IdentityRequest& value) {
  if (input == nullptr || length != kIdentityRequestSize || input[0] != 1 ||
      input[3] != 0 || input[39] != 0 || input[40] != 0 || input[41] != 0 ||
      input[42] != 0 || input[43] != 0) {
    return false;
  }
  IdentityRequest decoded{};
  decoded.operation = static_cast<IdentityOperation>(input[1]);
  decoded.playerId = input[2];
  decoded.requestId = get32(input + 4);
  decoded.expectedStateVersion = get32(input + 8);
  decoded.expectedSeatRevision = get16(input + 12);
  decoded.avatarCatalogVersion = get16(input + 14);
  decoded.recipe.avatarCatalogVersion = decoded.avatarCatalogVersion;
  decoded.recipe.hairPresetId = input[16];
  decoded.recipe.hairColorId = input[17];
  decoded.recipe.facePresetId = input[18];
  decoded.recipe.skinToneId = input[19];
  decoded.recipe.outfitPresetId = input[20];
  decoded.nameLength = input[21];
  std::memcpy(decoded.name.data(), input + 22, decoded.name.size());

  std::array<std::uint8_t, kIdentityRequestSize> scratch{};
  std::size_t encoded = 0;
  if (!encodeIdentityRequest(decoded, scratch.data(), scratch.size(), encoded) ||
      encoded != length || std::memcmp(scratch.data(), input, length) != 0) {
    return false;
  }
  value = decoded;
  return true;
}

bool encodeIdentitySnapshot(const IdentitySnapshot& value, std::uint8_t* output,
                            std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || !roomFor(kIdentitySnapshotSize, capacity) ||
      !validIdentitySnapshotValue(value)) {
    return false;
  }
  std::memset(output, 0, kIdentitySnapshotSize);
  output[0] = 1;
  output[1] = static_cast<std::uint8_t>(value.roomPhase);
  output[2] = static_cast<std::uint8_t>(value.selfStage);
  output[3] = static_cast<std::uint8_t>(value.result);
  put32(output + 4, value.requestId);
  put32(output + 8, value.stateVersion);
  put32(output + 12, value.identityRevision);
  put64(output + 16, value.serverEpochMs);
  put64(output + 24, value.countdownDeadlineEpochMs);
  put16(output + 32, value.avatarCatalogVersion);
  output[34] = value.playerCount;
  output[35] = value.selfPlayerId;
  output[36] = value.requiredHumanMask;
  output[37] = value.avatarFinalMask;
  output[38] = value.nameFinalMask;
  output[39] = value.readyMask;
  output[40] = value.onlineMask;
  output[41] = static_cast<std::uint8_t>(value.operationEcho);
  output[42] = value.flags;
  for (std::size_t index = 0; index < value.seats.size(); ++index) {
    const auto& seat = value.seats[index];
    auto* record = output + kIdentitySnapshotBaseSize + index * kIdentitySeatRecordSize;
    record[0] = seat.playerId;
    record[1] = seat.flags;
    record[2] = seat.seatColorId;
    put16(record + 4, seat.seatRevision);
    put16(record + 6, seat.avatarRevision);
    put64(record + 8, seat.avatarContentHash64);
    put16(record + 16, seat.recipe.avatarCatalogVersion);
    record[18] = seat.recipe.hairPresetId;
    record[19] = seat.recipe.hairColorId;
    record[20] = seat.recipe.facePresetId;
    record[21] = seat.recipe.skinToneId;
    record[22] = seat.recipe.outfitPresetId;
  }
  written = kIdentitySnapshotSize;
  return true;
}

bool decodeIdentitySnapshot(const std::uint8_t* input, std::size_t length,
                            IdentitySnapshot& value) {
  if (input == nullptr || length != kIdentitySnapshotSize || input[0] != 1 ||
      input[43] != 0) {
    return false;
  }
  IdentitySnapshot decoded{};
  decoded.roomPhase = static_cast<IdentityRoomPhase>(input[1]);
  decoded.selfStage = static_cast<IdentitySeatStage>(input[2]);
  decoded.result = static_cast<IdentityResultCode>(input[3]);
  decoded.requestId = get32(input + 4);
  decoded.stateVersion = get32(input + 8);
  decoded.identityRevision = get32(input + 12);
  decoded.serverEpochMs = get64(input + 16);
  decoded.countdownDeadlineEpochMs = get64(input + 24);
  decoded.avatarCatalogVersion = get16(input + 32);
  decoded.playerCount = input[34];
  decoded.selfPlayerId = input[35];
  decoded.requiredHumanMask = input[36];
  decoded.avatarFinalMask = input[37];
  decoded.nameFinalMask = input[38];
  decoded.readyMask = input[39];
  decoded.onlineMask = input[40];
  decoded.operationEcho = static_cast<IdentityOperation>(input[41]);
  decoded.flags = input[42];
  for (std::size_t index = 0; index < decoded.seats.size(); ++index) {
    auto& seat = decoded.seats[index];
    const auto* record = input + kIdentitySnapshotBaseSize + index * kIdentitySeatRecordSize;
    if (record[3] != 0) return false;
    seat.playerId = record[0];
    seat.flags = record[1];
    seat.seatColorId = record[2];
    seat.seatRevision = get16(record + 4);
    seat.avatarRevision = get16(record + 6);
    seat.avatarContentHash64 = get64(record + 8);
    seat.recipe.avatarCatalogVersion = get16(record + 16);
    seat.recipe.hairPresetId = record[18];
    seat.recipe.hairColorId = record[19];
    seat.recipe.facePresetId = record[20];
    seat.recipe.skinToneId = record[21];
    seat.recipe.outfitPresetId = record[22];
  }

  std::array<std::uint8_t, kIdentitySnapshotSize> scratch{};
  std::size_t encoded = 0;
  if (!encodeIdentitySnapshot(decoded, scratch.data(), scratch.size(), encoded) ||
      encoded != length || std::memcmp(scratch.data(), input, length) != 0) {
    return false;
  }
  value = decoded;
  return true;
}

bool encodePairRequest(const PairRequest& value, std::uint8_t* output, std::size_t capacity,
                       std::size_t& written) {
  if (output == nullptr || capacity < 26) return false;
  output[0] = 1;
  put32(output + 1, value.deviceNonce);
  put32(output + 5, value.capabilities);
  std::memcpy(output + 9, value.displayName, 17);
  written = 26;
  return true;
}

bool decodePairRequest(const std::uint8_t* input, std::size_t length, PairRequest& value) {
  if (input == nullptr || length != 26 || input[0] != 1) return false;
  value.deviceNonce = get32(input + 1);
  value.capabilities = get32(input + 5);
  std::memcpy(value.displayName, input + 9, 17);
  value.displayName[16] = '\0';
  return true;
}

bool encodePairAccept(const PairAccept& value, std::uint8_t* output, std::size_t capacity,
                      std::size_t& written) {
  if (output == nullptr || capacity < 17) return false;
  output[0] = 2;
  output[1] = value.accepted;
  output[2] = value.seatId;
  output[3] = value.wifiChannel;
  output[4] = value.reserved;
  put32(output + 5, value.serverDeviceId);
  put32(output + 9, value.stateVersion);
  put32(output + 13, value.sessionId);
  written = 17;
  return true;
}

bool decodePairAccept(const std::uint8_t* input, std::size_t length, PairAccept& value) {
  if (input == nullptr || (input[0] != 1 && input[0] != 2) ||
      (input[0] == 1 ? length != 13 : length != 17)) return false;
  value = PairAccept{};
  value.accepted = input[1];
  value.seatId = input[2];
  value.wifiChannel = input[3];
  value.reserved = input[4];
  value.serverDeviceId = get32(input + 5);
  value.stateVersion = get32(input + 9);
  if (input[0] == 2) value.sessionId = get32(input + 13);
  return true;
}

}  // namespace gridopoly::protocol
