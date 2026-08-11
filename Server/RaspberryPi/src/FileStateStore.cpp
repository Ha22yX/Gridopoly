#include "FileStateStore.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <system_error>

#include <gridopoly/core/BoardCatalog.h>
#include <gridopoly/protocol/Protocol.h>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gridopoly::pi {
namespace {

using namespace gridopoly::core;

constexpr std::uint32_t kStoreMagic = 0x32545347u;  // GST2
constexpr std::uint16_t kStoreSchema = 4;
constexpr std::uint16_t kTradeStoreSchema = 3;
constexpr std::uint16_t kLegacyStoreSchema = 2;

struct PersistedStateV2 {
  std::uint32_t magic{};
  std::uint16_t schema{};
  std::uint8_t boardSize{};
  std::uint8_t playerCount{};
  std::array<PlayerState, kMaxPlayers> players{};
  std::array<AssetState, kMaxAssets> assets{};
  std::array<GameEvent, kEventHistory> events{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t eventHead{};
  std::uint8_t eventCount{};
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

struct PersistedStateV3 {
  std::uint32_t magic{};
  std::uint16_t schema{};
  std::uint8_t boardSize{};
  std::uint8_t playerCount{};
  std::array<PlayerState, kMaxPlayers> players{};
  std::array<AssetState, kMaxAssets> assets{};
  std::array<GameEvent, kEventHistory> events{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t eventHead{};
  std::uint8_t eventCount{};
  std::uint8_t winnerPlayerId{};
  std::uint8_t phase{};
  PendingMove pendingMove{};
  PendingPurchase pendingPurchase{};
  PendingCard pendingCard{};
  PendingDebt pendingDebt{};
  AuctionState auction{};
  std::array<TradeWorkflow, kMaxConcurrentTrades> trades{};
  std::uint32_t stateVersion{};
  std::uint32_t nextEventSequence{};
  std::uint32_t nextAuctionGeneration{};
  std::uint32_t nextTradeId{};
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
  std::array<GameEvent, kEventHistory> events{};
  std::array<PlayerFinancialHistory, kMaxPlayers> financialHistory{};
  std::uint8_t financialHistoryInitialized{};
  std::uint8_t activePlayerId{};
  std::uint8_t roundNumber{};
  std::uint8_t eventHead{};
  std::uint8_t eventCount{};
  std::uint8_t winnerPlayerId{};
  std::uint8_t phase{};
  PendingMove pendingMove{};
  PendingPurchase pendingPurchase{};
  PendingCard pendingCard{};
  PendingDebt pendingDebt{};
  AuctionState auction{};
  std::array<TradeWorkflow, kMaxConcurrentTrades> trades{};
  std::uint32_t stateVersion{};
  std::uint32_t nextEventSequence{};
  std::uint32_t nextAuctionGeneration{};
  std::uint32_t nextTradeId{};
  std::uint32_t rngState{};
  std::uint32_t crc{};
};

template <typename T>
std::uint32_t stateCrc(const T& value) {
  return gridopoly::protocol::crc32(reinterpret_cast<const std::uint8_t*>(&value),
                                    sizeof(T) - sizeof(value.crc));
}

bool writeAtomically(const std::filesystem::path& path, const void* data, std::size_t length) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
  }
  auto temporary = path;
  temporary += ".tmp";
#if defined(__linux__)
  const int descriptor = ::open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
  if (descriptor < 0) return false;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::size_t written = 0;
  while (written < length) {
    const auto result = ::write(descriptor, bytes + written, length - written);
    if (result <= 0) {
      ::close(descriptor);
      std::filesystem::remove(temporary, error);
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  const bool synchronized = ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  if (!synchronized || !closed) {
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(static_cast<const char*>(data), static_cast<std::streamsize>(length));
  output.flush();
  if (!output) return false;
  output.close();
#endif
  std::filesystem::rename(temporary, path, error);
  if (!error) return true;
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  return !error;
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

FileStateStore::FileStateStore(std::filesystem::path path) : path_(std::move(path)) {}

bool FileStateStore::restore(GameEngine& engine) const {
  std::ifstream input(path_, std::ios::binary);
  if (!input) return false;
  input.seekg(0, std::ios::end);
  const auto fileSize = input.tellg();
  input.seekg(0, std::ios::beg);
  if (fileSize == static_cast<std::streamoff>(sizeof(PersistedStateV2))) {
    PersistedStateV2 stored{};
    input.read(reinterpret_cast<char*>(&stored), sizeof(stored));
    if (!input || stored.magic != kStoreMagic || stored.schema != kLegacyStoreSchema ||
        stored.crc != stateCrc(stored)) return false;
    const auto* board = BoardCatalog::findBySize(stored.boardSize);
    if (board == nullptr || stored.playerCount == 0 || stored.playerCount > kMaxPlayers ||
        stored.activePlayerId > stored.playerCount || stored.eventHead >= kEventHistory ||
        stored.eventCount > kEventHistory ||
        stored.phase > static_cast<std::uint8_t>(GamePhase::AwaitCard)) return false;
    engine.reset(*board, stored.rngState);
    auto& state = engine.mutableStateForRestore();
    state.players = stored.players;
    state.assets = stored.assets;
    state.events = stored.events;
    state.playerCount = stored.playerCount;
    state.activePlayerId = stored.activePlayerId;
    state.roundNumber = stored.roundNumber;
    state.eventHead = stored.eventHead;
    state.eventCount = stored.eventCount;
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
    state.nextTradeId = 1;
    state.rngState = stored.rngState;
    return true;
  }
  if (fileSize == static_cast<std::streamoff>(sizeof(PersistedStateV3))) {
    PersistedStateV3 stored{};
    input.read(reinterpret_cast<char*>(&stored), sizeof(stored));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(stored)) || input.peek() != EOF ||
        stored.magic != kStoreMagic || stored.schema != kTradeStoreSchema ||
        stored.crc != stateCrc(stored)) {
      return false;
    }
    const auto* board = BoardCatalog::findBySize(stored.boardSize);
    if (board == nullptr || stored.playerCount == 0 || stored.playerCount > kMaxPlayers ||
        stored.activePlayerId > stored.playerCount || stored.eventHead >= kEventHistory ||
        stored.eventCount > kEventHistory ||
        stored.phase > static_cast<std::uint8_t>(GamePhase::AwaitCard)) {
      return false;
    }
    engine.reset(*board, stored.rngState);
    auto& state = engine.mutableStateForRestore();
    state.players = stored.players;
    state.assets = stored.assets;
    state.events = stored.events;
    state.playerCount = stored.playerCount;
    state.activePlayerId = stored.activePlayerId;
    state.roundNumber = stored.roundNumber;
    state.eventHead = stored.eventHead;
    state.eventCount = stored.eventCount;
    state.winnerPlayerId = stored.winnerPlayerId;
    state.phase = static_cast<GamePhase>(stored.phase);
    state.pendingMove = stored.pendingMove;
    state.pendingPurchase = stored.pendingPurchase;
    state.pendingCard = stored.pendingCard;
    state.pendingDebt = stored.pendingDebt;
    state.auction = stored.auction;
    state.trades = stored.trades;
    state.stateVersion = stored.stateVersion;
    state.nextEventSequence = stored.nextEventSequence == 0 ? 1 : stored.nextEventSequence;
    state.nextAuctionGeneration = stored.nextAuctionGeneration == 0 ? 1 : stored.nextAuctionGeneration;
    state.nextTradeId = stored.nextTradeId == 0 ? 1 : stored.nextTradeId;
    state.rngState = stored.rngState;
    return true;
  }
  if (fileSize != static_cast<std::streamoff>(sizeof(PersistedState))) return false;
  PersistedState stored{};
  input.read(reinterpret_cast<char*>(&stored), sizeof(stored));
  if (input.gcount() != static_cast<std::streamsize>(sizeof(stored)) || input.peek() != EOF ||
      stored.magic != kStoreMagic || stored.schema != kStoreSchema || stored.crc != stateCrc(stored)) {
    return false;
  }
  const auto* board = BoardCatalog::findBySize(stored.boardSize);
  if (board == nullptr || stored.playerCount == 0 || stored.playerCount > kMaxPlayers ||
      stored.activePlayerId > stored.playerCount || stored.eventHead >= kEventHistory ||
      stored.eventCount > kEventHistory ||
      stored.financialHistoryInitialized > 1 || !validFinancialHistory(stored.financialHistory) ||
      stored.phase > static_cast<std::uint8_t>(GamePhase::AwaitCard)) {
    return false;
  }
  engine.reset(*board, stored.rngState);
  auto& state = engine.mutableStateForRestore();
  state.players = stored.players;
  state.assets = stored.assets;
  state.events = stored.events;
  state.financialHistory = stored.financialHistory;
  state.financialHistoryInitialized = stored.financialHistoryInitialized != 0;
  state.playerCount = stored.playerCount;
  state.activePlayerId = stored.activePlayerId;
  state.roundNumber = stored.roundNumber;
  state.eventHead = stored.eventHead;
  state.eventCount = stored.eventCount;
  state.winnerPlayerId = stored.winnerPlayerId;
  state.phase = static_cast<GamePhase>(stored.phase);
  state.pendingMove = stored.pendingMove;
  state.pendingPurchase = stored.pendingPurchase;
  state.pendingCard = stored.pendingCard;
  state.pendingDebt = stored.pendingDebt;
  state.auction = stored.auction;
  state.trades = stored.trades;
  state.stateVersion = stored.stateVersion;
  state.nextEventSequence = stored.nextEventSequence == 0 ? 1 : stored.nextEventSequence;
  state.nextAuctionGeneration = stored.nextAuctionGeneration == 0 ? 1 : stored.nextAuctionGeneration;
  state.nextTradeId = stored.nextTradeId == 0 ? 1 : stored.nextTradeId;
  state.rngState = stored.rngState;
  return true;
}

bool FileStateStore::save(const GameState& state) const {
  if (state.board == nullptr) return false;
  PersistedState stored{};
  stored.magic = kStoreMagic;
  stored.schema = kStoreSchema;
  stored.boardSize = state.board->tileCount;
  stored.playerCount = state.playerCount;
  stored.players = state.players;
  stored.assets = state.assets;
  stored.events = state.events;
  stored.financialHistory = state.financialHistory;
  stored.financialHistoryInitialized = state.financialHistoryInitialized ? 1 : 0;
  stored.activePlayerId = state.activePlayerId;
  stored.roundNumber = state.roundNumber;
  stored.eventHead = state.eventHead;
  stored.eventCount = state.eventCount;
  stored.winnerPlayerId = state.winnerPlayerId;
  stored.phase = static_cast<std::uint8_t>(state.phase);
  stored.pendingMove = state.pendingMove;
  stored.pendingPurchase = state.pendingPurchase;
  stored.pendingCard = state.pendingCard;
  stored.pendingDebt = state.pendingDebt;
  stored.auction = state.auction;
  stored.trades = state.trades;
  stored.stateVersion = state.stateVersion;
  stored.nextEventSequence = state.nextEventSequence;
  stored.nextAuctionGeneration = state.nextAuctionGeneration;
  stored.nextTradeId = state.nextTradeId;
  stored.rngState = state.rngState;
  stored.crc = stateCrc(stored);
  return writeAtomically(path_, &stored, sizeof(stored));
}

bool FileStateStore::clear() const {
  std::error_code error;
  if (!std::filesystem::exists(path_, error)) return !error;
  return std::filesystem::remove(path_, error) && !error;
}

}  // namespace gridopoly::pi
