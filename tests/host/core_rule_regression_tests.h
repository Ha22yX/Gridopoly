#pragma once

#include <limits>
#include <vector>

namespace core_rule_regression {

using namespace gridopoly::core;

inline void setup(GameEngine& game, ControllerKind first = ControllerKind::RealConsole,
                  std::uint8_t size = 32, std::uint8_t players = 4) {
  assert(game.reset(*BoardCatalog::findBySize(size), 1));
  assert(game.addPlayer("A", first));
  for (std::uint8_t i = 1; i < players; ++i) assert(game.addPlayer("Bot", ControllerKind::Bot));
  assert(game.start());
}

inline unsigned eventCount(const GameState& state, EventKind kind, std::uint8_t actor) {
  unsigned count = 0;
  for (const auto& event : state.events) {
    if (event.sequence != 0 && event.kind == kind && event.actorId == actor) ++count;
  }
  return count;
}

inline void assertNoMutation(const GameState& before, const GameState& after) {
  assert(after.stateVersion == before.stateVersion);
  assert(after.nextEventSequence == before.nextEventSequence);
  assert(after.phase == before.phase);
  for (std::uint8_t i = 0; i < before.playerCount; ++i) assert(after.players[i].cash == before.players[i].cash);
  for (std::uint8_t i = 0; i < before.board->assetCount; ++i) {
    assert(after.assets[i].ownerId == before.assets[i].ownerId);
    assert(after.assets[i].buildingLevel == before.assets[i].buildingLevel);
    assert(after.assets[i].mortgaged == before.assets[i].mortgaged);
  }
}

inline void assertNextPlayer(GameEngine& game, std::uint8_t id = 2) {
  const auto& state = game.state();
  assert(state.phase == GamePhase::AwaitRoll);
  assert(state.activePlayerId == id && game.decisionPlayerId() == id);
  assert(game.actionsFor(id) & ActionRoll);
  assert(!state.pendingMove.active && !state.pendingPurchase.active);
  assert(!state.pendingDebt.active && !state.pendingCard.active);
}

inline void bankruptTurnRegressions() {
  for (auto kind : {ControllerKind::RealConsole, ControllerKind::Bot}) {
    GameEngine rent;
    setup(rent, kind);
    auto& state = rent.mutableStateForRestore();
    state.players[0].cash = 0;
    state.assets[state.board->tiles[3].assetIndex].ownerId = 2;
    assert(rent.roll(1, 1, 2));
    if (kind == ControllerKind::RealConsole) {
      assert(rent.confirmPosition(1, 3));
      assert(rent.actionsFor(1) & ActionDeclareBankruptcy);
      assert(rent.declareBankruptcy(1));
    }
    assert(state.players[0].bankrupt && rent.actionsFor(1) == ActionNone);
    assertNextPlayer(rent);
    assert(eventCount(state, EventKind::TurnEnded, 1) == 1);
    assert(eventCount(state, EventKind::TurnStarted, 2) == 1);
    assert(state.roundNumber == 1);
    const auto beforeOldCommand = state;
    assert(!rent.endTurn(1));
    assertNoMutation(beforeOldCommand, state);
    assert(rent.runBots(1) == 1);

    // Payment-card bankruptcy must settle the card, then advance even though
    // a doubles roll would normally award the same player another turn.
    GameEngine card;
    setup(card, kind, 16);
    auto& cardState = card.mutableStateForRestore();
    cardState.players[0].cash = 1;
    assert(card.roll(1, 1, 1));
    if (kind == ControllerKind::RealConsole) {
      assert(card.confirmPosition(1, 2));
      assert(card.continueCard(1, cardState.pendingCard.cardInstanceId));
      assert(cardState.phase == GamePhase::AwaitDebt);
      assert(card.declareBankruptcy(1));
    }
    assert(cardState.players[0].bankrupt);
    assertNextPlayer(card);
    assert(eventCount(cardState, EventKind::CardApplied, 1) == 1);
    for (const auto& event : cardState.events) {
      if (event.kind == EventKind::CardApplied && event.actorId == 1) {
        assert(cardEventOutcome(event.detail) == CardEffectOutcome::Bankrupt);
        assert(event.amount == (kind == ControllerKind::Bot ? 1 : 0));
      }
    }
    assert(card.runBots(1) == 1);

    // On the third failed hold roll an insolvent bot used to be moved by the
    // old continuation, including after a GameOver transition.
    GameEngine hold;
    setup(hold, kind);
    auto& holdState = hold.mutableStateForRestore();
    holdState.players[0].cash = 0;
    holdState.players[0].inHold = true;
    holdState.players[0].position = 8;
    holdState.players[0].failedHoldRolls = 2;
    assert(hold.roll(1, 1, 2));
    if (kind == ControllerKind::RealConsole) assert(hold.declareBankruptcy(1));
    assert(holdState.players[0].bankrupt && holdState.players[0].position == 8);
    assertNextPlayer(hold);
    assert(eventCount(holdState, EventKind::ReleasedFromHold, 1) == 0);
    assert(eventCount(holdState, EventKind::MoveRequested, 1) == 0);

    GameEngine terminal;
    setup(terminal, kind, 32, 2);
    auto& finalState = terminal.mutableStateForRestore();
    finalState.players[0].cash = 0;
    finalState.assets[finalState.board->tiles[3].assetIndex].ownerId = 2;
    assert(terminal.roll(1, 1, 2));
    if (kind == ControllerKind::RealConsole) {
      assert(terminal.confirmPosition(1, 3));
      assert(terminal.declareBankruptcy(1));
    }
    assert(finalState.phase == GamePhase::GameOver && finalState.winnerPlayerId == 2);
    assert(eventCount(finalState, EventKind::GameFinished, 2) == 1);
    assert(eventCount(finalState, EventKind::TurnStarted, 2) == 0);
    assert(terminal.actionsFor(2) == ActionNone && terminal.runBots(10) == 0);
  }

  GameEngine wrap;
  setup(wrap);
  auto& state = wrap.mutableStateForRestore();
  state.activePlayerId = 4;
  state.players[0].bankrupt = true;
  state.players[3].controller = ControllerKind::RealConsole;
  state.players[3].cash = 0;
  state.assets[1].ownerId = 2;
  assert(wrap.roll(4, 1, 2));
  assert(wrap.confirmPosition(4, 3));
  assert(wrap.declareBankruptcy(4));
  assertNextPlayer(wrap);
  assert(state.roundNumber == 2);

  // Continue a four-player fixture through all three eliminations, rather than
  // testing only the two-player shortcut to GameOver.
  GameEngine elimination;
  setup(elimination);
  auto& finalState = elimination.mutableStateForRestore();
  finalState.assets[1].ownerId = 4;
  for (std::uint8_t id = 1; id <= 3; ++id) {
    finalState.players[id - 1].controller = ControllerKind::RealConsole;
    finalState.players[id - 1].cash = 0;
    assert(elimination.roll(id, 1, 2));
    assert(elimination.confirmPosition(id, 3));
    assert(elimination.declareBankruptcy(id));
    if (id < 3) assertNextPlayer(elimination, static_cast<std::uint8_t>(id + 1));
  }
  assert(finalState.phase == GamePhase::GameOver && finalState.winnerPlayerId == 4);
  assert(eventCount(finalState, EventKind::GameFinished, 4) == 1);
  assert(elimination.actionsFor(4) == ActionNone);
}

inline void buildingStockRegressions() {
  GameEngine game;
  setup(game);
  auto& state = game.mutableStateForRestore();
  state.players[0].cash = state.players[1].cash = 100000;
  std::vector<std::uint8_t> properties;
  for (std::uint8_t i = 0; i < state.board->assetCount; ++i) {
    if (state.board->assets[i].kind != TileKind::Property) continue;
    state.assets[i].ownerId = static_cast<std::uint8_t>(state.board->assets[i].group % 2 + 1);
    properties.push_back(i);
  }
  // Two owners compete for the same pool. No property reaches landmark level
  // during this fill, so exactly 24 ordinary buildings can be purchased.
  unsigned built = 0;
  while (built < state.board->buildingStock) {
    for (const auto asset : properties) {
      if (built == state.board->buildingStock) break;
      assert(game.build(state.assets[asset].ownerId, asset));
      ++built;
    }
  }
  assert(game.availableBuildings() == 0 && game.availableLandmarks() == state.board->landmarkStock);
  assert(!(game.actionsFor(1) & ActionBuild) && !(game.actionsFor(2) & ActionBuild));
  for (const auto asset : properties) {
    const auto before = state;
    assert(!game.build(state.assets[asset].ownerId, asset));
    assertNoMutation(before, state);
  }
  std::uint8_t sold = kNoAsset;
  for (const auto asset : properties) {
    if (game.canSellBuilding(state.assets[asset].ownerId, asset)) { sold = asset; break; }
  }
  assert(sold != kNoAsset);
  assert(game.sellBuilding(state.assets[sold].ownerId, sold));
  assert(game.availableBuildings() == 1);
  assert(game.actionsFor(state.assets[sold].ownerId) & ActionBuild);
  assert(game.build(state.assets[sold].ownerId, sold));
  assert(game.availableBuildings() == 0);
  GameEngine restored;
  restored.mutableStateForRestore() = state;
  assert(restored.availableBuildings() == 0);
  assert(!(restored.actionsFor(1) & ActionBuild));
}

inline void landmarkAndLiquidationRegressions() {
  auto board = *BoardCatalog::findBySize(32);
  board.landmarkStock = 1;  // Small stock exercises the boundary with a legal board estate.
  GameEngine game;
  assert(game.reset(board, 1));
  for (unsigned i = 0; i < 4; ++i) assert(game.addPlayer("Owner", ControllerKind::RealConsole));
  assert(game.start());
  auto& state = game.mutableStateForRestore();
  state.players[0].cash = 10000;
  state.assets[0] = {1, 4, false};
  state.assets[1] = {1, 4, false};
  assert(game.availableBuildings() == 16);
  assert(game.build(1, 0));
  assert(game.availableBuildings() == 20 && game.availableLandmarks() == 0);
  const auto beforeNoLandmark = state;
  assert(!game.build(1, 1));
  assertNoMutation(beforeNoLandmark, state);
  assert(!(game.actionsFor(1) & ActionBuild));
  assert(game.sellBuilding(1, 0));
  assert(game.availableBuildings() == 16 && game.availableLandmarks() == 1);
  assert(game.build(1, 0));

  // Another player occupies 17 more buildings (21 total on the board), leaving
  // only three in the bank. A landmark downgrade needs four, atomically.
  for (auto asset : {3, 4, 5}) state.assets[asset] = {2, 3, false};
  for (auto asset : {7, 8, 9}) state.assets[asset] = {2, 2, false};
  for (auto asset : {11, 12}) state.assets[asset] = {2, 1, false};
  assert(game.availableBuildings() == 3);
  const auto beforeDowngrade = state;
  assert(!game.canSellBuilding(1, 0));
  assert(!game.sellBuilding(1, 0));
  assertNoMutation(beforeDowngrade, state);
  assert(game.actionsFor(1) & ActionSellBuilding); // Explicit full-group sale is still available.
  assert(game.canLiquidateBuildingGroup(1, 0));

  assert(game.sellBuilding(2, 3));
  assert(game.availableBuildings() == 4);
  assert(game.sellBuilding(1, 0)); // Exactly four in stock is sufficient.
  assert(game.availableBuildings() == 0 && game.availableLandmarks() == 1);
  assert(game.build(1, 0));
  assert(game.build(2, 3));
  assert(game.availableBuildings() == 3 && game.availableLandmarks() == 0);

  // Failed ownership, phase and cash-range validation cannot partly liquidate.
  state.assets[1].ownerId = 2;
  const auto beforeWrongOwner = state;
  assert(!game.liquidateBuildingGroup(1, 0));
  assertNoMutation(beforeWrongOwner, state);
  state.assets[1].ownerId = 1;
  state.phase = GamePhase::AwaitCard;
  const auto beforePhase = state;
  assert(!game.liquidateBuildingGroup(1, 0));
  assertNoMutation(beforePhase, state);
  state.phase = GamePhase::AwaitRoll;
  state.players[0].cash = std::numeric_limits<std::int32_t>::max();
  const auto beforeOverflow = state;
  assert(!game.liquidateBuildingGroup(1, 0));
  assertNoMutation(beforeOverflow, state);
  state.players[0].cash = 0;
  const auto beforeSaleVersion = state.stateVersion;
  const auto beforeSaleEvents = state.nextEventSequence;
  assert(game.liquidateBuildingGroup(1, 0));
  assert(state.assets[0].buildingLevel == 0 && state.assets[1].buildingLevel == 0);
  assert(state.players[0].cash == (5 + 4) * 20);
  assert(state.stateVersion == beforeSaleVersion + 1 && state.nextEventSequence == beforeSaleEvents + 2);
  assert(game.availableBuildings() == 7 && game.availableLandmarks() == 1);
  assert(!(game.actionsFor(1) & ActionSellBuilding));

  // A bot with two landmarks and no downgrade stock remains solvent by using
  // the group sale during payment, without an impossible intermediate estate.
  GameEngine bot;
  setup(bot, ControllerKind::Bot);
  auto& botState = bot.mutableStateForRestore();
  botState.players[0].cash = 0;
  botState.assets[0] = {1, 5, false};
  botState.assets[1] = {1, 5, false};
  for (auto asset : {3, 4, 5, 7, 8, 9}) botState.assets[asset] = {2, 4, false};
  assert(bot.availableBuildings() == 0);
  assert(bot.roll(1, 1, 3)); // City fee 160; group liquidates for 200.
  assert(!botState.players[0].bankrupt && botState.players[0].cash == 40);
  assert(!botState.pendingDebt.active && botState.phase == GamePhase::TurnEnd);
  assert(botState.assets[0].buildingLevel == 0 && botState.assets[1].buildingLevel == 0);
  assert(bot.availableBuildings() == 0 && bot.availableLandmarks() == botState.board->landmarkStock);
}

inline void bankruptcyStockRecovery() {
  for (std::uint8_t creditor : {std::uint8_t{0}, std::uint8_t{2}}) {
    GameEngine game;
    setup(game);
    auto& state = game.mutableStateForRestore();
    state.players[0].cash = 1;
    state.assets[0] = {1, 5, false};
    state.assets[1] = {1, 4, false};
    state.assets[2] = {1, 0, true};
    state.phase = GamePhase::AwaitDebt;
    state.pendingDebt = {true, 1, creditor, kNoAsset, EventKind::FeePaid,
                         DebtContinuation::FinishLanding, 0, 0, 100000};
    const auto creditorCash = state.players[1].cash;
    assert(game.availableBuildings() == 20 && game.availableLandmarks() == 9);
    assert(game.declareBankruptcy(1));
    assertNextPlayer(game);
    assert(game.availableBuildings() == 24 && game.availableLandmarks() == 10);
    for (auto asset : {0, 1, 2}) {
      assert(state.assets[asset].ownerId == creditor && state.assets[asset].buildingLevel == 0);
    }
    assert(state.assets[2].mortgaged == (creditor != 0));
    assert(state.players[0].cash == 0);
    assert(state.players[1].cash == creditorCash + (creditor != 0 ? 181 : 0));
  }
}

inline void botAssetStrategyRegressions() {
  GameEngine rich;
  setup(rich, ControllerKind::Bot);
  auto& richState = rich.mutableStateForRestore();
  richState.phase = GamePhase::TurnEnd;
  richState.players[0].cash = 10000;
  richState.players[1].controller = ControllerKind::RealConsole;
  richState.assets[0].ownerId = richState.assets[1].ownerId = 1;
  const auto untouched = richState;
  assert(rich.runBots(0) == 0);
  assertNoMutation(untouched, richState);

  // One budget unit buys exactly one level; two more build evenly, and never
  // turn a single runBots action into an unbounded construction loop.
  assert(rich.runBots(1) == 1);
  assert(richState.assets[0].buildingLevel == 1 && richState.assets[1].buildingLevel == 0);
  assert(richState.phase == GamePhase::TurnEnd && richState.activePlayerId == 1);
  assert(richState.stateVersion == untouched.stateVersion + 1);
  assert(richState.players[0].cash == 9960);
  assert(rich.runBots(2) == 2);
  assert(richState.assets[0].buildingLevel == 2 && richState.assets[1].buildingLevel == 1);
  // Seven remaining levels plus EndTurn, then stop at the real-console seat.
  assert(rich.runBots(100) == 8);
  assert(richState.assets[0].buildingLevel == 5 && richState.assets[1].buildingLevel == 5);
  assert(richState.players[0].cash == 9600);
  assert(richState.activePlayerId == 2 && richState.phase == GamePhase::AwaitRoll);
  assert(eventCount(richState, EventKind::BuildingChanged, 1) == 10);
  assert(eventCount(richState, EventKind::TurnEnded, 1) == 1);
  assert(rich.availableBuildings() == 24 && rich.availableLandmarks() == 8);

  GameEngine opening;
  setup(opening, ControllerKind::Bot);
  auto& openingState = opening.mutableStateForRestore();
  openingState.assets[0].ownerId = openingState.assets[1].ownerId = 1;
  assert(opening.runBots(1) == 1);
  assert(openingState.assets[0].buildingLevel == 1);
  assert(openingState.phase == GamePhase::AwaitRoll);
  assert(eventCount(openingState, EventKind::DiceRolled, 1) == 0);

  for (const std::int32_t cash : {199, 200}) {
    GameEngine reserve;
    setup(reserve, ControllerKind::Bot);
    auto& state = reserve.mutableStateForRestore();
    state.phase = GamePhase::TurnEnd;
    state.players[0].cash = cash;
    state.players[1].controller = ControllerKind::RealConsole;
    state.assets[0].ownerId = state.assets[1].ownerId = 1;
    assert(reserve.runBots(1) == 1);
    if (cash == 199) {
      assert(state.players[0].cash == 199 && state.assets[0].buildingLevel == 0);
      assert(state.activePlayerId == 2);
    } else {
      assert(state.players[0].cash == 160 && state.assets[0].buildingLevel == 1);
      assert(state.activePlayerId == 1);
      assert(reserve.runBots(1) == 1);
      assert(state.activePlayerId == 2 && state.assets[1].buildingLevel == 0);
    }
  }

  for (const std::int32_t cash : {192, 193, 500}) {
    GameEngine redeem;
    setup(redeem, ControllerKind::Bot);
    auto& state = redeem.mutableStateForRestore();
    state.phase = GamePhase::TurnEnd;
    state.players[0].cash = cash;
    state.players[1].controller = ControllerKind::RealConsole;
    state.assets[0] = {1, 0, true};
    state.assets[1].ownerId = 1;
    assert(redeem.runBots(1) == 1);
    assert(state.assets[0].buildingLevel == 0 && state.assets[1].buildingLevel == 0);
    if (cash == 192) {
      assert(state.assets[0].mortgaged && state.players[0].cash == cash);
      assert(state.activePlayerId == 2);
    } else {
      assert(!state.assets[0].mortgaged && state.players[0].cash == cash - 33);
      assert(state.activePlayerId == 1);
      assert(eventCount(state, EventKind::AssetUnmortgaged, 1) == 1);
      assert(redeem.runBots(1) == 1);
      if (cash == 193) assert(state.activePlayerId == 2);
      else assert(state.assets[0].buildingLevel == 1); // Redemption unblocks the complete group.
    }
  }

  GameEngine empty;
  setup(empty, ControllerKind::Bot);
  auto& emptyState = empty.mutableStateForRestore();
  emptyState.phase = GamePhase::TurnEnd;
  emptyState.players[0].cash = 10000;
  emptyState.players[1].controller = ControllerKind::RealConsole;
  emptyState.assets[0].ownerId = emptyState.assets[1].ownerId = 1;
  for (auto asset : {3, 4, 5, 7, 8, 9}) emptyState.assets[asset] = {2, 4, false};
  assert(empty.availableBuildings() == 0);
  assert(empty.runBots(100) == 1); // Failed eligibility does not consume/retry the budget.
  assert(emptyState.activePlayerId == 2 && emptyState.players[0].cash == 10000);
  assert(eventCount(emptyState, EventKind::BuildingChanged, 1) == 0);

  GameEngine human;
  setup(human);
  auto& humanState = human.mutableStateForRestore();
  humanState.assets[0].ownerId = humanState.assets[1].ownerId = 1;
  const auto beforeHuman = humanState;
  assert(human.runBots(100) == 0);
  assertNoMutation(beforeHuman, humanState);

  // These ordinary four-bot games acquire their own deeds, develop complete
  // groups and reach a winner through the normal rules. The bound is only a
  // test watchdog; it does not alter the engine or force a terminal result.
  for (const std::uint32_t seed : {1u, 99u}) {
    GameEngine complete;
    assert(complete.reset(*BoardCatalog::findBySize(32), seed));
    for (unsigned i = 0; i < 4; ++i) assert(complete.addPlayer("Bot", ControllerKind::Bot));
    assert(complete.start());
    std::size_t actions = 0;
    while (complete.state().phase != GamePhase::GameOver && actions < 5000) {
      const auto version = complete.state().stateVersion;
      const auto count = complete.runBots(16);
      assert(count > 0 && count <= 16 && complete.state().stateVersion > version);
      actions += count;
      unsigned buildings = 0, landmarks = 0;
      for (std::uint8_t i = 0; i < complete.state().board->assetCount; ++i) {
        const auto level = complete.state().assets[i].buildingLevel;
        if (level == 5) ++landmarks;
        else buildings += level;
      }
      assert(buildings <= complete.state().board->buildingStock);
      assert(landmarks <= complete.state().board->landmarkStock);
    }
    assert(complete.state().phase == GamePhase::GameOver);
    unsigned alive = 0;
    for (std::uint8_t id = 1; id <= 4; ++id) {
      if (!complete.state().players[id - 1].bankrupt) {
        ++alive;
        assert(complete.state().winnerPlayerId == id);
      }
    }
    assert(alive == 1);
    const auto finished = complete.state();
    assert(complete.runBots(16) == 0);
    assertNoMutation(finished, complete.state());
  }
}

inline void run() {
  bankruptTurnRegressions();
  buildingStockRegressions();
  landmarkAndLiquidationRegressions();
  bankruptcyStockRecovery();
  botAssetStrategyRegressions();
  std::cout << "GRIDOPOLY_CORE_RULE_REGRESSIONS_PASS\n";
}

}  // namespace core_rule_regression
