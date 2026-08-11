#include <gridopoly/core/BoardCatalog.h>
#include <gridopoly/core/GameEngine.h>

#include <cassert>
#include <cstring>
#include <iostream>

using namespace gridopoly::core;

int main() {
  assert(BoardCatalog::count() == 4);
  assert(BoardCatalog::findBySize(16)->assetCount == 9);
  assert(BoardCatalog::findBySize(24)->assetCount == 16);
  assert(BoardCatalog::findBySize(32)->assetCount == 23);
  assert(BoardCatalog::findBySize(40)->assetCount == 28);
  assert(std::strcmp(BoardCatalog::findBySize(40)->id, "grid-city-40-v1") == 0);

  GameEngine game;
  assert(game.reset(*BoardCatalog::findBySize(16), 12345));
  assert(game.addPlayer("Human", ControllerKind::RealConsole));
  assert(game.addPlayer("Guest", ControllerKind::RealConsole));
  assert(game.start());
  assert(game.actionsFor(1) & ActionRoll);
  const auto versionBeforeAutomation = game.state().stateVersion;
  assert(game.runBots(8) == 0);
  assert(game.state().stateVersion == versionBeforeAutomation);

  assert(game.roll(1, 1, 2));
  assert(game.state().phase == GamePhase::AwaitMoveConfirm);
  assert(game.confirmPosition(1, 3));
  assert(game.state().phase == GamePhase::AwaitPurchase);
  const auto purchasePrice = game.state().board->assets[1].economy.price;
  assert(game.buy(1));
  assert(game.state().assets[1].ownerId == 1);
  assert(game.state().players[0].cash == 500 - purchasePrice);
  assert(game.endTurn(1));

  assert(game.roll(2, 1, 2));
  assert(game.confirmPosition(2, 3));
  assert(game.state().phase == GamePhase::AwaitDebt);
  assert(game.state().pendingDebt.active);
  assert(game.state().pendingDebt.debtorId == 2);
  assert(game.state().pendingDebt.creditorId == 1);
  assert(game.state().pendingDebt.amount == 3);
  assert(game.state().players[1].cash == 500);
  auto& debtState = game.mutableStateForRestore();
  debtState.players[1].cash = 0;
  debtState.assets[0].ownerId = 2;
  assert(!(game.actionsFor(2) & ActionPayDebt));
  assert(game.actionsFor(2) & ActionMortgage);
  assert(!(game.actionsFor(2) & ActionTrade));
  assert(game.runBots(8) == 0);
  assert(!game.payDebt(2));
  assert(game.mortgage(2, 0));
  assert(game.state().assets[0].mortgaged);
  assert(game.actionsFor(2) & ActionPayDebt);
  assert(game.payDebt(2));
  assert(game.state().players[1].cash == 50 - 3);
  assert(game.state().players[0].cash == 500 - purchasePrice + 3);

  GameEngine auction;
  assert(auction.reset(*BoardCatalog::findBySize(16), 777));
  assert(auction.addPlayer("Console A", ControllerKind::RealConsole));
  assert(auction.addPlayer("Console B", ControllerKind::RealConsole));
  assert(auction.start());
  assert(auction.roll(1, 1, 2));
  assert(auction.confirmPosition(1, 3));
  assert(auction.declinePurchase(1));
  assert(auction.state().phase == GamePhase::AwaitAuction);
  const auto auctionGeneration = auction.state().auction.generation;
  assert(auctionGeneration != 0);
  assert(auction.state().auction.requiredReadyMask == 0x03);
  assert(auction.state().auction.readyMask == 0x00);
  assert(auction.decisionPlayerId() == 0);
  assert(auction.actionsFor(1) == ActionAuctionReady);
  assert(auction.actionsFor(2) == ActionAuctionReady);
  assert(auction.runBots(8) == 0);
  assert(!auction.auctionBid(1, 10));
  assert(!auction.auctionPass(1));
  assert(!auction.auctionReady(1, 2, auctionGeneration));
  assert(!auction.auctionReady(1, 1, auctionGeneration + 1));
  assert(auction.auctionReady(1, 1, auctionGeneration));
  const auto versionAfterFirstReady = auction.state().stateVersion;
  assert(auction.auctionReady(1, 1, auctionGeneration));
  assert(auction.state().stateVersion == versionAfterFirstReady);
  assert(auction.state().auction.readyMask == 0x01);
  assert(auction.decisionPlayerId() == 0);
  assert(auction.auctionReady(2, 1, auctionGeneration));
  assert(auction.state().auction.readyMask == 0x03);
  assert(auction.decisionPlayerId() == 1);
  const auto versionAfterOpening = auction.state().stateVersion;
  assert(auction.auctionReady(2, 1, auctionGeneration));
  assert(auction.state().stateVersion == versionAfterOpening);
  assert(auction.decisionPlayerId() == 1);
  assert(auction.auctionPass(1));
  assert(auction.decisionPlayerId() == 2);
  assert(auction.auctionBid(2, 10));
  assert(auction.state().phase == GamePhase::TurnEnd);
  assert(auction.state().assets[1].ownerId == 2);
  assert(auction.state().players[1].cash == 490);

  GameEngine mixedAuction;
  assert(mixedAuction.reset(*BoardCatalog::findBySize(16), 778));
  assert(mixedAuction.addPlayer("Console", ControllerKind::RealConsole));
  assert(mixedAuction.addPlayer("Bot", ControllerKind::Bot));
  assert(mixedAuction.start());
  assert(mixedAuction.roll(1, 1, 2));
  assert(mixedAuction.confirmPosition(1, 3));
  assert(mixedAuction.declinePurchase(1));
  assert(mixedAuction.state().auction.requiredReadyMask == 0x03);
  assert(mixedAuction.state().auction.readyMask == 0x02);
  assert(mixedAuction.decisionPlayerId() == 0);
  assert(mixedAuction.runBots(8) == 0);
  assert(mixedAuction.auctionReady(1, 1, mixedAuction.state().auction.generation));
  assert(mixedAuction.decisionPlayerId() == 1);
  assert(mixedAuction.auctionPass(1));
  assert(mixedAuction.decisionPlayerId() == 2);
  assert(mixedAuction.runBots(1) == 1);
  assert(mixedAuction.state().phase == GamePhase::TurnEnd);

  GameEngine webAuction;
  assert(webAuction.reset(*BoardCatalog::findBySize(16), 779));
  assert(webAuction.addPlayer("Console", ControllerKind::RealConsole));
  assert(webAuction.addPlayer("Web", ControllerKind::Web));
  assert(webAuction.start());
  assert(webAuction.roll(1, 1, 2));
  assert(webAuction.confirmPosition(1, 3));
  assert(webAuction.declinePurchase(1));
  assert(webAuction.state().auction.requiredReadyMask == 0x03);
  assert(webAuction.state().auction.readyMask == 0x02);
  assert(webAuction.decisionPlayerId() == 0);

  // Voluntary trades are revisioned workflows. Creation confirms only the
  // editor's exact revision; the other participant's confirm performs one
  // atomic cash+multi-asset swap.
  GameEngine trades;
  assert(trades.reset(*BoardCatalog::findBySize(16), 880));
  assert(trades.addPlayer("Trader A", ControllerKind::RealConsole));
  assert(trades.addPlayer("Trader B", ControllerKind::RealConsole));
  assert(trades.start());
  auto& tradeState = trades.mutableStateForRestore();
  tradeState.assets[0].ownerId = 1;
  tradeState.assets[1].ownerId = 2;
  TradeOfferSide aGives{};
  TradeOfferSide bGives{};
  aGives.cash = 100;
  aGives.assetCount = 1;
  aGives.assets[0] = 0;
  bGives.cash = 25;
  bGives.assetCount = 1;
  bGives.assets[0] = 1;
  TradeWorkflow offer{};
  assert(trades.createTrade(1, 2, aGives, bGives, 1000, offer));
  assert(offer.tradeId != 0 && offer.revision == 1);
  assert(offer.status == TradeWorkflowStatus::Offered);
  assert(offer.confirmedMask == 0x01);
  const auto offeredVersion = trades.state().stateVersion;
  TradeWorkflow idempotentConfirm{};
  assert(trades.confirmTrade(1, offer.tradeId, offer.revision, 1050, idempotentConfirm));
  assert(idempotentConfirm.status == TradeWorkflowStatus::Offered);
  assert(trades.state().stateVersion == offeredVersion);
  TradeWorkflow busy{};
  assert(!trades.createTrade(1, 2, aGives, bGives, 1001, busy));
  TradeWorkflow settled{};
  assert(trades.confirmTrade(2, offer.tradeId, 1, 1100, settled));
  assert(settled.status == TradeWorkflowStatus::Settled);
  assert(trades.state().players[0].cash == 425);
  assert(trades.state().players[1].cash == 575);
  assert(trades.state().assets[0].ownerId == 2);
  assert(trades.state().assets[1].ownerId == 1);

  // A counter offer reverses the actor-relative wire sides into the stable
  // proposer orientation, increments revision, and clears the old confirm.
  tradeState.assets[2].ownerId = 1;
  tradeState.assets[3].ownerId = 2;
  aGives = {};
  bGives = {};
  aGives.assetCount = 1;
  aGives.assets[0] = 2;
  aGives.cash = 10;
  assert(trades.createTrade(1, 2, aGives, bGives, 2000, offer));
  TradeOfferSide counterSelf{};
  TradeOfferSide counterOther{};
  counterSelf.assetCount = 1;
  counterSelf.assets[0] = 3;
  counterSelf.cash = 20;
  counterOther.assetCount = 1;
  counterOther.assets[0] = 2;
  TradeWorkflow countered{};
  assert(trades.updateTrade(2, offer.tradeId, offer.revision, counterSelf, counterOther,
                            2100, countered));
  assert(countered.revision == 2);
  assert(countered.lastEditorId == 2);
  assert(countered.confirmedMask == 0x02);
  assert(countered.proposerGives.assets[0] == 2);
  assert(countered.counterpartyGives.assets[0] == 3);
  assert(!trades.confirmTrade(1, countered.tradeId, 1, 2200, settled));
  assert(trades.confirmTrade(1, countered.tradeId, 2, 2200, settled));
  assert(settled.status == TradeWorkflowStatus::Settled);
  assert(trades.state().assets[2].ownerId == 2);
  assert(trades.state().assets[3].ownerId == 1);

  // Reject is available to the receiver, cancel to the current editor, and
  // deadlines close abandoned offers without any economic side effect.
  tradeState.assets[4].ownerId = 1;
  aGives = {};
  bGives = {};
  aGives.assetCount = 1;
  aGives.assets[0] = 4;
  assert(trades.createTrade(1, 2, aGives, bGives, 3000, offer));
  assert(!trades.cancelTrade(2, offer.tradeId, offer.revision, settled));
  assert(trades.rejectTrade(2, offer.tradeId, offer.revision, settled));
  assert(settled.status == TradeWorkflowStatus::Rejected);
  assert(trades.state().assets[4].ownerId == 1);
  assert(trades.createTrade(1, 2, aGives, bGives, 4000, offer));
  assert(trades.cancelTrade(1, offer.tradeId, offer.revision, settled));
  assert(settled.status == TradeWorkflowStatus::Cancelled);
  assert(trades.createTrade(1, 2, aGives, bGives, 5000, offer));
  assert(trades.expireTrades(125001) == 1);
  assert(!trades.tradeForPlayer(1, busy));
  assert(trades.state().assets[4].ownerId == 1);

  // Mortgage/building safeguards are validated again at final confirmation.
  assert(trades.createTrade(1, 2, aGives, bGives, 130000, offer));
  tradeState.assets[4].mortgaged = true;
  assert(!trades.confirmTrade(2, offer.tradeId, offer.revision, 130100, settled));
  assert(settled.status == TradeWorkflowStatus::Invalidated);
  assert(trades.state().assets[4].ownerId == 1);

  // Bots process incoming trade revisions independently of their turn. A
  // non-losing offer is accepted, while an unfavorable offer is countered at
  // most once and then rejected if the human makes it unfavorable again.
  GameEngine botTrades;
  assert(botTrades.reset(*BoardCatalog::findBySize(16), 881));
  assert(botTrades.addPlayer("Human", ControllerKind::RealConsole));
  assert(botTrades.addPlayer("Bot", ControllerKind::Bot));
  assert(botTrades.start());
  auto& botTradeState = botTrades.mutableStateForRestore();
  botTradeState.assets[0].ownerId = 1;
  TradeOfferSide humanGives{};
  TradeOfferSide botGives{};
  humanGives.assetCount = 1;
  humanGives.assets[0] = 0;
  assert(botTrades.createTrade(1, 2, humanGives, botGives, 200000, offer));
  assert(botTrades.runTradeBots(200100, 1) == 1);
  assert(!botTrades.tradeForPlayer(1, busy));
  assert(botTrades.state().assets[0].ownerId == 2);

  botTradeState.assets[1].ownerId = 1;
  humanGives = {};
  botGives = {};
  humanGives.assetCount = 1;
  humanGives.assets[0] = 1;
  const auto assetValue = botTradeState.board->assets[1].economy.price;
  botGives.cash = assetValue + 100;
  assert(botTrades.createTrade(1, 2, humanGives, botGives, 201000, offer));
  assert(botTrades.runTradeBots(201100, 1) == 1);
  assert(botTrades.tradeForPlayer(1, countered));
  assert(countered.status == TradeWorkflowStatus::Countered);
  assert(countered.revision == 2);
  assert(countered.lastEditorId == 2);
  assert(countered.confirmedMask == 0x02);
  assert(countered.botCounteredMask == 0x02);
  assert(countered.proposerGives.cash == 100);

  humanGives.assetCount = 1;
  humanGives.assets[0] = 1;
  humanGives.cash = 0;
  botGives.cash = assetValue + 100;
  assert(botTrades.updateTrade(1, countered.tradeId, countered.revision,
                               humanGives, botGives, 201200, offer));
  assert(offer.botCounteredMask == 0x02);
  assert(botTrades.runTradeBots(201300, 1) == 1);
  assert(!botTrades.tradeForPlayer(1, busy));
  assert(botTrades.state().assets[1].ownerId == 1);

  // Six-player games can hold three independent drafts concurrently, while
  // the one-active-trade-per-player invariant remains enforced.
  GameEngine concurrentTrades;
  assert(concurrentTrades.reset(*BoardCatalog::findBySize(40), 882));
  for (int index = 0; index < 6; ++index) {
    assert(concurrentTrades.addPlayer("Trader", ControllerKind::RealConsole));
  }
  assert(concurrentTrades.start());
  TradeOfferSide smallCash{};
  smallCash.cash = 10;
  TradeOfferSide nothing{};
  TradeWorkflow pairOne{};
  TradeWorkflow pairTwo{};
  TradeWorkflow pairThree{};
  assert(concurrentTrades.createTrade(1, 2, smallCash, nothing, 300000, pairOne));
  assert(concurrentTrades.createTrade(3, 4, smallCash, nothing, 300001, pairTwo));
  assert(concurrentTrades.createTrade(5, 6, smallCash, nothing, 300002, pairThree));
  assert(pairOne.tradeId != pairTwo.tradeId && pairTwo.tradeId != pairThree.tradeId);
  assert(!concurrentTrades.createTrade(1, 3, smallCash, nothing, 300003, busy));

  // A real console must see the canonical draw before any payment/debt side
  // effect. Continuing the exact card instance then opens the debt, and the
  // effect-applied event is emitted only after payment completes.
  GameEngine cardGame;
  bool foundPaymentCard = false;
  for (std::uint32_t seed = 1; seed < 1000 && !foundPaymentCard; ++seed) {
    assert(cardGame.reset(*BoardCatalog::findBySize(16), seed));
    assert(cardGame.addPlayer("Card Player", ControllerKind::RealConsole));
    assert(cardGame.start());
    assert(cardGame.roll(1, 1, 1));
    assert(cardGame.confirmPosition(1, 2));
    foundPaymentCard = cardGame.state().pendingCard.active &&
        cardGame.state().pendingCard.cardIndex == 1;
  }
  assert(foundPaymentCard);
  const auto cashBeforeCard = cardGame.state().players[0].cash;
  const auto drawn = cardGame.state().pendingCard;
  assert(cardGame.state().phase == GamePhase::AwaitCard);
  assert(drawn.stage == PendingCardStage::AwaitContinue);
  assert(drawn.deckId == 2);
  assert(drawn.cardCatalogId == 10);
  assert(drawn.effectId == 10);
  assert(drawn.displayAmount == -25);
  assert(cardGame.state().players[0].cash == cashBeforeCard);
  assert(!cardGame.state().pendingDebt.active);
  assert(cardGame.actionsFor(1) == ActionCardContinue);
  const auto drawSequence = drawn.drawEventSequence;
  assert(!cardGame.continueCard(1, static_cast<std::uint16_t>(drawn.cardInstanceId + 1)));
  assert(cardGame.continueCard(1, drawn.cardInstanceId));
  assert(cardGame.state().phase == GamePhase::AwaitDebt);
  assert(cardGame.state().pendingCard.active);
  assert(cardGame.state().pendingCard.stage == PendingCardStage::AwaitSettlement);
  assert(cardGame.state().pendingDebt.active);
  assert(cardGame.state().pendingDebt.paymentEvent == EventKind::CardApplied);
  bool sawDebtOpened = false;
  bool sawEarlyApplied = false;
  for (const auto& event : cardGame.state().events) {
    if (event.sequence > drawSequence && event.kind == EventKind::DebtOpened) sawDebtOpened = true;
    if (event.sequence > drawSequence && event.kind == EventKind::CardApplied) sawEarlyApplied = true;
  }
  assert(sawDebtOpened);
  assert(!sawEarlyApplied);
  assert(cardGame.payDebt(1));
  assert(!cardGame.state().pendingCard.active);
  assert(!cardGame.state().pendingDebt.active);
  assert(cardGame.state().players[0].cash == cashBeforeCard - 25);
  std::uint32_t appliedSequence = 0;
  for (const auto& event : cardGame.state().events) {
    if (event.kind == EventKind::CardApplied) {
      appliedSequence = event.sequence;
      assert(cardEventInstanceId(event.detail) == drawn.cardInstanceId);
      assert(cardEventDeckId(event.detail) == 2);
      assert(cardEventCardIndex(event.detail) == 1);
      assert(cardEventOutcome(event.detail) == CardEffectOutcome::Applied);
    }
  }
  assert(appliedSequence > drawSequence);

  // Bots acknowledge the reveal barrier internally; no UI-only phase stalls
  // automated seats.
  GameEngine botCard;
  assert(botCard.reset(*BoardCatalog::findBySize(16), 123));
  assert(botCard.addPlayer("Bot", ControllerKind::Bot));
  assert(botCard.start());
  assert(botCard.roll(1, 1, 1));
  assert(botCard.state().phase != GamePhase::AwaitCard);

  // When a bot cannot cover a payment card, the financial ledger records the
  // cash that actually left the player rather than the larger face value.
  GameEngine insolventBotCard;
  bool foundInsolventPaymentCard = false;
  for (std::uint32_t seed = 1; seed < 1000 && !foundInsolventPaymentCard; ++seed) {
    assert(insolventBotCard.reset(*BoardCatalog::findBySize(16), seed));
    assert(insolventBotCard.addPlayer("Insolvent Bot", ControllerKind::Bot));
    assert(insolventBotCard.start());
    insolventBotCard.mutableStateForRestore().players[0].cash = 10;
    assert(insolventBotCard.roll(1, 1, 1));
    foundInsolventPaymentCard = insolventBotCard.state().players[0].bankrupt;
  }
  assert(foundInsolventPaymentCard);
  const auto& insolventHistory = insolventBotCard.state().financialHistory[0];
  assert(insolventHistory.count >= 2);
  const auto latestFinancialIndex = static_cast<std::uint8_t>(
      (insolventHistory.head + kPlayerFinancialHistory - 1) % kPlayerFinancialHistory);
  assert(insolventHistory.entries[latestFinancialIndex].kind == EventKind::CardApplied);
  assert(insolventHistory.entries[latestFinancialIndex].amount == -10);

  std::cout << "GRIDOPOLY_CORE_TESTS_PASS\n";
  return 0;
}
