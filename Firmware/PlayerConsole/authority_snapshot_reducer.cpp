#include "authority_snapshot_reducer.h"

#include <cstring>

bool authoritySnapshotToEvent(const gridopoly::protocol::StateSnapshot &snapshot,
                              bool resync, TransportEvent &event)
{
    if (snapshot.seatId == 0 || snapshot.playerCount == 0 ||
        snapshot.playerCount > 6 || snapshot.selfPosition >= snapshot.boardSize) return false;
    event = TransportEvent{};
    event.kind = TransportEventKind::StateSnapshotApplied;
    event.stateVersion = snapshot.stateVersion;
    event.cash = snapshot.selfCash;
    event.playerPosition = snapshot.selfPosition;
    event.targetPosition = snapshot.pendingTarget;
    event.selfSeatId = snapshot.seatId;
    event.activePlayerId = snapshot.activePlayerId;
    event.decisionPlayerId = snapshot.decisionPlayerId;
    event.playerCount = snapshot.playerCount;
    event.boardSize = snapshot.boardSize;
    event.pendingTarget = snapshot.pendingTarget;
    event.tileAssetIndex = snapshot.tileAssetIndex;
    event.tileOwnerId = snapshot.tileOwnerId;
    event.tileBuildingLevel = snapshot.tileBuildingLevel;
    event.tileFlags = snapshot.tileFlags;
    event.debtCreditorId = snapshot.debtCreditorId;
    event.debtAssetIndex = snapshot.debtAssetIndex;
    event.auctionAssetIndex = snapshot.auctionAssetIndex;
    event.auctionHighestBidderId = snapshot.auctionHighestBidderId;
    event.debtAmount = snapshot.debtAmount;
    event.auctionCurrentBid = snapshot.auctionCurrentBid;
    event.auctionMinimumBid = snapshot.auctionMinimumBid;
    event.phase = static_cast<AuthorityPhase>(snapshot.phase);
    event.availableActions = snapshot.availableActions;
    event.resync = resync;
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        event.players[index].playerId = snapshot.players[index].playerId;
        event.players[index].position = snapshot.players[index].position;
        event.players[index].cash = snapshot.players[index].cash;
        event.players[index].flags = snapshot.players[index].flags;
    }
    return true;
}

bool authoritySnapshotsEqual(const gridopoly::protocol::StateSnapshot &left,
                             const gridopoly::protocol::StateSnapshot &right)
{
    if (left.seatId != right.seatId || left.phase != right.phase ||
        left.activePlayerId != right.activePlayerId || left.round != right.round ||
        left.boardSize != right.boardSize || left.selfPosition != right.selfPosition ||
        left.selfCash != right.selfCash || left.availableActions != right.availableActions ||
        left.playerCount != right.playerCount || left.tileAssetIndex != right.tileAssetIndex ||
        left.tileOwnerId != right.tileOwnerId ||
        left.tileBuildingLevel != right.tileBuildingLevel || left.tileFlags != right.tileFlags ||
        left.pendingTarget != right.pendingTarget || left.stateVersion != right.stateVersion ||
        left.decisionPlayerId != right.decisionPlayerId ||
        left.debtCreditorId != right.debtCreditorId ||
        left.debtAssetIndex != right.debtAssetIndex ||
        left.auctionAssetIndex != right.auctionAssetIndex ||
        left.debtAmount != right.debtAmount ||
        left.auctionCurrentBid != right.auctionCurrentBid ||
        left.auctionMinimumBid != right.auctionMinimumBid ||
        left.auctionHighestBidderId != right.auctionHighestBidderId) return false;
    for (uint8_t index = 0; index < left.playerCount; ++index) {
        if (left.players[index].playerId != right.players[index].playerId ||
            left.players[index].position != right.players[index].position ||
            left.players[index].cash != right.players[index].cash ||
            left.players[index].flags != right.players[index].flags) return false;
    }
    return true;
}

bool fullAuthoritySnapshotToEvent(const gridopoly::protocol::AuthoritySnapshot &snapshot,
                                  bool resync, TransportEvent &event)
{
    if (snapshot.playerCount == 0 || snapshot.playerCount > 6 ||
        snapshot.assetCount > 28 || snapshot.boardSize == 0) return false;
    event = TransportEvent{};
    event.kind = TransportEventKind::AuthoritySnapshotApplied;
    event.stateVersion = snapshot.stateVersion;
    event.lastEventSequence = snapshot.lastEventSequence;
    event.boardIdHash = snapshot.boardIdHash;
    event.phase = static_cast<AuthorityPhase>(snapshot.phase);
    event.activePlayerId = snapshot.activePlayerId;
    event.decisionPlayerId = snapshot.decisionPlayerId;
    event.winnerPlayerId = snapshot.winnerPlayerId;
    event.boardSize = snapshot.boardSize;
    event.playerCount = snapshot.playerCount;
    event.assetCount = snapshot.assetCount;
    event.pendingMoveFlags = snapshot.pendingMoveFlags;
    event.pendingMovePlayerId = snapshot.pendingMovePlayerId;
    event.pendingMoveOrigin = snapshot.pendingMoveOrigin;
    event.pendingTarget = snapshot.pendingMoveTarget;
    event.pendingMoveDieA = snapshot.pendingMoveDieA;
    event.pendingMoveDieB = snapshot.pendingMoveDieB;
    event.pendingPurchaseFlags = snapshot.pendingPurchaseFlags;
    event.pendingPurchasePlayerId = snapshot.pendingPurchasePlayerId;
    event.pendingPurchaseAssetIndex = snapshot.pendingPurchaseAssetIndex;
    event.debtFlags = snapshot.debtFlags;
    event.debtDebtorId = snapshot.debtDebtorId;
    event.debtCreditorId = snapshot.debtCreditorId;
    event.debtAssetIndex = snapshot.debtAssetIndex;
    event.debtPaymentEvent = snapshot.debtPaymentEvent;
    event.debtContinuation = snapshot.debtContinuation;
    event.debtDieA = snapshot.debtDieA;
    event.debtDieB = snapshot.debtDieB;
    event.debtAmount = snapshot.debtAmount;
    event.auctionAssetIndex = snapshot.auctionAssetIndex;
    event.auctionCurrentBidderId = snapshot.auctionCurrentBidderId;
    event.auctionHighestBidderId = snapshot.auctionHighestBidderId;
    event.auctionPassedMask = snapshot.auctionPassedMask;
    event.auctionFlags = snapshot.auctionFlags;
    event.auctionReadyMask = snapshot.auctionReadyMask;
    event.auctionRequiredReadyMask = snapshot.auctionRequiredReadyMask;
    event.auctionCurrentBid = snapshot.auctionCurrentBid;
    event.auctionGeneration = snapshot.auctionGeneration;
    event.pendingCardFlags = snapshot.pendingCardFlags;
    event.pendingCardPlayerId = snapshot.pendingCardPlayerId;
    event.pendingCardDeckId = snapshot.pendingCardDeckId;
    event.pendingCardIndex = snapshot.pendingCardIndex;
    event.pendingCardInstanceId = snapshot.pendingCardInstanceId;
    event.pendingCardCatalogId = snapshot.pendingCardCatalogId;
    event.pendingCardEffectId = snapshot.pendingCardEffectId;
    event.pendingCardDisplayAmount = snapshot.pendingCardDisplayAmount;
    event.pendingCardTargetPlayerId = snapshot.pendingCardTargetPlayerId;
    event.pendingCardTargetPosition = snapshot.pendingCardTargetPosition;
    event.pendingCardDrawEventSequence = snapshot.pendingCardDrawEventSequence;
    event.resync = resync;
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        event.players[index].playerId = snapshot.players[index].playerId;
        event.players[index].position = snapshot.players[index].position;
        event.players[index].cash = snapshot.players[index].cash;
        event.players[index].flags = snapshot.players[index].flags;
        event.players[index].failedHoldRolls = snapshot.players[index].failedHoldRolls;
        event.players[index].doublesStreak = snapshot.players[index].doublesStreak;
    }
    for (uint8_t index = 0; index < snapshot.assetCount; ++index) {
        event.assets[index].ownerId = snapshot.assets[index].ownerId;
        event.assets[index].buildingLevel = snapshot.assets[index].buildingLevel;
        event.assets[index].flags = snapshot.assets[index].flags;
    }
    return true;
}

bool fullAuthoritySnapshotsEqual(const gridopoly::protocol::AuthoritySnapshot &left,
                                 const gridopoly::protocol::AuthoritySnapshot &right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool rosterSnapshotToEvent(const gridopoly::protocol::RosterSnapshot &snapshot,
                           bool resync, TransportEvent &event)
{
    if (snapshot.playerCount == 0 || snapshot.playerCount > 6) return false;
    event = TransportEvent{};
    event.kind = TransportEventKind::RosterSnapshotApplied;
    event.stateVersion = snapshot.stateVersion;
    event.playerCount = snapshot.playerCount;
    event.resync = resync;
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        event.players[index].playerId = snapshot.playerIds[index];
        std::memcpy(event.playerNames[index], snapshot.displayNames[index].data(), 17);
        event.playerNames[index][16] = '\0';
    }
    return true;
}

bool rosterSnapshotsEqual(const gridopoly::protocol::RosterSnapshot &left,
                          const gridopoly::protocol::RosterSnapshot &right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

bool gameEventToTransportEvent(const gridopoly::protocol::GameEventRecord &record,
                               uint32_t stateVersion, bool resync, TransportEvent &event)
{
    if (record.sequence == 0 || record.kind == 0) return false;
    event = TransportEvent{};
    event.kind = TransportEventKind::GameEventReceived;
    event.stateVersion = stateVersion;
    event.gameEvent.sequence = record.sequence;
    event.gameEvent.kind = record.kind;
    event.gameEvent.actorId = record.actorId;
    event.gameEvent.targetId = record.targetId;
    event.gameEvent.assetIndex = record.assetIndex;
    event.gameEvent.amount = record.amount;
    event.gameEvent.detail = record.detail;
    event.resync = resync;
    return true;
}

bool playerCardEventToTransportEvent(const gridopoly::protocol::PlayerCardEvent &card,
                                     bool resync, TransportEvent &event)
{
    if (card.eventSequence == 0 || card.playerId == 0 || card.playerId > 6 ||
        card.deckId < 1 || card.deckId > 2 || card.cardIndex > 7 ||
        card.cardInstanceId == 0) return false;
    event = TransportEvent{};
    event.kind = card.stage == gridopoly::protocol::PlayerCardStage::Drawn
        ? TransportEventKind::PlayerCardDrawn
        : TransportEventKind::PlayerCardEffectApplied;
    event.stateVersion = card.stateVersion;
    event.cardStage = static_cast<uint8_t>(card.stage);
    event.cardPlayerId = card.playerId;
    event.cardDeckId = card.deckId;
    event.cardIndex = card.cardIndex;
    event.cardFlags = card.flags;
    event.cardInstanceId = card.cardInstanceId;
    event.cardCatalogId = card.cardCatalogId;
    event.cardEffectId = card.effectId;
    event.cardAmount = card.amount;
    event.cardTargetPlayerId = card.targetPlayerId;
    event.cardTargetPosition = card.targetPosition;
    event.cardOutcome = card.outcome;
    event.cardEventSequence = card.eventSequence;
    event.resync = resync;
    return true;
}
