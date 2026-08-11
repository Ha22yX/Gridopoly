#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gridopoly::core {

constexpr std::size_t kMaxPlayers = 6;
constexpr std::size_t kMaxAssets = 28;
constexpr std::size_t kMaxTiles = 40;
constexpr std::size_t kEventHistory = 32;
constexpr std::size_t kPlayerFinancialHistory = 10;
constexpr std::size_t kMaxConcurrentTrades = 3;
constexpr std::size_t kMaxTradeAssetsPerSide = 28;
constexpr std::size_t kMaxTradeAssetsTotal = 28;
constexpr std::uint8_t kNoPlayer = 0;
constexpr std::uint8_t kNoAsset = 0xFF;
constexpr std::uint8_t kNoGroup = 0xFF;

enum class TileKind : std::uint8_t {
  Start = 0,
  Property = 1,
  Transit = 2,
  Utility = 3,
  CityEvent = 4,
  CivicFund = 5,
  Fee = 6,
  Hold = 7,
  Rest = 8,
  GoToHold = 9,
};

enum class GamePhase : std::uint8_t {
  Lobby = 0,
  AwaitRoll = 1,
  AwaitMoveConfirm = 2,
  AwaitPurchase = 3,
  AwaitAuction = 4,
  AwaitDebt = 5,
  TurnEnd = 6,
  GameOver = 7,
  AwaitCard = 8,
};

enum class ControllerKind : std::uint8_t {
  RealConsole = 0,
  Web = 1,
  Bot = 2,
  Unassigned = 3,
};

enum class EventKind : std::uint8_t {
  GameStarted = 1,
  TurnStarted = 2,
  DiceRolled = 3,
  MoveRequested = 4,
  MoveCompleted = 5,
  PassedStart = 6,
  PurchaseOffered = 7,
  AssetPurchased = 8,
  RentPaid = 9,
  FeePaid = 10,
  CardApplied = 11,
  SentToHold = 12,
  ReleasedFromHold = 13,
  AssetMortgaged = 14,
  AssetUnmortgaged = 15,
  BuildingChanged = 16,
  AuctionSettled = 17,
  PlayerBankrupt = 18,
  TurnEnded = 19,
  GameFinished = 20,
  TradeSettled = 21,
  AuctionStarted = 22,
  AuctionBid = 23,
  AuctionPassed = 24,
  DebtOpened = 25,
  DebtPaid = 26,
  // Compact projection of canonical domain event CARD_DRAWN (0x0240).
  // CardApplied remains the compact projection of CARD_EFFECT_APPLIED (0x0241).
  CardDrawn = 27,
  TradeCreated = 28,
  TradeUpdated = 29,
  TradeClosed = 30,
};

enum ActionMask : std::uint32_t {
  ActionNone = 0,
  ActionRoll = 1u << 0,
  ActionConfirmPosition = 1u << 1,
  ActionBuy = 1u << 2,
  ActionDecline = 1u << 3,
  ActionEndTurn = 1u << 4,
  ActionMortgage = 1u << 5,
  ActionUnmortgage = 1u << 6,
  ActionBuild = 1u << 7,
  ActionSellBuilding = 1u << 8,
  ActionTrade = 1u << 9,
  ActionPayHoldFee = 1u << 10,
  ActionPayDebt = 1u << 11,
  ActionDeclareBankruptcy = 1u << 12,
  ActionAuctionBid = 1u << 13,
  ActionAuctionPass = 1u << 14,
  ActionAuctionReady = 1u << 15,
  ActionCardContinue = 1u << 16,
};

struct PropertyEconomy {
  std::int32_t price{};
  std::array<std::int32_t, 6> rent{};
  std::int32_t buildingCost{};
  std::int32_t mortgageValue{};
};

struct AssetDefinition {
  const char* id{};
  TileKind kind{TileKind::Property};
  std::uint8_t tileIndex{};
  std::uint8_t group{kNoGroup};
  PropertyEconomy economy{};
};

struct TileDefinition {
  const char* id{};
  TileKind kind{TileKind::Rest};
  std::uint8_t assetIndex{kNoAsset};
  std::int32_t amount{};
};

struct BoardDefinition {
  const char* id{};
  std::uint8_t tileCount{};
  std::uint8_t assetCount{};
  std::uint8_t recommendedMinPlayers{};
  std::uint8_t recommendedMaxPlayers{};
  std::int32_t startingCash{};
  std::int32_t startAward{};
  std::int32_t holdReleaseFee{};
  std::uint8_t buildingStock{};
  std::uint8_t landmarkStock{};
  const TileDefinition* tiles{};
  const AssetDefinition* assets{};
};

struct PlayerState {
  std::uint8_t id{};
  char name[20]{};
  ControllerKind controller{ControllerKind::Bot};
  std::int32_t cash{};
  std::uint8_t position{};
  std::uint8_t failedHoldRolls{};
  std::uint8_t doublesStreak{};
  bool inHold{};
  bool bankrupt{};
  bool connected{};
};

struct AssetState {
  std::uint8_t ownerId{kNoPlayer};
  std::uint8_t buildingLevel{};
  bool mortgaged{};
};

struct GameEvent {
  std::uint32_t sequence{};
  EventKind kind{EventKind::TurnStarted};
  std::uint8_t actorId{};
  std::uint8_t targetId{};
  std::uint8_t assetIndex{kNoAsset};
  std::int32_t amount{};
  std::uint32_t detail{};
};

struct FinancialRecord {
  std::uint32_t sequence{};
  std::int32_t amount{};
  EventKind kind{EventKind::GameStarted};
  std::uint8_t counterpartyId{};
  std::uint8_t assetIndex{kNoAsset};
};

struct PlayerFinancialHistory {
  std::array<FinancialRecord, kPlayerFinancialHistory> entries{};
  std::uint8_t head{};
  std::uint8_t count{};
  bool truncated{};
};

struct PendingMove {
  bool active{};
  std::uint8_t playerId{};
  std::uint8_t origin{};
  std::uint8_t target{};
  std::uint8_t dieA{};
  std::uint8_t dieB{};
  bool passedStart{};
};

struct PendingPurchase {
  bool active{};
  std::uint8_t playerId{};
  std::uint8_t assetIndex{kNoAsset};
};

enum class PendingCardStage : std::uint8_t {
  None = 0,
  AwaitContinue = 1,
  AwaitSettlement = 2,
};

enum class CardEffectOutcome : std::uint8_t {
  Applied = 1,
  Partial = 2,
  Bankrupt = 3,
  Cancelled = 4,
};

struct PendingCard {
  bool active{};
  PendingCardStage stage{PendingCardStage::None};
  std::uint8_t playerId{};
  std::uint8_t deckId{};  // 1 Chance, 2 Community Chest.
  std::uint8_t cardIndex{};
  std::uint8_t targetPlayerId{};
  std::uint8_t targetPosition{};
  std::uint16_t cardInstanceId{};
  std::uint16_t cardCatalogId{};
  std::uint16_t effectId{};
  std::int32_t displayAmount{};
  std::uint32_t drawEventSequence{};
};

// The legacy GameEvent ring remains fixed-size. Card projections pack the
// canonical identity and outcome into detail while the dedicated player wire
// message exposes the fields separately.
constexpr std::uint32_t packCardEventDetail(std::uint16_t instanceId, std::uint8_t deckId,
                                            std::uint8_t cardIndex, CardEffectOutcome outcome,
                                            std::uint8_t targetPosition) {
  return static_cast<std::uint32_t>(instanceId) |
      (static_cast<std::uint32_t>(cardIndex & 0x07u) << 16) |
      (static_cast<std::uint32_t>((deckId == 2 ? 1u : 0u)) << 19) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(outcome) & 0x07u) << 20) |
      (static_cast<std::uint32_t>(targetPosition & 0x3Fu) << 23);
}

constexpr std::uint16_t cardEventInstanceId(std::uint32_t detail) {
  return static_cast<std::uint16_t>(detail & 0xFFFFu);
}
constexpr std::uint8_t cardEventCardIndex(std::uint32_t detail) {
  return static_cast<std::uint8_t>((detail >> 16) & 0x07u);
}
constexpr std::uint8_t cardEventDeckId(std::uint32_t detail) {
  return static_cast<std::uint8_t>(((detail >> 19) & 0x01u) + 1u);
}
constexpr CardEffectOutcome cardEventOutcome(std::uint32_t detail) {
  return static_cast<CardEffectOutcome>((detail >> 20) & 0x07u);
}
constexpr std::uint8_t cardEventTargetPosition(std::uint32_t detail) {
  return static_cast<std::uint8_t>((detail >> 23) & 0x3Fu);
}
constexpr std::uint16_t cardCatalogId(std::uint8_t deckId, std::uint8_t cardIndex) {
  return static_cast<std::uint16_t>((deckId == 2 ? 8u : 0u) + (cardIndex & 0x07u) + 1u);
}

enum class DebtContinuation : std::uint8_t {
  None = 0,
  FinishLanding = 1,
  ReleaseHoldAndMove = 2,
};

struct PendingDebt {
  bool active{};
  std::uint8_t debtorId{};
  std::uint8_t creditorId{};
  std::uint8_t assetIndex{kNoAsset};
  EventKind paymentEvent{EventKind::FeePaid};
  DebtContinuation continuation{DebtContinuation::None};
  std::uint8_t dieA{};
  std::uint8_t dieB{};
  std::int32_t amount{};
};

struct AuctionState {
  bool active{};
  std::uint8_t assetIndex{kNoAsset};
  std::uint8_t landingPlayerId{};
  std::uint8_t currentBidderId{};
  std::uint8_t highestBidderId{};
  std::uint8_t passedMask{};
  std::uint8_t readyMask{};
  std::uint8_t requiredReadyMask{};
  std::int32_t currentBid{};
  std::uint32_t generation{};
};

enum class TradeWorkflowStatus : std::uint8_t {
  None = 0,
  Offered = 1,
  Countered = 2,
  Settled = 3,
  Rejected = 4,
  Cancelled = 5,
  Expired = 6,
  Invalidated = 7,
};

struct TradeOfferSide {
  std::int32_t cash{};
  std::uint8_t assetCount{};
  std::array<std::uint8_t, kMaxTradeAssetsPerSide> assets{};
};

struct TradeWorkflow {
  std::uint32_t tradeId{};
  std::uint16_t revision{};
  TradeWorkflowStatus status{TradeWorkflowStatus::None};
  std::uint8_t proposerId{};
  std::uint8_t counterpartyId{};
  std::uint8_t lastEditorId{};
  std::uint8_t confirmedMask{};
  std::uint8_t botCounteredMask{};
  TradeOfferSide proposerGives{};
  TradeOfferSide counterpartyGives{};
  std::uint64_t deadlineEpochMs{};
};

struct GameState {
  const BoardDefinition* board{};
  std::array<PlayerState, kMaxPlayers> players{};
  std::array<AssetState, kMaxAssets> assets{};
  std::array<GameEvent, kEventHistory> events{};
  std::array<PlayerFinancialHistory, kMaxPlayers> financialHistory{};
  bool financialHistoryInitialized{};
  std::uint8_t playerCount{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t eventHead{};
  std::uint8_t eventCount{};
  std::uint8_t winnerPlayerId{};
  GamePhase phase{GamePhase::Lobby};
  PendingMove pendingMove{};
  PendingPurchase pendingPurchase{};
  PendingCard pendingCard{};
  PendingDebt pendingDebt{};
  AuctionState auction{};
  std::array<TradeWorkflow, kMaxConcurrentTrades> trades{};
  std::uint32_t stateVersion{};
  std::uint32_t nextEventSequence{1};
  std::uint32_t nextAuctionGeneration{1};
  std::uint32_t nextTradeId{1};
  std::uint32_t rngState{0x6D2B79F5u};
};

enum class ErrorCode : std::uint8_t {
  Ok = 0,
  InvalidPhase,
  InvalidPlayer,
  InvalidArgument,
  NotEnoughCash,
  NotOwner,
  RuleViolation,
  PositionMismatch,
  GameOver,
};

struct Result {
  ErrorCode code{ErrorCode::Ok};
  const char* message{"ok"};
  constexpr explicit operator bool() const { return code == ErrorCode::Ok; }
};

}  // namespace gridopoly::core
