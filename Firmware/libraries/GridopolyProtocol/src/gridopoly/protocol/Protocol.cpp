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

std::uint16_t get16(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) | (static_cast<std::uint16_t>(in[1]) << 8);
}

std::uint32_t get32(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
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
  if (output == nullptr || capacity < 13) return false;
  output[0] = 1;
  output[1] = value.accepted;
  output[2] = value.seatId;
  output[3] = value.wifiChannel;
  output[4] = value.reserved;
  put32(output + 5, value.serverDeviceId);
  put32(output + 9, value.stateVersion);
  written = 13;
  return true;
}

bool decodePairAccept(const std::uint8_t* input, std::size_t length, PairAccept& value) {
  if (input == nullptr || length != 13 || input[0] != 1) return false;
  value.accepted = input[1];
  value.seatId = input[2];
  value.wifiChannel = input[3];
  value.reserved = input[4];
  value.serverDeviceId = get32(input + 5);
  value.stateVersion = get32(input + 9);
  return true;
}

}  // namespace gridopoly::protocol
