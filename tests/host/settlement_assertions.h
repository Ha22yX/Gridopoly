#pragma once

#include <cassert>
#include <gridopoly/core/GameModel.h>

// Connection status and stateVersion may legitimately change on reconnect.
// Financial effects, ownership, pending debt and the game event stream may not.
inline void assertSettlementUnchanged(const gridopoly::core::GameState& before,
                                      const gridopoly::core::GameState& after) {
  assert(after.playerCount == before.playerCount);
  assert(after.phase == before.phase);
  assert(after.activePlayerId == before.activePlayerId);
  assert(after.rngState == before.rngState);
  for (std::size_t i = 0; i < before.players.size(); ++i) {
    assert(after.players[i].cash == before.players[i].cash);
    assert(after.players[i].position == before.players[i].position);
    assert(after.players[i].bankrupt == before.players[i].bankrupt);
  }
  for (std::size_t i = 0; i < before.assets.size(); ++i) {
    assert(after.assets[i].ownerId == before.assets[i].ownerId);
    assert(after.assets[i].buildingLevel == before.assets[i].buildingLevel);
    assert(after.assets[i].mortgaged == before.assets[i].mortgaged);
  }
  const auto& a = after.pendingDebt;
  const auto& b = before.pendingDebt;
  assert(a.active == b.active && a.debtorId == b.debtorId && a.creditorId == b.creditorId);
  assert(a.amount == b.amount && a.assetIndex == b.assetIndex);
  assert(a.paymentEvent == b.paymentEvent && a.continuation == b.continuation);
  assert(a.dieA == b.dieA && a.dieB == b.dieB);
  assert(after.eventHead == before.eventHead && after.eventCount == before.eventCount);
  assert(after.nextEventSequence == before.nextEventSequence);
  for (std::size_t i = 0; i < before.events.size(); ++i) {
    const auto& x = after.events[i];
    const auto& y = before.events[i];
    assert(x.sequence == y.sequence && x.kind == y.kind);
    assert(x.actorId == y.actorId && x.targetId == y.targetId && x.assetIndex == y.assetIndex);
    assert(x.amount == y.amount && x.detail == y.detail);
  }
}
