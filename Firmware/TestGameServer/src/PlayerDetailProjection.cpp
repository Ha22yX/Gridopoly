#include "PlayerDetailProjection.h"

namespace gridopoly::server {
namespace {

using namespace gridopoly::core;
using namespace gridopoly::protocol;

bool samePayment(const GameEvent& older, const GameEvent& newer) {
  const bool legacyDebtPair = newer.kind == EventKind::DebtPaid;
  const bool cardDebtPair = older.kind == EventKind::DebtPaid && newer.kind == EventKind::CardApplied;
  return (legacyDebtPair || cardDebtPair) && older.actorId == newer.actorId &&
         older.targetId == newer.targetId && older.assetIndex == newer.assetIndex &&
         older.amount == newer.amount;
}

bool projectFinancialEvent(const GameEvent& event, const GameEvent* nextNewer,
                           std::uint8_t playerId, std::int32_t startingCash,
                           PlayerDetailLedgerEntry& output) {
  // payDebt emits the original payment kind and then DebtPaid. Keep only the
  // latter so a single cash movement never appears twice in the ledger.
  if (nextNewer != nullptr && samePayment(event, *nextNewer)) return false;

  std::int32_t signedAmount = 0;
  std::uint8_t counterpartyId = 0;
  const bool actor = event.actorId == playerId;
  const bool target = event.targetId == playerId && event.targetId != 0;
  switch (event.kind) {
    case EventKind::GameStarted:
      signedAmount = startingCash;
      break;
    case EventKind::PassedStart:
      if (!actor) return false;
      signedAmount = event.amount;
      break;
    case EventKind::AssetPurchased:
    case EventKind::AssetUnmortgaged:
    case EventKind::AuctionSettled:
      if (!actor) return false;
      signedAmount = -event.amount;
      break;
    case EventKind::AssetMortgaged:
      if (!actor) return false;
      signedAmount = event.amount;
      break;
    case EventKind::BuildingChanged:
      if (!actor) return false;
      signedAmount = event.amount;
      break;
    case EventKind::RentPaid:
    case EventKind::FeePaid:
    case EventKind::DebtPaid:
      if (actor) {
        signedAmount = -event.amount;
        counterpartyId = event.targetId;
      } else if (target) {
        signedAmount = event.amount;
        counterpartyId = event.actorId;
      } else {
        return false;
      }
      break;
    case EventKind::TradeSettled:
      if (actor) {
        signedAmount = -event.amount;
        counterpartyId = event.targetId;
      } else if (target) {
        signedAmount = event.amount;
        counterpartyId = event.actorId;
      } else {
        return false;
      }
      break;
    case EventKind::CardApplied: {
      if (!actor || event.amount == 0) return false;
      const bool compactV2 = (event.detail >> 16) != 0;
      const auto card = compactV2 ? cardEventCardIndex(event.detail)
                                  : static_cast<std::uint8_t>(event.detail & 0xFFu);
      const bool cityEvent = compactV2 ? cardEventDeckId(event.detail) == 1
                                       : (event.detail & 0x100u) != 0;
      // Cards 1 and 5 are payments; card 7 is a payment only in the city deck.
      // Card 4 is movement, not a financial transaction.
      if (card == 4) return false;
      const bool debit = card == 1 || card == 5 || (card == 7 && cityEvent);
      signedAmount = debit ? -event.amount : event.amount;
      break;
    }
    default:
      return false;
  }
  if (signedAmount == 0) return false;
  output = PlayerDetailLedgerEntry{};
  output.sequence = event.sequence;
  output.amount = signedAmount;
  output.kind = static_cast<std::uint8_t>(event.kind);
  output.counterpartyId = counterpartyId;
  output.assetIndex = event.assetIndex;
  output.flags = signedAmount > 0 ? PlayerDetailLedgerFlagCredit : PlayerDetailLedgerFlagNone;
  if (counterpartyId == 0) output.flags |= PlayerDetailLedgerFlagBankCounterparty;
  if (event.assetIndex != kNoAsset) output.flags |= PlayerDetailLedgerFlagHasAsset;
  return true;
}

}  // namespace

bool makePlayerDetailProjection(const GameState& state, std::uint32_t requestId,
                                std::uint8_t targetPlayerId, std::uint32_t requestedStateVersion,
                                PlayerDetailResponse& output) {
  if (state.board == nullptr || requestId == 0 || targetPlayerId == 0 ||
      targetPlayerId > state.playerCount || state.board->assetCount > state.assets.size()) {
    return false;
  }
  const auto& player = state.players[targetPlayerId - 1];
  if (player.id != targetPlayerId) return false;

  output = PlayerDetailResponse{};
  output.requestId = requestId;
  output.stateVersion = state.stateVersion;
  output.cash = player.cash;
  output.targetPlayerId = targetPlayerId;
  output.position = player.position;
  if (requestedStateVersion != 0 && requestedStateVersion != state.stateVersion) {
    output.flags |= PlayerDetailFlagRequestedVersionStale;
  }

  for (std::uint8_t assetIndex = 0; assetIndex < state.board->assetCount; ++assetIndex) {
    const auto& asset = state.assets[assetIndex];
    if (asset.ownerId != targetPlayerId) continue;
    ++output.totalOwnedAssets;
    if (output.assetCount >= output.assets.size()) {
      output.flags |= PlayerDetailFlagAssetsTruncated;
      continue;
    }
    auto& projected = output.assets[output.assetCount++];
    projected.assetIndex = assetIndex;
    projected.state = static_cast<std::uint8_t>(asset.buildingLevel & PlayerDetailAssetBuildingMask);
    if (asset.mortgaged) projected.state |= PlayerDetailAssetMortgaged;
  }

  if (state.financialHistoryInitialized) {
    const auto& history = state.financialHistory[targetPlayerId - 1];
    for (std::uint8_t age = 0; age < history.count && output.ledgerCount < output.ledger.size(); ++age) {
      const auto index = static_cast<std::uint8_t>(
          (history.head + kPlayerFinancialHistory - 1u - age) % kPlayerFinancialHistory);
      const auto& record = history.entries[index];
      auto& projected = output.ledger[output.ledgerCount++];
      projected.sequence = record.sequence;
      projected.amount = record.amount;
      projected.kind = static_cast<std::uint8_t>(record.kind);
      projected.counterpartyId = record.counterpartyId;
      projected.assetIndex = record.assetIndex;
      projected.flags = record.amount > 0 ? PlayerDetailLedgerFlagCredit
                                         : PlayerDetailLedgerFlagNone;
      if (record.counterpartyId == 0) projected.flags |= PlayerDetailLedgerFlagBankCounterparty;
      if (record.assetIndex != kNoAsset) projected.flags |= PlayerDetailLedgerFlagHasAsset;
    }
    if (history.truncated || history.count > output.ledger.size()) {
      output.flags |= PlayerDetailFlagLedgerTruncated;
    }
  } else {
    // Schema-2/3 saves did not persist a dedicated ledger. Keep projecting
    // their retained event window until the engine lazily migrates it.
    const GameEvent* nextNewer = nullptr;
    std::uint8_t matchingLedgerEntries = 0;
    for (std::uint8_t age = 0; age < state.eventCount; ++age) {
      const auto index = static_cast<std::uint8_t>(
          (state.eventHead + kEventHistory - 1u - age) % kEventHistory);
      const auto& event = state.events[index];
      PlayerDetailLedgerEntry projected{};
      if (projectFinancialEvent(event, nextNewer, targetPlayerId,
                                state.board->startingCash, projected)) {
        ++matchingLedgerEntries;
        if (output.ledgerCount < output.ledger.size()) {
          output.ledger[output.ledgerCount++] = projected;
        } else {
          output.flags |= PlayerDetailFlagLedgerTruncated;
        }
      }
      nextNewer = &event;
    }
    if (matchingLedgerEntries > output.ledger.size()) {
      output.flags |= PlayerDetailFlagLedgerTruncated;
    }
  }
  return true;
}

}  // namespace gridopoly::server
