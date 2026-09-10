#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <gridopoly/core/GameModel.h>
#include "TileOrderTopology.h"

namespace gridopoly::pi {

enum class TileDebugResultCode : std::uint8_t {
  Ok = 0,
  InvalidModuleId,
  InvalidDeviceId,
  BoardUnavailable,
  TileNotFound,
  AssignmentNotFound,
  CapacityReached,
  ModuleOffline,
  DeviceMismatch,
  DeviceConflict,
  TileConflict,
  InvalidOrderReport,
  StaleOrderReport,
};

enum class TileDebugAssignmentSource : std::uint8_t {
  None = 0,
  Auto,
  Manual,
  Order,
};

enum class TileTagReaderState : std::uint8_t {
  Scanning = 0,
  Stable,
  Fault,
};

enum class TileMovementCueMode : std::uint8_t {
  None = 0,
  Departure,
  Destination,
};

const char* tileDebugAssignmentSourceName(TileDebugAssignmentSource source);
const char* tileTagReaderStateName(TileTagReaderState state);
const char* tileMovementCueModeName(TileMovementCueMode mode);

struct TileDebugResult {
  TileDebugResultCode code{TileDebugResultCode::Ok};
  const char* message{"ok"};
  bool changed{};
  std::uint64_t serverRevision{};
  constexpr explicit operator bool() const { return code == TileDebugResultCode::Ok; }
};

struct TileDebugTile {
  std::string tileId{};
  std::uint8_t mapIndex{};
  std::string displayName{};
  std::string kind{};
  std::uint32_t accentRgb{};
  std::string artworkKey{};
  std::int32_t purchasePrice{};
};

struct TileDebugPlayer {
  std::uint8_t playerId{};
  std::string displayName{};
  std::uint32_t rgb{};
};

// Transport-neutral tile-module downlink. The snake-case wire names remain:
// tile_id, artworkKey, accent, purchase_price, owner_player,
// owner_display_name and owner_color. This projection is never persisted and
// never mutates authoritative GameState.
struct TileModuleDebugState {
  std::string orderAnchorModuleId{};
  int orderOffset{};
  std::uint64_t orderEpoch{};
  std::string moduleId{};
  std::string deviceId{};
  std::string tileId{};
  std::uint8_t mapIndex{};
  std::string displayName{};
  std::string kind{};
  std::uint32_t accentRgb{};
  std::string artworkKey{};
  std::int32_t purchasePrice{};
  std::uint8_t ownerPlayerId{};
  std::string ownerDisplayName{};
  std::uint32_t ownerRgb{};
  TileDebugAssignmentSource source{TileDebugAssignmentSource::None};
  std::uint64_t revision{};
  std::uint64_t updatedAtMs{};
};

struct TileDebugModule {
  std::string orderAnchorTileId{};
  bool orderCapable{};
  std::string orderStatus{"legacy"};
  std::string orderChainId{};
  int orderIndex{-1};
  std::uint64_t orderEpoch{};
  std::string orderConflict{};
  std::string orderUpstreamModuleId{};
  std::string moduleId{};
  std::string deviceId{};
  bool online{};
  bool assigned{};
  TileDebugAssignmentSource source{TileDebugAssignmentSource::None};
  std::uint64_t lastSeenMs{};
  std::uint64_t leaseRemainingMs{};
  std::uint64_t registrationOrder{};
  TileTagReaderState tagReaderState{TileTagReaderState::Scanning};
  std::uint32_t tagRevision{};
  bool tagOverflow{};
};

struct TileTagReport {
  TileTagReaderState readerState{TileTagReaderState::Scanning};
  std::uint32_t revision{};
  bool overflow{};
  std::vector<std::uint32_t> tags{};
};

struct TileTagSighting {
  std::string moduleId{};
  std::string deviceId{};
  std::string tileId{};
  std::uint8_t mapIndex{};
  bool currentlySeen{};
  std::uint64_t lastSeenMs{};
};

struct TileDetectedTag {
  std::uint32_t uid{};
  bool currentlySeen{};
  std::uint64_t lastSeenMs{};
  std::vector<TileTagSighting> sightings{};
};

struct TileTagSnapshot {
  std::uint32_t roomId{};
  std::uint64_t revision{};
  std::uint64_t updatedAtMs{};
  std::vector<TileDetectedTag> tags{};
};

struct TileMovementCue {
  TileMovementCueMode mode{TileMovementCueMode::None};
  std::uint8_t playerId{};
  std::uint64_t revision{};
};

struct TileDebugSnapshot {
  std::uint64_t orderEpoch{};
  std::string orderStatus{"legacy"};
  std::uint64_t orderLeaseRemainingMs{};
  std::vector<TileOrderChain> orderChains{};
  std::uint32_t roomId{};
  std::string boardId{};
  std::uint8_t boardSize{};
  std::uint64_t revision{};
  std::uint64_t updatedAtMs{};
  std::vector<TileDebugModule> modules{};
  std::vector<TileDebugTile> tiles{};
  std::vector<TileDebugPlayer> players{};
  std::vector<TileModuleDebugState> assignments{};
};

struct TileDebugHeartbeatResponse {
  TileDebugResult result{};
  bool assigned{};
  TileDebugAssignmentSource source{TileDebugAssignmentSource::None};
  std::uint64_t leaseMs{};
  std::uint64_t serverRevision{};
  TileModuleDebugState assignment{};
  TileMovementCue movementCue{};
  TileTagReaderState tagReaderState{TileTagReaderState::Scanning};
  bool tagOverflow{};
  std::vector<std::uint32_t> stableTags{};
};

class TileDebugAssignments {
 public:
  using EpochClock = std::function<std::uint64_t()>;
  static constexpr std::size_t kMaximumAssignments = 64;
  static constexpr std::size_t kMaximumModules = 64;
  static constexpr std::uint64_t kLeaseMs = 15000;
  static constexpr std::uint64_t kTagHistoryMs = 60000;
  static constexpr std::size_t kMaximumReportedTags = 6;

  explicit TileDebugAssignments(EpochClock epochClock = {});

  TileDebugHeartbeatResponse heartbeat(
      std::uint32_t roomId, const gridopoly::core::GameState& state,
      const std::string& moduleId, const std::string& deviceId,
      const TileTagReport* tagReport = nullptr,
      bool movementCueReady = false,
      const TileOrderReport* orderReport = nullptr);
  TileDebugResult set(std::uint32_t roomId, const gridopoly::core::GameState& state,
                      const std::string& moduleId, const std::string& deviceId,
                      const std::string& tileId);
  TileDebugResult clear(std::uint32_t roomId, const gridopoly::core::GameState& state,
                        const std::string& moduleId);
  TileDebugSnapshot snapshot(std::uint32_t roomId,
                             const gridopoly::core::GameState& state);
  TileTagSnapshot tagSnapshot(std::uint32_t roomId,
                              const gridopoly::core::GameState& state);
  TileMovementCue movementCue(std::uint32_t roomId,
                              const gridopoly::core::GameState& state,
                              const std::string& moduleId,
                              bool movementCueReady);

 private:
  struct ModuleRecord {
    std::string moduleId{};
    std::string deviceId{};
    std::uint64_t registrationOrder{};
    std::uint64_t lastSeenMs{};
    TileTagReaderState tagReaderState{TileTagReaderState::Scanning};
    std::uint32_t tagRevision{};
    bool tagOverflow{};
    std::vector<std::uint32_t> stableTags{};
    std::unordered_map<std::uint32_t, std::uint64_t> tagLastSeenMs{};
  };

  struct OrderAnchor { std::string deviceId; std::string tileId; };
  std::map<std::string, OrderAnchor> orderAnchors_{};
  TileOrderTopology order_{};
  std::vector<TileOrderNode> orderNodes_{};
  std::vector<TileOrderChain> orderChains_{};
  std::string orderFingerprint_{};
  std::uint64_t orderEpoch_{};
  void refreshOrderLocked(const gridopoly::core::GameState& state, std::uint64_t now);

  mutable std::mutex mutex_{};
  EpochClock epochClock_{};
  std::unordered_map<std::string, ModuleRecord> modules_{};
  std::unordered_map<std::string, TileModuleDebugState> assignments_{};
  std::uint32_t contextRoomId_{};
  std::string contextBoardId_{};
  std::uint64_t nextRegistrationOrder_{1};
  std::uint64_t revision_{};
  std::uint64_t updatedAtMs_{};
  std::uint64_t tagRevision_{};
  std::uint64_t tagUpdatedAtMs_{};

  static bool validIdentifier(const std::string& value);
  static const gridopoly::core::TileDefinition* findTile(
      const gridopoly::core::BoardDefinition& board, const std::string& tileId,
      std::uint8_t& mapIndex);
  static TileDebugTile projectTile(const gridopoly::core::BoardDefinition& board,
                                   std::uint8_t mapIndex);
  static TileDebugPlayer projectPlayer(const gridopoly::core::GameState& state,
                                       std::uint8_t playerId);
  static TileModuleDebugState projectAssignment(
      const gridopoly::core::GameState& state, const std::string& moduleId,
      const std::string& deviceId, const std::string& tileId,
      TileDebugAssignmentSource source);
  bool refreshAuthorityProjectionLocked(
      const gridopoly::core::GameState& state, std::uint64_t now);
  void expireLocked(std::uint64_t now);
  void synchronizeContextLocked(std::uint32_t roomId,
                                const gridopoly::core::GameState& state,
                                std::uint64_t now);
  bool autoAssignLocked(const gridopoly::core::GameState& state,
                        const std::string& moduleId, std::uint64_t now,
                        bool commitRevision);
  bool applyTagReportLocked(ModuleRecord& module, const TileTagReport& report,
                            std::uint64_t now);
  bool pruneTagHistoryLocked(std::uint64_t now);
  TileMovementCue movementCueLocked(const gridopoly::core::GameState& state,
                                    const std::string& moduleId,
                                    bool movementCueReady) const;
  void commitRevisionLocked(std::uint64_t now);
  std::uint64_t leaseRemainingLocked(const ModuleRecord& module,
                                     std::uint64_t now) const;
  std::uint64_t nowMs() const;
};

}  // namespace gridopoly::pi
