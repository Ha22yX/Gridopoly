#include "TileDebugAssignments.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <utility>

#include "../../../Firmware/PlayerConsole/grid_city_visual_catalog.h"

namespace gridopoly::pi {
namespace {

using namespace gridopoly::core;

constexpr std::uint32_t kPlayerRgb[] = {
    0x58A7EBu, 0xEF7168u, 0x52DCB7u, 0xF2C453u, 0xC28AE8u, 0xEA8A55u,
};

const char* tileKindName(TileKind kind) {
  switch (kind) {
    case TileKind::Start: return "START";
    case TileKind::Property: return "PROPERTY";
    case TileKind::Transit: return "TRANSIT";
    case TileKind::Utility: return "UTILITY";
    case TileKind::CityEvent: return "CHANCE";
    case TileKind::CivicFund: return "COMMUNITY_CHEST";
    case TileKind::Fee: return "FEE";
    case TileKind::Hold: return "HOLD";
    case TileKind::Rest: return "REST";
    case TileKind::GoToHold: return "GO_TO_HOLD";
  }
  return "UNKNOWN";
}

const char* artworkKey(GridCityArtwork artwork) {
  switch (artwork) {
    case GridCityArtwork::RivetRow: return "a1-rivet-row";
    case GridCityArtwork::CopperLane: return "a2-copper-lane";
    case GridCityArtwork::LanternAvenue: return "b1-lantern-avenue";
    case GridCityArtwork::TidewayDrive: return "b2-tideway-drive";
    case GridCityArtwork::BeaconBoulevard: return "b3-beacon-boulevard";
    case GridCityArtwork::CanvasStreet: return "c1-canvas-street";
    case GridCityArtwork::BloomTerrace: return "c2-bloom-terrace";
    case GridCityArtwork::AuroraAvenue: return "c3-aurora-avenue";
    case GridCityArtwork::ArchiveWay: return "d1-archive-way";
    case GridCityArtwork::ForumDrive: return "d2-forum-drive";
    case GridCityArtwork::MeridianAvenue: return "d3-meridian-avenue";
    case GridCityArtwork::PulseStreet: return "e1-pulse-street";
    case GridCityArtwork::PrismBoulevard: return "e2-prism-boulevard";
    case GridCityArtwork::NovaAvenue: return "e3-nova-avenue";
    case GridCityArtwork::SunstepTerrace: return "f1-sunstep-terrace";
    case GridCityArtwork::HelixWay: return "f2-helix-way";
    case GridCityArtwork::HorizonDrive: return "f3-horizon-drive";
    case GridCityArtwork::CanopyLane: return "g1-canopy-lane";
    case GridCityArtwork::VerdantAvenue: return "g2-verdant-avenue";
    case GridCityArtwork::SummitBoulevard: return "g3-summit-boulevard";
    case GridCityArtwork::CrownPromenade: return "h1-crown-promenade";
    case GridCityArtwork::GrandMeridian: return "h2-grand-meridian";
    case GridCityArtwork::WestlineTerminal: return "transit-westline-terminal";
    case GridCityArtwork::NorthloopStation: return "transit-northloop-station";
    case GridCityArtwork::EastgateTerminal: return "transit-eastgate-terminal";
    case GridCityArtwork::SouthlineDepot: return "transit-southline-depot";
    case GridCityArtwork::MetroGrid: return "utility-metro-grid";
    case GridCityArtwork::BluewaterWorks: return "utility-bluewater-works";
    case GridCityArtwork::Chance: return "cover-chance";
    case GridCityArtwork::CommunityFund: return "cover-community-fund";
    case GridCityArtwork::IncomeTax: return "cover-income-tax";
    case GridCityArtwork::LuxuryTax: return "cover-luxury-tax";
    case GridCityArtwork::CentralLaunch: return "corner-central-launch";
    case GridCityArtwork::CivicHold: return "corner-civic-hold";
    case GridCityArtwork::FreePlaza: return "corner-free-plaza";
    case GridCityArtwork::HoldOrder: return "corner-hold-order";
    case GridCityArtwork::Fallback:
    case GridCityArtwork::Count: return "";
  }
  return "";
}

bool assignmentsEqual(const TileModuleDebugState& left,
                      const TileModuleDebugState& right) {
  return left.moduleId == right.moduleId && left.deviceId == right.deviceId &&
      left.tileId == right.tileId && left.mapIndex == right.mapIndex &&
      left.displayName == right.displayName && left.kind == right.kind &&
      left.accentRgb == right.accentRgb && left.artworkKey == right.artworkKey &&
      left.purchasePrice == right.purchasePrice &&
      left.ownerPlayerId == right.ownerPlayerId &&
      left.ownerDisplayName == right.ownerDisplayName && left.ownerRgb == right.ownerRgb &&
      left.source == right.source;
}

}  // namespace

const char* tileDebugAssignmentSourceName(TileDebugAssignmentSource source) {
  switch (source) {
    case TileDebugAssignmentSource::None: return "none";
    case TileDebugAssignmentSource::Auto: return "auto";
    case TileDebugAssignmentSource::Manual: return "manual";
  }
  return "none";
}

const char* tileTagReaderStateName(TileTagReaderState state) {
  switch (state) {
    case TileTagReaderState::Scanning: return "scanning";
    case TileTagReaderState::Stable: return "stable";
    case TileTagReaderState::Fault: return "fault";
  }
  return "scanning";
}

const char* tileMovementCueModeName(TileMovementCueMode mode) {
  switch (mode) {
    case TileMovementCueMode::None: return "none";
    case TileMovementCueMode::Departure: return "departure";
    case TileMovementCueMode::Destination: return "destination";
  }
  return "none";
}

TileDebugAssignments::TileDebugAssignments(EpochClock epochClock)
    : epochClock_(std::move(epochClock)) {
  if (!epochClock_) {
    epochClock_ = [] {
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
    };
  }
}

bool TileDebugAssignments::validIdentifier(const std::string& value) {
  if (value.empty() || value.size() > 32) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_' ||
        character == '.' || character == ':';
  });
}

const TileDefinition* TileDebugAssignments::findTile(const BoardDefinition& board,
                                                      const std::string& tileId,
                                                      std::uint8_t& mapIndex) {
  for (std::uint8_t index = 0; index < board.tileCount; ++index) {
    if (tileId == board.tiles[index].id) {
      mapIndex = index;
      return &board.tiles[index];
    }
  }
  return nullptr;
}

TileDebugTile TileDebugAssignments::projectTile(const BoardDefinition& board,
                                                std::uint8_t mapIndex) {
  TileDebugTile output{};
  if (mapIndex >= board.tileCount) return output;
  const auto& tile = board.tiles[mapIndex];
  const auto* visual = gridCityVisualById(tile.id);
  if (visual == nullptr) visual = &gridCityFallbackVisual();
  output.tileId = tile.id;
  output.mapIndex = mapIndex;
  output.displayName = visual->name;
  output.kind = tileKindName(tile.kind);
  output.accentRgb = visual->accent;
  output.artworkKey = artworkKey(visual->artwork);
  if (tile.assetIndex != kNoAsset && tile.assetIndex < board.assetCount) {
    output.purchasePrice = board.assets[tile.assetIndex].economy.price;
  }
  return output;
}

TileDebugPlayer TileDebugAssignments::projectPlayer(const GameState& state,
                                                     std::uint8_t playerId) {
  TileDebugPlayer output{};
  output.playerId = playerId;
  output.rgb = kPlayerRgb[(playerId - 1u) % (sizeof(kPlayerRgb) / sizeof(kPlayerRgb[0]))];
  if (playerId <= state.playerCount && state.players[playerId - 1u].name[0] != '\0') {
    output.displayName = state.players[playerId - 1u].name;
  } else {
    output.displayName = "P" + std::to_string(playerId);
  }
  return output;
}

TileModuleDebugState TileDebugAssignments::projectAssignment(
    const GameState& state, const std::string& moduleId, const std::string& deviceId,
    const std::string& tileId, TileDebugAssignmentSource source) {
  TileModuleDebugState output{};
  output.moduleId = moduleId;
  output.deviceId = deviceId;
  output.tileId = tileId;
  if (state.board != nullptr) {
    std::uint8_t index = 0;
    const auto* definition = findTile(*state.board, tileId, index);
    if (definition != nullptr) {
      const auto tile = projectTile(*state.board, index);
      output.mapIndex = tile.mapIndex;
      output.displayName = tile.displayName;
      output.kind = tile.kind;
      output.accentRgb = tile.accentRgb;
      output.artworkKey = tile.artworkKey;
      output.purchasePrice = tile.purchasePrice;
      if (definition->assetIndex != kNoAsset &&
          definition->assetIndex < state.board->assetCount) {
        const auto ownerPlayerId = state.assets[definition->assetIndex].ownerId;
        if (ownerPlayerId != kNoPlayer && ownerPlayerId <= state.playerCount &&
            ownerPlayerId <= kMaxPlayers) {
          output.ownerPlayerId = ownerPlayerId;
          const auto owner = projectPlayer(state, ownerPlayerId);
          output.ownerDisplayName = owner.displayName;
          output.ownerRgb = owner.rgb;
        }
      }
    }
  }
  output.source = source;
  return output;
}

bool TileDebugAssignments::refreshAuthorityProjectionLocked(
    const GameState& state, std::uint64_t now) {
  std::vector<TileModuleDebugState*> changed;
  changed.reserve(assignments_.size());
  for (auto& entry : assignments_) {
    auto& assignment = entry.second;
    const auto projected = projectAssignment(state, assignment.moduleId,
                                             assignment.deviceId, assignment.tileId,
                                             assignment.source);
    if (assignment.ownerPlayerId == projected.ownerPlayerId &&
        assignment.ownerDisplayName == projected.ownerDisplayName &&
        assignment.ownerRgb == projected.ownerRgb) {
      continue;
    }
    assignment.ownerPlayerId = projected.ownerPlayerId;
    assignment.ownerDisplayName = projected.ownerDisplayName;
    assignment.ownerRgb = projected.ownerRgb;
    changed.push_back(&assignment);
  }
  if (changed.empty()) return false;
  commitRevisionLocked(now);
  for (auto* assignment : changed) {
    assignment->revision = revision_;
    assignment->updatedAtMs = now;
  }
  return true;
}

void TileDebugAssignments::commitRevisionLocked(std::uint64_t now) {
  ++revision_;
  updatedAtMs_ = now;
}

std::uint64_t TileDebugAssignments::leaseRemainingLocked(
    const ModuleRecord& module, std::uint64_t now) const {
  if (now <= module.lastSeenMs) return kLeaseMs;
  const auto elapsed = now - module.lastSeenMs;
  return elapsed >= kLeaseMs ? 0 : kLeaseMs - elapsed;
}

void TileDebugAssignments::expireLocked(std::uint64_t now) {
  bool changed = false;
  bool tagsChanged = false;
  for (auto iterator = modules_.begin(); iterator != modules_.end();) {
    if (leaseRemainingLocked(iterator->second, now) != 0) {
      ++iterator;
      continue;
    }
    assignments_.erase(iterator->first);
    tagsChanged = tagsChanged || !iterator->second.tagLastSeenMs.empty();
    iterator = modules_.erase(iterator);
    changed = true;
  }
  if (changed) commitRevisionLocked(now);
  if (tagsChanged) {
    ++tagRevision_;
    tagUpdatedAtMs_ = now;
  }
}

void TileDebugAssignments::synchronizeContextLocked(std::uint32_t roomId,
                                                     const GameState& state,
                                                     std::uint64_t now) {
  const std::string boardId = state.board == nullptr ? std::string{} : state.board->id;
  if (contextRoomId_ == 0) {
    contextRoomId_ = roomId;
    contextBoardId_ = boardId;
    return;
  }
  if (contextRoomId_ == roomId && contextBoardId_ == boardId) return;

  contextRoomId_ = roomId;
  contextBoardId_ = boardId;
  assignments_.clear();
  bool hadTags = false;
  for (auto& entry : modules_) {
    auto& module = entry.second;
    hadTags = hadTags || !module.tagLastSeenMs.empty();
    module.tagReaderState = TileTagReaderState::Scanning;
    module.tagRevision = 0;
    module.tagOverflow = false;
    module.stableTags.clear();
    module.tagLastSeenMs.clear();
  }
  if (hadTags) {
    ++tagRevision_;
    tagUpdatedAtMs_ = now;
  }
  commitRevisionLocked(now);
  if (state.board == nullptr) return;

  std::vector<const ModuleRecord*> ordered;
  ordered.reserve(modules_.size());
  for (const auto& entry : modules_) ordered.push_back(&entry.second);
  std::sort(ordered.begin(), ordered.end(), [](const ModuleRecord* left,
                                                const ModuleRecord* right) {
    if (left->registrationOrder != right->registrationOrder) {
      return left->registrationOrder < right->registrationOrder;
    }
    return left->moduleId < right->moduleId;
  });

  std::uint8_t mapIndex = 0;
  for (const auto* module : ordered) {
    if (mapIndex >= state.board->tileCount) break;
    auto assignment = projectAssignment(state, module->moduleId, module->deviceId,
                                        state.board->tiles[mapIndex].id,
                                        TileDebugAssignmentSource::Auto);
    assignment.revision = revision_;
    assignment.updatedAtMs = now;
    assignments_[module->moduleId] = std::move(assignment);
    ++mapIndex;
  }
}

bool TileDebugAssignments::autoAssignLocked(const GameState& state,
                                            const std::string& moduleId,
                                            std::uint64_t now,
                                            bool commitRevision) {
  if (state.board == nullptr || assignments_.find(moduleId) != assignments_.end()) {
    return false;
  }
  const auto module = modules_.find(moduleId);
  if (module == modules_.end()) return false;

  for (std::uint8_t index = 0; index < state.board->tileCount; ++index) {
    const auto occupied = std::any_of(
        assignments_.begin(), assignments_.end(), [index](const auto& entry) {
          return entry.second.mapIndex == index;
        });
    if (occupied) continue;
    if (commitRevision) commitRevisionLocked(now);
    auto assignment = projectAssignment(state, moduleId, module->second.deviceId,
                                        state.board->tiles[index].id,
                                        TileDebugAssignmentSource::Auto);
    assignment.revision = revision_;
    assignment.updatedAtMs = now;
    assignments_[moduleId] = std::move(assignment);
    return true;
  }
  return false;
}

bool TileDebugAssignments::applyTagReportLocked(ModuleRecord& module,
                                                const TileTagReport& report,
                                                std::uint64_t now) {
  auto tags = report.tags;
  std::sort(tags.begin(), tags.end());
  tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
  const bool changed = module.tagReaderState != report.readerState ||
      module.tagRevision != report.revision || module.tagOverflow != report.overflow ||
      (report.readerState == TileTagReaderState::Stable && module.stableTags != tags);
  module.tagReaderState = report.readerState;
  module.tagRevision = report.revision;
  module.tagOverflow = report.overflow;
  if (report.readerState == TileTagReaderState::Stable) {
    module.stableTags = std::move(tags);
    for (const auto uid : module.stableTags) module.tagLastSeenMs[uid] = now;
  }
  if (changed) {
    ++tagRevision_;
    tagUpdatedAtMs_ = now;
  }
  return changed;
}

bool TileDebugAssignments::pruneTagHistoryLocked(std::uint64_t now) {
  bool changed = false;
  for (auto& entry : modules_) {
    auto& history = entry.second.tagLastSeenMs;
    for (auto iterator = history.begin(); iterator != history.end();) {
      const auto elapsed = now > iterator->second ? now - iterator->second : 0;
      if (elapsed <= kTagHistoryMs) {
        ++iterator;
        continue;
      }
      iterator = history.erase(iterator);
      changed = true;
    }
  }
  if (changed) {
    ++tagRevision_;
    tagUpdatedAtMs_ = now;
  }
  return changed;
}

TileMovementCue TileDebugAssignments::movementCueLocked(
    const GameState& state, const std::string& moduleId,
    bool movementCueReady) const {
  TileMovementCue output{};
  output.revision = state.stateVersion;
  if (!movementCueReady || state.phase != GamePhase::AwaitMoveConfirm ||
      !state.pendingMove.active) return output;
  const auto assignment = assignments_.find(moduleId);
  if (assignment == assignments_.end()) return output;
  output.playerId = state.pendingMove.playerId;
  if (assignment->second.mapIndex == state.pendingMove.target) {
    output.mode = TileMovementCueMode::Destination;
  } else if (assignment->second.mapIndex == state.pendingMove.origin) {
    output.mode = TileMovementCueMode::Departure;
  } else {
    output.playerId = 0;
  }
  return output;
}

std::uint64_t TileDebugAssignments::nowMs() const { return epochClock_(); }

TileDebugHeartbeatResponse TileDebugAssignments::heartbeat(
    std::uint32_t roomId, const GameState& state, const std::string& moduleId,
    const std::string& deviceId, const TileTagReport* tagReport,
    bool movementCueReady) {
  TileDebugHeartbeatResponse response{};
  response.leaseMs = kLeaseMs;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  if (!validIdentifier(moduleId)) {
    response.result = {TileDebugResultCode::InvalidModuleId, "invalid moduleId", false,
                       revision_};
    response.serverRevision = revision_;
    return response;
  }
  if (!validIdentifier(deviceId)) {
    response.result = {TileDebugResultCode::InvalidDeviceId, "invalid deviceId", false,
                       revision_};
    response.serverRevision = revision_;
    return response;
  }
  synchronizeContextLocked(roomId, state, now);
  if (state.board == nullptr) {
    response.result = {TileDebugResultCode::BoardUnavailable, "board unavailable", false,
                       revision_};
    response.serverRevision = revision_;
    return response;
  }
  const bool authorityChanged = refreshAuthorityProjectionLocked(state, now);

  auto module = modules_.find(moduleId);
  if (module == modules_.end()) {
    const auto duplicateDevice = std::find_if(
        modules_.begin(), modules_.end(), [&deviceId](const auto& entry) {
          return entry.second.deviceId == deviceId;
        });
    if (duplicateDevice != modules_.end()) {
      response.result = {TileDebugResultCode::DeviceConflict,
                         "deviceId is already registered to another module", false,
                         revision_};
      response.serverRevision = revision_;
      return response;
    }
    if (modules_.size() >= kMaximumModules) {
      response.result = {TileDebugResultCode::CapacityReached,
                         "tile module capacity reached", false, revision_};
      response.serverRevision = revision_;
      return response;
    }
    ModuleRecord record{};
    record.moduleId = moduleId;
    record.deviceId = deviceId;
    record.registrationOrder = nextRegistrationOrder_++;
    record.lastSeenMs = now;
    modules_.emplace(moduleId, std::move(record));
    if (!authorityChanged) commitRevisionLocked(now);
    autoAssignLocked(state, moduleId, now, false);
    response.result = {TileDebugResultCode::Ok, "ok", true, revision_};
  } else {
    if (module->second.deviceId != deviceId) {
      response.result = {TileDebugResultCode::DeviceMismatch,
                         "deviceId does not match the registered module", false,
                         revision_};
      response.serverRevision = revision_;
      return response;
    }
    module->second.lastSeenMs = now;
    const bool assignedNow = autoAssignLocked(state, moduleId, now, !authorityChanged);
    response.result = {TileDebugResultCode::Ok, "ok",
                       authorityChanged || assignedNow, revision_};
  }

  const auto assignment = assignments_.find(moduleId);
  if (assignment != assignments_.end()) {
    response.assigned = true;
    response.source = assignment->second.source;
    response.assignment = assignment->second;
  }
  const auto committedModule = modules_.find(moduleId);
  if (committedModule != modules_.end()) {
    if (tagReport != nullptr) applyTagReportLocked(committedModule->second, *tagReport, now);
    response.tagReaderState = committedModule->second.tagReaderState;
    response.tagOverflow = committedModule->second.tagOverflow;
    response.stableTags = committedModule->second.stableTags;
  }
  response.movementCue = movementCueLocked(state, moduleId, movementCueReady);
  response.serverRevision = revision_;
  return response;
}

TileDebugResult TileDebugAssignments::set(std::uint32_t roomId, const GameState& state,
                                          const std::string& moduleId,
                                          const std::string& deviceId,
                                          const std::string& tileId) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  if (!validIdentifier(moduleId)) {
    return {TileDebugResultCode::InvalidModuleId, "invalid moduleId", false, revision_};
  }
  if (!validIdentifier(deviceId)) {
    return {TileDebugResultCode::InvalidDeviceId, "invalid deviceId", false, revision_};
  }
  synchronizeContextLocked(roomId, state, now);
  if (state.board == nullptr) {
    return {TileDebugResultCode::BoardUnavailable, "board unavailable", false, revision_};
  }
  const bool authorityChanged = refreshAuthorityProjectionLocked(state, now);
  std::uint8_t mapIndex = 0;
  if (findTile(*state.board, tileId, mapIndex) == nullptr) {
    return {TileDebugResultCode::TileNotFound,
            "tileId is not on the current board", false, revision_};
  }
  const auto module = modules_.find(moduleId);
  if (module == modules_.end()) {
    return {TileDebugResultCode::ModuleOffline,
            "tile module is not online", false, revision_};
  }
  if (module->second.deviceId != deviceId) {
    return {TileDebugResultCode::DeviceMismatch,
            "deviceId does not match the registered module", false, revision_};
  }
  const auto conflict = std::find_if(
      assignments_.begin(), assignments_.end(),
      [&moduleId, mapIndex](const auto& entry) {
        return entry.first != moduleId && entry.second.mapIndex == mapIndex;
      });
  if (conflict != assignments_.end()) {
    return {TileDebugResultCode::TileConflict,
            "tile is assigned to another online module", false, revision_};
  }

  auto projected = projectAssignment(state, moduleId, deviceId, tileId,
                                     TileDebugAssignmentSource::Manual);
  const auto found = assignments_.find(moduleId);
  if (found != assignments_.end() && assignmentsEqual(found->second, projected)) {
    return {TileDebugResultCode::Ok, "ok", authorityChanged, revision_};
  }
  if (!authorityChanged) commitRevisionLocked(now);
  projected.revision = revision_;
  projected.updatedAtMs = now;
  assignments_[moduleId] = std::move(projected);
  return {TileDebugResultCode::Ok, "ok", true, revision_};
}

TileDebugResult TileDebugAssignments::clear(std::uint32_t roomId, const GameState& state,
                                            const std::string& moduleId) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  if (!validIdentifier(moduleId)) {
    return {TileDebugResultCode::InvalidModuleId, "invalid moduleId", false, revision_};
  }
  synchronizeContextLocked(roomId, state, now);
  const bool authorityChanged = refreshAuthorityProjectionLocked(state, now);
  if (modules_.find(moduleId) == modules_.end()) {
    return {TileDebugResultCode::ModuleOffline,
            "tile module is not online", false, revision_};
  }
  const auto found = assignments_.find(moduleId);
  if (found == assignments_.end()) {
    return {TileDebugResultCode::AssignmentNotFound,
            "temporary assignment not found", false, revision_};
  }
  assignments_.erase(found);
  if (!authorityChanged) commitRevisionLocked(now);
  return {TileDebugResultCode::Ok, "ok", true, revision_};
}

TileDebugSnapshot TileDebugAssignments::snapshot(std::uint32_t roomId,
                                                 const GameState& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  synchronizeContextLocked(roomId, state, now);
  refreshAuthorityProjectionLocked(state, now);
  TileDebugSnapshot output{};
  output.roomId = roomId;
  output.revision = revision_;
  output.updatedAtMs = updatedAtMs_;
  if (state.board != nullptr) {
    output.boardId = state.board->id;
    output.boardSize = state.board->tileCount;
    output.tiles.reserve(state.board->tileCount);
    for (std::uint8_t index = 0; index < state.board->tileCount; ++index) {
      output.tiles.push_back(projectTile(*state.board, index));
    }
  }
  output.players.reserve(kMaxPlayers);
  for (std::uint8_t playerId = 1; playerId <= kMaxPlayers; ++playerId) {
    output.players.push_back(projectPlayer(state, playerId));
  }
  output.assignments.reserve(assignments_.size());
  for (const auto& entry : assignments_) output.assignments.push_back(entry.second);
  std::sort(output.assignments.begin(), output.assignments.end(),
            [](const TileModuleDebugState& left, const TileModuleDebugState& right) {
              return left.moduleId < right.moduleId;
            });

  output.modules.reserve(modules_.size());
  for (const auto& entry : modules_) {
    TileDebugModule module{};
    module.moduleId = entry.second.moduleId;
    module.deviceId = entry.second.deviceId;
    module.online = true;
    module.lastSeenMs = entry.second.lastSeenMs;
    module.leaseRemainingMs = leaseRemainingLocked(entry.second, now);
    module.registrationOrder = entry.second.registrationOrder;
    module.tagReaderState = entry.second.tagReaderState;
    module.tagRevision = entry.second.tagRevision;
    module.tagOverflow = entry.second.tagOverflow;
    const auto assignment = assignments_.find(entry.first);
    if (assignment != assignments_.end()) {
      module.assigned = true;
      module.source = assignment->second.source;
    }
    output.modules.push_back(std::move(module));
  }
  std::sort(output.modules.begin(), output.modules.end(),
            [](const TileDebugModule& left, const TileDebugModule& right) {
              if (left.registrationOrder != right.registrationOrder) {
                return left.registrationOrder < right.registrationOrder;
              }
              return left.moduleId < right.moduleId;
            });
  return output;
}

TileTagSnapshot TileDebugAssignments::tagSnapshot(std::uint32_t roomId,
                                                  const GameState& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  synchronizeContextLocked(roomId, state, now);
  pruneTagHistoryLocked(now);

  std::unordered_map<std::uint32_t, TileDetectedTag> detected;
  for (const auto& entry : modules_) {
    const auto& module = entry.second;
    const auto assignment = assignments_.find(entry.first);
    for (const auto& seen : module.tagLastSeenMs) {
      auto& tag = detected[seen.first];
      tag.uid = seen.first;
      tag.lastSeenMs = std::max(tag.lastSeenMs, seen.second);
      const bool current = module.tagReaderState == TileTagReaderState::Stable &&
          std::find(module.stableTags.begin(), module.stableTags.end(), seen.first) !=
              module.stableTags.end();
      tag.currentlySeen = tag.currentlySeen || current;
      TileTagSighting sighting{};
      sighting.moduleId = module.moduleId;
      sighting.deviceId = module.deviceId;
      sighting.currentlySeen = current;
      sighting.lastSeenMs = seen.second;
      if (assignment != assignments_.end()) {
        sighting.tileId = assignment->second.tileId;
        sighting.mapIndex = assignment->second.mapIndex;
      }
      tag.sightings.push_back(std::move(sighting));
    }
  }

  TileTagSnapshot output{};
  output.roomId = roomId;
  output.revision = tagRevision_;
  output.updatedAtMs = tagUpdatedAtMs_;
  output.tags.reserve(detected.size());
  for (auto& entry : detected) {
    auto& sightings = entry.second.sightings;
    std::sort(sightings.begin(), sightings.end(), [](const TileTagSighting& left,
                                                      const TileTagSighting& right) {
      return left.moduleId < right.moduleId;
    });
    output.tags.push_back(std::move(entry.second));
  }
  std::sort(output.tags.begin(), output.tags.end(), [](const TileDetectedTag& left,
                                                       const TileDetectedTag& right) {
    return left.uid < right.uid;
  });
  return output;
}

TileMovementCue TileDebugAssignments::movementCue(std::uint32_t roomId,
                                                   const GameState& state,
                                                   const std::string& moduleId,
                                                   bool movementCueReady) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = nowMs();
  expireLocked(now);
  synchronizeContextLocked(roomId, state, now);
  return movementCueLocked(state, moduleId, movementCueReady);
}

}  // namespace gridopoly::pi
