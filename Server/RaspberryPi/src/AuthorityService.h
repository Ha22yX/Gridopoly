#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gridopoly/core/GameEngine.h>
#include <gridopoly/protocol/Protocol.h>

#include "FileStateStore.h"
#include "AvatarRenderer.h"
#include "FileIdentityStore.h"
#include "TagPairingModel.h"

namespace gridopoly::pi {

struct ConsoleDeviceInfo {
  std::uint32_t deviceId{}, capabilities{};
  std::string displayName;
  std::uint64_t lastSeenMs{};
  bool collision{};
};

struct ForcedRollState {
  bool active{};
  std::uint8_t playerId{};
  std::uint8_t targetTile{gridopoly::core::kNoAsset};
  std::uint8_t steps{};
  std::uint8_t originTile{gridopoly::core::kNoAsset};
};

struct AuthorityIdentityOptions {
  std::filesystem::path identityPath{};
  std::filesystem::path avatarComponentRoot{};
  std::filesystem::path avatarAssetRoot{};
  std::function<std::uint64_t()> epochClock{};
  std::filesystem::path avatarComponentV2Root{};
  std::uint16_t avatarCatalogVersion{gridopoly::protocol::kAvatarCatalogVersionV1};
};

struct MovementCueGateState {
  bool active{};
  bool ready{};
  std::uint8_t playerId{};
  std::uint8_t originTile{gridopoly::core::kNoAsset};
  std::uint8_t targetTile{gridopoly::core::kNoAsset};
};

enum class PlayerTagBindingResultCode : std::uint8_t {
  Ok = 0,
  InvalidPlayer,
  InvalidTag,
  RevisionMismatch,
  PersistFailed,
  NotAllowed,
};

struct PlayerTagBindingResult {
  PlayerTagBindingResultCode code{PlayerTagBindingResultCode::Ok};
  const char* message{"ok"};
  bool changed{};
  std::uint32_t revision{};
  constexpr explicit operator bool() const {
    return code == PlayerTagBindingResultCode::Ok;
  }
};

struct PlayerTagBindingSnapshot {
  std::uint32_t roomId{};
  std::uint32_t revision{};
  std::uint8_t playerCount{};
  std::array<std::uint32_t, gridopoly::core::kMaxPlayers> playerTagUids{};
};

struct DebugEditResult {
  int status{200};
  const char* message{"ok"};
  std::uint32_t roomId{}, version{};
  bool changed{};
};

class AuthorityService {
 public:
  static constexpr std::uint32_t kMinimumBotActionIntervalMs = 100;
  static constexpr std::uint32_t kMaximumBotActionIntervalMs = 10000;

  AuthorityService(std::filesystem::path statePath, std::filesystem::path metadataPath,
                   std::uint32_t serverDeviceId,
                   std::chrono::milliseconds botActionInterval =
                       std::chrono::milliseconds(1200),
                   AuthorityIdentityOptions identityOptions = {});
  ~AuthorityService();

  AuthorityService(const AuthorityService&) = delete;
  AuthorityService& operator=(const AuthorityService&) = delete;

  bool initialize();
  void tick();
  bool flush();
  DebugEditResult debugSetPlayerCash(std::uint32_t roomId, std::uint32_t expectedVersion,
                                    std::uint8_t playerId, std::int32_t cash);
  DebugEditResult debugSetAssetOwner(std::uint32_t roomId, std::uint32_t expectedVersion,
                                    const std::string& tileId, std::uint8_t ownerId);

  gridopoly::core::Result newGame(std::uint8_t boardSize, std::uint8_t botCount);
  gridopoly::core::Result newGame(std::uint8_t boardSize, std::uint8_t humanCount,
                                 std::uint8_t botCount);
  gridopoly::core::Result execute(gridopoly::protocol::ActionCode action,
                                  std::uint8_t playerId, std::uint8_t assetIndex,
                                  std::int32_t argument, std::uint32_t expectedStateVersion = 0);

  bool makeSnapshot(std::uint8_t seatId, gridopoly::protocol::StateSnapshot& output) const;
  bool makeAuthoritySnapshot(gridopoly::protocol::AuthoritySnapshot& output) const;
  bool makeRosterSnapshot(gridopoly::protocol::RosterSnapshot& output) const;
  bool makePlayerDetail(std::uint32_t requestId, std::uint8_t targetPlayerId,
                        std::uint32_t requestedStateVersion,
                        gridopoly::protocol::PlayerDetailResponse& output) const;
  bool makeIdentitySnapshot(std::uint8_t sourceSeat,
                            gridopoly::protocol::IdentitySnapshot& output,
                            bool resync = false) const;
  void handleIdentityRequest(std::uint8_t sourceSeat,
                             const gridopoly::protocol::IdentityRequest& request,
                             gridopoly::protocol::IdentitySnapshot& output);
  bool renderAvatarPreview(const gridopoly::protocol::AvatarRecipe& recipe,
                           AvatarPreviewResult& output);
  std::filesystem::path avatarComponentRoot(std::uint16_t catalogVersion = 1) const;
  std::filesystem::path avatarAssetRoot() const;
  void handleTradeRequest(std::uint8_t sourceSeat,
                          const gridopoly::protocol::TradeRequest& request,
                          gridopoly::protocol::TradeResponse& output);
  bool makeTradeResync(std::uint8_t sourceSeat,
                       gridopoly::protocol::TradeResponse& output) const;
  bool makeTradeNotification(std::uint8_t sourceSeat, std::uint32_t tradeId,
                             gridopoly::protocol::TradeResponse& output) const;

  bool observeConsoleDevice(std::uint32_t deviceId, const gridopoly::protocol::PairRequest& request);
  void touchConsoleDevice(std::uint32_t deviceId);
  bool automaticPlayers() const;
  std::uint8_t frozenSeatForDevice(std::uint32_t deviceId) const;
  gridopoly::core::Result newAutoGame(std::uint8_t boardSize);
  std::string playerDevicesJson() const;
  void observeTagReader(std::uint32_t roomId, const TagPairReaderObservation& report);
  gridopoly::protocol::TagPairSnapshot makeTagPairSnapshot(std::uint8_t self, bool resync = false) const;
  gridopoly::protocol::TagPairSnapshot moduleTagPairSnapshot(const std::string& moduleId) const;
  void handleTagPairRequest(std::uint8_t source, const gridopoly::protocol::TagPairRequest& request,
                            gridopoly::protocol::TagPairSnapshot& output);
  std::string tagPairingJson() const;
  static std::string tagPairingJson(const gridopoly::protocol::TagPairSnapshot& value);
  std::uint32_t tagPairingRevision() const;
  bool tagPairingBlocksArrival(const std::string& moduleId) const;

  bool activateConsoleSeat(std::uint8_t seatId, const char* displayName);
  bool isHumanSeat(std::uint8_t seatId) const;
  void setConsoleConnected(std::uint8_t seatId, bool connected);
  void setPeerCount(std::uint8_t count);
  bool setBotActionIntervalMs(std::uint32_t intervalMs);
  gridopoly::core::Result setForcedRollTarget(std::uint8_t playerId,
                                               std::uint8_t targetTile,
                                               std::uint32_t expectedStateVersion = 0);
  bool clearForcedRollTarget();
  PlayerTagBindingResult setPlayerTagBinding(std::uint8_t playerId,
                                             std::uint32_t tagUid,
                                             std::uint32_t expectedRevision);
  PlayerTagBindingResult clearPlayerTagBinding(std::uint8_t playerId,
                                               std::uint32_t expectedRevision);
  PlayerTagBindingSnapshot playerTagBindings() const;
  MovementCueGateState movementCueGateState() const;
  bool movementCueReadyFor(const gridopoly::core::GameState& state) const;
  gridopoly::core::Result confirmTaggedArrival(
      std::uint8_t playerId, std::uint32_t tagUid, std::uint8_t targetTile,
      std::uint32_t expectedStateVersion);

  gridopoly::core::GameState stateCopy() const;
  std::uint32_t roomId() const;
  std::uint32_t stateVersion() const;
  std::uint32_t latestEventSequence() const;
  std::uint32_t serverDeviceId() const { return serverDeviceId_; }
  std::uint32_t networkId() const { return serverDeviceId_ ^ 0xA5A5A5A5u; }
  std::uint32_t botActionIntervalMs() const;
  std::uint32_t controlVersion() const;
  std::uint32_t tagBindingRevision() const;
  std::uint32_t identityRevision() const;
  gridopoly::protocol::IdentityRoomPhase identityPhase() const;
  ForcedRollState forcedRollState() const;
  std::uint8_t peerCount() const;

  std::string syncJson(const std::string& serviceIp) const;
  std::string boardJson() const;
  std::string stateJson(const std::string& serviceIp) const;

 private:
  DebugEditResult debugMutate(std::uint32_t roomId, std::uint32_t expectedVersion,
      const std::function<gridopoly::core::Result(gridopoly::core::GameEngine&)>& mutation);
  mutable std::mutex mutex_;
  gridopoly::core::GameEngine engine_{};
  FileStateStore store_;
  FileIdentityStore identityStore_;
  std::filesystem::path metadataPath_;
  std::uint32_t serverDeviceId_{};
  std::uint32_t roomId_{1};
  std::uint32_t persistedVersion_{};
  std::uint32_t observedVersion_{};
  std::uint8_t peerCount_{};
  bool persistPending_{};
  std::chrono::steady_clock::time_point dirtySince_{};
  std::chrono::steady_clock::time_point persistDue_{};
  std::chrono::steady_clock::time_point lastBotAt_{};
  std::chrono::milliseconds botActionInterval_{1200};
  ForcedRollState forcedRoll_{};
  MovementCueGateState movementCueGate_{};
  std::uint32_t controlVersion_{1};
  std::array<std::uint32_t, gridopoly::core::kMaxPlayers> playerTagUids_{};
  std::uint32_t tagBindingRevision_{1};
  IdentityRoomState identity_{};
  std::map<std::uint32_t, ConsoleDeviceInfo> consoleDevices_;
  std::array<ConsoleDeviceInfo, 4> frozenDevices_{};
  bool automaticPlayers_{};
  std::array<std::string, 4> tagReaderOrigins_{};
  std::uint8_t pendingTagRemovalMask_{};
  TagPairingModel tagPairing_;
  std::uint32_t observedTagPairRevision_{};
  gridopoly::core::Result newIdentityGameLocked(std::uint8_t boardSize, std::uint8_t humans,
      std::uint8_t bots, const std::array<ConsoleDeviceInfo, 4>& devices, bool automatic);
  void tagPairChangedLocked();
  bool commitPairedTagLocked(std::uint8_t player, std::uint32_t uid);
  bool commitTagRemovalLocked(const std::string& module);
  void tagPairNameLocked(gridopoly::protocol::TagPairSnapshot& output) const;
  std::string devicesJsonLocked(bool frozen) const;
  std::function<std::uint64_t()> epochClock_{};
  std::uint16_t defaultAvatarCatalogVersion_{gridopoly::protocol::kAvatarCatalogVersionV1};
  std::unique_ptr<AvatarRenderer> avatarRenderer_{};

  struct AvatarJob {
    std::uint32_t roomId{};
    std::uint8_t playerId{};
    std::uint16_t seatRevision{};
    std::uint16_t avatarRevision{};
    gridopoly::protocol::AvatarRecipe recipe{};
  };
  struct AvatarCompletion {
    AvatarJob job{};
    AvatarRenderResult result{};
  };
  mutable std::mutex avatarWorkerMutex_{};
  std::condition_variable avatarWorkerCv_{};
  std::deque<AvatarJob> avatarJobs_{};
  std::deque<AvatarCompletion> avatarCompletions_{};
  std::thread avatarWorker_{};
  bool stopAvatarWorker_{};

  bool loadMetadataLocked();
  bool saveMetadataLocked() const;
  gridopoly::core::Result validateForcedRollLocked(std::uint8_t playerId,
                                                    std::uint8_t targetTile,
                                                    std::uint32_t expectedStateVersion,
                                                    ForcedRollState& output,
                                                    std::uint8_t& dieA,
                                                    std::uint8_t& dieB) const;
  gridopoly::core::Result executeRollLocked(std::uint8_t playerId);
  void clearForcedRollLocked();
  bool synchronizeMovementCueGateLocked();
  bool movementCueReadyForLocked(const gridopoly::core::GameState& state) const;
  gridopoly::core::Result markMovementCueReadyLocked(
      std::uint8_t playerId, std::uint8_t assetIndex, std::int32_t targetTile,
      std::uint32_t expectedStateVersion);
  void clearPlayerTagBindingsLocked();
  void noteChangedLocked();
  void touchIdentityVersionLocked();
  bool flushLocked();
  bool saveIdentityLocked() const;
  void initializeCompatibilityIdentityLocked();
  void startAvatarWorker();
  void stopAvatarWorker();
  void avatarWorkerLoop();
  void queueAvatarLocked(std::uint8_t playerId,
                         const gridopoly::protocol::AvatarRecipe& recipe,
                         std::uint16_t avatarRevision,
                         std::uint16_t seatRevision);
  void drainAvatarCompletionsLocked();
  void maybeStartIdentityCountdownLocked();
  void maybeActivateIdentityRoomLocked();
  void projectIdentityLocked(std::uint8_t sourceSeat, std::uint32_t requestId,
                             gridopoly::protocol::IdentityOperation operation,
                             gridopoly::protocol::IdentityResultCode result,
                             std::uint8_t flags,
                             gridopoly::protocol::IdentitySnapshot& output) const;
  gridopoly::core::Result executeLocked(gridopoly::protocol::ActionCode action,
                                        std::uint8_t playerId, std::uint8_t assetIndex,
                                        std::int32_t argument,
                                        std::uint32_t expectedStateVersion);
  void projectTradeLocked(std::uint8_t sourceSeat, std::uint32_t requestId,
                          gridopoly::protocol::TradeOperation operation,
                          gridopoly::protocol::TradeResultCode result,
                          const gridopoly::core::TradeWorkflow* workflow,
                          std::uint64_t nowEpochMs, std::uint8_t extraFlags,
                          gridopoly::protocol::TradeResponse& output) const;
};

}  // namespace gridopoly::pi
