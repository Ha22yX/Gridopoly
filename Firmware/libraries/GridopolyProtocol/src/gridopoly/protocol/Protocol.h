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
  AuthoritySnapshot = 0x12,
  RosterSnapshot = 0x13,
  ActionRequest = 0x20,
  ActionResult = 0x21,
  Ack = 0x22,
  Error = 0x23,
  PlayerDetailRequest = 0x24,
  PlayerDetailResponse = 0x25,
  PlayerCardEvent = 0x26,
  TradeRequest = 0x27,
  TradeResponse = 0x28,
  IdentityRequest = 0x29,
  IdentitySnapshot = 0x2A,
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
  AuctionReady = 15,
  CardContinue = 16,
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

struct AuthorityPlayerState {
  std::uint8_t playerId{};
  std::uint8_t position{};
  std::int32_t cash{};
  std::uint8_t flags{};  // bit0 held, bit1 bankrupt, bit2 connected, bits3-4 controller.
  std::uint8_t failedHoldRolls{};
  std::uint8_t doublesStreak{};
};

struct AuthorityAssetState {
  std::uint8_t ownerId{};
  std::uint8_t buildingLevel{};
  std::uint8_t flags{};  // bit0 mortgaged.
};

struct AuthoritySnapshot {
  std::uint8_t phase{};
  std::uint8_t activePlayerId{};
  std::uint8_t decisionPlayerId{};
  std::uint8_t winnerPlayerId{};
  std::uint8_t boardSize{};
  std::uint8_t playerCount{};
  std::uint8_t assetCount{};
  std::uint16_t round{};
  std::uint32_t stateVersion{};
  std::uint32_t lastEventSequence{};
  std::uint32_t boardIdHash{};
  std::uint8_t pendingMoveFlags{};  // bit0 active, bit1 passed start.
  std::uint8_t pendingMovePlayerId{};
  std::uint8_t pendingMoveOrigin{};
  std::uint8_t pendingMoveTarget{0xFF};
  std::uint8_t pendingMoveDieA{};
  std::uint8_t pendingMoveDieB{};
  std::uint8_t pendingPurchaseFlags{};  // bit0 active.
  std::uint8_t pendingPurchasePlayerId{};
  std::uint8_t pendingPurchaseAssetIndex{0xFF};
  std::uint8_t debtFlags{};  // bit0 active.
  std::uint8_t debtDebtorId{};
  std::uint8_t debtCreditorId{};
  std::uint8_t debtAssetIndex{0xFF};
  std::uint8_t debtPaymentEvent{};
  std::uint8_t debtContinuation{};
  std::uint8_t debtDieA{};
  std::uint8_t debtDieB{};
  std::int32_t debtAmount{};
  std::uint8_t auctionFlags{};  // bit0 active, bit1 opening barrier.
  std::uint8_t auctionAssetIndex{0xFF};
  std::uint8_t auctionLandingPlayerId{};
  std::uint8_t auctionCurrentBidderId{};
  std::uint8_t auctionHighestBidderId{};
  std::uint8_t auctionPassedMask{};
  std::uint8_t auctionReadyMask{};
  std::uint8_t auctionRequiredReadyMask{};
  std::int32_t auctionCurrentBid{};
  std::uint32_t auctionGeneration{};
  std::uint8_t pendingCardFlags{};  // bit0 active, bit1 revealed, bit2 continued, bit3 settlement.
  std::uint8_t pendingCardPlayerId{};
  std::uint8_t pendingCardDeckId{};
  std::uint8_t pendingCardIndex{};
  std::uint16_t pendingCardInstanceId{};
  std::uint16_t pendingCardCatalogId{};
  std::uint16_t pendingCardEffectId{};
  std::int32_t pendingCardDisplayAmount{};
  std::uint8_t pendingCardTargetPlayerId{};
  std::uint8_t pendingCardTargetPosition{};
  std::uint32_t pendingCardDrawEventSequence{};
  std::array<AuthorityPlayerState, 6> players{};
  std::array<AuthorityAssetState, 28> assets{};
};

struct RosterSnapshot {
  std::uint32_t stateVersion{};
  std::uint8_t playerCount{};
  std::array<std::uint8_t, 6> playerIds{};
  std::array<std::array<char, 17>, 6> displayNames{};
};

struct GameEventRecord {
  std::uint32_t sequence{};
  std::uint8_t kind{};
  std::uint8_t actorId{};
  std::uint8_t targetId{};
  std::uint8_t assetIndex{0xFF};
  std::int32_t amount{};
  std::uint32_t detail{};
};

constexpr std::size_t kMaxEventsPerBatch = 13;

struct GameEventBatch {
  std::uint32_t stateVersion{};
  std::uint8_t eventCount{};
  std::array<GameEventRecord, kMaxEventsPerBatch> events{};
};

constexpr std::uint16_t kDomainEventCardDrawn = 0x0240;
constexpr std::uint16_t kDomainEventCardEffectApplied = 0x0241;
constexpr std::size_t kPlayerCardEventSize = 32;

enum class PlayerCardStage : std::uint8_t {
  Drawn = 1,
  EffectApplied = 2,
};

enum PlayerCardFlags : std::uint8_t {
  PlayerCardFlagNone = 0,
  PlayerCardFlagKeepable = 1u << 0,
  PlayerCardFlagReplay = 1u << 1,
};

struct PlayerCardEvent {
  PlayerCardStage stage{PlayerCardStage::Drawn};
  std::uint16_t domainEventType{kDomainEventCardDrawn};
  std::uint32_t stateVersion{};
  std::uint32_t eventSequence{};
  std::uint8_t playerId{};
  std::uint8_t deckId{};
  std::uint8_t cardIndex{};
  std::uint8_t flags{};
  std::uint16_t cardInstanceId{};
  std::uint16_t cardCatalogId{};
  std::uint16_t effectId{};
  std::int32_t amount{};
  std::uint8_t targetPlayerId{};
  std::uint8_t targetPosition{};
  std::uint8_t outcome{};
};

struct Heartbeat {
  std::uint8_t flags{};  // bit0 requests a full resync.
  std::uint32_t appliedStateVersion{};
  std::uint32_t appliedEventSequence{};
};

struct ActionRequest {
  ActionCode action{ActionCode::Roll};
  std::uint8_t playerId{};
  std::uint8_t assetIndex{0xFF};
  std::int32_t argument{};
  std::uint32_t expectedStateVersion{};
};

constexpr std::size_t kMaxPlayerDetailAssets = 28;
constexpr std::size_t kMaxPlayerDetailLedgerEntries = 10;
constexpr std::size_t kPlayerDetailRequestSize = 12;
constexpr std::size_t kPlayerDetailResponseBaseSize = 20;
constexpr std::size_t kPlayerDetailAssetSize = 2;
constexpr std::size_t kPlayerDetailLedgerEntrySize = 12;
constexpr std::size_t kMaxPlayerDetailResponseSize =
    kPlayerDetailResponseBaseSize + kMaxPlayerDetailAssets * kPlayerDetailAssetSize +
    kMaxPlayerDetailLedgerEntries * kPlayerDetailLedgerEntrySize;
static_assert(kMaxPlayerDetailResponseSize <= kMaxPayloadSize,
              "player detail response must fit one ESP-NOW payload");

enum PlayerDetailResponseFlags : std::uint8_t {
  PlayerDetailFlagNone = 0,
  PlayerDetailFlagAssetsTruncated = 1u << 0,
  PlayerDetailFlagLedgerTruncated = 1u << 1,
  PlayerDetailFlagRequestedVersionStale = 1u << 2,
};

enum PlayerDetailAssetStateBits : std::uint8_t {
  PlayerDetailAssetBuildingMask = 0x07,
  PlayerDetailAssetMortgaged = 1u << 3,
};

enum PlayerDetailLedgerFlags : std::uint8_t {
  PlayerDetailLedgerFlagNone = 0,
  PlayerDetailLedgerFlagCredit = 1u << 0,
  PlayerDetailLedgerFlagBankCounterparty = 1u << 1,
  PlayerDetailLedgerFlagHasAsset = 1u << 2,
};

struct PlayerDetailRequest {
  std::uint32_t requestId{};
  std::uint8_t targetPlayerId{};
  std::uint32_t expectedStateVersion{};
};

struct PlayerDetailAsset {
  std::uint8_t assetIndex{0xFF};
  std::uint8_t state{};  // bits0-2 building level, bit3 mortgaged.
};

struct PlayerDetailLedgerEntry {
  std::uint32_t sequence{};
  std::int32_t amount{};  // Signed from the requested player's perspective.
  std::uint8_t kind{};
  std::uint8_t counterpartyId{};  // 0 is the bank.
  std::uint8_t assetIndex{0xFF};
  std::uint8_t flags{};
};

struct PlayerDetailResponse {
  std::uint32_t requestId{};
  std::uint32_t stateVersion{};
  std::int32_t cash{};
  std::uint8_t targetPlayerId{};
  std::uint8_t position{};
  std::uint8_t flags{};
  std::uint8_t assetCount{};
  std::uint8_t ledgerCount{};
  std::uint8_t totalOwnedAssets{};
  std::array<PlayerDetailAsset, kMaxPlayerDetailAssets> assets{};
  std::array<PlayerDetailLedgerEntry, kMaxPlayerDetailLedgerEntries> ledger{};
};

constexpr std::size_t kMaxTradeAssetsPerSide = 28;
constexpr std::size_t kMaxTradeAssetsTotal = 28;
constexpr std::size_t kTradeRequestBaseSize = 32;
constexpr std::size_t kTradeResponseBaseSize = 40;
constexpr std::size_t kMaxTradeRequestSize =
    kTradeRequestBaseSize + kMaxTradeAssetsTotal;
constexpr std::size_t kMaxTradeResponseSize =
    kTradeResponseBaseSize + kMaxTradeAssetsTotal;
static_assert(kMaxTradeRequestSize <= kMaxPayloadSize,
              "trade request must fit one protocol payload");
static_assert(kMaxTradeResponseSize <= kMaxPayloadSize,
              "trade response must fit one protocol payload");

enum class TradeOperation : std::uint8_t {
  Query = 1,
  Create = 2,
  Update = 3,
  Confirm = 4,
  Reject = 5,
  Cancel = 6,
};

enum class TradeStatus : std::uint8_t {
  None = 0,
  Offered = 1,
  Countered = 2,
  Settled = 3,
  Rejected = 4,
  Cancelled = 5,
  Expired = 6,
  Invalidated = 7,
};

enum class TradeResultCode : std::uint8_t {
  Ok = 0,
  NoActiveTrade = 1,
  InvalidRequest = 2,
  Unauthorized = 3,
  StateVersionStale = 4,
  RevisionStale = 5,
  ParticipantBusy = 6,
  RuleViolation = 7,
  NotEnoughCash = 8,
  AssetUnavailable = 9,
  Expired = 10,
  RequestIdConflict = 11,
};

enum TradeResponseFlags : std::uint8_t {
  TradeResponseFlagNone = 0,
  TradeResponseFlagSelfConfirmed = 1u << 0,
  TradeResponseFlagCounterpartyConfirmed = 1u << 1,
  TradeResponseFlagSelfOriginated = 1u << 2,
  TradeResponseFlagSelfLastEdited = 1u << 3,
  TradeResponseFlagResync = 1u << 4,
  TradeResponseFlagTerminal = 1u << 5,
  TradeResponseFlagRequestedVersionStale = 1u << 6,
};

// Requests are actor-relative. self* is offered by the authenticated source
// seat; counterparty* is offered by targetPlayerId. The server canonicalizes
// both sides against the stored proposer/counterparty orientation.
struct TradeRequest {
  TradeOperation operation{TradeOperation::Query};
  std::uint8_t targetPlayerId{};
  std::uint16_t expectedRevision{};
  std::uint32_t requestId{};
  std::uint32_t expectedStateVersion{};
  std::uint32_t tradeId{};
  std::int32_t selfGivesCash{};
  std::int32_t counterpartyGivesCash{};
  std::uint8_t selfAssetCount{};
  std::uint8_t counterpartyAssetCount{};
  std::array<std::uint8_t, kMaxTradeAssetsPerSide> selfAssets{};
  std::array<std::uint8_t, kMaxTradeAssetsPerSide> counterpartyAssets{};
};

// Responses are also recipient-relative so the player screen never needs to
// reverse proposer/counterparty lists. requestId==0 is reserved for a
// FlagResync recovery projection sent after pairing/full resync.
struct TradeResponse {
  TradeOperation operation{TradeOperation::Query};
  TradeResultCode result{TradeResultCode::Ok};
  TradeStatus status{TradeStatus::None};
  std::uint8_t flags{};
  std::uint8_t selfPlayerId{};
  std::uint8_t counterpartyId{};
  std::uint16_t revision{};
  std::uint32_t requestId{};
  std::uint32_t stateVersion{};
  std::uint32_t tradeId{};
  std::uint32_t expiresInMs{};
  std::int32_t selfGivesCash{};
  std::int32_t counterpartyGivesCash{};
  std::uint8_t confirmedMask{};
  std::uint8_t originatorId{};
  std::uint8_t selfAssetCount{};
  std::uint8_t counterpartyAssetCount{};
  std::array<std::uint8_t, kMaxTradeAssetsPerSide> selfAssets{};
  std::array<std::uint8_t, kMaxTradeAssetsPerSide> counterpartyAssets{};
};

constexpr std::uint16_t kAvatarCatalogVersionV1 = 1;
constexpr std::size_t kIdentityRequestSize = 44;
constexpr std::size_t kIdentitySeatRecordSize = 23;
constexpr std::size_t kIdentitySnapshotBaseSize = 44;
constexpr std::size_t kIdentitySnapshotSize =
    kIdentitySnapshotBaseSize + 6 * kIdentitySeatRecordSize;
static_assert(kIdentitySnapshotSize <= kMaxPayloadSize,
              "identity snapshot must fit one protocol payload");

enum class IdentityOperation : std::uint8_t {
  None = 0,
  Query = 1,
  ConfirmAvatar = 2,
  ConfirmName = 3,
};

enum class IdentityRoomPhase : std::uint8_t {
  AvatarSetup = 1,
  Countdown = 2,
  Active = 3,
};

enum class IdentitySeatStage : std::uint8_t {
  AvatarSetup = 1,
  AvatarGenerating = 2,
  NameSetup = 3,
  Ready = 4,
  Countdown = 5,
  Active = 6,
};

enum class IdentityResultCode : std::uint8_t {
  Ok = 0,
  InvalidRequest = 1,
  Unauthorized = 2,
  StateVersionStale = 3,
  SeatRevisionStale = 4,
  CatalogMismatch = 5,
  InvalidRecipe = 6,
  InvalidName = 7,
  DuplicateName = 8,
  NotAllowed = 9,
  RequestIdConflict = 10,
  AvatarGenerationFailed = 11,
};

enum IdentitySnapshotFlags : std::uint8_t {
  IdentitySnapshotFlagNone = 0,
  IdentitySnapshotFlagReplay = 1u << 0,
  IdentitySnapshotFlagResync = 1u << 1,
};

enum IdentitySeatFlags : std::uint8_t {
  IdentitySeatNone = 0,
  IdentitySeatPresent = 1u << 0,
  IdentitySeatHuman = 1u << 1,
  IdentitySeatBot = 1u << 2,
  IdentitySeatAvatarGenerating = 1u << 3,
  IdentitySeatAvatarFinal = 1u << 4,
  IdentitySeatNameFinal = 1u << 5,
  IdentitySeatReady = 1u << 6,
  IdentitySeatConnected = 1u << 7,
};

struct AvatarRecipe {
  std::uint16_t avatarCatalogVersion{};
  std::uint8_t hairPresetId{};
  std::uint8_t hairColorId{};
  std::uint8_t facePresetId{};
  std::uint8_t skinToneId{};
  std::uint8_t outfitPresetId{};
};

struct IdentityRequest {
  IdentityOperation operation{IdentityOperation::Query};
  std::uint8_t playerId{};
  std::uint32_t requestId{};
  std::uint32_t expectedStateVersion{};
  std::uint16_t expectedSeatRevision{};
  std::uint16_t avatarCatalogVersion{};
  AvatarRecipe recipe{};
  std::uint8_t nameLength{};
  std::array<char, 17> name{};
};

struct IdentitySeatRecord {
  std::uint8_t playerId{};
  std::uint8_t flags{};
  std::uint8_t seatColorId{};
  std::uint16_t seatRevision{};
  std::uint16_t avatarRevision{};
  std::uint64_t avatarContentHash64{};
  AvatarRecipe recipe{};
};

struct IdentitySnapshot {
  IdentityRoomPhase roomPhase{IdentityRoomPhase::AvatarSetup};
  IdentitySeatStage selfStage{IdentitySeatStage::AvatarSetup};
  IdentityResultCode result{IdentityResultCode::Ok};
  std::uint32_t requestId{};
  std::uint32_t stateVersion{};
  std::uint32_t identityRevision{};
  std::uint64_t serverEpochMs{};
  std::uint64_t countdownDeadlineEpochMs{};
  std::uint16_t avatarCatalogVersion{kAvatarCatalogVersionV1};
  std::uint8_t playerCount{};
  std::uint8_t selfPlayerId{};
  std::uint8_t requiredHumanMask{};
  std::uint8_t avatarFinalMask{};
  std::uint8_t nameFinalMask{};
  std::uint8_t readyMask{};
  std::uint8_t onlineMask{};
  IdentityOperation operationEcho{IdentityOperation::None};
  std::uint8_t flags{};
  std::array<IdentitySeatRecord, 6> seats{};
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
  std::uint32_t sessionId{};  // UDP schema v2; zero for ESP-NOW sessions.
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t length);
bool encodeFrame(const Header& header, const std::uint8_t* payload, std::size_t payloadLength,
                 std::uint8_t* output, std::size_t capacity, std::size_t& written);
bool decodeFrame(const std::uint8_t* input, std::size_t length, DecodedFrame& output);

bool encodeStateSnapshot(const StateSnapshot& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written);
bool decodeStateSnapshot(const std::uint8_t* input, std::size_t length, StateSnapshot& value);
bool encodeAuthoritySnapshot(const AuthoritySnapshot& value, std::uint8_t* output, std::size_t capacity,
                             std::size_t& written);
bool decodeAuthoritySnapshot(const std::uint8_t* input, std::size_t length, AuthoritySnapshot& value);
bool encodeRosterSnapshot(const RosterSnapshot& value, std::uint8_t* output, std::size_t capacity,
                          std::size_t& written);
bool decodeRosterSnapshot(const std::uint8_t* input, std::size_t length, RosterSnapshot& value);
bool encodeGameEventBatch(const GameEventBatch& value, std::uint8_t* output, std::size_t capacity,
                          std::size_t& written);
bool decodeGameEventBatch(const std::uint8_t* input, std::size_t length, GameEventBatch& value);
bool encodePlayerCardEvent(const PlayerCardEvent& value, std::uint8_t* output,
                           std::size_t capacity, std::size_t& written);
bool decodePlayerCardEvent(const std::uint8_t* input, std::size_t length,
                           PlayerCardEvent& value);
bool encodeHeartbeat(const Heartbeat& value, std::uint8_t* output, std::size_t capacity,
                     std::size_t& written);
bool decodeHeartbeat(const std::uint8_t* input, std::size_t length, Heartbeat& value);
bool encodeActionRequest(const ActionRequest& value, std::uint8_t* output, std::size_t capacity,
                         std::size_t& written);
bool decodeActionRequest(const std::uint8_t* input, std::size_t length, ActionRequest& value);
bool encodePlayerDetailRequest(const PlayerDetailRequest& value, std::uint8_t* output,
                               std::size_t capacity, std::size_t& written);
bool decodePlayerDetailRequest(const std::uint8_t* input, std::size_t length,
                               PlayerDetailRequest& value);
bool encodePlayerDetailResponse(const PlayerDetailResponse& value, std::uint8_t* output,
                                std::size_t capacity, std::size_t& written);
bool decodePlayerDetailResponse(const std::uint8_t* input, std::size_t length,
                                PlayerDetailResponse& value);
bool encodeTradeRequest(const TradeRequest& value, std::uint8_t* output,
                        std::size_t capacity, std::size_t& written);
bool decodeTradeRequest(const std::uint8_t* input, std::size_t length,
                        TradeRequest& value);
bool encodeTradeResponse(const TradeResponse& value, std::uint8_t* output,
                         std::size_t capacity, std::size_t& written);
bool decodeTradeResponse(const std::uint8_t* input, std::size_t length,
                         TradeResponse& value);
bool encodeIdentityRequest(const IdentityRequest& value, std::uint8_t* output,
                           std::size_t capacity, std::size_t& written);
bool decodeIdentityRequest(const std::uint8_t* input, std::size_t length,
                           IdentityRequest& value);
bool encodeIdentitySnapshot(const IdentitySnapshot& value, std::uint8_t* output,
                            std::size_t capacity, std::size_t& written);
bool decodeIdentitySnapshot(const std::uint8_t* input, std::size_t length,
                            IdentitySnapshot& value);
bool encodePairRequest(const PairRequest& value, std::uint8_t* output, std::size_t capacity,
                       std::size_t& written);
bool decodePairRequest(const std::uint8_t* input, std::size_t length, PairRequest& value);
bool encodePairAccept(const PairAccept& value, std::uint8_t* output, std::size_t capacity,
                      std::size_t& written);
bool decodePairAccept(const std::uint8_t* input, std::size_t length, PairAccept& value);

}  // namespace gridopoly::protocol
