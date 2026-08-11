#pragma once

#include "BoardCatalog.h"

namespace gridopoly::core {

class GameEngine {
 public:
  GameEngine();

  Result reset(const BoardDefinition& board, std::uint32_t seed);
  Result addPlayer(const char* name, ControllerKind controller, bool connected = true);
  Result start();

  Result roll(std::uint8_t playerId, std::uint8_t dieA = 0, std::uint8_t dieB = 0);
  Result confirmPosition(std::uint8_t playerId, std::uint8_t observedPosition);
  Result buy(std::uint8_t playerId);
  Result declinePurchase(std::uint8_t playerId);
  Result endTurn(std::uint8_t playerId);
  Result payHoldFee(std::uint8_t playerId);
  Result mortgage(std::uint8_t playerId, std::uint8_t assetIndex);
  Result unmortgage(std::uint8_t playerId, std::uint8_t assetIndex);
  Result build(std::uint8_t playerId, std::uint8_t assetIndex);
  Result sellBuilding(std::uint8_t playerId, std::uint8_t assetIndex);
  Result continueCard(std::uint8_t playerId, std::uint16_t cardInstanceId);
  Result payDebt(std::uint8_t playerId);
  Result declareBankruptcy(std::uint8_t playerId);
  Result auctionBid(std::uint8_t playerId, std::int32_t amount);
  Result auctionPass(std::uint8_t playerId);
  Result auctionReady(std::uint8_t playerId, std::uint8_t assetIndex, std::uint32_t generation);
  bool tradeForPlayer(std::uint8_t playerId, TradeWorkflow& output) const;
  Result createTrade(std::uint8_t actorId, std::uint8_t counterpartyId,
                     const TradeOfferSide& actorGives,
                     const TradeOfferSide& counterpartyGives,
                     std::uint64_t nowEpochMs, TradeWorkflow& output);
  Result updateTrade(std::uint8_t actorId, std::uint32_t tradeId,
                     std::uint16_t expectedRevision,
                     const TradeOfferSide& actorGives,
                     const TradeOfferSide& counterpartyGives,
                     std::uint64_t nowEpochMs, TradeWorkflow& output);
  Result confirmTrade(std::uint8_t actorId, std::uint32_t tradeId,
                      std::uint16_t expectedRevision, std::uint64_t nowEpochMs,
                      TradeWorkflow& output);
  Result rejectTrade(std::uint8_t actorId, std::uint32_t tradeId,
                     std::uint16_t expectedRevision, TradeWorkflow& output);
  Result cancelTrade(std::uint8_t actorId, std::uint32_t tradeId,
                     std::uint16_t expectedRevision, TradeWorkflow& output);
  std::size_t expireTrades(std::uint64_t nowEpochMs);
  std::size_t runTradeBots(std::uint64_t nowEpochMs, std::size_t maxActions = 1);

  std::size_t runBots(std::size_t maxActions = 16);
  std::uint32_t actionsFor(std::uint8_t playerId) const;
  std::uint8_t decisionPlayerId() const;
  const GameState& state() const { return state_; }
  GameState& mutableStateForRestore() { return state_; }

 private:
  GameState state_{};

  PlayerState* player(std::uint8_t playerId);
  const PlayerState* player(std::uint8_t playerId) const;
  Result requireActive(std::uint8_t playerId, GamePhase phase) const;
  Result resolveLanding(PlayerState& actor, std::uint8_t diceTotal);
  Result resolveAsset(PlayerState& actor, std::uint8_t assetIndex, std::uint8_t diceTotal);
  Result resolveCard(PlayerState& actor, bool cityEvent);
  Result applyPendingCard(PlayerState& actor);
  void emitCardApplied(const PendingCard& card, std::int32_t amount, CardEffectOutcome outcome);
  void completePendingCard(std::int32_t amount, CardEffectOutcome outcome, bool finish = true);
  void completeMovement(PlayerState& actor);
  void queueMovement(PlayerState& actor, std::uint8_t dieA, std::uint8_t dieB);
  void finishLanding(PlayerState& actor);
  void sendToHold(PlayerState& actor);
  void beginAuction(std::uint8_t landingPlayerId, std::uint8_t assetIndex);
  void openAuctionBidding();
  void advanceAuction();
  void settleAuction();
  bool transferPayment(PlayerState& payer, std::uint8_t creditorId, std::int32_t amount,
                       EventKind eventKind, std::uint8_t assetIndex = kNoAsset,
                       DebtContinuation continuation = DebtContinuation::FinishLanding,
                       std::uint8_t dieA = 0, std::uint8_t dieB = 0,
                       std::int32_t* settledAmount = nullptr);
  void resumeDebtContinuation(const PendingDebt& debt);
  std::int32_t maximumLiquidationCash(std::uint8_t playerId) const;
  void bankrupt(PlayerState& debtor, std::uint8_t creditorId);
  void checkGameOver();
  void advanceTurn();
  bool ownsCompleteGroup(std::uint8_t playerId, std::uint8_t group) const;
  bool tradeMutationPhaseAllowed() const;
  Result validateTradeOffer(const TradeWorkflow& trade) const;
  bool settleTrade(TradeWorkflow& trade);
  std::int64_t tradeSideValue(const TradeOfferSide& side) const;
  TradeWorkflow* findTrade(std::uint32_t tradeId);
  const TradeWorkflow* findTrade(std::uint32_t tradeId) const;
  TradeWorkflow* activeTradeForPlayer(std::uint8_t playerId);
  const TradeWorkflow* activeTradeForPlayer(std::uint8_t playerId) const;
  std::uint8_t ownedKindCount(std::uint8_t playerId, TileKind kind) const;
  std::uint8_t holdPosition() const;
  std::uint32_t nextRandom();
  std::uint8_t randomDie();
  void ensureFinancialHistoryInitialized();
  void recordFinancial(std::uint8_t playerId, std::int32_t amount, EventKind kind,
                       std::uint8_t counterpartyId, std::uint8_t assetIndex,
                       std::uint32_t sequence);
  void recordFinancialEvent(const GameEvent& event);
  void emit(EventKind kind, std::uint8_t actorId, std::uint8_t targetId = 0,
            std::uint8_t assetIndex = kNoAsset, std::int32_t amount = 0, std::uint32_t detail = 0);
  void changed();
};

}  // namespace gridopoly::core
