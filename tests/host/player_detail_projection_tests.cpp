#include "../../Firmware/TestGameServer/src/PlayerDetailProjection.h"

#include <gridopoly/core/BoardCatalog.h>
#include <gridopoly/core/GameEngine.h>

#include <cassert>
#include <iostream>

using namespace gridopoly::core;
using namespace gridopoly::protocol;
using namespace gridopoly::server;

namespace {

void addEvent(GameState& state, EventKind kind, std::uint8_t actor, std::uint8_t target,
              std::uint8_t asset, std::int32_t amount, std::uint32_t detail = 0) {
  auto& event = state.events[state.eventHead];
  event = {state.nextEventSequence++, kind, actor, target, asset, amount, detail};
  state.eventHead = static_cast<std::uint8_t>((state.eventHead + 1) % kEventHistory);
  if (state.eventCount < kEventHistory) ++state.eventCount;
}

}  // namespace

int main() {
  GameState state{};
  state.board = BoardCatalog::findBySize(40);
  assert(state.board != nullptr);
  state.playerCount = 2;
  state.players[0].id = 1;
  state.players[0].cash = 875;
  state.players[0].position = 17;
  state.players[1].id = 2;
  state.players[1].cash = 1525;
  state.stateVersion = 44;
  state.nextEventSequence = 1;

  state.assets[0] = {1, 3, true};
  state.assets[7] = {1, 0, false};
  state.assets[12] = {2, 1, false};

  addEvent(state, EventKind::AssetPurchased, 1, 0, 0, 100);
  addEvent(state, EventKind::RentPaid, 1, 2, 12, 30);
  addEvent(state, EventKind::AssetMortgaged, 1, 0, 7, 50);
  addEvent(state, EventKind::CardApplied, 1, 0, kNoAsset, 25, 1);  // Card 1 is a debit.
  addEvent(state, EventKind::FeePaid, 1, 0, kNoAsset, 40);
  addEvent(state, EventKind::DebtPaid, 1, 0, kNoAsset, 40);  // Canonicalizes the preceding fee.
  for (std::uint8_t i = 0; i < 8; ++i) {
    addEvent(state, EventKind::PassedStart, 1, 0, kNoAsset, 200);
  }

  PlayerDetailResponse detail{};
  assert(makePlayerDetailProjection(state, 77, 1, 43, detail));
  assert(detail.requestId == 77);
  assert(detail.stateVersion == 44);
  assert(detail.cash == 875);
  assert(detail.position == 17);
  assert((detail.flags & PlayerDetailFlagRequestedVersionStale) != 0);
  assert((detail.flags & PlayerDetailFlagLedgerTruncated) != 0);
  assert(detail.assetCount == 2);
  assert(detail.totalOwnedAssets == 2);
  assert(detail.assets[0].assetIndex == 0);
  assert((detail.assets[0].state & PlayerDetailAssetBuildingMask) == 3);
  assert((detail.assets[0].state & PlayerDetailAssetMortgaged) != 0);
  assert(detail.ledgerCount == 10);
  assert(detail.ledger[0].sequence == 14);
  assert(detail.ledger[0].amount == 200);
  assert((detail.ledger[0].flags & PlayerDetailLedgerFlagCredit) != 0);
  assert(detail.ledger[8].kind == static_cast<std::uint8_t>(EventKind::DebtPaid));
  assert(detail.ledger[8].amount == -40);
  assert(detail.ledger[9].kind == static_cast<std::uint8_t>(EventKind::CardApplied));
  assert(detail.ledger[9].amount == -25);

  PlayerDetailResponse creditor{};
  assert(makePlayerDetailProjection(state, 78, 2, 44, creditor));
  assert(creditor.ledgerCount == 1);
  assert(creditor.ledger[0].kind == static_cast<std::uint8_t>(EventKind::RentPaid));
  assert(creditor.ledger[0].amount == 30);
  assert(creditor.ledger[0].counterpartyId == 1);
  assert(!makePlayerDetailProjection(state, 79, 3, 44, creditor));
  assert(!makePlayerDetailProjection(state, 0, 1, 44, creditor));

  // A schema-2/3 projection can still recover the initial bank funding before
  // the engine's first post-migration event materializes the dedicated ring.
  GameState legacyFunding{};
  legacyFunding.board = BoardCatalog::findBySize(16);
  assert(legacyFunding.board != nullptr);
  legacyFunding.playerCount = 1;
  legacyFunding.players[0].id = 1;
  legacyFunding.players[0].cash = legacyFunding.board->startingCash;
  legacyFunding.nextEventSequence = 1;
  addEvent(legacyFunding, EventKind::GameStarted, 0, 0, kNoAsset, 0);
  PlayerDetailResponse legacyFundingDetail{};
  assert(makePlayerDetailProjection(legacyFunding, 79, 1, 0, legacyFundingDetail));
  assert(legacyFundingDetail.ledgerCount == 1);
  assert(legacyFundingDetail.ledger[0].kind == static_cast<std::uint8_t>(EventKind::GameStarted));
  assert(legacyFundingDetail.ledger[0].amount == legacyFunding.board->startingCash);

  GameEngine retainedLedger;
  const auto* retainedBoard = BoardCatalog::findBySize(40);
  assert(retainedBoard != nullptr);
  assert(retainedLedger.reset(*retainedBoard, 0xC0FFEEu));
  assert(retainedLedger.addPlayer("Ledger", ControllerKind::RealConsole));
  assert(retainedLedger.start());
  for (std::uint8_t payment = 0; payment < 10; ++payment) {
    auto& retainedState = retainedLedger.mutableStateForRestore();
    retainedState.activePlayerId = 1;
    retainedState.phase = GamePhase::AwaitRoll;
    retainedState.players[0].inHold = true;
    assert(retainedLedger.payHoldFee(1));
    for (std::uint8_t filler = 0; filler < 4; ++filler) {
      addEvent(retainedState, EventKind::TurnStarted, 1, 0, kNoAsset, 0);
    }
  }
  PlayerDetailResponse retained{};
  assert(makePlayerDetailProjection(retainedLedger.state(), 80, 1,
                                    retainedLedger.state().stateVersion, retained));
  assert(retained.ledgerCount == 10);
  for (std::uint8_t entry = 0; entry < retained.ledgerCount; ++entry) {
    assert(retained.ledger[entry].kind == static_cast<std::uint8_t>(EventKind::FeePaid));
    assert(retained.ledger[entry].amount == -retainedBoard->holdReleaseFee);
  }

  GameEngine bankruptcyLedger;
  assert(bankruptcyLedger.reset(*retainedBoard, 0xBADC0DEu));
  assert(bankruptcyLedger.addPlayer("Debtor", ControllerKind::RealConsole));
  assert(bankruptcyLedger.addPlayer("Creditor", ControllerKind::RealConsole));
  assert(bankruptcyLedger.start());
  auto& bankruptcyState = bankruptcyLedger.mutableStateForRestore();
  bankruptcyState.players[0].cash = 100;
  bankruptcyState.players[1].cash = 500;
  bankruptcyState.phase = GamePhase::AwaitDebt;
  bankruptcyState.pendingDebt = {true, 1, 2, kNoAsset, EventKind::RentPaid,
                                 DebtContinuation::FinishLanding, 0, 0, 200};
  assert(bankruptcyLedger.declareBankruptcy(1));
  assert(bankruptcyLedger.state().players[0].cash == 0);
  assert(bankruptcyLedger.state().players[1].cash == 600);
  PlayerDetailResponse debtorDetail{};
  PlayerDetailResponse creditorDetail{};
  assert(makePlayerDetailProjection(bankruptcyLedger.state(), 81, 1,
                                    bankruptcyLedger.state().stateVersion, debtorDetail));
  assert(makePlayerDetailProjection(bankruptcyLedger.state(), 82, 2,
                                    bankruptcyLedger.state().stateVersion, creditorDetail));
  assert(debtorDetail.ledger[0].kind == static_cast<std::uint8_t>(EventKind::PlayerBankrupt));
  assert(debtorDetail.ledger[0].amount == -100);
  assert(debtorDetail.ledger[0].counterpartyId == 2);
  assert(creditorDetail.ledger[0].kind == static_cast<std::uint8_t>(EventKind::PlayerBankrupt));
  assert(creditorDetail.ledger[0].amount == 100);
  assert(creditorDetail.ledger[0].counterpartyId == 1);

  std::cout << "GRIDOPOLY_PLAYER_DETAIL_PROJECTION_TESTS_PASS\n";
  return 0;
}
