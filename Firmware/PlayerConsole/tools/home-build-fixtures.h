#pragma once
#include "../app_state.h"
#include <GridopolyCore.h>
#include <GridopolyProtocol.h>
#include <cstring>

struct HomeBuildFixture {
    TransportEvent compact{}, full{};
    uint8_t asset = 0xFF, peer = 0xFF, position = 0xFF;
    HomeBuildFixture() {
        const auto *board = gridopoly::core::BoardCatalog::findBySize(16);
        for (uint8_t i = 0; i < board->assetCount; ++i) {
            if (board->assets[i].kind != gridopoly::core::TileKind::Property) continue;
            if (asset == 0xFF) asset = i;
            else if (board->assets[i].group == board->assets[asset].group) { peer = i; break; }
        }
        for (uint8_t i = 0; i < board->tileCount; ++i) if (board->tiles[i].assetIndex == asset) { position = i; break; }
        compact.kind = TransportEventKind::StateSnapshotApplied;
        compact.roomId = 800; compact.stateVersion = 10; compact.playerCount = 2;
        compact.selfSeatId = compact.activePlayerId = compact.decisionPlayerId = 1;
        compact.boardSize = 16; compact.phase = AuthorityPhase::AwaitRoll;
        compact.availableActions = (1u << 0) | (1u << 7);
        compact.playerPosition = position; compact.tileAssetIndex = asset; compact.tileOwnerId = 1;
        compact.cash = 1000;
        compact.players[0].playerId = 1; compact.players[0].position = position; compact.players[0].cash = 1000;
        compact.players[1].playerId = 2; compact.players[1].cash = 1000;
        full = compact; full.kind = TransportEventKind::AuthoritySnapshotApplied;
        full.assetCount = board->assetCount;
        full.boardIdHash = gridopoly::protocol::crc32(reinterpret_cast<const uint8_t *>(board->id), std::strlen(board->id));
        for (uint8_t i = 0; i < board->assetCount; ++i)
            if (board->assets[i].group == board->assets[asset].group) full.assets[i].ownerId = 1;
    }
    void apply(AppState &state, uint32_t now = 100) const {
        appHandleTransportEvent(state, compact, now);
        appHandleTransportEvent(state, full, now + 1);
    }
};
inline uint8_t homeFocusFor(const AppState &state, HomeAction wanted) {
    HomeAction actions[5]{}; const uint8_t count = appHomeActions(state, actions);
    for (uint8_t i = 0; i < count; ++i) if (actions[i] == wanted) return i;
    return 0xFF;
}
