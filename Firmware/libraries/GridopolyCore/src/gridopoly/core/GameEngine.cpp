#include "GameEngine.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace gridopoly::core {
namespace {

constexpr Result ok() { return {ErrorCode::Ok, "ok"}; }
constexpr Result error(ErrorCode code, const char* message) { return {code, message}; }
constexpr std::uint64_t kTradeLifetimeMs = 120000;

constexpr bool activeTradeStatus(TradeWorkflowStatus status) {
  return status == TradeWorkflowStatus::Offered || status == TradeWorkflowStatus::Countered;
}

constexpr std::uint8_t playerBit(std::uint8_t playerId) {
  return playerId == 0 || playerId > kMaxPlayers
      ? 0
      : static_cast<std::uint8_t>(1u << (playerId - 1));
}

}  // namespace

GameEngine::GameEngine() { reset(BoardCatalog::at(0), 0x6D2B79F5u); }

Result GameEngine::reset(const BoardDefinition& board, std::uint32_t seed) {
  state_ = GameState{};
  state_.board = &board;
  state_.rngState = seed == 0 ? 0x6D2B79F5u : seed;
  state_.phase = GamePhase::Lobby;
  state_.stateVersion = 1;
  return ok();
}

Result GameEngine::addPlayer(const char* name, ControllerKind controller, bool connected) {
  if (state_.phase != GamePhase::Lobby) {
    return error(ErrorCode::InvalidPhase, "players can only join in lobby");
  }
  if (state_.playerCount >= kMaxPlayers || name == nullptr || name[0] == '\0') {
    return error(ErrorCode::InvalidArgument, "invalid player");
  }
  auto& item = state_.players[state_.playerCount];
  item = PlayerState{};
  item.id = static_cast<std::uint8_t>(state_.playerCount + 1);
  std::size_t nameLength = 0;
  while (name[nameLength] != '\0' && nameLength + 1 < sizeof(item.name)) {
    item.name[nameLength] = name[nameLength];
    ++nameLength;
  }
  item.name[nameLength] = '\0';
  item.controller = controller;
  item.connected = connected;
  item.cash = state_.board->startingCash;
  state_.playerCount++;
  changed();
  return ok();
}

Result GameEngine::start() {
  if (state_.phase != GamePhase::Lobby || state_.playerCount == 0) {
    return error(ErrorCode::InvalidPhase, "game requires at least one player");
  }
  state_.activePlayerId = 1;
  state_.roundNumber = 1;
  state_.phase = GamePhase::AwaitRoll;
  emit(EventKind::GameStarted, 0, 0, kNoAsset, state_.playerCount);
  emit(EventKind::TurnStarted, state_.activePlayerId);
  changed();
  return ok();
}

Result GameEngine::roll(std::uint8_t playerId, std::uint8_t dieA, std::uint8_t dieB) {
  auto required = requireActive(playerId, GamePhase::AwaitRoll);
  if (!required) {
    return required;
  }
  auto* actor = player(playerId);
  if (dieA == 0) dieA = randomDie();
  if (dieB == 0) dieB = randomDie();
  if (dieA < 1 || dieA > 6 || dieB < 1 || dieB > 6) {
    return error(ErrorCode::InvalidArgument, "dice must be 1..6");
  }
  const bool isDouble = dieA == dieB;
  emit(EventKind::DiceRolled, actor->id, 0, kNoAsset, dieA + dieB,
       static_cast<std::uint32_t>(dieA) | (static_cast<std::uint32_t>(dieB) << 8));

  if (actor->inHold) {
    if (isDouble) {
      actor->inHold = false;
      actor->failedHoldRolls = 0;
      actor->doublesStreak = 0;
      emit(EventKind::ReleasedFromHold, actor->id);
    } else {
      actor->failedHoldRolls++;
      if (actor->failedHoldRolls < 3) {
        state_.phase = GamePhase::TurnEnd;
        changed();
        return ok();
      }
      if (!transferPayment(*actor, 0, state_.board->holdReleaseFee, EventKind::FeePaid,
                           kNoAsset, DebtContinuation::ReleaseHoldAndMove, dieA, dieB)) {
        changed();
        return ok();
      }
      actor->inHold = false;
      actor->failedHoldRolls = 0;
      emit(EventKind::ReleasedFromHold, actor->id, 0, kNoAsset, state_.board->holdReleaseFee);
    }
  } else {
    actor->doublesStreak = isDouble ? static_cast<std::uint8_t>(actor->doublesStreak + 1) : 0;
    if (actor->doublesStreak >= 3) {
      sendToHold(*actor);
      state_.phase = GamePhase::TurnEnd;
      changed();
      return ok();
    }
  }

  queueMovement(*actor, dieA, dieB);
  changed();
  return ok();
}

Result GameEngine::confirmPosition(std::uint8_t playerId, std::uint8_t observedPosition) {
  auto required = requireActive(playerId, GamePhase::AwaitMoveConfirm);
  if (!required) return required;
  if (!state_.pendingMove.active || state_.pendingMove.playerId != playerId) {
    return error(ErrorCode::RuleViolation, "no pending movement");
  }
  if (observedPosition != state_.pendingMove.target) {
    return error(ErrorCode::PositionMismatch, "observed position does not match target");
  }
  auto* actor = player(playerId);
  completeMovement(*actor);
  changed();
  return ok();
}

Result GameEngine::buy(std::uint8_t playerId) {
  auto required = requireActive(playerId, GamePhase::AwaitPurchase);
  if (!required) return required;
  if (!state_.pendingPurchase.active || state_.pendingPurchase.playerId != playerId) {
    return error(ErrorCode::RuleViolation, "no purchase offer");
  }
  auto* actor = player(playerId);
  const auto assetIndex = state_.pendingPurchase.assetIndex;
  const auto& definition = state_.board->assets[assetIndex];
  auto& asset = state_.assets[assetIndex];
  if (asset.ownerId != kNoPlayer) return error(ErrorCode::RuleViolation, "asset already owned");
  if (actor->cash < definition.economy.price) return error(ErrorCode::NotEnoughCash, "not enough cash");
  actor->cash -= definition.economy.price;
  asset.ownerId = actor->id;
  state_.pendingPurchase = {};
  emit(EventKind::AssetPurchased, actor->id, 0, assetIndex, definition.economy.price);
  finishLanding(*actor);
  changed();
  return ok();
}

Result GameEngine::declinePurchase(std::uint8_t playerId) {
  auto required = requireActive(playerId, GamePhase::AwaitPurchase);
  if (!required) return required;
  if (!state_.pendingPurchase.active || state_.pendingPurchase.playerId != playerId) {
    return error(ErrorCode::RuleViolation, "no purchase offer");
  }
  const auto assetIndex = state_.pendingPurchase.assetIndex;
  state_.pendingPurchase = {};
  beginAuction(playerId, assetIndex);
  changed();
  return ok();
}

Result GameEngine::endTurn(std::uint8_t playerId) {
  auto required = requireActive(playerId, GamePhase::TurnEnd);
  if (!required) return required;
  emit(EventKind::TurnEnded, playerId);
  advanceTurn();
  changed();
  return ok();
}

Result GameEngine::payHoldFee(std::uint8_t playerId) {
  auto required = requireActive(playerId, GamePhase::AwaitRoll);
  if (!required) return required;
  auto* actor = player(playerId);
  if (!actor->inHold) return error(ErrorCode::RuleViolation, "player is not held");
  if (actor->cash < state_.board->holdReleaseFee) return error(ErrorCode::NotEnoughCash, "not enough cash");
  actor->cash -= state_.board->holdReleaseFee;
  actor->inHold = false;
  actor->failedHoldRolls = 0;
  emit(EventKind::FeePaid, actor->id, 0, kNoAsset, state_.board->holdReleaseFee);
  emit(EventKind::ReleasedFromHold, actor->id, 0, kNoAsset, state_.board->holdReleaseFee);
  changed();
  return ok();
}

Result GameEngine::mortgage(std::uint8_t playerId, std::uint8_t assetIndex) {
  if (assetIndex >= state_.board->assetCount) return error(ErrorCode::InvalidArgument, "invalid asset");
  auto* actor = player(playerId);
  auto& asset = state_.assets[assetIndex];
  const auto& definition = state_.board->assets[assetIndex];
  if (actor == nullptr || asset.ownerId != playerId) return error(ErrorCode::NotOwner, "not owner");
  if (asset.mortgaged || asset.buildingLevel != 0) return error(ErrorCode::RuleViolation, "asset cannot be mortgaged");
  if (definition.kind == TileKind::Property) {
    for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
      if (state_.board->assets[i].group == definition.group && state_.assets[i].buildingLevel != 0) {
        return error(ErrorCode::RuleViolation, "group has buildings");
      }
    }
  }
  asset.mortgaged = true;
  actor->cash += definition.economy.mortgageValue;
  emit(EventKind::AssetMortgaged, playerId, 0, assetIndex, definition.economy.mortgageValue);
  changed();
  return ok();
}

Result GameEngine::unmortgage(std::uint8_t playerId, std::uint8_t assetIndex) {
  if (assetIndex >= state_.board->assetCount) return error(ErrorCode::InvalidArgument, "invalid asset");
  auto* actor = player(playerId);
  auto& asset = state_.assets[assetIndex];
  const auto& definition = state_.board->assets[assetIndex];
  if (actor == nullptr || asset.ownerId != playerId) return error(ErrorCode::NotOwner, "not owner");
  if (!asset.mortgaged) return error(ErrorCode::RuleViolation, "asset is not mortgaged");
  const auto cost = definition.economy.mortgageValue + (definition.economy.mortgageValue + 9) / 10;
  if (actor->cash < cost) return error(ErrorCode::NotEnoughCash, "not enough cash");
  actor->cash -= cost;
  asset.mortgaged = false;
  emit(EventKind::AssetUnmortgaged, playerId, 0, assetIndex, cost);
  changed();
  return ok();
}

Result GameEngine::build(std::uint8_t playerId, std::uint8_t assetIndex) {
  if (assetIndex >= state_.board->assetCount) return error(ErrorCode::InvalidArgument, "invalid asset");
  auto* actor = player(playerId);
  auto& asset = state_.assets[assetIndex];
  const auto& definition = state_.board->assets[assetIndex];
  if (actor == nullptr || definition.kind != TileKind::Property || asset.ownerId != playerId) {
    return error(ErrorCode::NotOwner, "not a buildable owned property");
  }
  if (asset.mortgaged || asset.buildingLevel >= 5 || !ownsCompleteGroup(playerId, definition.group)) {
    return error(ErrorCode::RuleViolation, "group is not buildable");
  }
  std::uint8_t minimum = 5;
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    if (state_.board->assets[i].group == definition.group) {
      if (state_.assets[i].mortgaged) return error(ErrorCode::RuleViolation, "group has mortgage");
      minimum = std::min(minimum, state_.assets[i].buildingLevel);
    }
  }
  if (asset.buildingLevel > minimum || actor->cash < definition.economy.buildingCost) {
    return error(actor->cash < definition.economy.buildingCost ? ErrorCode::NotEnoughCash : ErrorCode::RuleViolation,
                 actor->cash < definition.economy.buildingCost ? "not enough cash" : "build evenly");
  }
  actor->cash -= definition.economy.buildingCost;
  asset.buildingLevel++;
  emit(EventKind::BuildingChanged, playerId, 0, assetIndex, -definition.economy.buildingCost, asset.buildingLevel);
  changed();
  return ok();
}

Result GameEngine::sellBuilding(std::uint8_t playerId, std::uint8_t assetIndex) {
  if (assetIndex >= state_.board->assetCount) return error(ErrorCode::InvalidArgument, "invalid asset");
  auto* actor = player(playerId);
  auto& asset = state_.assets[assetIndex];
  const auto& definition = state_.board->assets[assetIndex];
  if (actor == nullptr || asset.ownerId != playerId || asset.buildingLevel == 0) {
    return error(ErrorCode::RuleViolation, "no building to sell");
  }
  std::uint8_t maximum = 0;
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    if (state_.board->assets[i].group == definition.group) {
      maximum = std::max(maximum, state_.assets[i].buildingLevel);
    }
  }
  if (asset.buildingLevel < maximum) return error(ErrorCode::RuleViolation, "sell evenly");
  asset.buildingLevel--;
  const auto proceeds = definition.economy.buildingCost / 2;
  actor->cash += proceeds;
  emit(EventKind::BuildingChanged, playerId, 0, assetIndex, proceeds, asset.buildingLevel);
  changed();
  return ok();
}

Result GameEngine::continueCard(std::uint8_t playerId, std::uint16_t cardInstanceId) {
  auto required = requireActive(playerId, GamePhase::AwaitCard);
  if (!required) return required;
  if (!state_.pendingCard.active || state_.pendingCard.playerId != playerId ||
      state_.pendingCard.stage != PendingCardStage::AwaitContinue) {
    return error(ErrorCode::RuleViolation, "no revealed card awaiting this player");
  }
  if (cardInstanceId == 0 || cardInstanceId != state_.pendingCard.cardInstanceId) {
    return error(ErrorCode::RuleViolation, "card instance mismatch");
  }
  auto* actor = player(playerId);
  if (actor == nullptr || actor->bankrupt) return error(ErrorCode::InvalidPlayer, "invalid player");
  state_.pendingCard.stage = PendingCardStage::AwaitSettlement;
  const auto result = applyPendingCard(*actor);
  changed();
  return result;
}

Result GameEngine::payDebt(std::uint8_t playerId) {
  if (state_.phase != GamePhase::AwaitDebt || !state_.pendingDebt.active ||
      state_.pendingDebt.debtorId != playerId) {
    return error(ErrorCode::InvalidPhase, "no debt awaiting this player");
  }
  auto* debtor = player(playerId);
  if (debtor == nullptr || debtor->bankrupt) return error(ErrorCode::InvalidPlayer, "invalid debtor");
  if (debtor->cash < state_.pendingDebt.amount) {
    return error(ErrorCode::NotEnoughCash, "raise funds before paying debt");
  }
  const auto debt = state_.pendingDebt;
  debtor->cash -= debt.amount;
  if (debt.creditorId != 0) {
    if (auto* creditor = player(debt.creditorId)) creditor->cash += debt.amount;
  }
  if (debt.paymentEvent == EventKind::CardApplied && state_.pendingCard.active &&
      state_.pendingCard.playerId == debt.debtorId) {
    emit(EventKind::DebtPaid, debt.debtorId, debt.creditorId, debt.assetIndex, debt.amount);
    state_.pendingDebt = {};
    completePendingCard(debt.amount, CardEffectOutcome::Applied);
  } else {
    emit(debt.paymentEvent, debt.debtorId, debt.creditorId, debt.assetIndex, debt.amount);
    emit(EventKind::DebtPaid, debt.debtorId, debt.creditorId, debt.assetIndex, debt.amount);
    state_.pendingDebt = {};
    resumeDebtContinuation(debt);
  }
  changed();
  return ok();
}

Result GameEngine::declareBankruptcy(std::uint8_t playerId) {
  if (state_.phase != GamePhase::AwaitDebt || !state_.pendingDebt.active ||
      state_.pendingDebt.debtorId != playerId) {
    return error(ErrorCode::InvalidPhase, "no debt awaiting this player");
  }
  if (maximumLiquidationCash(playerId) >= state_.pendingDebt.amount) {
    return error(ErrorCode::RuleViolation, "player can still raise enough funds");
  }
  const auto creditorId = state_.pendingDebt.creditorId;
  const bool cardDebt = state_.pendingDebt.paymentEvent == EventKind::CardApplied &&
      state_.pendingCard.active && state_.pendingCard.playerId == playerId;
  state_.pendingDebt = {};
  auto* debtor = player(playerId);
  if (debtor == nullptr) return error(ErrorCode::InvalidPlayer, "invalid debtor");
  bankrupt(*debtor, creditorId);
  if (cardDebt) completePendingCard(0, CardEffectOutcome::Bankrupt, false);
  if (state_.phase != GamePhase::GameOver) state_.phase = GamePhase::TurnEnd;
  changed();
  return ok();
}

Result GameEngine::auctionBid(std::uint8_t playerId, std::int32_t amount) {
  if (state_.phase != GamePhase::AwaitAuction || !state_.auction.active ||
      state_.auction.readyMask != state_.auction.requiredReadyMask ||
      state_.auction.currentBidderId != playerId) {
    return error(ErrorCode::InvalidPhase, "not this player's auction decision");
  }
  auto* bidder = player(playerId);
  const auto minimum = state_.auction.currentBid == 0 ? 10 : state_.auction.currentBid + 10;
  if (bidder == nullptr || bidder->bankrupt) return error(ErrorCode::InvalidPlayer, "invalid bidder");
  if (amount < minimum) return error(ErrorCode::InvalidArgument, "bid is below minimum");
  if (bidder->cash < amount) return error(ErrorCode::NotEnoughCash, "bid exceeds available cash");
  state_.auction.currentBid = amount;
  state_.auction.highestBidderId = playerId;
  emit(EventKind::AuctionBid, playerId, 0, state_.auction.assetIndex, amount);
  advanceAuction();
  changed();
  return ok();
}

Result GameEngine::auctionPass(std::uint8_t playerId) {
  if (state_.phase != GamePhase::AwaitAuction || !state_.auction.active ||
      state_.auction.readyMask != state_.auction.requiredReadyMask ||
      state_.auction.currentBidderId != playerId) {
    return error(ErrorCode::InvalidPhase, "not this player's auction decision");
  }
  state_.auction.passedMask |= static_cast<std::uint8_t>(1u << (playerId - 1));
  emit(EventKind::AuctionPassed, playerId, 0, state_.auction.assetIndex, state_.auction.currentBid);
  advanceAuction();
  changed();
  return ok();
}

Result GameEngine::auctionReady(std::uint8_t playerId, std::uint8_t assetIndex,
                                std::uint32_t generation) {
  if (state_.phase != GamePhase::AwaitAuction || !state_.auction.active) {
    return error(ErrorCode::InvalidPhase, "no auction opening");
  }
  if (assetIndex != state_.auction.assetIndex || generation == 0 ||
      generation != state_.auction.generation) {
    return error(ErrorCode::RuleViolation, "auction generation mismatch");
  }
  const auto* participant = player(playerId);
  const auto bit = playerId == 0 || playerId > kMaxPlayers
      ? 0u
      : static_cast<unsigned>(1u << (playerId - 1));
  if (participant == nullptr || participant->bankrupt ||
      (state_.auction.requiredReadyMask & bit) == 0) {
    return error(ErrorCode::InvalidPlayer, "player is not an auction participant");
  }
  if ((state_.auction.readyMask & bit) != 0) return ok();
  state_.auction.readyMask |= static_cast<std::uint8_t>(bit);
  if (state_.auction.readyMask == state_.auction.requiredReadyMask) openAuctionBidding();
  changed();
  return ok();
}

bool GameEngine::tradeForPlayer(std::uint8_t playerId, TradeWorkflow& output) const {
  const auto* workflow = activeTradeForPlayer(playerId);
  if (workflow == nullptr) return false;
  output = *workflow;
  return true;
}

Result GameEngine::createTrade(std::uint8_t actorId, std::uint8_t counterpartyId,
                               const TradeOfferSide& actorGives,
                               const TradeOfferSide& counterpartyGives,
                               std::uint64_t nowEpochMs, TradeWorkflow& output) {
  if (!tradeMutationPhaseAllowed()) return error(ErrorCode::InvalidPhase, "trade is blocked by workflow");
  if (actorId == counterpartyId || actorId == 0 || counterpartyId == 0 || nowEpochMs == 0) {
    return error(ErrorCode::InvalidArgument, "invalid trade participants");
  }
  const auto* actor = player(actorId);
  const auto* counterparty = player(counterpartyId);
  if (actor == nullptr || counterparty == nullptr || actor->bankrupt || counterparty->bankrupt) {
    return error(ErrorCode::InvalidPlayer, "trade participant unavailable");
  }
  if (activeTradeForPlayer(actorId) != nullptr || activeTradeForPlayer(counterpartyId) != nullptr) {
    return error(ErrorCode::RuleViolation, "trade participant already busy");
  }
  TradeWorkflow* slot = nullptr;
  for (auto& candidate : state_.trades) {
    if (!activeTradeStatus(candidate.status)) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr) return error(ErrorCode::RuleViolation, "trade capacity exhausted");
  TradeWorkflow candidate{};
  candidate.tradeId = state_.nextTradeId++;
  if (candidate.tradeId == 0) candidate.tradeId = state_.nextTradeId++;
  if (state_.nextTradeId == 0) state_.nextTradeId = 1;
  candidate.revision = 1;
  candidate.status = TradeWorkflowStatus::Offered;
  candidate.proposerId = actorId;
  candidate.counterpartyId = counterpartyId;
  candidate.lastEditorId = actorId;
  candidate.confirmedMask = playerBit(actorId);
  candidate.proposerGives = actorGives;
  candidate.counterpartyGives = counterpartyGives;
  candidate.deadlineEpochMs = nowEpochMs + kTradeLifetimeMs;
  const auto validation = validateTradeOffer(candidate);
  if (!validation) return validation;
  *slot = candidate;
  output = candidate;
  emit(EventKind::TradeCreated, actorId, counterpartyId, kNoAsset,
       actorGives.cash - counterpartyGives.cash, candidate.tradeId);
  changed();
  return ok();
}

Result GameEngine::updateTrade(std::uint8_t actorId, std::uint32_t tradeId,
                               std::uint16_t expectedRevision,
                               const TradeOfferSide& actorGives,
                               const TradeOfferSide& counterpartyGives,
                               std::uint64_t nowEpochMs, TradeWorkflow& output) {
  if (!tradeMutationPhaseAllowed()) return error(ErrorCode::InvalidPhase, "trade is blocked by workflow");
  auto* workflow = findTrade(tradeId);
  if (workflow == nullptr || !activeTradeStatus(workflow->status)) {
    return error(ErrorCode::InvalidArgument, "active trade not found");
  }
  if (workflow->revision != expectedRevision) return error(ErrorCode::RuleViolation, "trade revision mismatch");
  if (actorId != workflow->proposerId && actorId != workflow->counterpartyId) {
    return error(ErrorCode::InvalidPlayer, "not a trade participant");
  }
  if (workflow->revision == std::numeric_limits<std::uint16_t>::max()) {
    return error(ErrorCode::RuleViolation, "trade revision exhausted");
  }
  TradeWorkflow candidate = *workflow;
  if (actorId == candidate.proposerId) {
    candidate.proposerGives = actorGives;
    candidate.counterpartyGives = counterpartyGives;
  } else {
    candidate.counterpartyGives = actorGives;
    candidate.proposerGives = counterpartyGives;
  }
  candidate.revision++;
  candidate.status = TradeWorkflowStatus::Countered;
  candidate.lastEditorId = actorId;
  candidate.confirmedMask = playerBit(actorId);
  const auto* editor = player(actorId);
  if (editor != nullptr && editor->controller == ControllerKind::Bot) {
    candidate.botCounteredMask |= playerBit(actorId);
  }
  candidate.deadlineEpochMs = nowEpochMs + kTradeLifetimeMs;
  const auto validation = validateTradeOffer(candidate);
  if (!validation) return validation;
  *workflow = candidate;
  output = candidate;
  emit(EventKind::TradeUpdated, actorId,
       actorId == candidate.proposerId ? candidate.counterpartyId : candidate.proposerId,
       kNoAsset, actorGives.cash - counterpartyGives.cash, candidate.tradeId);
  changed();
  return ok();
}

Result GameEngine::confirmTrade(std::uint8_t actorId, std::uint32_t tradeId,
                                std::uint16_t expectedRevision, std::uint64_t nowEpochMs,
                                TradeWorkflow& output) {
  if (!tradeMutationPhaseAllowed()) return error(ErrorCode::InvalidPhase, "trade is blocked by workflow");
  auto* workflow = findTrade(tradeId);
  if (workflow == nullptr || !activeTradeStatus(workflow->status)) {
    return error(ErrorCode::InvalidArgument, "active trade not found");
  }
  if (workflow->revision != expectedRevision) return error(ErrorCode::RuleViolation, "trade revision mismatch");
  if (actorId != workflow->proposerId && actorId != workflow->counterpartyId) {
    return error(ErrorCode::InvalidPlayer, "not a trade participant");
  }
  if (nowEpochMs >= workflow->deadlineEpochMs) {
    workflow->status = TradeWorkflowStatus::Expired;
    emit(EventKind::TradeClosed, workflow->proposerId, workflow->counterpartyId,
         kNoAsset, static_cast<std::int32_t>(workflow->status), workflow->tradeId);
    output = *workflow;
    changed();
    return error(ErrorCode::RuleViolation, "trade expired");
  }
  const auto actorBit = playerBit(actorId);
  if ((workflow->confirmedMask & actorBit) != 0) {
    output = *workflow;
    return ok();
  }
  const auto validation = validateTradeOffer(*workflow);
  if (!validation) {
    workflow->status = TradeWorkflowStatus::Invalidated;
    emit(EventKind::TradeClosed, workflow->proposerId, workflow->counterpartyId,
         kNoAsset, static_cast<std::int32_t>(workflow->status), workflow->tradeId);
    output = *workflow;
    changed();
    return validation;
  }
  workflow->confirmedMask |= actorBit;
  const auto required = static_cast<std::uint8_t>(playerBit(workflow->proposerId) |
                                                   playerBit(workflow->counterpartyId));
  if ((workflow->confirmedMask & required) == required && !settleTrade(*workflow)) {
    workflow->status = TradeWorkflowStatus::Invalidated;
    emit(EventKind::TradeClosed, workflow->proposerId, workflow->counterpartyId,
         kNoAsset, static_cast<std::int32_t>(workflow->status), workflow->tradeId);
    output = *workflow;
    changed();
    return error(ErrorCode::RuleViolation, "trade settlement failed validation");
  }
  output = *workflow;
  changed();
  return ok();
}

Result GameEngine::rejectTrade(std::uint8_t actorId, std::uint32_t tradeId,
                               std::uint16_t expectedRevision, TradeWorkflow& output) {
  auto* workflow = findTrade(tradeId);
  if (workflow == nullptr || !activeTradeStatus(workflow->status)) {
    return error(ErrorCode::InvalidArgument, "active trade not found");
  }
  if (workflow->revision != expectedRevision) return error(ErrorCode::RuleViolation, "trade revision mismatch");
  if (actorId != workflow->proposerId && actorId != workflow->counterpartyId) {
    return error(ErrorCode::InvalidPlayer, "not a trade participant");
  }
  if (actorId == workflow->lastEditorId) {
    return error(ErrorCode::RuleViolation, "offer editor must cancel instead of reject");
  }
  workflow->status = TradeWorkflowStatus::Rejected;
  emit(EventKind::TradeClosed, actorId, workflow->lastEditorId, kNoAsset,
       static_cast<std::int32_t>(workflow->status), workflow->tradeId);
  output = *workflow;
  changed();
  return ok();
}

Result GameEngine::cancelTrade(std::uint8_t actorId, std::uint32_t tradeId,
                               std::uint16_t expectedRevision, TradeWorkflow& output) {
  auto* workflow = findTrade(tradeId);
  if (workflow == nullptr || !activeTradeStatus(workflow->status)) {
    return error(ErrorCode::InvalidArgument, "active trade not found");
  }
  if (workflow->revision != expectedRevision) return error(ErrorCode::RuleViolation, "trade revision mismatch");
  if (actorId != workflow->lastEditorId) {
    return error(ErrorCode::InvalidPlayer, "only the current offer editor may cancel");
  }
  workflow->status = TradeWorkflowStatus::Cancelled;
  const auto other = actorId == workflow->proposerId ? workflow->counterpartyId : workflow->proposerId;
  emit(EventKind::TradeClosed, actorId, other, kNoAsset,
       static_cast<std::int32_t>(workflow->status), workflow->tradeId);
  output = *workflow;
  changed();
  return ok();
}

std::size_t GameEngine::expireTrades(std::uint64_t nowEpochMs) {
  std::size_t expired = 0;
  for (auto& workflow : state_.trades) {
    if (!activeTradeStatus(workflow.status) || nowEpochMs < workflow.deadlineEpochMs) continue;
    workflow.status = TradeWorkflowStatus::Expired;
    emit(EventKind::TradeClosed, workflow.proposerId, workflow.counterpartyId,
         kNoAsset, static_cast<std::int32_t>(workflow.status), workflow.tradeId);
    ++expired;
  }
  if (expired != 0) changed();
  return expired;
}

bool GameEngine::tradeMutationPhaseAllowed() const {
  return state_.phase == GamePhase::AwaitRoll || state_.phase == GamePhase::TurnEnd;
}

Result GameEngine::validateTradeOffer(const TradeWorkflow& trade) const {
  if (state_.board == nullptr || !activeTradeStatus(trade.status) ||
      trade.proposerId == trade.counterpartyId || trade.proposerGives.cash < 0 ||
      trade.counterpartyGives.cash < 0 ||
      trade.proposerGives.assetCount > trade.proposerGives.assets.size() ||
      trade.counterpartyGives.assetCount > trade.counterpartyGives.assets.size() ||
      static_cast<std::size_t>(trade.proposerGives.assetCount) +
          trade.counterpartyGives.assetCount > kMaxTradeAssetsTotal) {
    return error(ErrorCode::InvalidArgument, "invalid trade structure");
  }
  const auto* proposer = player(trade.proposerId);
  const auto* counterparty = player(trade.counterpartyId);
  if (proposer == nullptr || counterparty == nullptr || proposer->bankrupt ||
      counterparty->bankrupt) {
    return error(ErrorCode::InvalidPlayer, "trade participant unavailable");
  }
  if (proposer->cash < trade.proposerGives.cash ||
      counterparty->cash < trade.counterpartyGives.cash) {
    return error(ErrorCode::NotEnoughCash, "trade cash exceeds balance");
  }
  if (trade.proposerGives.cash == 0 && trade.counterpartyGives.cash == 0 &&
      trade.proposerGives.assetCount == 0 && trade.counterpartyGives.assetCount == 0) {
    return error(ErrorCode::InvalidArgument, "empty trade offer");
  }
  std::array<bool, kMaxAssets> seen{};
  const auto validateSide = [&](const TradeOfferSide& side, std::uint8_t ownerId) -> Result {
    for (std::uint8_t index = 0; index < side.assetCount; ++index) {
      const auto assetIndex = side.assets[index];
      if (assetIndex >= state_.board->assetCount || seen[assetIndex]) {
        return error(ErrorCode::InvalidArgument, "invalid or duplicate trade asset");
      }
      seen[assetIndex] = true;
      const auto& asset = state_.assets[assetIndex];
      const auto& definition = state_.board->assets[assetIndex];
      if (asset.ownerId != ownerId || asset.mortgaged || asset.buildingLevel != 0) {
        return error(ErrorCode::NotOwner, "trade asset unavailable");
      }
      if (definition.kind == TileKind::Property) {
        for (std::uint8_t other = 0; other < state_.board->assetCount; ++other) {
          if (state_.board->assets[other].group == definition.group &&
              state_.assets[other].buildingLevel != 0) {
            return error(ErrorCode::RuleViolation, "sell group buildings before trade");
          }
        }
      }
    }
    return ok();
  };
  auto validation = validateSide(trade.proposerGives, trade.proposerId);
  if (!validation) return validation;
  validation = validateSide(trade.counterpartyGives, trade.counterpartyId);
  if (!validation) return validation;
  const auto proposerFinal = static_cast<std::int64_t>(proposer->cash) - trade.proposerGives.cash +
      trade.counterpartyGives.cash;
  const auto counterpartyFinal = static_cast<std::int64_t>(counterparty->cash) -
      trade.counterpartyGives.cash + trade.proposerGives.cash;
  if (proposerFinal < 0 || counterpartyFinal < 0 ||
      proposerFinal > std::numeric_limits<std::int32_t>::max() ||
      counterpartyFinal > std::numeric_limits<std::int32_t>::max()) {
    return error(ErrorCode::RuleViolation, "trade cash result overflow");
  }
  return ok();
}

bool GameEngine::settleTrade(TradeWorkflow& trade) {
  if (!validateTradeOffer(trade)) return false;
  auto* proposer = player(trade.proposerId);
  auto* counterparty = player(trade.counterpartyId);
  const auto proposerCash = static_cast<std::int64_t>(proposer->cash) - trade.proposerGives.cash +
      trade.counterpartyGives.cash;
  const auto counterpartyCash = static_cast<std::int64_t>(counterparty->cash) -
      trade.counterpartyGives.cash + trade.proposerGives.cash;
  proposer->cash = static_cast<std::int32_t>(proposerCash);
  counterparty->cash = static_cast<std::int32_t>(counterpartyCash);
  for (std::uint8_t i = 0; i < trade.proposerGives.assetCount; ++i) {
    state_.assets[trade.proposerGives.assets[i]].ownerId = trade.counterpartyId;
  }
  for (std::uint8_t i = 0; i < trade.counterpartyGives.assetCount; ++i) {
    state_.assets[trade.counterpartyGives.assets[i]].ownerId = trade.proposerId;
  }
  trade.status = TradeWorkflowStatus::Settled;
  const auto eventAsset = trade.proposerGives.assetCount != 0
      ? trade.proposerGives.assets[0]
      : (trade.counterpartyGives.assetCount != 0 ? trade.counterpartyGives.assets[0] : kNoAsset);
  emit(EventKind::TradeSettled, trade.proposerId, trade.counterpartyId, eventAsset,
       trade.proposerGives.cash - trade.counterpartyGives.cash, trade.tradeId);
  return true;
}

std::int64_t GameEngine::tradeSideValue(const TradeOfferSide& side) const {
  std::int64_t value = side.cash;
  if (state_.board == nullptr) return value;
  for (std::uint8_t index = 0; index < side.assetCount; ++index) {
    const auto assetIndex = side.assets[index];
    if (assetIndex < state_.board->assetCount) {
      value += state_.board->assets[assetIndex].economy.price;
    }
  }
  return value;
}

TradeWorkflow* GameEngine::findTrade(std::uint32_t tradeId) {
  if (tradeId == 0) return nullptr;
  for (auto& workflow : state_.trades) if (workflow.tradeId == tradeId) return &workflow;
  return nullptr;
}

const TradeWorkflow* GameEngine::findTrade(std::uint32_t tradeId) const {
  if (tradeId == 0) return nullptr;
  for (const auto& workflow : state_.trades) if (workflow.tradeId == tradeId) return &workflow;
  return nullptr;
}

TradeWorkflow* GameEngine::activeTradeForPlayer(std::uint8_t playerId) {
  for (auto& workflow : state_.trades) {
    if (activeTradeStatus(workflow.status) &&
        (workflow.proposerId == playerId || workflow.counterpartyId == playerId)) return &workflow;
  }
  return nullptr;
}

const TradeWorkflow* GameEngine::activeTradeForPlayer(std::uint8_t playerId) const {
  for (const auto& workflow : state_.trades) {
    if (activeTradeStatus(workflow.status) &&
        (workflow.proposerId == playerId || workflow.counterpartyId == playerId)) return &workflow;
  }
  return nullptr;
}

std::size_t GameEngine::runTradeBots(std::uint64_t nowEpochMs, std::size_t maxActions) {
  if (!tradeMutationPhaseAllowed() || nowEpochMs == 0 || maxActions == 0) return 0;
  std::size_t actions = 0;
  for (std::size_t slot = 0; slot < state_.trades.size() && actions < maxActions; ++slot) {
    const auto snapshot = state_.trades[slot];
    if (!activeTradeStatus(snapshot.status)) continue;

    std::uint8_t botId = 0;
    const auto consider = [&](std::uint8_t playerId) {
      const auto* candidate = player(playerId);
      return candidate != nullptr && !candidate->bankrupt &&
          candidate->controller == ControllerKind::Bot &&
          snapshot.lastEditorId != playerId &&
          (snapshot.confirmedMask & playerBit(playerId)) == 0;
    };
    if (consider(snapshot.proposerId)) botId = snapshot.proposerId;
    else if (consider(snapshot.counterpartyId)) botId = snapshot.counterpartyId;
    if (botId == 0) continue;

    const bool botIsProposer = botId == snapshot.proposerId;
    const auto selfSide = botIsProposer ? snapshot.proposerGives : snapshot.counterpartyGives;
    const auto otherSide = botIsProposer ? snapshot.counterpartyGives : snapshot.proposerGives;
    const auto netValue = tradeSideValue(otherSide) - tradeSideValue(selfSide);
    TradeWorkflow output{};
    if (netValue >= 0) {
      const auto beforeStatus = snapshot.status;
      const auto result = confirmTrade(botId, snapshot.tradeId, snapshot.revision, nowEpochMs, output);
      if (result || output.status != beforeStatus) ++actions;
      continue;
    }

    const auto botBit = playerBit(botId);
    const auto otherId = botIsProposer ? snapshot.counterpartyId : snapshot.proposerId;
    const auto* otherPlayer = player(otherId);
    const auto requestedCash = static_cast<std::int64_t>(otherSide.cash) - netValue;
    if ((snapshot.botCounteredMask & botBit) == 0 && otherPlayer != nullptr &&
        requestedCash <= otherPlayer->cash &&
        requestedCash <= std::numeric_limits<std::int32_t>::max()) {
      auto counterOther = otherSide;
      counterOther.cash = static_cast<std::int32_t>(requestedCash);
      if (updateTrade(botId, snapshot.tradeId, snapshot.revision, selfSide, counterOther,
                      nowEpochMs, output)) {
        ++actions;
        continue;
      }
    }
    if (rejectTrade(botId, snapshot.tradeId, snapshot.revision, output)) ++actions;
  }
  return actions;
}

std::size_t GameEngine::runBots(std::size_t maxActions) {
  std::size_t actions = 0;
  while (actions < maxActions && state_.phase != GamePhase::Lobby && state_.phase != GamePhase::GameOver) {
    auto* actor = player(decisionPlayerId());
    if (actor == nullptr || actor->controller != ControllerKind::Bot) break;
    Result result = ok();
    switch (state_.phase) {
      case GamePhase::AwaitRoll:
        result = roll(actor->id);
        break;
      case GamePhase::AwaitMoveConfirm:
        result = confirmPosition(actor->id, state_.pendingMove.target);
        break;
      case GamePhase::AwaitPurchase: {
        const auto& asset = state_.board->assets[state_.pendingPurchase.assetIndex];
        const bool affordable = actor->cash >= asset.economy.price;
        const bool reserveSafe = actor->cash - asset.economy.price >= state_.board->startAward / 2;
        result = affordable && reserveSafe ? buy(actor->id) : declinePurchase(actor->id);
        break;
      }
      case GamePhase::AwaitCard:
        result = continueCard(actor->id, state_.pendingCard.cardInstanceId);
        break;
      case GamePhase::AwaitAuction: {
        const auto& asset = state_.board->assets[state_.auction.assetIndex];
        const auto minimum = state_.auction.currentBid == 0 ? 10 : state_.auction.currentBid + 10;
        const auto appetite = static_cast<std::int32_t>(65 + nextRandom() % 36u);
        const auto limit = std::min(actor->cash, asset.economy.price * appetite / 100);
        result = minimum <= limit ? auctionBid(actor->id, minimum) : auctionPass(actor->id);
        break;
      }
      case GamePhase::AwaitDebt: {
        if (actor->cash >= state_.pendingDebt.amount) {
          result = payDebt(actor->id);
          break;
        }
        bool raised = false;
        for (std::uint8_t i = 0; i < state_.board->assetCount && !raised; ++i) {
          if (state_.assets[i].ownerId == actor->id && state_.assets[i].buildingLevel != 0) {
            raised = static_cast<bool>(sellBuilding(actor->id, i));
          }
        }
        for (std::uint8_t i = 0; i < state_.board->assetCount && !raised; ++i) {
          if (state_.assets[i].ownerId == actor->id && !state_.assets[i].mortgaged &&
              state_.assets[i].buildingLevel == 0) {
            raised = static_cast<bool>(mortgage(actor->id, i));
          }
        }
        if (raised) {
          actions++;
          continue;
        }
        result = declareBankruptcy(actor->id);
        break;
      }
      case GamePhase::TurnEnd:
        result = endTurn(actor->id);
        break;
      default:
        return actions;
    }
    if (!result) break;
    actions++;
  }
  return actions;
}

std::uint32_t GameEngine::actionsFor(std::uint8_t playerId) const {
  const auto* actor = player(playerId);
  if (actor == nullptr || actor->bankrupt || state_.phase == GamePhase::GameOver) return ActionNone;
  std::uint32_t result = ActionNone;
  if (state_.phase == GamePhase::AwaitDebt) {
    if (state_.pendingDebt.active && state_.pendingDebt.debtorId == playerId) {
      if (actor->cash >= state_.pendingDebt.amount) result |= ActionPayDebt;
      if (maximumLiquidationCash(playerId) < state_.pendingDebt.amount) result |= ActionDeclareBankruptcy;
      result |= ActionMortgage | ActionSellBuilding;
    }
    return result;
  }
  if (state_.phase == GamePhase::AwaitCard) {
    if (state_.pendingCard.active && state_.pendingCard.playerId == playerId &&
        state_.pendingCard.stage == PendingCardStage::AwaitContinue) {
      result |= ActionCardContinue;
    }
    return result;
  }
  if (state_.phase == GamePhase::AwaitAuction) {
    const auto bit = static_cast<std::uint8_t>(1u << (playerId - 1));
    if (state_.auction.active && state_.auction.readyMask != state_.auction.requiredReadyMask) {
      if ((state_.auction.requiredReadyMask & bit) != 0 && (state_.auction.readyMask & bit) == 0) {
        result |= ActionAuctionReady;
      }
    } else if (state_.auction.active && state_.auction.currentBidderId == playerId) {
      result |= ActionAuctionPass;
      const auto minimum = state_.auction.currentBid == 0 ? 10 : state_.auction.currentBid + 10;
      if (actor->cash >= minimum) result |= ActionAuctionBid;
    }
    return result;
  }
  if (state_.activePlayerId == playerId) {
    if (state_.phase == GamePhase::AwaitRoll) {
      result |= ActionRoll;
      if (actor->inHold && actor->cash >= state_.board->holdReleaseFee) result |= ActionPayHoldFee;
    } else if (state_.phase == GamePhase::AwaitMoveConfirm) {
      result |= ActionConfirmPosition;
    } else if (state_.phase == GamePhase::AwaitPurchase) {
      result |= ActionDecline;
      const auto& asset = state_.board->assets[state_.pendingPurchase.assetIndex];
      if (actor->cash >= asset.economy.price) result |= ActionBuy;
    } else if (state_.phase == GamePhase::TurnEnd) {
      result |= ActionEndTurn;
    }
  }
  result |= ActionMortgage | ActionUnmortgage | ActionBuild | ActionSellBuilding;
  if (tradeMutationPhaseAllowed()) result |= ActionTrade;
  return result;
}

std::uint8_t GameEngine::decisionPlayerId() const {
  if (state_.phase == GamePhase::AwaitCard && state_.pendingCard.active) {
    return state_.pendingCard.playerId;
  }
  if (state_.phase == GamePhase::AwaitDebt && state_.pendingDebt.active) return state_.pendingDebt.debtorId;
  if (state_.phase == GamePhase::AwaitAuction && state_.auction.active) {
    return state_.auction.readyMask == state_.auction.requiredReadyMask
        ? state_.auction.currentBidderId
        : 0;
  }
  return state_.activePlayerId;
}

PlayerState* GameEngine::player(std::uint8_t playerId) {
  if (playerId == 0 || playerId > state_.playerCount) return nullptr;
  return &state_.players[playerId - 1];
}

const PlayerState* GameEngine::player(std::uint8_t playerId) const {
  if (playerId == 0 || playerId > state_.playerCount) return nullptr;
  return &state_.players[playerId - 1];
}

Result GameEngine::requireActive(std::uint8_t playerId, GamePhase phase) const {
  const auto* actor = player(playerId);
  if (state_.phase == GamePhase::GameOver) return error(ErrorCode::GameOver, "game over");
  if (actor == nullptr || actor->bankrupt || state_.activePlayerId != playerId) {
    return error(ErrorCode::InvalidPlayer, "not active player");
  }
  if (state_.phase != phase) return error(ErrorCode::InvalidPhase, "action not allowed in phase");
  return ok();
}

void GameEngine::queueMovement(PlayerState& actor, std::uint8_t dieA, std::uint8_t dieB) {
  const auto total = static_cast<std::uint8_t>(dieA + dieB);
  const auto origin = actor.position;
  const auto rawTarget = static_cast<std::uint16_t>(origin + total);
  const bool passed = rawTarget >= state_.board->tileCount;
  const auto target = static_cast<std::uint8_t>(rawTarget % state_.board->tileCount);
  state_.pendingMove = {true, actor.id, origin, target, dieA, dieB, passed};
  state_.phase = GamePhase::AwaitMoveConfirm;
  emit(EventKind::MoveRequested, actor.id, 0, kNoAsset, total,
       static_cast<std::uint32_t>(origin) | (static_cast<std::uint32_t>(target) << 8));
  if (actor.controller == ControllerKind::Bot) completeMovement(actor);
}

void GameEngine::completeMovement(PlayerState& actor) {
  const auto move = state_.pendingMove;
  state_.pendingMove = {};
  if (move.passedStart) {
    actor.cash += state_.board->startAward;
    emit(EventKind::PassedStart, actor.id, 0, kNoAsset, state_.board->startAward);
  }
  actor.position = move.target;
  emit(EventKind::MoveCompleted, actor.id, 0, kNoAsset, move.target,
       static_cast<std::uint32_t>(move.origin) | (static_cast<std::uint32_t>(move.target) << 8));
  resolveLanding(actor, static_cast<std::uint8_t>(move.dieA + move.dieB));
}

Result GameEngine::resolveLanding(PlayerState& actor, std::uint8_t diceTotal) {
  const auto& tile = state_.board->tiles[actor.position];
  switch (tile.kind) {
    case TileKind::Property:
    case TileKind::Transit:
    case TileKind::Utility:
      return resolveAsset(actor, tile.assetIndex, diceTotal);
    case TileKind::Fee:
      if (transferPayment(actor, 0, tile.amount, EventKind::FeePaid)) finishLanding(actor);
      break;
    case TileKind::CityEvent:
      resolveCard(actor, true);
      break;
    case TileKind::CivicFund:
      resolveCard(actor, false);
      break;
    case TileKind::GoToHold:
      sendToHold(actor);
      state_.phase = GamePhase::TurnEnd;
      break;
    default:
      finishLanding(actor);
      break;
  }
  return ok();
}

Result GameEngine::resolveAsset(PlayerState& actor, std::uint8_t assetIndex, std::uint8_t diceTotal) {
  auto& asset = state_.assets[assetIndex];
  const auto& definition = state_.board->assets[assetIndex];
  if (asset.ownerId == kNoPlayer) {
    state_.pendingPurchase = {true, actor.id, assetIndex};
    state_.phase = GamePhase::AwaitPurchase;
    emit(EventKind::PurchaseOffered, actor.id, 0, assetIndex, definition.economy.price);
    return ok();
  }
  if (asset.ownerId == actor.id || asset.mortgaged) {
    finishLanding(actor);
    return ok();
  }
  std::int32_t rent = 0;
  if (definition.kind == TileKind::Property) {
    rent = definition.economy.rent[asset.buildingLevel];
    if (asset.buildingLevel == 0 && ownsCompleteGroup(asset.ownerId, definition.group)) rent *= 2;
  } else if (definition.kind == TileKind::Transit) {
    const auto count = ownedKindCount(asset.ownerId, TileKind::Transit);
    rent = definition.economy.rent[std::min<std::uint8_t>(3, static_cast<std::uint8_t>(count - 1))];
  } else {
    const auto count = ownedKindCount(asset.ownerId, TileKind::Utility);
    rent = diceTotal * definition.economy.rent[count >= 2 ? 1 : 0];
  }
  if (transferPayment(actor, asset.ownerId, rent, EventKind::RentPaid, assetIndex)) finishLanding(actor);
  return ok();
}

Result GameEngine::resolveCard(PlayerState& actor, bool cityEvent) {
  const auto card = static_cast<std::uint8_t>(nextRandom() % 8u);
  const auto deckId = static_cast<std::uint8_t>(cityEvent ? 1 : 2);
  auto& pending = state_.pendingCard;
  pending = PendingCard{};
  pending.active = true;
  pending.stage = PendingCardStage::AwaitContinue;
  pending.playerId = actor.id;
  pending.deckId = deckId;
  pending.cardIndex = card;
  pending.cardCatalogId = cardCatalogId(deckId, card);
  pending.effectId = pending.cardCatalogId;
  pending.cardInstanceId = static_cast<std::uint16_t>(((state_.nextEventSequence - 1u) % 65534u) + 1u);
  pending.drawEventSequence = state_.nextEventSequence;
  pending.targetPosition = actor.position;
  switch (card) {
    case 0:
      pending.displayAmount = cityEvent ? 100 : 50;
      break;
    case 1:
      pending.displayAmount = -(cityEvent ? 50 : 25);
      break;
    case 2:
      pending.displayAmount = state_.board->startAward;
      pending.targetPosition = 0;
      break;
    case 3:
      pending.targetPosition = holdPosition();
      break;
    case 4:
      pending.displayAmount = -3;
      pending.targetPosition = static_cast<std::uint8_t>(
          (actor.position + state_.board->tileCount - 3) % state_.board->tileCount);
      break;
    case 5:
      pending.displayAmount = -(cityEvent ? 75 : 50);
      break;
    case 6:
      pending.displayAmount = cityEvent ? 150 : 100;
      break;
    default:
      pending.displayAmount = cityEvent ? -100 : 75;
      break;
  }
  state_.phase = GamePhase::AwaitCard;
  emit(EventKind::CardDrawn, actor.id, 0, kNoAsset, pending.displayAmount,
       packCardEventDetail(pending.cardInstanceId, pending.deckId, pending.cardIndex,
                           CardEffectOutcome::Applied, pending.targetPosition));
  if (actor.controller == ControllerKind::Bot) {
    return continueCard(actor.id, pending.cardInstanceId);
  }
  return ok();
}

Result GameEngine::applyPendingCard(PlayerState& actor) {
  if (!state_.pendingCard.active || state_.pendingCard.playerId != actor.id ||
      state_.pendingCard.stage != PendingCardStage::AwaitSettlement) {
    return error(ErrorCode::RuleViolation, "card is not ready to settle");
  }
  const auto card = state_.pendingCard.cardIndex;
  const bool cityEvent = state_.pendingCard.deckId == 1;
  switch (card) {
    case 0: {
      const auto amount = cityEvent ? 100 : 50;
      actor.cash += amount;
      completePendingCard(amount, CardEffectOutcome::Applied);
      return ok();
    }
    case 1: {
      const auto amount = cityEvent ? 50 : 25;
      std::int32_t settledAmount = 0;
      if (transferPayment(actor, 0, amount, EventKind::CardApplied,
                          kNoAsset, DebtContinuation::None, 0, 0, &settledAmount)) {
        completePendingCard(settledAmount, actor.bankrupt ? CardEffectOutcome::Bankrupt
                                                          : CardEffectOutcome::Applied,
                            !actor.bankrupt);
      }
      return ok();
    }
    case 2: {
      const auto award = actor.position == 0 ? 0 : state_.board->startAward;
      actor.cash += award;
      actor.position = 0;
      completePendingCard(award, CardEffectOutcome::Applied);
      return ok();
    }
    case 3: {
      sendToHold(actor);
      completePendingCard(0, CardEffectOutcome::Applied, false);
      state_.phase = GamePhase::TurnEnd;
      return ok();
    }
    case 4: {
      actor.position = state_.pendingCard.targetPosition;
      const auto settled = state_.pendingCard;
      emitCardApplied(settled, -3, CardEffectOutcome::Applied);
      state_.pendingCard = {};
      return resolveLanding(actor, 7);
    }
    case 5: {
      const auto amount = cityEvent ? 75 : 50;
      std::int32_t settledAmount = 0;
      if (transferPayment(actor, 0, amount, EventKind::CardApplied,
                          kNoAsset, DebtContinuation::None, 0, 0, &settledAmount)) {
        completePendingCard(settledAmount, actor.bankrupt ? CardEffectOutcome::Bankrupt
                                                          : CardEffectOutcome::Applied,
                            !actor.bankrupt);
      }
      return ok();
    }
    case 6: {
      const auto amount = cityEvent ? 150 : 100;
      actor.cash += amount;
      completePendingCard(amount, CardEffectOutcome::Applied);
      return ok();
    }
    default: {
      if (cityEvent) {
        std::int32_t settledAmount = 0;
        if (transferPayment(actor, 0, 100, EventKind::CardApplied,
                            kNoAsset, DebtContinuation::None, 0, 0, &settledAmount)) {
          completePendingCard(settledAmount, actor.bankrupt ? CardEffectOutcome::Bankrupt
                                                             : CardEffectOutcome::Applied,
                              !actor.bankrupt);
        }
      } else {
        actor.cash += 75;
        completePendingCard(75, CardEffectOutcome::Applied);
      }
      return ok();
    }
  }
}

void GameEngine::emitCardApplied(const PendingCard& card, std::int32_t amount,
                                 CardEffectOutcome outcome) {
  emit(EventKind::CardApplied, card.playerId, card.targetPlayerId, kNoAsset, amount,
       packCardEventDetail(card.cardInstanceId, card.deckId, card.cardIndex, outcome,
                           card.targetPosition));
}

void GameEngine::completePendingCard(std::int32_t amount, CardEffectOutcome outcome, bool finish) {
  if (!state_.pendingCard.active) return;
  const auto card = state_.pendingCard;
  emitCardApplied(card, amount, outcome);
  state_.pendingCard = {};
  if (finish) {
    if (auto* actor = player(card.playerId)) finishLanding(*actor);
  }
}

void GameEngine::finishLanding(PlayerState& actor) {
  if (state_.phase == GamePhase::GameOver) return;
  state_.pendingPurchase = {};
  if (actor.bankrupt || actor.inHold || actor.doublesStreak == 0) state_.phase = GamePhase::TurnEnd;
  else state_.phase = GamePhase::AwaitRoll;
}

void GameEngine::sendToHold(PlayerState& actor) {
  actor.position = holdPosition();
  actor.inHold = true;
  actor.failedHoldRolls = 0;
  actor.doublesStreak = 0;
  emit(EventKind::SentToHold, actor.id);
}

void GameEngine::beginAuction(std::uint8_t landingPlayerId, std::uint8_t assetIndex) {
  state_.auction = {};
  state_.auction.active = true;
  state_.auction.assetIndex = assetIndex;
  state_.auction.landingPlayerId = landingPlayerId;
  state_.auction.generation = state_.nextAuctionGeneration++;
  if (state_.auction.generation == 0) state_.auction.generation = state_.nextAuctionGeneration++;
  if (state_.nextAuctionGeneration == 0) state_.nextAuctionGeneration = 1;
  for (std::uint8_t playerId = 1; playerId <= state_.playerCount; ++playerId) {
    const auto* participant = player(playerId);
    if (participant == nullptr || participant->bankrupt) continue;
    const auto bit = static_cast<std::uint8_t>(1u << (playerId - 1));
    state_.auction.requiredReadyMask |= bit;
    // Bot and Web controllers do not render a physical introduction page, so
    // they acknowledge the opening barrier immediately. RealConsole remains
    // pending until its screen sends AuctionReady for this generation.
    if (participant->controller == ControllerKind::Bot || participant->controller == ControllerKind::Web) {
      state_.auction.readyMask |= bit;
    }
  }
  state_.phase = GamePhase::AwaitAuction;
  emit(EventKind::AuctionStarted, landingPlayerId, 0, assetIndex, 10, state_.auction.generation);
  if (state_.auction.readyMask == state_.auction.requiredReadyMask) openAuctionBidding();
}

void GameEngine::openAuctionBidding() {
  if (!state_.auction.active || state_.auction.readyMask != state_.auction.requiredReadyMask ||
      state_.auction.currentBidderId != 0) return;
  for (std::size_t offset = 0; offset < state_.playerCount; ++offset) {
    const auto candidate = static_cast<std::uint8_t>(
        ((state_.auction.landingPlayerId - 1 + offset) % state_.playerCount) + 1);
    const auto* bidder = player(candidate);
    if (bidder != nullptr && !bidder->bankrupt) {
      state_.auction.currentBidderId = candidate;
      return;
    }
  }
}

void GameEngine::advanceAuction() {
  if (!state_.auction.active) return;
  const auto isEligible = [this](std::uint8_t id) {
    const auto* bidder = player(id);
    return bidder != nullptr && !bidder->bankrupt &&
           (state_.auction.passedMask & static_cast<std::uint8_t>(1u << (id - 1))) == 0;
  };

  bool hasRequiredBidder = false;
  for (std::uint8_t id = 1; id <= state_.playerCount; ++id) {
    if (!isEligible(id)) continue;
    if (state_.auction.highestBidderId == 0 || id != state_.auction.highestBidderId) {
      hasRequiredBidder = true;
      break;
    }
  }
  if (!hasRequiredBidder) {
    settleAuction();
    return;
  }

  const auto current = state_.auction.currentBidderId;
  for (std::size_t offset = 1; offset <= state_.playerCount; ++offset) {
    const auto candidate = static_cast<std::uint8_t>(((current - 1 + offset) % state_.playerCount) + 1);
    if (isEligible(candidate) &&
        (state_.auction.highestBidderId == 0 || candidate != state_.auction.highestBidderId)) {
      state_.auction.currentBidderId = candidate;
      return;
    }
  }
  settleAuction();
}

void GameEngine::settleAuction() {
  if (!state_.auction.active) return;
  const auto auction = state_.auction;
  if (auction.highestBidderId != 0) {
    auto* winner = player(auction.highestBidderId);
    if (winner != nullptr && !winner->bankrupt && winner->cash >= auction.currentBid) {
      winner->cash -= auction.currentBid;
      state_.assets[auction.assetIndex].ownerId = winner->id;
    }
  }
  emit(EventKind::AuctionSettled, auction.highestBidderId, 0, auction.assetIndex, auction.currentBid);
  state_.auction = {};
  if (auto* landingPlayer = player(auction.landingPlayerId)) finishLanding(*landingPlayer);
}

bool GameEngine::transferPayment(PlayerState& payer, std::uint8_t creditorId, std::int32_t amount,
                                 EventKind eventKind, std::uint8_t assetIndex,
                                 DebtContinuation continuation, std::uint8_t dieA, std::uint8_t dieB,
                                 std::int32_t* settledAmount) {
  if (settledAmount != nullptr) *settledAmount = 0;
  if (amount <= 0 || payer.bankrupt) return true;
  if (payer.controller != ControllerKind::Bot) {
    state_.pendingDebt = {true, payer.id, creditorId, assetIndex, eventKind, continuation, dieA, dieB, amount};
    state_.phase = GamePhase::AwaitDebt;
    emit(EventKind::DebtOpened, payer.id, creditorId, assetIndex, amount);
    return false;
  }

  while (payer.cash < amount) {
    bool raised = false;
    for (std::uint8_t i = 0; i < state_.board->assetCount && !raised; ++i) {
      if (state_.assets[i].ownerId == payer.id && state_.assets[i].buildingLevel != 0) {
        raised = static_cast<bool>(sellBuilding(payer.id, i));
      }
    }
    for (std::uint8_t i = 0; i < state_.board->assetCount && !raised; ++i) {
      if (state_.assets[i].ownerId == payer.id && !state_.assets[i].mortgaged &&
          state_.assets[i].buildingLevel == 0) {
        raised = static_cast<bool>(mortgage(payer.id, i));
      }
    }
    if (!raised) break;
  }

  const auto paid = std::min(amount, payer.cash);
  payer.cash -= paid;
  if (settledAmount != nullptr) *settledAmount = paid;
  if (creditorId != 0) {
    if (auto* creditor = player(creditorId)) creditor->cash += paid;
  }
  if (eventKind != EventKind::CardApplied) {
    emit(eventKind, payer.id, creditorId, assetIndex, paid);
  }
  if (paid < amount) bankrupt(payer, creditorId);
  return true;
}

void GameEngine::resumeDebtContinuation(const PendingDebt& debt) {
  auto* debtor = player(debt.debtorId);
  if (debtor == nullptr || debtor->bankrupt || state_.phase == GamePhase::GameOver) return;
  if (debt.continuation == DebtContinuation::ReleaseHoldAndMove) {
    debtor->inHold = false;
    debtor->failedHoldRolls = 0;
    emit(EventKind::ReleasedFromHold, debtor->id, 0, kNoAsset, debt.amount);
    queueMovement(*debtor, debt.dieA, debt.dieB);
  } else if (debt.continuation == DebtContinuation::FinishLanding) {
    finishLanding(*debtor);
  } else {
    state_.phase = GamePhase::TurnEnd;
  }
}

std::int32_t GameEngine::maximumLiquidationCash(std::uint8_t playerId) const {
  const auto* owner = player(playerId);
  if (owner == nullptr) return 0;
  std::int64_t total = owner->cash;
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    const auto& asset = state_.assets[i];
    if (asset.ownerId != playerId) continue;
    const auto& definition = state_.board->assets[i];
    total += static_cast<std::int64_t>(asset.buildingLevel) * (definition.economy.buildingCost / 2);
    if (!asset.mortgaged) total += definition.economy.mortgageValue;
  }
  return total > 2147483647LL ? 2147483647 : static_cast<std::int32_t>(total);
}

void GameEngine::bankrupt(PlayerState& debtor, std::uint8_t creditorId) {
  const auto remainingCash = debtor.cash;
  debtor.bankrupt = true;
  debtor.inHold = false;
  debtor.cash = 0;
  if (remainingCash > 0 && creditorId != 0) {
    if (auto* creditor = player(creditorId)) creditor->cash += remainingCash;
  }
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    auto& asset = state_.assets[i];
    if (asset.ownerId != debtor.id) continue;
    asset.ownerId = creditorId;
    if (creditorId == 0) {
      asset.mortgaged = false;
      asset.buildingLevel = 0;
    }
  }
  emit(EventKind::PlayerBankrupt, debtor.id, creditorId, kNoAsset, remainingCash);
  checkGameOver();
}

void GameEngine::checkGameOver() {
  std::uint8_t alive = 0;
  std::uint8_t winner = 0;
  for (std::uint8_t id = 1; id <= state_.playerCount; ++id) {
    const auto* item = player(id);
    if (item != nullptr && !item->bankrupt) {
      alive++;
      winner = id;
    }
  }
  if ((state_.playerCount > 1 && alive <= 1) || (state_.playerCount == 1 && alive == 0)) {
    state_.winnerPlayerId = state_.playerCount == 1 ? 0 : winner;
    state_.phase = GamePhase::GameOver;
    emit(EventKind::GameFinished, winner);
  }
}

void GameEngine::advanceTurn() {
  if (state_.phase == GamePhase::GameOver) return;
  const auto previous = state_.activePlayerId;
  for (std::size_t offset = 1; offset <= state_.playerCount; ++offset) {
    const auto candidate = static_cast<std::uint8_t>(((previous - 1 + offset) % state_.playerCount) + 1);
    const auto* item = player(candidate);
    if (item != nullptr && !item->bankrupt) {
      state_.activePlayerId = candidate;
      if (candidate <= previous) state_.roundNumber++;
      state_.phase = GamePhase::AwaitRoll;
      emit(EventKind::TurnStarted, candidate);
      return;
    }
  }
  checkGameOver();
}

bool GameEngine::ownsCompleteGroup(std::uint8_t playerId, std::uint8_t group) const {
  bool found = false;
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    const auto& definition = state_.board->assets[i];
    if (definition.kind != TileKind::Property || definition.group != group) continue;
    found = true;
    if (state_.assets[i].ownerId != playerId) return false;
  }
  return found;
}

std::uint8_t GameEngine::ownedKindCount(std::uint8_t playerId, TileKind kind) const {
  std::uint8_t count = 0;
  for (std::uint8_t i = 0; i < state_.board->assetCount; ++i) {
    if (state_.board->assets[i].kind == kind && state_.assets[i].ownerId == playerId) count++;
  }
  return count;
}

std::uint8_t GameEngine::holdPosition() const {
  for (std::uint8_t i = 0; i < state_.board->tileCount; ++i) {
    if (state_.board->tiles[i].kind == TileKind::Hold) return i;
  }
  return 0;
}

std::uint32_t GameEngine::nextRandom() {
  std::uint32_t value = state_.rngState;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  state_.rngState = value == 0 ? 0xA341316Cu : value;
  return state_.rngState;
}

std::uint8_t GameEngine::randomDie() { return static_cast<std::uint8_t>(nextRandom() % 6u + 1u); }

void GameEngine::ensureFinancialHistoryInitialized() {
  if (state_.financialHistoryInitialized) return;
  state_.financialHistory = {};
  state_.financialHistoryInitialized = true;
  for (std::uint8_t offset = 0; offset < state_.eventCount; ++offset) {
    const auto index = static_cast<std::uint8_t>(
        (state_.eventHead + kEventHistory - state_.eventCount + offset) % kEventHistory);
    recordFinancialEvent(state_.events[index]);
  }
}

void GameEngine::recordFinancial(std::uint8_t playerId, std::int32_t amount, EventKind kind,
                                 std::uint8_t counterpartyId, std::uint8_t assetIndex,
                                 std::uint32_t sequence) {
  if (playerId == 0 || playerId > state_.playerCount || amount == 0) return;
  auto& history = state_.financialHistory[playerId - 1];
  history.entries[history.head] = {sequence, amount, kind, counterpartyId, assetIndex};
  history.head = static_cast<std::uint8_t>((history.head + 1) % kPlayerFinancialHistory);
  if (history.count < kPlayerFinancialHistory) {
    ++history.count;
  } else {
    history.truncated = true;
  }
}

void GameEngine::recordFinancialEvent(const GameEvent& event) {
  switch (event.kind) {
    case EventKind::GameStarted:
      if (state_.board == nullptr) return;
      for (std::uint8_t playerId = 1; playerId <= state_.playerCount; ++playerId) {
        recordFinancial(playerId, state_.board->startingCash, event.kind, 0, kNoAsset,
                        event.sequence);
      }
      return;
    case EventKind::PassedStart:
    case EventKind::AssetMortgaged:
      recordFinancial(event.actorId, event.amount, event.kind, 0, event.assetIndex,
                      event.sequence);
      return;
    case EventKind::AssetPurchased:
    case EventKind::AssetUnmortgaged:
    case EventKind::AuctionSettled:
      recordFinancial(event.actorId, -event.amount, event.kind, 0, event.assetIndex,
                      event.sequence);
      return;
    case EventKind::BuildingChanged:
      recordFinancial(event.actorId, event.amount, event.kind, 0, event.assetIndex,
                      event.sequence);
      return;
    case EventKind::RentPaid:
    case EventKind::FeePaid:
      recordFinancial(event.actorId, -event.amount, event.kind, event.targetId,
                      event.assetIndex, event.sequence);
      recordFinancial(event.targetId, event.amount, event.kind, event.actorId,
                      event.assetIndex, event.sequence);
      return;
    case EventKind::CardApplied: {
      if (event.amount == 0) return;
      const bool compactV2 = (event.detail >> 16) != 0;
      const auto card = compactV2 ? cardEventCardIndex(event.detail)
                                  : static_cast<std::uint8_t>(event.detail & 0xFFu);
      const bool chance = compactV2 ? cardEventDeckId(event.detail) == 1
                                    : (event.detail & 0x100u) != 0;
      if (card == 4) return;
      const bool debit = card == 1 || card == 5 || (card == 7 && chance);
      recordFinancial(event.actorId, debit ? -event.amount : event.amount, event.kind,
                      event.targetId, event.assetIndex, event.sequence);
      return;
    }
    case EventKind::TradeSettled:
      recordFinancial(event.actorId, -event.amount, event.kind, event.targetId,
                      event.assetIndex, event.sequence);
      recordFinancial(event.targetId, event.amount, event.kind, event.actorId,
                      event.assetIndex, event.sequence);
      return;
    case EventKind::PlayerBankrupt:
      recordFinancial(event.actorId, -event.amount, event.kind, event.targetId,
                      event.assetIndex, event.sequence);
      recordFinancial(event.targetId, event.amount, event.kind, event.actorId,
                      event.assetIndex, event.sequence);
      return;
    case EventKind::DebtPaid:
      // The original rent/fee event or the following CardApplied owns the cash entry.
      return;
    default:
      return;
  }
}

void GameEngine::emit(EventKind kind, std::uint8_t actorId, std::uint8_t targetId,
                      std::uint8_t assetIndex, std::int32_t amount, std::uint32_t detail) {
  ensureFinancialHistoryInitialized();
  auto& event = state_.events[state_.eventHead];
  event = {state_.nextEventSequence++, kind, actorId, targetId, assetIndex, amount, detail};
  recordFinancialEvent(event);
  state_.eventHead = static_cast<std::uint8_t>((state_.eventHead + 1) % kEventHistory);
  if (state_.eventCount < kEventHistory) state_.eventCount++;
}

void GameEngine::changed() { state_.stateVersion++; }

}  // namespace gridopoly::core
