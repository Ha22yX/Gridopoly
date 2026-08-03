#include "authority_snapshot_reducer.h"

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
