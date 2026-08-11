#include "AuthorityService.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include <gridopoly/core/BoardCatalog.h>

#include "../../../Firmware/TestGameServer/src/PlayerDetailProjection.h"

namespace gridopoly::pi {
namespace {

using namespace gridopoly::core;
using namespace gridopoly::protocol;

constexpr Result error(ErrorCode code, const char* message) { return {code, message}; }

std::uint32_t randomNonZero() {
  std::random_device random;
  const auto high = static_cast<std::uint32_t>(random()) << 16;
  const auto low = static_cast<std::uint32_t>(random());
  const auto value = high ^ low;
  return value == 0 ? 1 : value;
}

std::uint64_t unixEpochMs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

std::filesystem::path defaultIdentityPath(const std::filesystem::path& statePath,
                                          const AuthorityIdentityOptions& options) {
  if (!options.identityPath.empty()) return options.identityPath;
  const auto parent = statePath.parent_path();
  return parent.empty() ? std::filesystem::path("identity.bin") : parent / "identity.bin";
}

bool sameRecipe(const AvatarRecipe& left, const AvatarRecipe& right) {
  return left.avatarCatalogVersion == right.avatarCatalogVersion &&
      left.hairPresetId == right.hairPresetId && left.hairColorId == right.hairColorId &&
      left.facePresetId == right.facePresetId && left.skinToneId == right.skinToneId &&
      left.outfitPresetId == right.outfitPresetId;
}

std::uint16_t nextRevision(std::uint16_t value) {
  value = static_cast<std::uint16_t>(value + 1);
  return value == 0 ? 1 : value;
}

std::uint32_t nextRevision(std::uint32_t value) {
  ++value;
  return value == 0 ? 1 : value;
}

std::string hex16(std::uint64_t value) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << value;
  return output.str();
}

TradeStatus protocolTradeStatus(TradeWorkflowStatus status) {
  switch (status) {
    case TradeWorkflowStatus::Offered: return TradeStatus::Offered;
    case TradeWorkflowStatus::Countered: return TradeStatus::Countered;
    case TradeWorkflowStatus::Settled: return TradeStatus::Settled;
    case TradeWorkflowStatus::Rejected: return TradeStatus::Rejected;
    case TradeWorkflowStatus::Cancelled: return TradeStatus::Cancelled;
    case TradeWorkflowStatus::Expired: return TradeStatus::Expired;
    case TradeWorkflowStatus::Invalidated: return TradeStatus::Invalidated;
    default: return TradeStatus::None;
  }
}

TradeResultCode protocolTradeResult(const Result& result, const TradeWorkflow* workflow) {
  if (result) return TradeResultCode::Ok;
  if (workflow != nullptr && workflow->status == TradeWorkflowStatus::Expired) {
    return TradeResultCode::Expired;
  }
  switch (result.code) {
    case ErrorCode::InvalidPlayer: return TradeResultCode::Unauthorized;
    case ErrorCode::InvalidArgument: return TradeResultCode::InvalidRequest;
    case ErrorCode::NotEnoughCash: return TradeResultCode::NotEnoughCash;
    case ErrorCode::NotOwner: return TradeResultCode::AssetUnavailable;
    default: return TradeResultCode::RuleViolation;
  }
}

const char* controllerName(ControllerKind kind) {
  switch (kind) {
    case ControllerKind::RealConsole: return "WIFI-UDP";
    case ControllerKind::Web: return "WEB";
    case ControllerKind::Bot: return "BOT";
    default: return "NONE";
  }
}

void appendJsonString(std::ostringstream& output, const char* value) {
  output << '"';
  if (value != nullptr) {
    while (*value != '\0') {
      const auto c = static_cast<unsigned char>(*value++);
      switch (c) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
          if (c >= 0x20) output << static_cast<char>(c);
          break;
      }
    }
  }
  output << '"';
}

std::uint32_t actionMaskFor(ActionCode action) {
  switch (action) {
    case ActionCode::Roll: return ActionRoll;
    case ActionCode::ConfirmPosition: return ActionConfirmPosition;
    case ActionCode::Buy: return ActionBuy;
    case ActionCode::Decline: return ActionDecline;
    case ActionCode::EndTurn: return ActionEndTurn;
    case ActionCode::PayHoldFee: return ActionPayHoldFee;
    case ActionCode::Mortgage: return ActionMortgage;
    case ActionCode::Unmortgage: return ActionUnmortgage;
    case ActionCode::Build: return ActionBuild;
    case ActionCode::SellBuilding: return ActionSellBuilding;
    case ActionCode::PayDebt: return ActionPayDebt;
    case ActionCode::DeclareBankruptcy: return ActionDeclareBankruptcy;
    case ActionCode::AuctionBid: return ActionAuctionBid;
    case ActionCode::AuctionPass: return ActionAuctionPass;
    case ActionCode::AuctionReady: return ActionAuctionReady;
    case ActionCode::CardContinue: return ActionCardContinue;
    default: return ActionNone;
  }
}

void appendWorkflowJson(std::ostringstream& out, const GameState& state) {
  out << "\"debt\":{\"active\":" << (state.pendingDebt.active ? "true" : "false")
      << ",\"debtor\":" << static_cast<unsigned>(state.pendingDebt.debtorId)
      << ",\"creditor\":" << static_cast<unsigned>(state.pendingDebt.creditorId)
      << ",\"asset\":" << static_cast<unsigned>(state.pendingDebt.assetIndex)
      << ",\"amount\":" << state.pendingDebt.amount << "},";
  out << "\"auction\":{\"active\":" << (state.auction.active ? "true" : "false")
      << ",\"opening\":"
      << (state.auction.active && state.auction.readyMask != state.auction.requiredReadyMask
              ? "true" : "false")
      << ",\"asset\":" << static_cast<unsigned>(state.auction.assetIndex)
      << ",\"bidder\":" << static_cast<unsigned>(state.auction.currentBidderId)
      << ",\"highestBidder\":" << static_cast<unsigned>(state.auction.highestBidderId)
      << ",\"readyMask\":" << static_cast<unsigned>(state.auction.readyMask)
      << ",\"requiredReadyMask\":" << static_cast<unsigned>(state.auction.requiredReadyMask)
      << ",\"generation\":" << state.auction.generation
      << ",\"currentBid\":" << state.auction.currentBid
      << ",\"minimumBid\":" << (state.auction.currentBid == 0 ? 10 : state.auction.currentBid + 10)
      << "},";
  out << "\"card\":{\"active\":" << (state.pendingCard.active ? "true" : "false")
      << ",\"stage\":" << static_cast<unsigned>(state.pendingCard.stage)
      << ",\"player\":" << static_cast<unsigned>(state.pendingCard.playerId)
      << ",\"deck\":" << static_cast<unsigned>(state.pendingCard.deckId)
      << ",\"catalog\":" << state.pendingCard.cardCatalogId
      << ",\"instance\":" << state.pendingCard.cardInstanceId << "},";
}

}  // namespace

AuthorityService::AuthorityService(std::filesystem::path statePath,
                                   std::filesystem::path metadataPath,
                                   std::uint32_t serverDeviceId,
                                   std::chrono::milliseconds botActionInterval,
                                   AuthorityIdentityOptions identityOptions)
    : store_(statePath), identityStore_(defaultIdentityPath(statePath, identityOptions)),
      metadataPath_(std::move(metadataPath)),
      serverDeviceId_(serverDeviceId == 0 ? randomNonZero() : serverDeviceId),
      botActionInterval_(std::clamp(
          botActionInterval, std::chrono::milliseconds(kMinimumBotActionIntervalMs),
          std::chrono::milliseconds(kMaximumBotActionIntervalMs))),
      epochClock_(identityOptions.epochClock ? std::move(identityOptions.epochClock)
                                             : std::function<std::uint64_t()>(unixEpochMs)) {
  if (!identityOptions.avatarComponentRoot.empty() && !identityOptions.avatarAssetRoot.empty()) {
    avatarRenderer_ = std::make_unique<AvatarRenderer>(
        std::move(identityOptions.avatarComponentRoot), std::move(identityOptions.avatarAssetRoot));
    if (avatarRenderer_->valid()) startAvatarWorker();
  }
}

AuthorityService::~AuthorityService() {
  stopAvatarWorker();
  std::lock_guard<std::mutex> lock(mutex_);
  flushLocked();
  saveIdentityLocked();
}

void AuthorityService::startAvatarWorker() {
  avatarWorker_ = std::thread([this]() { avatarWorkerLoop(); });
}

void AuthorityService::stopAvatarWorker() {
  {
    std::lock_guard<std::mutex> lock(avatarWorkerMutex_);
    stopAvatarWorker_ = true;
  }
  avatarWorkerCv_.notify_all();
  if (avatarWorker_.joinable()) avatarWorker_.join();
}

void AuthorityService::avatarWorkerLoop() {
  for (;;) {
    AvatarJob job{};
    {
      std::unique_lock<std::mutex> lock(avatarWorkerMutex_);
      avatarWorkerCv_.wait(lock, [this]() {
        return stopAvatarWorker_ || !avatarJobs_.empty();
      });
      if (stopAvatarWorker_ && avatarJobs_.empty()) return;
      job = avatarJobs_.front();
      avatarJobs_.pop_front();
    }
    AvatarCompletion completion{};
    completion.job = job;
    if (avatarRenderer_ != nullptr && avatarRenderer_->valid()) {
      completion.result = avatarRenderer_->render(job.roomId, job.playerId,
                                                  job.avatarRevision, job.recipe);
    }
    {
      std::lock_guard<std::mutex> lock(avatarWorkerMutex_);
      avatarCompletions_.push_back(std::move(completion));
    }
  }
}

void AuthorityService::queueAvatarLocked(std::uint8_t playerId,
                                         const AvatarRecipe& recipe,
                                         std::uint16_t avatarRevision,
                                         std::uint16_t seatRevision) {
  if (avatarRenderer_ == nullptr || !avatarRenderer_->valid()) return;
  {
    std::lock_guard<std::mutex> lock(avatarWorkerMutex_);
    avatarJobs_.push_back({roomId_, playerId, seatRevision, avatarRevision, recipe});
  }
  avatarWorkerCv_.notify_one();
}

void AuthorityService::drainAvatarCompletionsLocked() {
  std::deque<AvatarCompletion> completions;
  {
    std::lock_guard<std::mutex> lock(avatarWorkerMutex_);
    completions.swap(avatarCompletions_);
  }
  for (const auto& completion : completions) {
    if (completion.job.roomId != roomId_ || completion.job.playerId == 0 ||
        completion.job.playerId > identity_.playerCount) continue;
    auto& seat = identity_.seats[completion.job.playerId - 1];
    if (!seat.avatarGenerating || seat.seatRevision != completion.job.seatRevision ||
        !sameRecipe(seat.pendingRecipe, completion.job.recipe)) continue;
    seat.avatarGenerating = false;
    seat.pendingRecipe = {};
    if (completion.result.ok) {
      seat.avatarFinal = true;
      seat.avatarRevision = completion.job.avatarRevision;
      seat.avatarContentHash64 = completion.result.contentHash64;
      seat.recipe = completion.job.recipe;
      if (seat.bot && seat.nameFinal) seat.ready = true;
    } else {
      seat.avatarFinal = false;
      seat.ready = false;
    }
    seat.seatRevision = nextRevision(seat.seatRevision);
    identity_.identityRevision = nextRevision(identity_.identityRevision);
    touchIdentityVersionLocked();
    maybeStartIdentityCountdownLocked();
    saveIdentityLocked();
  }
}

bool AuthorityService::saveIdentityLocked() const {
  return validIdentityRoomState(identity_) && identityStore_.save(identity_);
}

void AuthorityService::initializeCompatibilityIdentityLocked() {
  const auto& state = engine_.state();
  std::uint8_t humanCount = 0;
  while (humanCount < state.playerCount &&
         state.players[humanCount].controller == ControllerKind::RealConsole) {
    ++humanCount;
  }
  if (humanCount == 0 || humanCount > state.playerCount) humanCount = 1;
  const auto botCount = static_cast<std::uint8_t>(state.playerCount - humanCount);
  initializeIdentityRoom(identity_, roomId_, state.rngState == 0 ? randomNonZero() : state.rngState,
                         humanCount, botCount);
  identity_.phase = IdentityRoomPhase::Active;
  identity_.countdownDeadlineEpochMs = 0;
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    auto& seat = identity_.seats[index];
    seat.connected = state.players[index].connected || seat.bot;
    seat.name.fill('\0');
    std::strncpy(seat.name.data(), state.players[index].name, seat.name.size() - 1);
    seat.nameFinal = true;
    seat.ready = false;
    seat.avatarGenerating = true;
    seat.pendingRecipe = deterministicBotRecipe(identity_.roomSeed, seat.playerId,
                                                identity_.avatarCatalogVersion);
    queueAvatarLocked(seat.playerId, seat.pendingRecipe, 1, seat.seatRevision);
  }
  saveIdentityLocked();
}

bool AuthorityService::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadMetadataLocked();
  const bool restored = store_.restore(engine_);
  if (!restored) {
    const auto* board = BoardCatalog::findBySize(32);
    if (board == nullptr || !engine_.reset(*board, randomNonZero()) ||
        !engine_.addPlayer("Player Console", ControllerKind::RealConsole, false)) return false;
    for (std::uint8_t index = 0; index < 3; ++index) {
      char name[20]{};
      std::snprintf(name, sizeof(name), "Bot %u", static_cast<unsigned>(index + 1));
      if (!engine_.addPlayer(name, ControllerKind::Bot, true)) return false;
    }
    if (!engine_.start()) return false;
  } else {
    auto& state = engine_.mutableStateForRestore();
    for (std::uint8_t index = 0; index < state.playerCount; ++index) {
      if (state.players[index].controller == ControllerKind::RealConsole) {
        state.players[index].connected = false;
      }
    }
    ++state.stateVersion;
  }
  if (roomId_ == 0) roomId_ = randomNonZero();
  IdentityRoomState restoredIdentity{};
  if (identityStore_.restore(restoredIdentity) && restoredIdentity.roomId == roomId_ &&
      restoredIdentity.playerCount == engine_.state().playerCount) {
    identity_ = restoredIdentity;
    bool identityChanged = false;
    for (std::uint8_t index = 0; index < identity_.playerCount; ++index) {
      auto& seat = identity_.seats[index];
      if (seat.human && seat.connected) {
        seat.connected = false;
        identityChanged = true;
      }
      if (seat.avatarGenerating) {
        queueAvatarLocked(seat.playerId, seat.pendingRecipe,
                          nextRevision(seat.avatarRevision), seat.seatRevision);
      }
    }
    if (identityChanged) {
      identity_.identityRevision = nextRevision(identity_.identityRevision);
      saveIdentityLocked();
    }
  } else {
    initializeCompatibilityIdentityLocked();
  }
  if (forcedRoll_.active && !restored) {
    clearForcedRollLocked();
    ++controlVersion_;
    if (controlVersion_ == 0) controlVersion_ = 1;
  } else if (forcedRoll_.active) {
    ForcedRollState validated{};
    std::uint8_t dieA = 0;
    std::uint8_t dieB = 0;
    if (!validateForcedRollLocked(forcedRoll_.playerId, forcedRoll_.targetTile, 0,
                                  validated, dieA, dieB) ||
        validated.originTile != forcedRoll_.originTile) {
      clearForcedRollLocked();
      ++controlVersion_;
      if (controlVersion_ == 0) controlVersion_ = 1;
    } else {
      forcedRoll_ = validated;
    }
  }
  persistedVersion_ = restored ? engine_.state().stateVersion - 1 : 0;
  observedVersion_ = engine_.state().stateVersion;
  noteChangedLocked();
  lastBotAt_ = std::chrono::steady_clock::now();
  maybeActivateIdentityRoomLocked();
  return saveMetadataLocked();
}

bool AuthorityService::loadMetadataLocked() {
  std::ifstream input(metadataPath_, std::ios::binary);
  std::uint32_t magic = 0;
  std::uint32_t storedRoom = 0;
  std::uint32_t storedDevice = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&storedRoom), sizeof(storedRoom));
  input.read(reinterpret_cast<char*>(&storedDevice), sizeof(storedDevice));
  if (input && magic == 0x314D5047u && storedRoom != 0) {
    roomId_ = storedRoom;
    if (storedDevice != 0) serverDeviceId_ = storedDevice;
    std::uint32_t storedBotIntervalMs = 0;
    input.read(reinterpret_cast<char*>(&storedBotIntervalMs), sizeof(storedBotIntervalMs));
    if (input && storedBotIntervalMs >= kMinimumBotActionIntervalMs &&
        storedBotIntervalMs <= kMaximumBotActionIntervalMs) {
      botActionInterval_ = std::chrono::milliseconds(storedBotIntervalMs);
    }
    if (!input) return true;
    std::uint32_t storedControlVersion = 0;
    input.read(reinterpret_cast<char*>(&storedControlVersion), sizeof(storedControlVersion));
    if (!input) return true;
    std::uint8_t active = 0;
    std::uint8_t playerId = 0;
    std::uint8_t targetTile = kNoAsset;
    std::uint8_t originTile = kNoAsset;
    input.read(reinterpret_cast<char*>(&active), sizeof(active));
    input.read(reinterpret_cast<char*>(&playerId), sizeof(playerId));
    input.read(reinterpret_cast<char*>(&targetTile), sizeof(targetTile));
    input.read(reinterpret_cast<char*>(&originTile), sizeof(originTile));
    if (input) {
      controlVersion_ = storedControlVersion == 0 ? 1 : storedControlVersion;
      forcedRoll_.active = active != 0;
      forcedRoll_.playerId = playerId;
      forcedRoll_.targetTile = targetTile;
      forcedRoll_.originTile = originTile;
    }
    return true;
  }
  roomId_ = randomNonZero();
  return false;
}

bool AuthorityService::saveMetadataLocked() const {
  std::error_code error;
  if (!metadataPath_.parent_path().empty()) {
    std::filesystem::create_directories(metadataPath_.parent_path(), error);
    if (error) return false;
  }
  auto temporary = metadataPath_;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  const std::uint32_t magic = 0x314D5047u;
  output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  output.write(reinterpret_cast<const char*>(&roomId_), sizeof(roomId_));
  output.write(reinterpret_cast<const char*>(&serverDeviceId_), sizeof(serverDeviceId_));
  const auto botIntervalMs = static_cast<std::uint32_t>(botActionInterval_.count());
  output.write(reinterpret_cast<const char*>(&botIntervalMs), sizeof(botIntervalMs));
  output.write(reinterpret_cast<const char*>(&controlVersion_), sizeof(controlVersion_));
  const std::uint8_t forcedActive = forcedRoll_.active ? 1 : 0;
  output.write(reinterpret_cast<const char*>(&forcedActive), sizeof(forcedActive));
  output.write(reinterpret_cast<const char*>(&forcedRoll_.playerId), sizeof(forcedRoll_.playerId));
  output.write(reinterpret_cast<const char*>(&forcedRoll_.targetTile), sizeof(forcedRoll_.targetTile));
  output.write(reinterpret_cast<const char*>(&forcedRoll_.originTile), sizeof(forcedRoll_.originTile));
  output.flush();
  if (!output) return false;
  output.close();
  std::filesystem::rename(temporary, metadataPath_, error);
  if (!error) return true;
  std::filesystem::remove(metadataPath_, error);
  error.clear();
  std::filesystem::rename(temporary, metadataPath_, error);
  return !error;
}

void AuthorityService::noteChangedLocked() {
  const auto now = std::chrono::steady_clock::now();
  if (!persistPending_) dirtySince_ = now;
  persistPending_ = true;
  persistDue_ = now + std::chrono::milliseconds(750);
  observedVersion_ = engine_.state().stateVersion;
}

void AuthorityService::touchIdentityVersionLocked() {
  auto& version = engine_.mutableStateForRestore().stateVersion;
  version = nextRevision(version);
  noteChangedLocked();
}

bool AuthorityService::flushLocked() {
  if (!persistPending_ && persistedVersion_ == engine_.state().stateVersion) {
    return saveIdentityLocked();
  }
  if (!store_.save(engine_.state())) return false;
  if (!saveIdentityLocked()) return false;
  persistedVersion_ = engine_.state().stateVersion;
  persistPending_ = false;
  return true;
}

void AuthorityService::maybeStartIdentityCountdownLocked() {
  if (identity_.phase != IdentityRoomPhase::AvatarSetup || identity_.playerCount == 0) return;
  for (std::uint8_t index = 0; index < identity_.playerCount; ++index) {
    if (!identity_.seats[index].ready) return;
  }
  identity_.phase = IdentityRoomPhase::Countdown;
  identity_.countdownDeadlineEpochMs = epochClock_() + 5000;
  identity_.identityRevision = nextRevision(identity_.identityRevision);
  touchIdentityVersionLocked();
}

void AuthorityService::maybeActivateIdentityRoomLocked() {
  if (identity_.phase != IdentityRoomPhase::Countdown ||
      epochClock_() < identity_.countdownDeadlineEpochMs) return;
  const auto result = engine_.start();
  if (!result) return;
  identity_.phase = IdentityRoomPhase::Active;
  identity_.countdownDeadlineEpochMs = 0;
  identity_.identityRevision = nextRevision(identity_.identityRevision);
  noteChangedLocked();
  saveIdentityLocked();
}

bool AuthorityService::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  return flushLocked();
}

void AuthorityService::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  drainAvatarCompletionsLocked();
  maybeStartIdentityCountdownLocked();
  maybeActivateIdentityRoomLocked();
  engine_.expireTrades(unixEpochMs());
  if (identity_.phase == IdentityRoomPhase::Active &&
      now - lastBotAt_ >= botActionInterval_) {
    lastBotAt_ = now;
    engine_.runTradeBots(unixEpochMs(), 1);
    const auto& state = engine_.state();
    const auto activeIndex = state.activePlayerId == 0
        ? state.playerCount
        : static_cast<std::uint8_t>(state.activePlayerId - 1);
    if (forcedRoll_.active && state.phase == GamePhase::AwaitRoll &&
        activeIndex < state.playerCount &&
        state.players[activeIndex].controller == ControllerKind::Bot &&
        forcedRoll_.playerId == state.activePlayerId) {
      const auto result = executeRollLocked(state.activePlayerId);
      if (!result) engine_.runBots(1);
    } else {
      engine_.runBots(1);
    }
  }
  if (engine_.state().stateVersion != observedVersion_) noteChangedLocked();
  if (persistPending_ &&
      (now >= persistDue_ || now - dirtySince_ >= std::chrono::seconds(5))) {
    flushLocked();
  }
}

Result AuthorityService::newGame(std::uint8_t boardSize, std::uint8_t botCount) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto* board = BoardCatalog::findBySize(boardSize);
  if (board == nullptr || botCount > 5) return error(ErrorCode::InvalidArgument, "unsupported board or bot count");
  ++roomId_;
  if (roomId_ == 0) roomId_ = 1;
  auto result = engine_.reset(*board, randomNonZero());
  if (result) result = engine_.addPlayer("Player Console", ControllerKind::RealConsole, false);
  for (std::uint8_t index = 0; result && index < botCount; ++index) {
    char name[20]{};
    std::snprintf(name, sizeof(name), "Bot %u", static_cast<unsigned>(index + 1));
    result = engine_.addPlayer(name, ControllerKind::Bot, true);
  }
  if (result) result = engine_.start();
  if (result) {
    {
      std::lock_guard<std::mutex> workerLock(avatarWorkerMutex_);
      avatarJobs_.clear();
      avatarCompletions_.clear();
    }
    peerCount_ = 0;
    if (forcedRoll_.active) {
      clearForcedRollLocked();
      ++controlVersion_;
      if (controlVersion_ == 0) controlVersion_ = 1;
    }
    store_.clear();
    identityStore_.clear();
    persistedVersion_ = 0;
    noteChangedLocked();
    initializeCompatibilityIdentityLocked();
    flushLocked();
    saveMetadataLocked();
  }
  return result;
}

Result AuthorityService::newGame(std::uint8_t boardSize, std::uint8_t humanCount,
                                 std::uint8_t botCount) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto* board = BoardCatalog::findBySize(boardSize);
  const auto playerCountWide = static_cast<unsigned>(humanCount) + botCount;
  if (board == nullptr || humanCount == 0 || humanCount > 6 || botCount > 5 ||
      playerCountWide < 2 || playerCountWide > 6) {
    return error(ErrorCode::InvalidArgument, "unsupported board or player counts");
  }
  const auto playerCount = static_cast<std::uint8_t>(playerCountWide);
  if (avatarRenderer_ == nullptr || !avatarRenderer_->valid()) {
    return error(ErrorCode::RuleViolation, "avatar renderer is unavailable");
  }

  ++roomId_;
  if (roomId_ == 0) roomId_ = 1;
  const auto roomSeed = randomNonZero();
  auto result = engine_.reset(*board, roomSeed);
  for (std::uint8_t index = 0; result && index < humanCount; ++index) {
    char name[20]{};
    std::snprintf(name, sizeof(name), "Player %u", static_cast<unsigned>(index + 1));
    result = engine_.addPlayer(name, ControllerKind::RealConsole, false);
  }
  for (std::uint8_t index = 0; result && index < botCount; ++index) {
    char name[20]{};
    std::snprintf(name, sizeof(name), "Bot %u", static_cast<unsigned>(index + 1));
    result = engine_.addPlayer(name, ControllerKind::Bot, true);
  }
  if (result && !initializeIdentityRoom(identity_, roomId_, roomSeed, humanCount, botCount)) {
    result = error(ErrorCode::RuleViolation, "identity room initialization failed");
  }
  if (!result) return result;

  {
    std::lock_guard<std::mutex> workerLock(avatarWorkerMutex_);
    avatarJobs_.clear();
    avatarCompletions_.clear();
  }
  for (std::uint8_t index = humanCount; index < playerCount; ++index) {
    const auto& seat = identity_.seats[index];
    queueAvatarLocked(seat.playerId, seat.pendingRecipe, 1, seat.seatRevision);
  }
  peerCount_ = 0;
  clearForcedRollLocked();
  ++controlVersion_;
  if (controlVersion_ == 0) controlVersion_ = 1;
  store_.clear();
  identityStore_.clear();
  persistedVersion_ = 0;
  noteChangedLocked();
  if (!flushLocked() || !saveMetadataLocked()) {
    return error(ErrorCode::RuleViolation, "new game persist failed");
  }
  lastBotAt_ = std::chrono::steady_clock::now();
  return {};
}

Result AuthorityService::execute(ActionCode action, std::uint8_t playerId,
                                 std::uint8_t assetIndex, std::int32_t argument,
                                 std::uint32_t expectedStateVersion) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto before = engine_.state().stateVersion;
  const auto result = executeLocked(action, playerId, assetIndex, argument, expectedStateVersion);
  if (engine_.state().stateVersion != before) noteChangedLocked();
  return result;
}

Result AuthorityService::executeLocked(ActionCode action, std::uint8_t playerId,
                                       std::uint8_t assetIndex, std::int32_t argument,
                                       std::uint32_t expectedStateVersion) {
  if (action != ActionCode::AuctionReady && expectedStateVersion != 0 &&
      expectedStateVersion != engine_.state().stateVersion) {
    return error(ErrorCode::RuleViolation, "state version mismatch");
  }
  const auto required = actionMaskFor(action);
  if (required == ActionNone) return error(ErrorCode::InvalidArgument, "unknown action");
  if (action != ActionCode::AuctionReady && (engine_.actionsFor(playerId) & required) == 0) {
    return error(ErrorCode::RuleViolation, "action not allowed in current state");
  }
  switch (action) {
    case ActionCode::Roll: return executeRollLocked(playerId);
    case ActionCode::ConfirmPosition:
      return engine_.confirmPosition(playerId, argument < 0 || argument > 255
          ? engine_.state().pendingMove.target : static_cast<std::uint8_t>(argument));
    case ActionCode::Buy: return engine_.buy(playerId);
    case ActionCode::Decline: return engine_.declinePurchase(playerId);
    case ActionCode::EndTurn: return engine_.endTurn(playerId);
    case ActionCode::PayHoldFee: return engine_.payHoldFee(playerId);
    case ActionCode::Mortgage: return engine_.mortgage(playerId, assetIndex);
    case ActionCode::Unmortgage: return engine_.unmortgage(playerId, assetIndex);
    case ActionCode::Build: return engine_.build(playerId, assetIndex);
    case ActionCode::SellBuilding: return engine_.sellBuilding(playerId, assetIndex);
    case ActionCode::PayDebt: return engine_.payDebt(playerId);
    case ActionCode::DeclareBankruptcy: return engine_.declareBankruptcy(playerId);
    case ActionCode::AuctionBid: return engine_.auctionBid(playerId, argument);
    case ActionCode::AuctionPass: return engine_.auctionPass(playerId);
    case ActionCode::AuctionReady:
      return engine_.auctionReady(playerId, assetIndex, static_cast<std::uint32_t>(argument));
    case ActionCode::CardContinue:
      return engine_.continueCard(playerId, static_cast<std::uint16_t>(argument));
    default: return error(ErrorCode::InvalidArgument, "unknown action");
  }
}

bool AuthorityService::makeSnapshot(std::uint8_t seatId, StateSnapshot& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& state = engine_.state();
  if (state.board == nullptr || seatId == 0 || seatId > state.playerCount) return false;
  const auto& self = state.players[seatId - 1];
  output = StateSnapshot{};
  output.seatId = seatId;
  output.phase = static_cast<std::uint8_t>(state.phase);
  output.activePlayerId = state.activePlayerId;
  output.round = state.roundNumber;
  output.boardSize = state.board->tileCount;
  output.selfPosition = self.position;
  output.selfCash = self.cash;
  output.availableActions = engine_.actionsFor(seatId);
  output.playerCount = state.playerCount;
  output.pendingTarget = state.pendingMove.active ? state.pendingMove.target : 0xFF;
  output.stateVersion = state.stateVersion;
  output.decisionPlayerId = engine_.decisionPlayerId();
  if (state.pendingDebt.active) {
    output.debtCreditorId = state.pendingDebt.creditorId;
    output.debtAssetIndex = state.pendingDebt.assetIndex;
    output.debtAmount = state.pendingDebt.amount;
  }
  if (state.auction.active) {
    output.auctionAssetIndex = state.auction.assetIndex;
    output.auctionCurrentBid = state.auction.currentBid;
    output.auctionMinimumBid = state.auction.currentBid == 0 ? 10 : state.auction.currentBid + 10;
    output.auctionHighestBidderId = state.auction.highestBidderId;
  }
  const auto& tile = state.board->tiles[self.position];
  output.tileAssetIndex = tile.assetIndex;
  if (tile.assetIndex != kNoAsset) {
    const auto& asset = state.assets[tile.assetIndex];
    output.tileOwnerId = asset.ownerId;
    output.tileBuildingLevel = asset.buildingLevel;
    output.tileFlags = asset.mortgaged ? 1 : 0;
  }
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    const auto& player = state.players[index];
    output.players[index] = {player.id, player.position, player.cash,
      static_cast<std::uint8_t>((player.inHold ? 1u : 0u) | (player.bankrupt ? 2u : 0u) |
          (player.connected ? 4u : 0u) | (static_cast<std::uint8_t>(player.controller) << 3))};
  }
  return true;
}

bool AuthorityService::makeAuthoritySnapshot(AuthoritySnapshot& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& state = engine_.state();
  if (state.board == nullptr || state.playerCount > output.players.size() ||
      state.board->assetCount > output.assets.size()) return false;
  output = AuthoritySnapshot{};
  output.phase = static_cast<std::uint8_t>(state.phase);
  output.activePlayerId = state.activePlayerId;
  output.decisionPlayerId = engine_.decisionPlayerId();
  output.winnerPlayerId = state.winnerPlayerId;
  output.boardSize = state.board->tileCount;
  output.playerCount = state.playerCount;
  output.assetCount = state.board->assetCount;
  output.round = state.roundNumber;
  output.stateVersion = state.stateVersion;
  output.lastEventSequence = state.nextEventSequence == 0 ? 0 : state.nextEventSequence - 1;
  output.boardIdHash = crc32(reinterpret_cast<const std::uint8_t*>(state.board->id),
                             std::strlen(state.board->id));
  if (state.pendingMove.active) {
    output.pendingMoveFlags = 1u | (state.pendingMove.passedStart ? 2u : 0u);
    output.pendingMovePlayerId = state.pendingMove.playerId;
    output.pendingMoveOrigin = state.pendingMove.origin;
    output.pendingMoveTarget = state.pendingMove.target;
    output.pendingMoveDieA = state.pendingMove.dieA;
    output.pendingMoveDieB = state.pendingMove.dieB;
  }
  if (state.pendingPurchase.active) {
    output.pendingPurchaseFlags = 1;
    output.pendingPurchasePlayerId = state.pendingPurchase.playerId;
    output.pendingPurchaseAssetIndex = state.pendingPurchase.assetIndex;
  }
  if (state.pendingDebt.active) {
    output.debtFlags = 1;
    output.debtDebtorId = state.pendingDebt.debtorId;
    output.debtCreditorId = state.pendingDebt.creditorId;
    output.debtAssetIndex = state.pendingDebt.assetIndex;
    output.debtPaymentEvent = static_cast<std::uint8_t>(state.pendingDebt.paymentEvent);
    output.debtContinuation = static_cast<std::uint8_t>(state.pendingDebt.continuation);
    output.debtDieA = state.pendingDebt.dieA;
    output.debtDieB = state.pendingDebt.dieB;
    output.debtAmount = state.pendingDebt.amount;
  }
  if (state.auction.active) {
    output.auctionFlags = 1u | (state.auction.readyMask == state.auction.requiredReadyMask ? 0u : 2u);
    output.auctionAssetIndex = state.auction.assetIndex;
    output.auctionLandingPlayerId = state.auction.landingPlayerId;
    output.auctionCurrentBidderId = state.auction.currentBidderId;
    output.auctionHighestBidderId = state.auction.highestBidderId;
    output.auctionPassedMask = state.auction.passedMask;
    output.auctionReadyMask = state.auction.readyMask;
    output.auctionRequiredReadyMask = state.auction.requiredReadyMask;
    output.auctionCurrentBid = state.auction.currentBid;
    output.auctionGeneration = state.auction.generation;
  }
  if (state.pendingCard.active) {
    output.pendingCardFlags = 1u | 2u;
    if (state.pendingCard.stage == PendingCardStage::AwaitSettlement) output.pendingCardFlags |= 4u | 8u;
    output.pendingCardPlayerId = state.pendingCard.playerId;
    output.pendingCardDeckId = state.pendingCard.deckId;
    output.pendingCardIndex = state.pendingCard.cardIndex;
    output.pendingCardInstanceId = state.pendingCard.cardInstanceId;
    output.pendingCardCatalogId = state.pendingCard.cardCatalogId;
    output.pendingCardEffectId = state.pendingCard.effectId;
    output.pendingCardDisplayAmount = state.pendingCard.displayAmount;
    output.pendingCardTargetPlayerId = state.pendingCard.targetPlayerId;
    output.pendingCardTargetPosition = state.pendingCard.targetPosition;
    output.pendingCardDrawEventSequence = state.pendingCard.drawEventSequence;
  }
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    const auto& source = state.players[index];
    auto& target = output.players[index];
    target.playerId = source.id;
    target.position = source.position;
    target.cash = source.cash;
    target.flags = static_cast<std::uint8_t>((source.inHold ? 1u : 0u) |
        (source.bankrupt ? 2u : 0u) | (source.connected ? 4u : 0u) |
        (static_cast<std::uint8_t>(source.controller) << 3));
    target.failedHoldRolls = source.failedHoldRolls;
    target.doublesStreak = source.doublesStreak;
  }
  for (std::uint8_t index = 0; index < state.board->assetCount; ++index) {
    output.assets[index].ownerId = state.assets[index].ownerId;
    output.assets[index].buildingLevel = state.assets[index].buildingLevel;
    output.assets[index].flags = state.assets[index].mortgaged ? 1u : 0u;
  }
  return true;
}

bool AuthorityService::makeRosterSnapshot(RosterSnapshot& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& state = engine_.state();
  if (state.board == nullptr || state.playerCount > output.playerIds.size()) return false;
  output = RosterSnapshot{};
  output.stateVersion = state.stateVersion;
  output.playerCount = state.playerCount;
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    output.playerIds[index] = state.players[index].id;
    const char* publishedName = state.players[index].name;
    if (identity_.roomId == roomId_ && index < identity_.playerCount &&
        identity_.seats[index].human && !identity_.seats[index].nameFinal) {
      publishedName = "";
    }
    std::strncpy(output.displayNames[index].data(), publishedName,
                 output.displayNames[index].size() - 1);
    output.displayNames[index].back() = '\0';
  }
  return true;
}

bool AuthorityService::makePlayerDetail(std::uint32_t requestId, std::uint8_t targetPlayerId,
                                        std::uint32_t requestedStateVersion,
                                        PlayerDetailResponse& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return gridopoly::server::makePlayerDetailProjection(engine_.state(), requestId,
      targetPlayerId, requestedStateVersion, output);
}

void AuthorityService::projectIdentityLocked(std::uint8_t sourceSeat,
                                             std::uint32_t requestId,
                                             IdentityOperation operation,
                                             IdentityResultCode result,
                                             std::uint8_t flags,
                                             IdentitySnapshot& output) const {
  output = IdentitySnapshot{};
  output.roomPhase = identity_.phase;
  output.result = result;
  output.requestId = requestId;
  output.stateVersion = engine_.state().stateVersion;
  output.identityRevision = identity_.identityRevision;
  output.serverEpochMs = epochClock_();
  output.countdownDeadlineEpochMs = identity_.countdownDeadlineEpochMs;
  output.avatarCatalogVersion = identity_.avatarCatalogVersion;
  output.playerCount = identity_.playerCount;
  output.selfPlayerId = sourceSeat;
  output.operationEcho = requestId == 0 ? IdentityOperation::None : operation;
  output.flags = flags;
  for (std::uint8_t index = 0; index < identity_.playerCount; ++index) {
    const auto& source = identity_.seats[index];
    const auto bit = static_cast<std::uint8_t>(1u << index);
    if (source.human) output.requiredHumanMask |= bit;
    if (source.avatarFinal) output.avatarFinalMask |= bit;
    if (source.nameFinal) output.nameFinalMask |= bit;
    if (source.ready) output.readyMask |= bit;
    if (source.connected) output.onlineMask |= bit;
    auto& target = output.seats[index];
    target.playerId = source.playerId;
    auto seatFlags = static_cast<std::uint8_t>(
        IdentitySeatPresent | (source.human ? IdentitySeatHuman : IdentitySeatBot));
    if (source.avatarGenerating) seatFlags |= IdentitySeatAvatarGenerating;
    if (source.avatarFinal) seatFlags |= IdentitySeatAvatarFinal;
    if (source.nameFinal) seatFlags |= IdentitySeatNameFinal;
    if (source.ready) seatFlags |= IdentitySeatReady;
    if (source.connected) seatFlags |= IdentitySeatConnected;
    target.flags = seatFlags;
    target.seatColorId = source.seatColorId;
    target.seatRevision = source.seatRevision;
    if (source.avatarFinal) {
      target.avatarRevision = source.avatarRevision;
      target.avatarContentHash64 = source.avatarContentHash64;
      target.recipe = source.recipe;
    }
  }
  const auto& self = identity_.seats[sourceSeat - 1];
  if (identity_.phase == IdentityRoomPhase::Active) {
    output.selfStage = IdentitySeatStage::Active;
  } else if (identity_.phase == IdentityRoomPhase::Countdown) {
    output.selfStage = IdentitySeatStage::Countdown;
  } else if (self.avatarGenerating) {
    output.selfStage = IdentitySeatStage::AvatarGenerating;
  } else if (!self.avatarFinal) {
    output.selfStage = IdentitySeatStage::AvatarSetup;
  } else if (!self.nameFinal) {
    output.selfStage = IdentitySeatStage::NameSetup;
  } else {
    output.selfStage = IdentitySeatStage::Ready;
  }
}

bool AuthorityService::makeIdentitySnapshot(std::uint8_t sourceSeat,
                                            IdentitySnapshot& output,
                                            bool resync) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourceSeat == 0 || sourceSeat > identity_.playerCount ||
      !validIdentityRoomState(identity_)) return false;
  projectIdentityLocked(sourceSeat, 0, IdentityOperation::None, IdentityResultCode::Ok,
                        resync ? IdentitySnapshotFlagResync : 0, output);
  return true;
}

void AuthorityService::handleIdentityRequest(std::uint8_t sourceSeat,
                                             const IdentityRequest& request,
                                             IdentitySnapshot& output) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourceSeat == 0 || sourceSeat > identity_.playerCount ||
      request.playerId != sourceSeat || !identity_.seats[sourceSeat - 1].human) {
    const auto safeSeat = sourceSeat >= 1 && sourceSeat <= identity_.playerCount
        ? sourceSeat : static_cast<std::uint8_t>(1);
    projectIdentityLocked(safeSeat, request.requestId, request.operation,
                          IdentityResultCode::Unauthorized, 0, output);
    return;
  }
  auto& seat = identity_.seats[sourceSeat - 1];
  std::array<std::uint8_t, kIdentityRequestSize> canonical{};
  std::size_t requestBytes = 0;
  if (!encodeIdentityRequest(request, canonical.data(), canonical.size(), requestBytes) ||
      requestBytes != canonical.size()) {
    projectIdentityLocked(sourceSeat, request.requestId, request.operation,
                          IdentityResultCode::InvalidRequest, 0, output);
    return;
  }
  if (seat.hasCachedRequest && seat.lastRequestId == request.requestId) {
    if (std::memcmp(seat.lastRequest.data(), canonical.data(), canonical.size()) != 0) {
      projectIdentityLocked(sourceSeat, request.requestId, request.operation,
                            IdentityResultCode::RequestIdConflict, 0, output);
      return;
    }
    if (decodeIdentitySnapshot(seat.cachedResponse.data(), seat.cachedResponse.size(), output)) {
      output.flags |= IdentitySnapshotFlagReplay;
      // The authoritative result is replayed verbatim, but serverEpochMs is a
      // sampling timestamp rather than mutation state. Refresh it so a retry
      // during the persisted countdown cannot restart the client's local
      // five-second display window.
      output.serverEpochMs = epochClock_();
      return;
    }
  }

  auto result = IdentityResultCode::Ok;
  if (request.operation != IdentityOperation::Query) {
    if (identity_.phase != IdentityRoomPhase::AvatarSetup) {
      result = IdentityResultCode::NotAllowed;
    } else if (request.expectedStateVersion != engine_.state().stateVersion) {
      result = IdentityResultCode::StateVersionStale;
    } else if (request.expectedSeatRevision != seat.seatRevision) {
      result = IdentityResultCode::SeatRevisionStale;
    }
  }

  if (result == IdentityResultCode::Ok && request.operation == IdentityOperation::ConfirmAvatar) {
    if (seat.nameFinal || request.avatarCatalogVersion != identity_.avatarCatalogVersion) {
      result = seat.nameFinal ? IdentityResultCode::NotAllowed
                              : IdentityResultCode::CatalogMismatch;
    } else if (!validAvatarRecipe(request.recipe)) {
      result = IdentityResultCode::InvalidRecipe;
    } else if (avatarRenderer_ == nullptr || !avatarRenderer_->valid()) {
      result = IdentityResultCode::AvatarGenerationFailed;
    } else {
      const auto desiredAvatarRevision = nextRevision(seat.avatarRevision);
      seat.avatarGenerating = true;
      seat.pendingRecipe = request.recipe;
      seat.ready = false;
      seat.seatRevision = nextRevision(seat.seatRevision);
      identity_.identityRevision = nextRevision(identity_.identityRevision);
      touchIdentityVersionLocked();
      queueAvatarLocked(sourceSeat, request.recipe, desiredAvatarRevision,
                        seat.seatRevision);
    }
  } else if (result == IdentityResultCode::Ok &&
             request.operation == IdentityOperation::ConfirmName) {
    std::array<char, 17> display{};
    std::string folded;
    const auto name = std::string_view(request.name.data(), request.nameLength);
    if (seat.nameFinal || !seat.avatarFinal || seat.avatarGenerating) {
      result = IdentityResultCode::NotAllowed;
    } else if (!validateIdentityName(name, display, folded)) {
      result = IdentityResultCode::InvalidName;
    } else if (identityNameExists(identity_, std::string_view(display.data()), sourceSeat)) {
      result = IdentityResultCode::DuplicateName;
    } else {
      seat.name = display;
      seat.nameFinal = true;
      seat.ready = true;
      seat.seatRevision = nextRevision(seat.seatRevision);
      identity_.identityRevision = nextRevision(identity_.identityRevision);
      auto& player = engine_.mutableStateForRestore().players[sourceSeat - 1];
      std::memset(player.name, 0, sizeof(player.name));
      std::memcpy(player.name, display.data(), display.size());
      touchIdentityVersionLocked();
      maybeStartIdentityCountdownLocked();
    }
  }

  projectIdentityLocked(sourceSeat, request.requestId, request.operation, result, 0, output);
  seat.hasCachedRequest = true;
  seat.lastRequestId = request.requestId;
  seat.lastRequest = canonical;
  std::size_t responseBytes = 0;
  if (!encodeIdentitySnapshot(output, seat.cachedResponse.data(), seat.cachedResponse.size(),
                              responseBytes) || responseBytes != seat.cachedResponse.size()) {
    seat.hasCachedRequest = false;
    seat.lastRequestId = 0;
    seat.lastRequest.fill(0);
    seat.cachedResponse.fill(0);
  }
  saveIdentityLocked();
}

bool AuthorityService::renderAvatarPreview(const AvatarRecipe& recipe,
                                           AvatarPreviewResult& output) {
  if (avatarRenderer_ == nullptr || !avatarRenderer_->valid()) return false;
  output = avatarRenderer_->renderPreview(recipe);
  return output.ok;
}

std::filesystem::path AuthorityService::avatarComponentRoot() const {
  return avatarRenderer_ == nullptr ? std::filesystem::path{} : avatarRenderer_->componentRoot();
}

std::filesystem::path AuthorityService::avatarAssetRoot() const {
  return avatarRenderer_ == nullptr ? std::filesystem::path{} : avatarRenderer_->outputRoot();
}

void AuthorityService::projectTradeLocked(std::uint8_t sourceSeat, std::uint32_t requestId,
                                          TradeOperation operation, TradeResultCode result,
                                          const TradeWorkflow* workflow,
                                          std::uint64_t nowEpochMs, std::uint8_t extraFlags,
                                          TradeResponse& output) const {
  output = TradeResponse{};
  output.operation = operation;
  output.result = result;
  output.selfPlayerId = sourceSeat;
  output.requestId = requestId;
  output.stateVersion = engine_.state().stateVersion;
  output.flags = extraFlags;
  if (workflow == nullptr) return;
  output.status = protocolTradeStatus(workflow->status);
  output.tradeId = workflow->tradeId;
  output.revision = workflow->revision;
  output.confirmedMask = workflow->confirmedMask;
  output.originatorId = workflow->proposerId;
  const bool selfIsProposer = sourceSeat == workflow->proposerId;
  output.counterpartyId = selfIsProposer ? workflow->counterpartyId : workflow->proposerId;
  if ((workflow->confirmedMask & (1u << (sourceSeat - 1))) != 0) {
    output.flags |= TradeResponseFlagSelfConfirmed;
  }
  if (output.counterpartyId != 0 &&
      (workflow->confirmedMask & (1u << (output.counterpartyId - 1))) != 0) {
    output.flags |= TradeResponseFlagCounterpartyConfirmed;
  }
  if (selfIsProposer) output.flags |= TradeResponseFlagSelfOriginated;
  if (workflow->lastEditorId == sourceSeat) output.flags |= TradeResponseFlagSelfLastEdited;
  if (workflow->status != TradeWorkflowStatus::Offered &&
      workflow->status != TradeWorkflowStatus::Countered) {
    output.flags |= TradeResponseFlagTerminal;
  }
  if (workflow->deadlineEpochMs > nowEpochMs) {
    output.expiresInMs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        workflow->deadlineEpochMs - nowEpochMs, UINT32_MAX));
  }
  const auto& selfSide = selfIsProposer ? workflow->proposerGives : workflow->counterpartyGives;
  const auto& otherSide = selfIsProposer ? workflow->counterpartyGives : workflow->proposerGives;
  output.selfGivesCash = selfSide.cash;
  output.counterpartyGivesCash = otherSide.cash;
  output.selfAssetCount = selfSide.assetCount;
  output.counterpartyAssetCount = otherSide.assetCount;
  for (std::uint8_t i = 0; i < selfSide.assetCount; ++i) output.selfAssets[i] = selfSide.assets[i];
  for (std::uint8_t i = 0; i < otherSide.assetCount; ++i) {
    output.counterpartyAssets[i] = otherSide.assets[i];
  }
}

void AuthorityService::handleTradeRequest(std::uint8_t sourceSeat,
                                          const TradeRequest& request,
                                          TradeResponse& output) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = unixEpochMs();
  const auto before = engine_.state().stateVersion;
  TradeWorkflow current{};
  const bool hasCurrent = engine_.tradeForPlayer(sourceSeat, current);
  const bool staleVersion = request.expectedStateVersion != 0 &&
      request.expectedStateVersion != engine_.state().stateVersion;
  if (sourceSeat == 0 || sourceSeat > engine_.state().playerCount) {
    projectTradeLocked(sourceSeat == 0 ? 1 : sourceSeat, request.requestId, request.operation,
                       TradeResultCode::Unauthorized, nullptr, now, 0, output);
    return;
  }
  if (request.operation == TradeOperation::Query) {
    projectTradeLocked(sourceSeat, request.requestId, request.operation,
        hasCurrent ? TradeResultCode::Ok : TradeResultCode::NoActiveTrade,
        hasCurrent ? &current : nullptr, now,
        staleVersion ? TradeResponseFlagRequestedVersionStale : 0, output);
    return;
  }
  if (request.expectedStateVersion == 0) {
    projectTradeLocked(sourceSeat, request.requestId, request.operation,
        TradeResultCode::InvalidRequest, hasCurrent ? &current : nullptr, now, 0, output);
    return;
  }
  if (staleVersion) {
    projectTradeLocked(sourceSeat, request.requestId, request.operation,
        TradeResultCode::StateVersionStale, hasCurrent ? &current : nullptr, now,
        TradeResponseFlagRequestedVersionStale, output);
    return;
  }

  if (request.operation == TradeOperation::Create) {
    TradeWorkflow targetTrade{};
    if (hasCurrent || engine_.tradeForPlayer(request.targetPlayerId, targetTrade)) {
      projectTradeLocked(sourceSeat, request.requestId, request.operation,
          TradeResultCode::ParticipantBusy, hasCurrent ? &current : nullptr, now, 0, output);
      return;
    }
  } else {
    if (!hasCurrent || current.tradeId != request.tradeId) {
      projectTradeLocked(sourceSeat, request.requestId, request.operation,
          TradeResultCode::Unauthorized, hasCurrent ? &current : nullptr, now, 0, output);
      return;
    }
    const auto otherId = sourceSeat == current.proposerId ? current.counterpartyId : current.proposerId;
    if ((request.targetPlayerId != 0 && request.targetPlayerId != otherId)) {
      projectTradeLocked(sourceSeat, request.requestId, request.operation,
          TradeResultCode::Unauthorized, &current, now, 0, output);
      return;
    }
    if (request.expectedRevision != current.revision) {
      projectTradeLocked(sourceSeat, request.requestId, request.operation,
          TradeResultCode::RevisionStale, &current, now, 0, output);
      return;
    }
  }

  TradeOfferSide selfSide{};
  TradeOfferSide otherSide{};
  selfSide.cash = request.selfGivesCash;
  selfSide.assetCount = request.selfAssetCount;
  otherSide.cash = request.counterpartyGivesCash;
  otherSide.assetCount = request.counterpartyAssetCount;
  for (std::uint8_t i = 0; i < selfSide.assetCount; ++i) selfSide.assets[i] = request.selfAssets[i];
  for (std::uint8_t i = 0; i < otherSide.assetCount; ++i) {
    otherSide.assets[i] = request.counterpartyAssets[i];
  }
  TradeWorkflow projected{};
  Result result{ErrorCode::InvalidArgument, "unknown trade operation"};
  switch (request.operation) {
    case TradeOperation::Create:
      result = engine_.createTrade(sourceSeat, request.targetPlayerId, selfSide, otherSide,
                                   now, projected);
      break;
    case TradeOperation::Update:
      result = engine_.updateTrade(sourceSeat, request.tradeId, request.expectedRevision,
                                   selfSide, otherSide, now, projected);
      break;
    case TradeOperation::Confirm:
      result = engine_.confirmTrade(sourceSeat, request.tradeId, request.expectedRevision,
                                    now, projected);
      break;
    case TradeOperation::Reject:
      result = engine_.rejectTrade(sourceSeat, request.tradeId, request.expectedRevision, projected);
      break;
    case TradeOperation::Cancel:
      result = engine_.cancelTrade(sourceSeat, request.tradeId, request.expectedRevision, projected);
      break;
    default: break;
  }
  if (engine_.state().stateVersion != before) noteChangedLocked();
  if (projected.tradeId == 0 && engine_.tradeForPlayer(sourceSeat, current)) projected = current;
  const auto resultCode = protocolTradeResult(result, projected.tradeId == 0 ? nullptr : &projected);
  projectTradeLocked(sourceSeat, request.requestId, request.operation, resultCode,
                     projected.tradeId == 0 ? nullptr : &projected, now, 0, output);
}

bool AuthorityService::makeTradeResync(std::uint8_t sourceSeat, TradeResponse& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourceSeat == 0 || sourceSeat > engine_.state().playerCount) return false;
  TradeWorkflow workflow{};
  const bool active = engine_.tradeForPlayer(sourceSeat, workflow);
  projectTradeLocked(sourceSeat, 0, TradeOperation::Query,
                     active ? TradeResultCode::Ok : TradeResultCode::NoActiveTrade,
                     active ? &workflow : nullptr, unixEpochMs(),
                     TradeResponseFlagResync, output);
  return true;
}

bool AuthorityService::makeTradeNotification(std::uint8_t sourceSeat, std::uint32_t tradeId,
                                             TradeResponse& output) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourceSeat == 0 || tradeId == 0) return false;
  const TradeWorkflow* found = nullptr;
  for (const auto& workflow : engine_.state().trades) {
    if (workflow.tradeId == tradeId &&
        (workflow.proposerId == sourceSeat || workflow.counterpartyId == sourceSeat)) {
      found = &workflow;
      break;
    }
  }
  if (found == nullptr) return false;
  projectTradeLocked(sourceSeat, 0, TradeOperation::Query, TradeResultCode::Ok,
                     found, unixEpochMs(), TradeResponseFlagResync, output);
  return true;
}

bool AuthorityService::activateConsoleSeat(std::uint8_t seatId, const char* displayName) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = engine_.mutableStateForRestore();
  if (seatId == 0 || seatId > state.playerCount) return false;
  if (identity_.roomId == roomId_ && identity_.playerCount == state.playerCount) {
    if (seatId > identity_.humanCount || !identity_.seats[seatId - 1].human) return false;
    auto& player = state.players[seatId - 1];
    if (player.controller != ControllerKind::RealConsole) {
      player.controller = ControllerKind::RealConsole;
      ++state.stateVersion;
      noteChangedLocked();
    }
    return true;
  }
  auto& player = state.players[seatId - 1];
  bool changed = false;
  if (player.controller != ControllerKind::RealConsole) {
    player.controller = ControllerKind::RealConsole;
    changed = true;
  }
  if (displayName != nullptr && displayName[0] != '\0') {
    char normalized[sizeof(player.name)]{};
    std::strncpy(normalized, displayName, sizeof(normalized) - 1);
    if (std::strncmp(player.name, normalized, sizeof(player.name)) != 0) {
      std::memcpy(player.name, normalized, sizeof(player.name));
      changed = true;
    }
  }
  if (changed) {
    ++state.stateVersion;
    noteChangedLocked();
  }
  return true;
}

bool AuthorityService::isHumanSeat(std::uint8_t seatId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return seatId >= 1 && seatId <= identity_.playerCount &&
      identity_.seats[seatId - 1].human;
}

void AuthorityService::setConsoleConnected(std::uint8_t seatId, bool connected) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = engine_.mutableStateForRestore();
  if (seatId == 0 || seatId > state.playerCount) return;
  auto& player = state.players[seatId - 1];
  if (player.controller == ControllerKind::RealConsole && player.connected != connected) {
    player.connected = connected;
    ++state.stateVersion;
    noteChangedLocked();
  }
  if (seatId <= identity_.playerCount) {
    auto& seat = identity_.seats[seatId - 1];
    if (seat.human && seat.connected != connected) {
      seat.connected = connected;
      identity_.identityRevision = nextRevision(identity_.identityRevision);
      saveIdentityLocked();
    }
  }
}

void AuthorityService::setPeerCount(std::uint8_t count) {
  std::lock_guard<std::mutex> lock(mutex_);
  peerCount_ = count;
}

bool AuthorityService::setBotActionIntervalMs(std::uint32_t intervalMs) {
  if (intervalMs < kMinimumBotActionIntervalMs || intervalMs > kMaximumBotActionIntervalMs) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto previous = botActionInterval_;
  botActionInterval_ = std::chrono::milliseconds(intervalMs);
  lastBotAt_ = std::chrono::steady_clock::now();
  if (saveMetadataLocked()) return true;
  botActionInterval_ = previous;
  return false;
}

Result AuthorityService::validateForcedRollLocked(std::uint8_t playerId,
                                                  std::uint8_t targetTile,
                                                  std::uint32_t expectedStateVersion,
                                                  ForcedRollState& output,
                                                  std::uint8_t& dieA,
                                                  std::uint8_t& dieB) const {
  const auto& state = engine_.state();
  if (expectedStateVersion != 0 && expectedStateVersion != state.stateVersion) {
    return error(ErrorCode::RuleViolation, "state version mismatch");
  }
  if (state.board == nullptr || playerId == 0 || playerId > state.playerCount) {
    return error(ErrorCode::InvalidPlayer, "invalid forced-roll player");
  }
  if (targetTile >= state.board->tileCount) {
    return error(ErrorCode::InvalidArgument, "forced-roll target outside board");
  }
  const auto& player = state.players[playerId - 1];
  if (player.bankrupt) {
    return error(ErrorCode::RuleViolation, "bankrupt player cannot roll");
  }
  if (player.inHold) {
    return error(ErrorCode::RuleViolation, "held player cannot use a forced destination");
  }
  if (state.activePlayerId == playerId && state.phase != GamePhase::AwaitRoll) {
    return error(ErrorCode::RuleViolation, "active player is no longer waiting to roll");
  }
  const auto steps = static_cast<std::uint8_t>(
      (targetTile + state.board->tileCount - player.position) % state.board->tileCount);
  if (steps < 2 || steps > 12) {
    return error(ErrorCode::InvalidArgument, "destination must be 2 to 12 spaces clockwise");
  }

  dieA = 0;
  dieB = 0;
  std::uint8_t bestDifference = 0xFF;
  for (std::uint8_t first = 1; first <= 6; ++first) {
    if (steps <= first) continue;
    const auto second = static_cast<std::uint8_t>(steps - first);
    if (second < 1 || second > 6 || first == second) continue;
    const auto difference = static_cast<std::uint8_t>(
        first > second ? first - second : second - first);
    if (difference < bestDifference) {
      bestDifference = difference;
      dieA = first;
      dieB = second;
    }
  }
  if (dieA == 0) {
    dieA = static_cast<std::uint8_t>(steps / 2);
    dieB = dieA;
  }
  if (dieA == dieB && player.doublesStreak >= 2) {
    return error(ErrorCode::RuleViolation, "destination would trigger the third-double Hold rule");
  }

  output.active = true;
  output.playerId = playerId;
  output.targetTile = targetTile;
  output.steps = steps;
  output.originTile = player.position;
  return {};
}

void AuthorityService::clearForcedRollLocked() {
  forcedRoll_ = ForcedRollState{};
}

Result AuthorityService::setForcedRollTarget(std::uint8_t playerId,
                                             std::uint8_t targetTile,
                                             std::uint32_t expectedStateVersion) {
  std::lock_guard<std::mutex> lock(mutex_);
  ForcedRollState validated{};
  std::uint8_t dieA = 0;
  std::uint8_t dieB = 0;
  const auto result = validateForcedRollLocked(playerId, targetTile, expectedStateVersion,
                                               validated, dieA, dieB);
  if (!result) return result;
  if (forcedRoll_.active && forcedRoll_.playerId == validated.playerId &&
      forcedRoll_.targetTile == validated.targetTile &&
      forcedRoll_.originTile == validated.originTile) {
    return {};
  }
  const auto previous = forcedRoll_;
  const auto previousControlVersion = controlVersion_;
  forcedRoll_ = validated;
  ++controlVersion_;
  if (controlVersion_ == 0) controlVersion_ = 1;
  if (saveMetadataLocked()) return {};
  forcedRoll_ = previous;
  controlVersion_ = previousControlVersion;
  return error(ErrorCode::RuleViolation, "forced-roll setting persist failed");
}

bool AuthorityService::clearForcedRollTarget() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!forcedRoll_.active) return true;
  const auto previous = forcedRoll_;
  const auto previousControlVersion = controlVersion_;
  clearForcedRollLocked();
  ++controlVersion_;
  if (controlVersion_ == 0) controlVersion_ = 1;
  if (saveMetadataLocked()) return true;
  forcedRoll_ = previous;
  controlVersion_ = previousControlVersion;
  return false;
}

Result AuthorityService::executeRollLocked(std::uint8_t playerId) {
  if (!forcedRoll_.active || forcedRoll_.playerId != playerId) {
    return engine_.roll(playerId);
  }
  ForcedRollState validated{};
  std::uint8_t dieA = 0;
  std::uint8_t dieB = 0;
  const auto validation = validateForcedRollLocked(
      forcedRoll_.playerId, forcedRoll_.targetTile, 0, validated, dieA, dieB);
  if (!validation) {
    clearForcedRollLocked();
    ++controlVersion_;
    if (controlVersion_ == 0) controlVersion_ = 1;
    saveMetadataLocked();
    return validation;
  }
  if (validated.originTile != forcedRoll_.originTile) {
    // The player was moved by another authoritative effect after the web
    // override was armed.  The old destination no longer represents the
    // requested 2..12-space roll, so consume it and preserve the player's
    // actual Roll action with an ordinary random roll.
    clearForcedRollLocked();
    ++controlVersion_;
    if (controlVersion_ == 0) controlVersion_ = 1;
    saveMetadataLocked();
    return engine_.roll(playerId);
  }
  const auto result = engine_.roll(playerId, dieA, dieB);
  if (result) {
    clearForcedRollLocked();
    ++controlVersion_;
    if (controlVersion_ == 0) controlVersion_ = 1;
    saveMetadataLocked();
  }
  return result;
}

GameState AuthorityService::stateCopy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return engine_.state();
}

std::uint32_t AuthorityService::roomId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return roomId_;
}

std::uint32_t AuthorityService::stateVersion() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return engine_.state().stateVersion;
}

std::uint32_t AuthorityService::latestEventSequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return engine_.state().nextEventSequence == 0 ? 0 : engine_.state().nextEventSequence - 1;
}

std::uint8_t AuthorityService::peerCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return peerCount_;
}

std::uint32_t AuthorityService::botActionIntervalMs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(botActionInterval_.count());
}

std::uint32_t AuthorityService::controlVersion() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return controlVersion_;
}

std::uint32_t AuthorityService::identityRevision() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return identity_.identityRevision;
}

IdentityRoomPhase AuthorityService::identityPhase() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return identity_.phase;
}

ForcedRollState AuthorityService::forcedRollState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return forcedRoll_;
}

std::string AuthorityService::syncJson(const std::string& serviceIp) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& state = engine_.state();
  std::ostringstream out;
  out << "{\"schema\":2,\"version\":" << state.stateVersion
      << ",\"roomId\":" << roomId_
      << ",\"network\":" << (serverDeviceId_ ^ 0xA5A5A5A5u)
      << ",\"controlVersion\":" << controlVersion_
      << ",\"forcedRoll\":{\"active\":" << (forcedRoll_.active ? "true" : "false")
      << ",\"player\":" << static_cast<unsigned>(forcedRoll_.playerId)
      << ",\"target\":" << static_cast<unsigned>(forcedRoll_.targetTile)
      << ",\"steps\":" << static_cast<unsigned>(forcedRoll_.steps)
      << ",\"origin\":" << static_cast<unsigned>(forcedRoll_.originTile) << "}"
      << ",\"phase\":" << static_cast<unsigned>(state.phase)
      << ",\"round\":" << static_cast<unsigned>(state.roundNumber)
      << ",\"activePlayer\":" << static_cast<unsigned>(state.activePlayerId)
      << ",\"decisionPlayer\":" << static_cast<unsigned>(engine_.decisionPlayerId())
      << ",\"actions\":" << engine_.actionsFor(engine_.decisionPlayerId())
      << ",\"espnowPeers\":" << static_cast<unsigned>(peerCount_)
      << ",\"wifi\":{\"connected\":true,\"ip\":";
  appendJsonString(out, serviceIp.c_str());
  out << "},\"board\":{\"id\":";
  appendJsonString(out, state.board->id);
  out << ",\"size\":" << static_cast<unsigned>(state.board->tileCount) << "},";
  appendWorkflowJson(out, state);
  std::uint8_t avatarMask = 0;
  std::uint8_t nameMask = 0;
  std::uint8_t readyMask = 0;
  std::uint8_t onlineMask = 0;
  for (std::uint8_t index = 0; index < identity_.playerCount; ++index) {
    const auto bit = static_cast<std::uint8_t>(1u << index);
    if (identity_.seats[index].avatarFinal) avatarMask |= bit;
    if (identity_.seats[index].nameFinal) nameMask |= bit;
    if (identity_.seats[index].ready) readyMask |= bit;
    if (identity_.seats[index].connected) onlineMask |= bit;
  }
  out << "\"identity\":{\"revision\":" << identity_.identityRevision
      << ",\"phase\":" << static_cast<unsigned>(identity_.phase)
      << ",\"serverEpochMs\":" << epochClock_()
      << ",\"humanCount\":" << static_cast<unsigned>(identity_.humanCount)
      << ",\"botCount\":" << static_cast<unsigned>(identity_.botCount)
      << ",\"avatarFinalMask\":" << static_cast<unsigned>(avatarMask)
      << ",\"nameFinalMask\":" << static_cast<unsigned>(nameMask)
      << ",\"readyMask\":" << static_cast<unsigned>(readyMask)
      << ",\"onlineMask\":" << static_cast<unsigned>(onlineMask)
      << ",\"countdownDeadlineEpochMs\":" << identity_.countdownDeadlineEpochMs
      << "},";
  out << "\"players\":[";
  for (std::uint8_t index = 0; index < state.playerCount; ++index) {
    if (index != 0) out << ',';
    const auto& player = state.players[index];
    out << "{\"id\":" << static_cast<unsigned>(player.id) << ",\"name\":";
    const auto identityPublished = index < identity_.playerCount &&
        identity_.seats[index].nameFinal;
    appendJsonString(out, identityPublished ? player.name : "");
    out << ",\"controller\":";
    appendJsonString(out, controllerName(player.controller));
    out << ",\"connected\":" << (player.connected ? "true" : "false")
        << ",\"cash\":" << player.cash
        << ",\"position\":" << static_cast<unsigned>(player.position)
        << ",\"doubles\":" << static_cast<unsigned>(player.doublesStreak)
        << ",\"held\":" << (player.inHold ? "true" : "false")
        << ",\"bankrupt\":" << (player.bankrupt ? "true" : "false")
        << ",\"identityFlags\":";
    std::uint8_t identityFlags = 0;
    if (index < identity_.playerCount) {
      const auto& seat = identity_.seats[index];
      identityFlags = static_cast<std::uint8_t>((seat.human ? 1u : 2u) |
          (seat.avatarGenerating ? 4u : 0u) | (seat.avatarFinal ? 8u : 0u) |
          (seat.nameFinal ? 16u : 0u) | (seat.ready ? 32u : 0u));
    }
    out << static_cast<unsigned>(identityFlags) << ",\"avatarUrl\":";
    if (index < identity_.playerCount && identity_.seats[index].avatarFinal) {
      const auto& seat = identity_.seats[index];
      const auto key = "p" + std::to_string(seat.playerId) + "-a" +
          std::to_string(seat.avatarRevision) + "-" + hex16(seat.avatarContentHash64) + ".png";
      // Keep the Web projection origin-relative. The server is reachable on
      // both the player AP and the household uplink, so pinning this URL to
      // 10.42.0.1 would break browsers using the other interface.
      const auto url = "/assets/avatars/" + std::to_string(roomId_) + "/" + key;
      appendJsonString(out, url.c_str());
    } else {
      appendJsonString(out, "");
    }
    out << '}';
  }
  out << "],\"assets\":[";
  for (std::uint8_t index = 0; index < state.board->assetCount; ++index) {
    if (index != 0) out << ',';
    out << '[' << static_cast<unsigned>(state.assets[index].ownerId) << ','
        << static_cast<unsigned>(state.assets[index].buildingLevel) << ','
        << (state.assets[index].mortgaged ? 1 : 0) << ']';
  }
  out << "],\"events\":[";
  constexpr std::uint8_t visibleLimit = 10;
  const auto visible = std::min<std::uint8_t>(state.eventCount, visibleLimit);
  const auto first = static_cast<std::uint8_t>(
      (state.eventHead + kEventHistory - visible) % kEventHistory);
  for (std::uint8_t offset = 0; offset < visible; ++offset) {
    if (offset != 0) out << ',';
    const auto& event = state.events[(first + offset) % kEventHistory];
    out << '[' << event.sequence << ',' << static_cast<unsigned>(event.kind) << ','
        << static_cast<unsigned>(event.actorId) << ',' << static_cast<unsigned>(event.targetId) << ','
        << static_cast<unsigned>(event.assetIndex) << ',' << event.amount << ']';
  }
  out << "]}";
  return out.str();
}

std::string AuthorityService::boardJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto& state = engine_.state();
  std::ostringstream out;
  out << "{\"schema\":1,\"roomId\":" << roomId_ << ",\"board\":{\"id\":";
  appendJsonString(out, state.board->id);
  out << ",\"size\":" << static_cast<unsigned>(state.board->tileCount) << "},\"tiles\":[";
  for (std::uint8_t index = 0; index < state.board->tileCount; ++index) {
    if (index != 0) out << ',';
    const auto& tile = state.board->tiles[index];
    out << '[';
    appendJsonString(out, tile.id);
    out << ',' << static_cast<unsigned>(tile.kind) << ',' << static_cast<unsigned>(tile.assetIndex)
        << ',' << (tile.assetIndex == kNoAsset ? 0 : state.board->assets[tile.assetIndex].economy.price)
        << ']';
  }
  out << "]}";
  return out.str();
}

std::string AuthorityService::stateJson(const std::string& serviceIp) const {
  return syncJson(serviceIp);
}

}  // namespace gridopoly::pi
