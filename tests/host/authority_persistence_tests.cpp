#include <cassert>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "../../Server/RaspberryPi/src/AuthorityService.h"
#include "../../Server/RaspberryPi/src/FileStateStore.h"

using namespace gridopoly::core;
using namespace gridopoly::protocol;

namespace {

struct LegacyPersistedStateV2 {
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

struct LegacyPersistedStateV3 {
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

template <typename T>
std::uint32_t legacyCrc(const T& value) {
  return crc32(reinterpret_cast<const std::uint8_t*>(&value),
               sizeof(value) - sizeof(value.crc));
}

}  // namespace

int main() {
  const auto temporary = std::filesystem::temp_directory_path() /
      ("gridopoly-authority-persistence-" + std::to_string(
#if defined(_WIN32)
          ::_getpid()
#else
          ::getpid()
#endif
      ));
  std::filesystem::remove_all(temporary);
  std::filesystem::create_directories(temporary);
  const auto statePath = temporary / "state.bin";
  const auto metadataPath = temporary / "authority.meta";

  std::uint32_t room = 0;
  std::uint32_t version = 0;
  PendingCard pending{};
  {
    gridopoly::pi::AuthorityService authority(statePath, metadataPath, 0x10203040u);
    assert(authority.initialize());
    assert(authority.botActionIntervalMs() == 1200);
    assert(!authority.setBotActionIntervalMs(99));
    assert(authority.botActionIntervalMs() == 1200);
    assert(authority.setBotActionIntervalMs(1750));
    assert(authority.botActionIntervalMs() == 1750);
    GameState state{};
    bool reachedCard = false;
    for (int attempt = 0; attempt < 200 && !reachedCard; ++attempt) {
      assert(authority.newGame(16, 1));
      version = authority.stateVersion();
      assert(authority.execute(ActionCode::Roll, 1, 0xFF, 0, version));
      state = authority.stateCopy();
      assert(state.pendingMove.active);
      assert(authority.execute(ActionCode::ConfirmPosition, 1, 0xFF,
                               state.pendingMove.target, state.stateVersion));
      state = authority.stateCopy();
      reachedCard = state.phase == GamePhase::AwaitCard;
    }
    assert(reachedCard);
    room = authority.roomId();
    assert(state.phase == GamePhase::AwaitCard);
    assert(state.pendingCard.active);
    assert(state.financialHistoryInitialized);
    assert(state.financialHistory[0].count >= 1);
    pending = state.pendingCard;
    assert(authority.flush());
  }

  {
    gridopoly::pi::AuthorityService restored(statePath, metadataPath, 0);
    assert(restored.initialize());
    assert(restored.botActionIntervalMs() == 1750);
    const auto state = restored.stateCopy();
    assert(restored.roomId() == room);
    assert(restored.serverDeviceId() == 0x10203040u);
    assert(state.pendingCard.active);
    assert(state.pendingCard.stage == PendingCardStage::AwaitContinue);
    assert(state.pendingCard.cardInstanceId == pending.cardInstanceId);
    assert(state.pendingCard.cardCatalogId == pending.cardCatalogId);
    assert(state.financialHistoryInitialized);
    assert(state.financialHistory[0].count >= 1);
    AuthoritySnapshot snapshot{};
    assert(restored.makeAuthoritySnapshot(snapshot));
    assert(snapshot.pendingCardFlags == 0x03);
    assert(snapshot.pendingCardInstanceId == pending.cardInstanceId);
    assert(snapshot.pendingCardDrawEventSequence == pending.drawEventSequence);
    assert(restored.execute(ActionCode::CardContinue, 1, 0xFF,
                            pending.cardInstanceId, restored.stateVersion()));
    assert(restored.flush());
  }

  // The pre-settings 12-byte metadata layout remains readable. The first
  // successful initialization appends the configured interval and web-control
  // extension, and the next process restores it instead of falling back.
  {
    const auto legacyMetadataPath = temporary / "legacy-authority.meta";
    const auto legacyStatePath = temporary / "legacy-settings-state.bin";
    const std::uint32_t magic = 0x314D5047u;
    const std::uint32_t legacyRoom = 0x11223344u;
    const std::uint32_t legacyDevice = 0x55667788u;
    std::ofstream output(legacyMetadataPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    output.write(reinterpret_cast<const char*>(&legacyRoom), sizeof(legacyRoom));
    output.write(reinterpret_cast<const char*>(&legacyDevice), sizeof(legacyDevice));
    output.close();
    gridopoly::pi::AuthorityService legacyMetadata(
        legacyStatePath, legacyMetadataPath, 0, std::chrono::milliseconds(2300));
    assert(legacyMetadata.initialize());
    assert(legacyMetadata.roomId() == legacyRoom);
    assert(legacyMetadata.serverDeviceId() == legacyDevice);
    assert(legacyMetadata.botActionIntervalMs() == 2300);
    assert(std::filesystem::file_size(legacyMetadataPath) == 24);
  }
  {
    gridopoly::pi::AuthorityService restoredSettings(
        temporary / "legacy-settings-state.bin", temporary / "legacy-authority.meta", 0,
        std::chrono::milliseconds(1200));
    assert(restoredSettings.initialize());
    assert(restoredSettings.roomId() == 0x11223344u);
    assert(restoredSettings.serverDeviceId() == 0x55667788u);
    assert(restoredSettings.botActionIntervalMs() == 2300);
  }

  // A web-admin destination override uses real dice, remains separate from
  // game state versioning, and is consumed exactly once after a successful
  // matching roll.
  {
    const auto forcedStatePath = temporary / "forced-roll-state.bin";
    const auto forcedMetadataPath = temporary / "forced-roll-authority.meta";
    gridopoly::pi::AuthorityService authority(
        forcedStatePath, forcedMetadataPath, 0x31415926u);
    assert(authority.initialize());
    assert(authority.newGame(16, 1));
    const auto stateVersion = authority.stateVersion();
    const auto controlVersion = authority.controlVersion();
    assert(!authority.setForcedRollTarget(1, 1, stateVersion));
    assert(!authority.setForcedRollTarget(1, 13, stateVersion));
    assert(!authority.forcedRollState().active);
    assert(authority.setForcedRollTarget(1, 7, stateVersion));
    const auto armed = authority.forcedRollState();
    assert(armed.active);
    assert(armed.playerId == 1);
    assert(armed.targetTile == 7);
    assert(armed.steps == 7);
    assert(armed.originTile == 0);
    assert(authority.stateVersion() == stateVersion);
    assert(authority.controlVersion() == controlVersion + 1);
    assert(authority.setForcedRollTarget(1, 7, stateVersion));
    assert(authority.controlVersion() == controlVersion + 1);
    assert(authority.execute(ActionCode::Roll, 1, 0xFF, 0, stateVersion));
    const auto rolled = authority.stateCopy();
    assert(rolled.phase == GamePhase::AwaitMoveConfirm);
    assert(rolled.pendingMove.playerId == 1);
    assert(rolled.pendingMove.target == 7);
    assert(rolled.players[0].doublesStreak == 0);
    assert(!authority.forcedRollState().active);
    assert(!authority.setForcedRollTarget(1, 5, authority.stateVersion()));

    // Starting a new room clears even an override armed for another player.
    assert(authority.setForcedRollTarget(2, 5, authority.stateVersion()));
    assert(authority.newGame(16, 1));
    assert(!authority.forcedRollState().active);
    assert(authority.setForcedRollTarget(1, 2, authority.stateVersion()));
    assert(authority.execute(ActionCode::Roll, 1, 0xFF, 0, authority.stateVersion()));
    const auto doubled = authority.stateCopy();
    assert(doubled.pendingMove.target == 2);
    assert(doubled.players[0].doublesStreak == 1);
  }

  // A pending override survives a service restart, while legacy metadata
  // without its extension remains readable. Clearing is also persistent.
  {
    const auto forcedStatePath = temporary / "forced-roll-restore-state.bin";
    const auto forcedMetadataPath = temporary / "forced-roll-restore-authority.meta";
    std::uint32_t controlVersion = 0;
    {
      gridopoly::pi::AuthorityService authority(
          forcedStatePath, forcedMetadataPath, 0x27182818u);
      assert(authority.initialize());
      assert(authority.newGame(16, 1));
      assert(authority.setForcedRollTarget(2, 5, authority.stateVersion()));
      controlVersion = authority.controlVersion();
      assert(authority.flush());
    }
    {
      gridopoly::pi::AuthorityService restored(
          forcedStatePath, forcedMetadataPath, 0);
      assert(restored.initialize());
      const auto armed = restored.forcedRollState();
      assert(armed.active);
      assert(armed.playerId == 2);
      assert(armed.targetTile == 5);
      assert(armed.steps == 5);
      assert(armed.originTile == 0);
      assert(restored.controlVersion() == controlVersion);
      assert(restored.clearForcedRollTarget());
    }
    {
      gridopoly::pi::AuthorityService restored(
          forcedStatePath, forcedMetadataPath, 0);
      assert(restored.initialize());
      assert(!restored.forcedRollState().active);
    }
  }

  // A persisted override is invalidated when the player has moved since it
  // was armed. This prevents a service restart from applying a stale target
  // to a different origin while preserving the room and game state.
  {
    const auto forcedStatePath = temporary / "forced-roll-stale-state.bin";
    const auto forcedMetadataPath = temporary / "forced-roll-stale-authority.meta";
    GameEngine prepared;
    assert(prepared.reset(*BoardCatalog::findBySize(16), 0x24681357u));
    assert(prepared.addPlayer("Console", ControllerKind::RealConsole));
    assert(prepared.addPlayer("Bot", ControllerKind::Bot));
    assert(prepared.start());
    prepared.mutableStateForRestore().players[1].position = 1;
    gridopoly::pi::FileStateStore store(forcedStatePath);
    assert(store.save(prepared.state()));

    const std::uint32_t magic = 0x314D5047u;
    const std::uint32_t room = 0x13572468u;
    const std::uint32_t device = 0x10293847u;
    const std::uint32_t botIntervalMs = 1200;
    const std::uint32_t controlVersion = 7;
    const std::uint8_t active = 1;
    const std::uint8_t playerId = 2;
    const std::uint8_t targetTile = 5;
    const std::uint8_t staleOriginTile = 0;
    std::ofstream output(forcedMetadataPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    output.write(reinterpret_cast<const char*>(&room), sizeof(room));
    output.write(reinterpret_cast<const char*>(&device), sizeof(device));
    output.write(reinterpret_cast<const char*>(&botIntervalMs), sizeof(botIntervalMs));
    output.write(reinterpret_cast<const char*>(&controlVersion), sizeof(controlVersion));
    output.write(reinterpret_cast<const char*>(&active), sizeof(active));
    output.write(reinterpret_cast<const char*>(&playerId), sizeof(playerId));
    output.write(reinterpret_cast<const char*>(&targetTile), sizeof(targetTile));
    output.write(reinterpret_cast<const char*>(&staleOriginTile), sizeof(staleOriginTile));
    output.close();

    gridopoly::pi::AuthorityService restored(
        forcedStatePath, forcedMetadataPath, 0);
    assert(restored.initialize());
    assert(restored.roomId() == room);
    assert(restored.stateCopy().players[1].position == 1);
    assert(!restored.forcedRollState().active);
    assert(restored.controlVersion() == controlVersion + 1);
  }

  // A forced double may not bypass the third-consecutive-double Hold rule.
  {
    const auto forcedStatePath = temporary / "forced-roll-double-state.bin";
    const auto forcedMetadataPath = temporary / "forced-roll-double-authority.meta";
    GameEngine prepared;
    assert(prepared.reset(*BoardCatalog::findBySize(16), 0xABCD1234u));
    assert(prepared.addPlayer("Double Test", ControllerKind::Web));
    assert(prepared.start());
    prepared.mutableStateForRestore().players[0].doublesStreak = 2;
    gridopoly::pi::FileStateStore store(forcedStatePath);
    assert(store.save(prepared.state()));
    gridopoly::pi::AuthorityService authority(
        forcedStatePath, forcedMetadataPath, 0x11112222u);
    assert(authority.initialize());
    const auto rejected = authority.setForcedRollTarget(
        1, 2, authority.stateVersion());
    assert(!rejected);
    assert(rejected.code == ErrorCode::RuleViolation);
    assert(!authority.forcedRollState().active);
  }

  // Bot timing uses the same one-shot forced dice path instead of the random
  // GameEngine bot roll, then resumes normal bot processing on later ticks.
  {
    const auto forcedStatePath = temporary / "forced-roll-bot-state.bin";
    const auto forcedMetadataPath = temporary / "forced-roll-bot-authority.meta";
    GameEngine prepared;
    assert(prepared.reset(*BoardCatalog::findBySize(16), 0x99887766u));
    assert(prepared.addPlayer("Console", ControllerKind::RealConsole));
    assert(prepared.addPlayer("Bot", ControllerKind::Bot));
    assert(prepared.start());
    auto& preparedState = prepared.mutableStateForRestore();
    preparedState.activePlayerId = 2;
    preparedState.phase = GamePhase::AwaitRoll;
    gridopoly::pi::FileStateStore store(forcedStatePath);
    assert(store.save(prepared.state()));
    gridopoly::pi::AuthorityService authority(
        forcedStatePath, forcedMetadataPath, 0x33334444u,
        std::chrono::milliseconds(100));
    assert(authority.initialize());
    assert(authority.setForcedRollTarget(2, 8, authority.stateVersion()));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    authority.tick();
    const auto rolled = authority.stateCopy();
    assert(rolled.phase == GamePhase::TurnEnd);
    assert(!rolled.pendingMove.active);
    assert(rolled.players[1].position == 8);
    assert(rolled.players[1].doublesStreak == 0);
    assert(!authority.forcedRollState().active);
  }

  // A schema-2 save from the pre-trade server is migrated in place instead
  // of resetting the room/game merely because schema 3 adds trade slots.
  {
    GameEngine legacyEngine;
    assert(legacyEngine.reset(*BoardCatalog::findBySize(16), 0x12345678u));
    assert(legacyEngine.addPlayer("Legacy Console", ControllerKind::RealConsole));
    assert(legacyEngine.addPlayer("Legacy Bot", ControllerKind::Bot));
    assert(legacyEngine.start());
    const auto& state = legacyEngine.state();
    LegacyPersistedStateV2 legacy{};
    legacy.magic = 0x32545347u;
    legacy.schema = 2;
    legacy.boardSize = state.board->tileCount;
    legacy.playerCount = state.playerCount;
    legacy.players = state.players;
    legacy.assets = state.assets;
    legacy.events = state.events;
    legacy.activePlayerId = state.activePlayerId;
    legacy.roundNumber = state.roundNumber;
    legacy.eventHead = state.eventHead;
    legacy.eventCount = state.eventCount;
    legacy.winnerPlayerId = state.winnerPlayerId;
    legacy.phase = static_cast<std::uint8_t>(state.phase);
    legacy.pendingMove = state.pendingMove;
    legacy.pendingPurchase = state.pendingPurchase;
    legacy.pendingCard = state.pendingCard;
    legacy.pendingDebt = state.pendingDebt;
    legacy.auction = state.auction;
    legacy.stateVersion = state.stateVersion;
    legacy.nextEventSequence = state.nextEventSequence;
    legacy.nextAuctionGeneration = state.nextAuctionGeneration;
    legacy.rngState = state.rngState;
    legacy.crc = legacyCrc(legacy);
    const auto legacyPath = temporary / "legacy-v2.bin";
    std::ofstream output(legacyPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
    output.close();
    GameEngine migrated;
    gridopoly::pi::FileStateStore legacyStore(legacyPath);
    assert(legacyStore.restore(migrated));
    assert(migrated.state().stateVersion == state.stateVersion);
    assert(migrated.state().playerCount == 2);
    assert(migrated.state().nextTradeId == 1);
    TradeWorkflow noTrade{};
    assert(!migrated.tradeForPlayer(1, noTrade));
  }

  // Schema 3 introduced trades but predates the dedicated financial ledger.
  // It restores in place and lazily migrates retained financial events.
  {
    GameEngine legacyEngine;
    assert(legacyEngine.reset(*BoardCatalog::findBySize(16), 0x87654321u));
    assert(legacyEngine.addPlayer("Schema 3", ControllerKind::RealConsole));
    assert(legacyEngine.start());
    const auto& state = legacyEngine.state();
    LegacyPersistedStateV3 legacy{};
    legacy.magic = 0x32545347u;
    legacy.schema = 3;
    legacy.boardSize = state.board->tileCount;
    legacy.playerCount = state.playerCount;
    legacy.players = state.players;
    legacy.assets = state.assets;
    legacy.events = state.events;
    legacy.activePlayerId = state.activePlayerId;
    legacy.roundNumber = state.roundNumber;
    legacy.eventHead = state.eventHead;
    legacy.eventCount = state.eventCount;
    legacy.winnerPlayerId = state.winnerPlayerId;
    legacy.phase = static_cast<std::uint8_t>(state.phase);
    legacy.pendingMove = state.pendingMove;
    legacy.pendingPurchase = state.pendingPurchase;
    legacy.pendingCard = state.pendingCard;
    legacy.pendingDebt = state.pendingDebt;
    legacy.auction = state.auction;
    legacy.trades = state.trades;
    legacy.stateVersion = state.stateVersion;
    legacy.nextEventSequence = state.nextEventSequence;
    legacy.nextAuctionGeneration = state.nextAuctionGeneration;
    legacy.nextTradeId = state.nextTradeId;
    legacy.rngState = state.rngState;
    legacy.crc = legacyCrc(legacy);
    const auto legacyPath = temporary / "legacy-v3.bin";
    std::ofstream output(legacyPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
    output.close();
    GameEngine migrated;
    gridopoly::pi::FileStateStore legacyStore(legacyPath);
    assert(legacyStore.restore(migrated));
    assert(!migrated.state().financialHistoryInitialized);
    assert(migrated.roll(1, 1, 2));
    assert(migrated.state().financialHistoryInitialized);
    assert(migrated.state().financialHistory[0].count == 1);
    assert(migrated.state().financialHistory[0].entries[0].kind == EventKind::GameStarted);
  }

  std::filesystem::remove_all(temporary);
  std::cout << "GRIDOPOLY_AUTHORITY_PERSISTENCE_TESTS_PASS\n";
  return 0;
}
