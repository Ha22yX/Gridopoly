#include "StateStore.h"

#include <gridopoly/core/BoardCatalog.h>
#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::server {
namespace {

using namespace gridopoly::core;

constexpr std::uint32_t kStoreMagic = 0x31545347u;  // GST1
constexpr std::uint16_t kStoreSchema = 5;
constexpr std::uint16_t kLegacyStoreSchema = 4;

struct PersistedStateV4 {
  std::uint32_t magic{};
  std::uint16_t schema{};
  std::uint8_t boardSize{};
  std::uint8_t playerCount{};
  std::array<PlayerState, kMaxPlayers> players{};
  std::array<AssetState, kMaxAssets> assets{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t winnerPlayerId{};
  std::uint8_t phase{};
  PendingMove pendingMove{};
  PendingPurchase pendingPurchase{};
  PendingCard pendingCard{};
  PendingDebt pendingDebt{};
  AuctionState auction{};
  std::uint32_t stateVersion{};
  std::uint32_t nextEventSequence{};
  std::uint32_t nextAuctionGeneration{};
  std::uint32_t rngState{};
  std::uint32_t crc{};
};

struct PersistedState {
  std::uint32_t magic{};
  std::uint16_t schema{};
  std::uint8_t boardSize{};
  std::uint8_t playerCount{};
  std::array<PlayerState, kMaxPlayers> players{};
  std::array<AssetState, kMaxAssets> assets{};
  std::array<PlayerFinancialHistory, kMaxPlayers> financialHistory{};
  std::uint8_t financialHistoryInitialized{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t winnerPlayerId{};
  std::uint8_t phase{};
  PendingMove pendingMove{};
  PendingPurchase pendingPurchase{};
  PendingCard pendingCard{};
  PendingDebt pendingDebt{};
  AuctionState auction{};
  std::uint32_t stateVersion{};
  std::uint32_t nextEventSequence{};
  std::uint32_t nextAuctionGeneration{};
  std::uint32_t rngState{};
  std::uint32_t crc{};
};

template <typename T>
std::uint32_t stateCrc(const T& value) {
  return gridopoly::protocol::crc32(reinterpret_cast<const std::uint8_t*>(&value),
                                    sizeof(T) - sizeof(value.crc));
}

bool validFinancialHistory(const std::array<PlayerFinancialHistory, kMaxPlayers>& histories) {
  for (const auto& history : histories) {
    if (history.head >= kPlayerFinancialHistory || history.count > kPlayerFinancialHistory) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool StateStore::begin() {
  if (open_) return true;
  open_ = preferences_.begin("gridopoly", false);
  return open_;
}

std::uint32_t StateStore::loadBotActionIntervalMs(std::uint32_t fallback,
                                                  std::uint32_t minimum,
                                                  std::uint32_t maximum) {
  if (!begin()) return fallback;
  const auto stored = preferences_.getUInt("botDelay", fallback);
  return stored >= minimum && stored <= maximum ? stored : fallback;
}

bool StateStore::saveBotActionIntervalMs(std::uint32_t intervalMs) {
  return begin() && preferences_.putUInt("botDelay", intervalMs) == sizeof(intervalMs);
}

bool StateStore::restore(GameEngine& engine) {
  if (!begin()) return false;
  const auto storedLength = preferences_.getBytesLength("state");
  if (storedLength == sizeof(PersistedStateV4)) {
    PersistedStateV4 stored{};
    if (preferences_.getBytes("state", &stored, sizeof(stored)) != sizeof(stored) ||
        stored.magic != kStoreMagic || stored.schema != kLegacyStoreSchema ||
        stored.crc != stateCrc(stored)) {
      return false;
    }
    const auto* board = BoardCatalog::findBySize(stored.boardSize);
    if (board == nullptr || stored.playerCount == 0 || stored.playerCount > kMaxPlayers ||
        stored.activePlayerId > stored.playerCount ||
        stored.phase > static_cast<std::uint8_t>(GamePhase::AwaitCard)) {
      return false;
    }
    engine.reset(*board, stored.rngState);
    auto& state = engine.mutableStateForRestore();
    state.players = stored.players;
    state.assets = stored.assets;
    state.playerCount = stored.playerCount;
    state.activePlayerId = stored.activePlayerId;
    state.roundNumber = stored.roundNumber;
    state.winnerPlayerId = stored.winnerPlayerId;
    state.phase = static_cast<GamePhase>(stored.phase);
    state.pendingMove = stored.pendingMove;
    state.pendingPurchase = stored.pendingPurchase;
    state.pendingCard = stored.pendingCard;
    state.pendingDebt = stored.pendingDebt;
    state.auction = stored.auction;
    state.stateVersion = stored.stateVersion;
    state.nextEventSequence = stored.nextEventSequence == 0 ? 1 : stored.nextEventSequence;
    state.nextAuctionGeneration = stored.nextAuctionGeneration == 0 ? 1 : stored.nextAuctionGeneration;
    state.rngState = stored.rngState;
    return true;
  }
  if (storedLength != sizeof(PersistedState)) return false;
  PersistedState stored{};
  if (preferences_.getBytes("state", &stored, sizeof(stored)) != sizeof(stored) ||
      stored.magic != kStoreMagic || stored.schema != kStoreSchema || stored.crc != stateCrc(stored)) {
    return false;
  }
  const auto* board = BoardCatalog::findBySize(stored.boardSize);
  if (board == nullptr || stored.playerCount == 0 || stored.playerCount > kMaxPlayers ||
      stored.activePlayerId > stored.playerCount || stored.financialHistoryInitialized > 1 ||
      !validFinancialHistory(stored.financialHistory) ||
      stored.phase > static_cast<std::uint8_t>(GamePhase::AwaitCard)) {
    return false;
  }
  engine.reset(*board, stored.rngState);
  auto& state = engine.mutableStateForRestore();
  state.players = stored.players;
  state.assets = stored.assets;
  state.financialHistory = stored.financialHistory;
  state.financialHistoryInitialized = stored.financialHistoryInitialized != 0;
  state.playerCount = stored.playerCount;
  state.activePlayerId = stored.activePlayerId;
  state.roundNumber = stored.roundNumber;
  state.winnerPlayerId = stored.winnerPlayerId;
  state.phase = static_cast<GamePhase>(stored.phase);
  state.pendingMove = stored.pendingMove;
  state.pendingPurchase = stored.pendingPurchase;
  state.pendingCard = stored.pendingCard;
  state.pendingDebt = stored.pendingDebt;
  state.auction = stored.auction;
  state.stateVersion = stored.stateVersion;
  state.nextEventSequence = stored.nextEventSequence;
  state.nextAuctionGeneration = stored.nextAuctionGeneration == 0 ? 1 : stored.nextAuctionGeneration;
  state.rngState = stored.rngState;
  return true;
}

bool StateStore::save(const GameState& state) {
  if (!begin() || state.board == nullptr) return false;
  PersistedState stored{};
  stored.magic = kStoreMagic;
  stored.schema = kStoreSchema;
  stored.boardSize = state.board->tileCount;
  stored.playerCount = state.playerCount;
  stored.players = state.players;
  stored.assets = state.assets;
  stored.financialHistory = state.financialHistory;
  stored.financialHistoryInitialized = state.financialHistoryInitialized ? 1 : 0;
  stored.activePlayerId = state.activePlayerId;
  stored.roundNumber = state.roundNumber;
  stored.winnerPlayerId = state.winnerPlayerId;
  stored.phase = static_cast<std::uint8_t>(state.phase);
  stored.pendingMove = state.pendingMove;
  stored.pendingPurchase = state.pendingPurchase;
  stored.pendingCard = state.pendingCard;
  stored.pendingDebt = state.pendingDebt;
  stored.auction = state.auction;
  stored.stateVersion = state.stateVersion;
  stored.nextEventSequence = state.nextEventSequence;
  stored.nextAuctionGeneration = state.nextAuctionGeneration;
  stored.rngState = state.rngState;
  stored.crc = stateCrc(stored);
  return preferences_.putBytes("state", &stored, sizeof(stored)) == sizeof(stored);
}

void StateStore::clear() {
  if (begin()) preferences_.remove("state");
}

}  // namespace gridopoly::server
