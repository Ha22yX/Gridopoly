#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gridopoly::protocol {

constexpr std::uint32_t kMagic = 0x44495247u;  // "GRID" on the wire.
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kMaxFrameSize = 250;
constexpr std::size_t kMaxPayloadSize = kMaxFrameSize - kHeaderSize;

enum class MessageType : std::uint8_t {
  Discover = 0x01,
  PairRequest = 0x02,
  PairAccept = 0x03,
  Heartbeat = 0x04,
  StateSnapshot = 0x10,
  GameEvent = 0x11,
  ActionRequest = 0x20,
  ActionResult = 0x21,
  Ack = 0x22,
  Error = 0x23,
};

enum FrameFlags : std::uint16_t {
  FlagNone = 0,
  FlagAckRequired = 1u << 0,
  FlagResponse = 1u << 1,
  FlagBroadcast = 1u << 2,
  FlagResync = 1u << 3,
};

enum class ActionCode : std::uint8_t {
  Roll = 1,
  ConfirmPosition = 2,
  Buy = 3,
  Decline = 4,
  EndTurn = 5,
  PayHoldFee = 6,
  Mortgage = 7,
  Unmortgage = 8,
  Build = 9,
  SellBuilding = 10,
  PayDebt = 11,
  DeclareBankruptcy = 12,
  AuctionBid = 13,
  AuctionPass = 14,
};

struct Header {
  MessageType type{MessageType::Heartbeat};
  std::uint16_t flags{};
  std::uint32_t sequence{};
  std::uint32_t acknowledgement{};
  std::uint32_t roomId{};
  std::uint32_t deviceId{};
  std::uint16_t payloadLength{};
  std::uint32_t payloadCrc32{};
};

struct DecodedFrame {
  Header header{};
  const std::uint8_t* payload{};
};

struct PlayerSummary {
  std::uint8_t playerId{};
  std::uint8_t position{};
  std::int32_t cash{};
  std::uint8_t flags{};  // bit0 held, bit1 bankrupt, bit2 connected, bits3-4 controller.
};

struct StateSnapshot {
  std::uint8_t seatId{};
  std::uint8_t phase{};
  std::uint8_t activePlayerId{};
  std::uint16_t round{};
  std::uint8_t boardSize{};
  std::uint8_t selfPosition{};
  std::int32_t selfCash{};
  std::uint32_t availableActions{};
  std::uint8_t playerCount{};
  std::uint8_t tileAssetIndex{0xFF};
  std::uint8_t tileOwnerId{};
  std::uint8_t tileBuildingLevel{};
  std::uint8_t tileFlags{};
  std::uint8_t pendingTarget{0xFF};
  std::uint32_t stateVersion{};
  std::uint8_t decisionPlayerId{};
  std::uint8_t debtCreditorId{};
  std::uint8_t debtAssetIndex{0xFF};
  std::uint8_t auctionAssetIndex{0xFF};
  std::int32_t debtAmount{};
  std::int32_t auctionCurrentBid{};
  std::int32_t auctionMinimumBid{};
  std::uint8_t auctionHighestBidderId{};
  std::array<PlayerSummary, 6> players{};
};

struct ActionRequest {
  ActionCode action{ActionCode::Roll};
  std::uint8_t playerId{};
  std::uint8_t assetIndex{0xFF};
  std::int32_t argument{};
  std::uint32_t expectedStateVersion{};
};

struct PairRequest {
  std::uint32_t deviceNonce{};
  std::uint32_t capabilities{};
  char displayName[17]{};
};

struct PairAccept {
  std::uint8_t accepted{};
  std::uint8_t seatId{};
  std::uint8_t wifiChannel{};
  std::uint8_t reserved{};
  std::uint32_t serverDeviceId{};
  std::uint32_t stateVersion{};
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t length);
bool encodeFrame(const Header& header, const std::uint8_t* payload, std::size_t payloadLength,
                 std::uint8_t* output, std::size_t capacity, std::size_t& written);
bool decodeFrame(const std::uint8_t* input, std::size_t length, DecodedFrame& output);

bool encodeStateSnapshot(const StateSnapshot& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written);
bool decodeStateSnapshot(const std::uint8_t* input, std::size_t length, StateSnapshot& value);
bool encodeActionRequest(const ActionRequest& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written);
bool decodeActionRequest(const std::uint8_t* input, std::size_t length, ActionRequest& value);
bool encodePairRequest(const PairRequest& value, std::uint8_t* output, std::size_t capacity,
                       std::size_t& written);
bool decodePairRequest(const std::uint8_t* input, std::size_t length, PairRequest& value);
bool encodePairAccept(const PairAccept& value, std::uint8_t* output, std::size_t capacity,
                      std::size_t& written);
bool decodePairAccept(const std::uint8_t* input, std::size_t length, PairAccept& value);

}  // namespace gridopoly::protocol
