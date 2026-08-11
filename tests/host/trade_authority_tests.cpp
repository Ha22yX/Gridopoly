#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "../../Server/RaspberryPi/src/AuthorityService.h"

using namespace gridopoly::core;
using namespace gridopoly::protocol;

namespace {

int processId() {
#if defined(_WIN32)
  return ::_getpid();
#else
  return ::getpid();
#endif
}

TradeRequest cashOffer(std::uint32_t requestId, std::uint32_t version,
                       std::uint8_t target, std::int32_t selfCash,
                       std::int32_t targetCash) {
  TradeRequest request{};
  request.operation = TradeOperation::Create;
  request.requestId = requestId;
  request.expectedStateVersion = version;
  request.targetPlayerId = target;
  request.selfGivesCash = selfCash;
  request.counterpartyGivesCash = targetCash;
  return request;
}

}  // namespace

int main() {
  constexpr auto kTestBotInterval = std::chrono::milliseconds(120);
  const auto temporary = std::filesystem::temp_directory_path() /
      ("gridopoly-trade-authority-" + std::to_string(processId()));
  std::filesystem::remove_all(temporary);
  std::filesystem::create_directories(temporary);
  const auto statePath = temporary / "state.bin";
  const auto metadataPath = temporary / "authority.meta";

  std::uint32_t roomId = 0;
  std::uint32_t activeTradeId = 0;
  std::uint16_t activeRevision = 0;
  {
    gridopoly::pi::AuthorityService authority(statePath, metadataPath, 0x44556677u,
                                               kTestBotInterval);
    assert(authority.initialize());
    assert(authority.newGame(16, 1));
    roomId = authority.roomId();
    const auto initial = authority.stateCopy();
    assert(initial.playerCount == 2);
    assert(initial.players[0].cash == 500 && initial.players[1].cash == 500);

    TradeResponse response{};
    auto unbound = cashOffer(99, 0, 2, 100, 25);
    authority.handleTradeRequest(1, unbound, response);
    assert(response.result == TradeResultCode::InvalidRequest);
    assert(authority.stateVersion() == initial.stateVersion);

    auto unaffordable = cashOffer(98, authority.stateVersion(), 2, 501, 0);
    authority.handleTradeRequest(1, unaffordable, response);
    assert(response.result == TradeResultCode::NotEnoughCash);
    assert(authority.stateVersion() == initial.stateVersion);

    auto unavailableAsset = cashOffer(97, authority.stateVersion(), 2, 0, 0);
    unavailableAsset.selfAssetCount = 1;
    unavailableAsset.selfAssets[0] = 0;
    authority.handleTradeRequest(1, unavailableAsset, response);
    assert(response.result == TradeResultCode::AssetUnavailable);
    assert(authority.stateVersion() == initial.stateVersion);

    auto create = cashOffer(1, authority.stateVersion(), 2, 100, 25);
    authority.handleTradeRequest(1, create, response);
    assert(response.result == TradeResultCode::Ok);
    assert(response.status == TradeStatus::Offered);
    assert(response.revision == 1 && response.tradeId != 0);
    assert((response.flags & TradeResponseFlagSelfConfirmed) != 0);

    // A stale global version cannot mutate a draft even when tradeId/revision
    // are otherwise correct.
    TradeRequest stale{};
    stale.operation = TradeOperation::Confirm;
    stale.requestId = 2;
    stale.expectedStateVersion = create.expectedStateVersion;
    stale.tradeId = response.tradeId;
    stale.expectedRevision = response.revision;
    stale.targetPlayerId = 1;
    TradeResponse staleResponse{};
    authority.handleTradeRequest(2, stale, staleResponse);
    assert(staleResponse.result == TradeResultCode::StateVersionStale);
    assert((staleResponse.flags & TradeResponseFlagRequestedVersionStale) != 0);

    stale.requestId = 3;
    stale.expectedStateVersion = authority.stateVersion();
    TradeResponse settled{};
    authority.handleTradeRequest(2, stale, settled);
    assert(settled.result == TradeResultCode::Ok);
    assert(settled.status == TradeStatus::Settled);
    const auto after = authority.stateCopy();
    assert(after.players[0].cash == 425);
    assert(after.players[1].cash == 575);

    // No active trade is returned after settlement.
    TradeRequest query{};
    query.operation = TradeOperation::Query;
    query.requestId = 4;
    query.expectedStateVersion = authority.stateVersion();
    TradeResponse noTrade{};
    authority.handleTradeRequest(1, query, noTrade);
    assert(noTrade.result == TradeResultCode::NoActiveTrade);

    // The authority tick lets bot participants answer through the same
    // revisioned workflow. This favorable one-way cash offer is accepted and
    // settled without requiring a second console session.
    auto automated = cashOffer(5, authority.stateVersion(), 2, 10, 0);
    TradeResponse botOffer{};
    authority.handleTradeRequest(1, automated, botOffer);
    assert(botOffer.result == TradeResultCode::Ok);
    const auto beforeBotDecision = authority.stateVersion();
    authority.tick();
    assert(authority.stateVersion() == beforeBotDecision);
    std::this_thread::sleep_for(kTestBotInterval + std::chrono::milliseconds(30));
    authority.tick();
    query.requestId = 6;
    query.expectedStateVersion = authority.stateVersion();
    authority.handleTradeRequest(1, query, noTrade);
    assert(noTrade.result == TradeResultCode::NoActiveTrade);
    const auto afterBotTrade = authority.stateCopy();
    assert(afterBotTrade.players[0].cash == 415);
    assert(afterBotTrade.players[1].cash == 585);

    // Persist a bot counteroffer and verify both the recovery projection and
    // the one-counter-only guard survive a service restart.
    auto persistent = cashOffer(7, authority.stateVersion(), 2, 0, 100);
    TradeResponse opened{};
    authority.handleTradeRequest(1, persistent, opened);
    assert(opened.result == TradeResultCode::Ok);
    std::this_thread::sleep_for(kTestBotInterval + std::chrono::milliseconds(30));
    authority.tick();
    TradeResponse botCounter{};
    assert(authority.makeTradeResync(1, botCounter));
    assert(botCounter.status == TradeStatus::Countered);
    assert(botCounter.revision == 2);
    assert(botCounter.selfGivesCash == 100);
    assert(botCounter.counterpartyGivesCash == 100);
    activeTradeId = botCounter.tradeId;
    activeRevision = botCounter.revision;
    assert(authority.flush());
  }

  {
    gridopoly::pi::AuthorityService restored(statePath, metadataPath, 0,
                                              kTestBotInterval);
    assert(restored.initialize());
    assert(restored.roomId() == roomId);
    TradeResponse resync{};
    assert(restored.makeTradeResync(1, resync));
    assert(resync.requestId == 0);
    assert((resync.flags & TradeResponseFlagResync) != 0);
    assert(resync.tradeId == activeTradeId);
    assert(resync.revision == activeRevision);
    assert(resync.status == TradeStatus::Countered);
    assert(resync.selfGivesCash == 100);
    assert(resync.counterpartyGivesCash == 100);

    TradeRequest secondUnfair{};
    secondUnfair.operation = TradeOperation::Update;
    secondUnfair.requestId = 8;
    secondUnfair.expectedStateVersion = restored.stateVersion();
    secondUnfair.tradeId = activeTradeId;
    secondUnfair.expectedRevision = activeRevision;
    secondUnfair.targetPlayerId = 2;
    secondUnfair.counterpartyGivesCash = 100;
    TradeResponse humanCounter{};
    restored.handleTradeRequest(1, secondUnfair, humanCounter);
    assert(humanCounter.result == TradeResultCode::Ok);
    assert(humanCounter.revision == 3);
    std::this_thread::sleep_for(kTestBotInterval + std::chrono::milliseconds(30));
    restored.tick();
    assert(restored.makeTradeResync(1, resync));
    assert(resync.requestId == 0);
    assert(resync.result == TradeResultCode::NoActiveTrade);
    assert(resync.status == TradeStatus::None);
    assert((resync.flags & TradeResponseFlagResync) != 0);
  }

  std::filesystem::remove_all(temporary);
  std::cout << "GRIDOPOLY_TRADE_AUTHORITY_TESTS_PASS\n";
  return 0;
}
