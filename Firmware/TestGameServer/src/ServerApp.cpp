#include "ServerApp.h"

#include <ESPmDNS.h>
#include <ESP.h>
#include <WiFi.h>
#include <cstdlib>
#include <cstring>
#include <gridopoly/core/BoardCatalog.h>

#include "EspNowTransport.h"
#include "PlayerDetailProjection.h"
#include "WebUiGzip.h"

namespace gridopoly::server {
namespace {

using namespace gridopoly::core;
using namespace gridopoly::protocol;

constexpr std::uint32_t kMinimumBotActionIntervalMs = 100;
constexpr std::uint32_t kMaximumBotActionIntervalMs = 10000;

const char* controllerName(ControllerKind kind) {
  switch (kind) {
    case ControllerKind::RealConsole: return "ESP-NOW";
    case ControllerKind::Web: return "WEB";
    case ControllerKind::Bot: return "BOT";
    default: return "NONE";
  }
}

void appendJsonString(String& output, const char* value) {
  output += '"';
  if (value != nullptr) {
    while (*value != '\0') {
      const char c = *value++;
      if (c == '"' || c == '\\') output += '\\';
      if (static_cast<unsigned char>(c) >= 0x20) output += c;
    }
  }
  output += '"';
}

ActionCode parseAction(const char* value) {
  if (std::strcmp(value, "roll") == 0) return ActionCode::Roll;
  if (std::strcmp(value, "confirm") == 0) return ActionCode::ConfirmPosition;
  if (std::strcmp(value, "buy") == 0) return ActionCode::Buy;
  if (std::strcmp(value, "decline") == 0) return ActionCode::Decline;
  if (std::strcmp(value, "end") == 0) return ActionCode::EndTurn;
  if (std::strcmp(value, "holdfee") == 0) return ActionCode::PayHoldFee;
  if (std::strcmp(value, "mortgage") == 0) return ActionCode::Mortgage;
  if (std::strcmp(value, "unmortgage") == 0) return ActionCode::Unmortgage;
  if (std::strcmp(value, "build") == 0) return ActionCode::Build;
  if (std::strcmp(value, "sell") == 0) return ActionCode::SellBuilding;
  if (std::strcmp(value, "paydebt") == 0) return ActionCode::PayDebt;
  if (std::strcmp(value, "bankrupt") == 0) return ActionCode::DeclareBankruptcy;
  if (std::strcmp(value, "bid") == 0) return ActionCode::AuctionBid;
  if (std::strcmp(value, "passbid") == 0) return ActionCode::AuctionPass;
  if (std::strcmp(value, "auctionready") == 0) return ActionCode::AuctionReady;
  if (std::strcmp(value, "cardcontinue") == 0) return ActionCode::CardContinue;
  return static_cast<ActionCode>(0);
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

std::uint32_t networkFingerprint(bool connected, const IPAddress& ip) {
  const auto packed = static_cast<std::uint32_t>(ip[0]) | (static_cast<std::uint32_t>(ip[1]) << 8) |
                      (static_cast<std::uint32_t>(ip[2]) << 16) | (static_cast<std::uint32_t>(ip[3]) << 24);
  return packed ^ (connected ? 0xA5A5A5A5u : 0x5A5A5A5Au);
}

}  // namespace

ServerApp::ServerApp() = default;

void ServerApp::begin(EspNowTransport* transport, NetworkSupervisor* network) {
  transport_ = transport;
  network_ = network;
  roomId_ = esp_random();
  if (roomId_ == 0) roomId_ = 1;
  stateJsonCache_.reserve(14000);
  syncJsonCache_.reserve(2400);
  boardJsonCache_.reserve(1800);
  store_.begin();
  botActionIntervalMs_ = store_.loadBotActionIntervalMs(
      1200, kMinimumBotActionIntervalMs, kMaximumBotActionIntervalMs);
  const bool restoredFromStore = store_.restore(engine_);
  if (!restoredFromStore) {
    newGame(32, 3);
  } else {
    persistedVersion_ = engine_.state().stateVersion;
    auto& restored = engine_.mutableStateForRestore();
    bool clearedStaleConnection = false;
    for (std::uint8_t index = 0; index < restored.playerCount; ++index) {
      auto& player = restored.players[index];
      if (player.controller == ControllerKind::RealConsole && player.connected) {
        player.connected = false;
        clearedStaleConnection = true;
      }
    }
    if (clearedStaleConnection) restored.stateVersion++;
  }
  Serial.printf("GRIDOPOLY_STORE restored=%u version=%lu persisted=%lu\n",
                restoredFromStore ? 1u : 0u,
                static_cast<unsigned long>(engine_.state().stateVersion),
                static_cast<unsigned long>(persistedVersion_));
  http_.begin();
  if (WiFi.status() == WL_CONNECTED && MDNS.begin("gridopoly-test")) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
  }
  wifiWasConnected_ = WiFi.status() == WL_CONNECTED;
}

void ServerApp::loop() {
  maintainMdns();
  // Radio ACK/action traffic is latency-sensitive and uses the same Wi-Fi TX
  // pool as TCP. Give it first access before one bounded HTTP response chunk.
  if (transport_ != nullptr) transport_->loop();
  HttpRequest request{};
  if (http_.poll(request)) handleHttp(request);
  const auto now = millis();
  if (now - lastBotAt_ >= botActionIntervalMs_) {
    lastBotAt_ = now;
    engine_.runBots(1);
  }
  saveIfChanged();
}

void ServerApp::maintainMdns() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected == wifiWasConnected_) return;
  wifiWasConnected_ = connected;
  if (!connected) {
    if (mdnsStarted_) MDNS.end();
    mdnsStarted_ = false;
    http_.stop();
    return;
  }
  restartNetworkServices();
  if (transport_ != nullptr) transport_->notifyNetworkRecovered();
}

void ServerApp::suspendNetworkServices() {
  if (mdnsStarted_) MDNS.end();
  mdnsStarted_ = false;
  http_.stop();
  stateJsonCacheValid_ = false;
  syncJsonCacheValid_ = false;
  // Capture the current asynchronous driver state. If disconnect() has not
  // completed yet, maintainMdns() will observe the subsequent down edge; if
  // it has, the eventual up edge still restarts all listeners exactly once.
  wifiWasConnected_ = WiFi.status() == WL_CONNECTED;
}

void ServerApp::restartNetworkServices() {
  if (mdnsStarted_) MDNS.end();
  mdnsStarted_ = false;
  stateJsonCacheValid_ = false;
  syncJsonCacheValid_ = false;
  if (WiFi.status() != WL_CONNECTED) {
    http_.stop();
    return;
  }
  http_.restart();
  if (MDNS.begin("gridopoly-test")) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
  }
}

Result ServerApp::newGame(std::uint8_t boardSize, std::uint8_t botCount) {
  const auto* board = BoardCatalog::findBySize(boardSize);
  if (board == nullptr || botCount > 5) return {ErrorCode::InvalidArgument, "unsupported board or bot count"};
  roomId_++;
  if (roomId_ == 0) roomId_ = 1;
  engine_.reset(*board, esp_random());
  stateJsonCacheValid_ = false;
  syncJsonCacheValid_ = false;
  boardJsonCacheValid_ = false;
  auto result = engine_.addPlayer("Player Console", ControllerKind::RealConsole, false);
  if (!result) return result;
  for (std::uint8_t index = 0; index < botCount; ++index) {
    char name[20]{};
    snprintf(name, sizeof(name), "Bot %u", static_cast<unsigned>(index + 1));
    result = engine_.addPlayer(name, ControllerKind::Bot, true);
    if (!result) return result;
  }
  result = engine_.start();
  if (!result) return result;
  store_.clear();
  persistedVersion_ = 0;
  saveIfChanged();
  if (transport_ != nullptr) transport_->notifyRoomChanged();
  return result;
}

Result ServerApp::execute(ActionCode action, std::uint8_t playerId, std::uint8_t assetIndex,
                          std::int32_t argument, std::uint32_t expectedStateVersion) {
  if (action != ActionCode::AuctionReady && expectedStateVersion != 0 &&
      expectedStateVersion != engine_.state().stateVersion) {
    return {ErrorCode::RuleViolation, "state version mismatch"};
  }
  const auto requiredAction = actionMaskFor(action);
  if (requiredAction == ActionNone) return {ErrorCode::InvalidArgument, "unknown action"};
  // AuctionReady has its own room/peer/asset/generation validation and must
  // remain idempotent after the ready bit is already set. Every other action
  // is rejected unless the current authoritative phase explicitly exposes it.
  if (action != ActionCode::AuctionReady && (engine_.actionsFor(playerId) & requiredAction) == 0) {
    return {ErrorCode::RuleViolation, "action not allowed in current state"};
  }
  switch (action) {
    case ActionCode::Roll:
      return engine_.roll(playerId);
    case ActionCode::ConfirmPosition: {
      auto position = static_cast<std::uint8_t>(argument);
      if (argument < 0 || argument > 255) position = engine_.state().pendingMove.target;
      return engine_.confirmPosition(playerId, position);
    }
    case ActionCode::Buy:
      return engine_.buy(playerId);
    case ActionCode::Decline:
      return engine_.declinePurchase(playerId);
    case ActionCode::EndTurn:
      return engine_.endTurn(playerId);
    case ActionCode::PayHoldFee:
      return engine_.payHoldFee(playerId);
    case ActionCode::Mortgage:
      return engine_.mortgage(playerId, assetIndex);
    case ActionCode::Unmortgage:
      return engine_.unmortgage(playerId, assetIndex);
    case ActionCode::Build:
      return engine_.build(playerId, assetIndex);
    case ActionCode::SellBuilding:
      return engine_.sellBuilding(playerId, assetIndex);
    case ActionCode::PayDebt:
      return engine_.payDebt(playerId);
    case ActionCode::DeclareBankruptcy:
      return engine_.declareBankruptcy(playerId);
    case ActionCode::AuctionBid:
      return engine_.auctionBid(playerId, argument);
    case ActionCode::AuctionPass:
      return engine_.auctionPass(playerId);
    case ActionCode::AuctionReady:
      return engine_.auctionReady(playerId, assetIndex, static_cast<std::uint32_t>(argument));
    case ActionCode::CardContinue:
      return engine_.continueCard(playerId, static_cast<std::uint16_t>(argument));
    default:
      return {ErrorCode::InvalidArgument, "unknown action"};
  }
}

bool ServerApp::makeSnapshot(std::uint8_t seatId, StateSnapshot& output) const {
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
    auto& summary = output.players[index];
    summary.playerId = player.id;
    summary.position = player.position;
    summary.cash = player.cash;
    summary.flags = (player.inHold ? 1u : 0u) | (player.bankrupt ? 2u : 0u) |
                    (player.connected ? 4u : 0u) |
                    (static_cast<std::uint8_t>(player.controller) << 3);
  }
  return true;
}

bool ServerApp::makeAuthoritySnapshot(AuthoritySnapshot& output) const {
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
    output.auctionFlags = 1u |
        (state.auction.readyMask == state.auction.requiredReadyMask ? 0u : 2u);
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
    if (state.pendingCard.stage == PendingCardStage::AwaitSettlement) {
      output.pendingCardFlags |= 4u | 8u;
    }
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

  for (std::uint8_t i = 0; i < state.playerCount; ++i) {
    const auto& source = state.players[i];
    auto& target = output.players[i];
    target.playerId = source.id;
    target.position = source.position;
    target.cash = source.cash;
    target.flags = (source.inHold ? 1u : 0u) | (source.bankrupt ? 2u : 0u) |
                   (source.connected ? 4u : 0u) |
                   (static_cast<std::uint8_t>(source.controller) << 3);
    target.failedHoldRolls = source.failedHoldRolls;
    target.doublesStreak = source.doublesStreak;
  }
  for (std::uint8_t i = 0; i < state.board->assetCount; ++i) {
    output.assets[i].ownerId = state.assets[i].ownerId;
    output.assets[i].buildingLevel = state.assets[i].buildingLevel;
    output.assets[i].flags = state.assets[i].mortgaged ? 1u : 0u;
  }
  return true;
}

bool ServerApp::makeRosterSnapshot(RosterSnapshot& output) const {
  const auto& state = engine_.state();
  if (state.board == nullptr || state.playerCount > output.playerIds.size()) return false;
  output = RosterSnapshot{};
  output.stateVersion = state.stateVersion;
  output.playerCount = state.playerCount;
  for (std::uint8_t i = 0; i < state.playerCount; ++i) {
    output.playerIds[i] = state.players[i].id;
    std::size_t length = 0;
    while (length < 16 && state.players[i].name[length] != '\0') {
      output.displayNames[i][length] = state.players[i].name[length];
      ++length;
    }
    output.displayNames[i][length] = '\0';
  }
  return true;
}

bool ServerApp::makePlayerDetail(std::uint32_t requestId, std::uint8_t targetPlayerId,
                                 std::uint32_t requestedStateVersion,
                                 PlayerDetailResponse& output) const {
  return makePlayerDetailProjection(engine_.state(), requestId, targetPlayerId,
                                    requestedStateVersion, output);
}

bool ServerApp::activateConsoleSeat(std::uint8_t seatId, const char* displayName) {
  auto& state = engine_.mutableStateForRestore();
  if (seatId == 0 || seatId > state.playerCount) return false;
  auto& player = state.players[seatId - 1];
  bool changed = false;
  if (player.controller != ControllerKind::RealConsole) {
    player.controller = ControllerKind::RealConsole;
    changed = true;
  }
  if (player.connected) {
    player.connected = false;
    changed = true;
  }
  if (displayName != nullptr && displayName[0] != '\0') {
    char normalized[sizeof(player.name)]{};
    std::size_t index = 0;
    while (displayName[index] != '\0' && index + 1 < sizeof(normalized)) {
      normalized[index] = displayName[index];
      ++index;
    }
    if (std::strncmp(player.name, normalized, sizeof(player.name)) != 0) {
      std::memcpy(player.name, normalized, sizeof(player.name));
      changed = true;
    }
  }
  if (changed) state.stateVersion++;
  return true;
}

void ServerApp::setConsoleConnected(std::uint8_t seatId, bool connected) {
  auto& state = engine_.mutableStateForRestore();
  if (seatId == 0 || seatId > state.playerCount) return;
  auto& player = state.players[seatId - 1];
  if (player.controller == ControllerKind::RealConsole && player.connected != connected) {
    player.connected = connected;
    state.stateVersion++;
  }
}

std::uint8_t ServerApp::espNowPeerCount() const { return transport_ == nullptr ? 0 : transport_->peerCount(); }

void ServerApp::handleHttp(const HttpRequest& request) {
  if (request.method == HttpMethod::Get && request.pathEquals("/")) {
    ++httpRequestCount_;
    if (request.ifNoneMatch[0] != '\0' &&
        std::strstr(request.ifNoneMatch, kWebUiEtagToken) != nullptr) {
      http_.sendNotModified(kWebUiEtag);
    } else {
      http_.send(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kWebUiGzip),
                 kWebUiGzipSize, "no-cache", "gzip", true, kWebUiEtag);
    }
  } else if (request.method == HttpMethod::Get && request.pathEquals("/health")) {
    handleHealth();
  } else if (request.method == HttpMethod::Get && request.pathEquals("/api/state")) {
    handleState(request);
  } else if (request.method == HttpMethod::Get && request.pathEquals("/api/sync")) {
    handleSync(request);
  } else if (request.method == HttpMethod::Post && request.pathEquals("/api/web-detach")) {
    if (network_ != nullptr) network_->detachWebClient();
    http_.sendEmpty(204);
  } else if (request.method == HttpMethod::Get && request.pathEquals("/api/board")) {
    handleBoard(request);
  } else if ((request.method == HttpMethod::Get || request.method == HttpMethod::Post) &&
             request.pathEquals("/api/settings")) {
    handleSettings(request);
  } else if (request.method == HttpMethod::Post && request.pathEquals("/api/action")) {
    handleAction(request);
  } else if (request.method == HttpMethod::Post && request.pathEquals("/api/new")) {
    handleNewGame(request);
  } else {
    ++httpRequestCount_;
    static constexpr char kNotFound[] = "{\"ok\":false,\"error\":\"not_found\"}";
    http_.send(404, "application/json", kNotFound, sizeof(kNotFound) - 1);
  }
}

void ServerApp::handleHealth() {
  ++httpRequestCount_;
  const auto httpDiagnostics = http_.diagnostics();
  const auto networkDiagnostics = network_ == nullptr ? NetworkDiagnostics{} : network_->diagnostics();
  String body;
  body.reserve(1024);
  body += "{\"ok\":true,\"version\":";
  body += engine_.state().stateVersion;
  body += ",\"wifi\":";
  body += WiFi.status() == WL_CONNECTED ? "true" : "false";
  body += ",\"peers\":";
  body += static_cast<unsigned>(espNowPeerCount());
  body += ",\"heap\":";
  body += ESP.getFreeHeap();
  body += ",\"minHeap\":";
  body += ESP.getMinFreeHeap();
  body += ",\"maxAllocHeap\":";
  body += ESP.getMaxAllocHeap();
  body += ",\"uptimeMs\":";
  body += millis();
  body += ",\"botActionIntervalMs\":";
  body += botActionIntervalMs_;
  body += ",\"persistedVersion\":";
  body += persistedVersion_;
  body += ",\"http\":{\"requests\":";
  body += httpRequestCount_;
  body += ",\"notModified\":";
  body += conditionalStateHits_;
  body += ",\"cacheHits\":";
  body += stateCacheHits_;
  body += ",\"cacheMisses\":";
  body += stateCacheMisses_;
  body += ",\"accepted\":";
  body += httpDiagnostics.accepted;
  body += ",\"rejected\":";
  body += httpDiagnostics.rejected;
  body += ",\"completed\":";
  body += httpDiagnostics.completed;
  body += ",\"wouldBlock\":";
  body += httpDiagnostics.writeWouldBlock;
  body += ",\"sendErrors\":";
  body += httpDiagnostics.sendErrors;
  body += ",\"requestTimeouts\":";
  body += httpDiagnostics.requestTimeouts;
  body += ",\"responseTimeouts\":";
  body += httpDiagnostics.responseTimeouts;
  body += ",\"oversizedResponses\":";
  body += httpDiagnostics.oversizedResponses;
  body += ",\"largestResponseBytes\":";
  body += httpDiagnostics.largestResponseBytes;
  body += ",\"stateBytes\":";
  body += stateJson().length();
  body += ",\"syncBytes\":";
  body += syncJson().length();
  body += ",\"active\":";
  body += static_cast<unsigned>(httpDiagnostics.activeConnections);
  body += ",\"pending\":";
  body += static_cast<unsigned>(httpDiagnostics.pendingResponses);
  body += '}';
  body += ",\"networkHealth\":{\"associated\":";
  body += networkDiagnostics.associated ? "true" : "false";
  body += ",\"ipUsable\":";
  body += networkDiagnostics.ipUsable ? "true" : "false";
  body += ",\"rssi\":";
  body += networkDiagnostics.rssi;
  body += ",\"probeSuccesses\":";
  body += networkDiagnostics.probeSuccesses;
  body += ",\"probeTimeouts\":";
  body += networkDiagnostics.probeTimeouts;
  body += ",\"probeErrors\":";
  body += networkDiagnostics.probeErrors;
  body += ",\"consecutiveFailures\":";
  body += static_cast<unsigned>(networkDiagnostics.consecutiveFailures);
  body += ",\"lastRttMs\":";
  body += networkDiagnostics.lastRttMs;
  body += ",\"serviceRestarts\":";
  body += networkDiagnostics.serviceRestarts;
  body += ",\"staReconnects\":";
  body += networkDiagnostics.staReconnects;
  body += ",\"reconnectAttempts\":";
  body += networkDiagnostics.reconnectAttempts;
  body += ",\"connectStarts\":";
  body += networkDiagnostics.connectStarts;
  body += ",\"gratuitousArpQueued\":";
  body += networkDiagnostics.gratuitousArpQueued;
  body += ",\"gratuitousArpErrors\":";
  body += networkDiagnostics.gratuitousArpErrors;
  body += ",\"clientArpRequests\":";
  body += networkDiagnostics.clientArpRequests;
  body += ",\"clientArpErrors\":";
  body += networkDiagnostics.clientArpErrors;
  body += ",\"webServiceRestarts\":";
  body += networkDiagnostics.webServiceRestarts;
  body += ",\"webStaRecoveries\":";
  body += networkDiagnostics.webStaRecoveries;
  body += ",\"webSilenceMs\":";
  body += networkDiagnostics.webSilenceMs;
  body += '}';
  if (transport_ != nullptr) {
    const auto diagnostics = transport_->diagnostics();
    body += ",\"espnow\":{\"rxFrames\":";
    body += diagnostics.rxFrames;
    body += ",\"txFrames\":";
    body += diagnostics.txFrames;
    body += ",\"rxDropped\":";
    body += diagnostics.rxDropped;
    body += ",\"txQueueFailures\":";
    body += diagnostics.txQueueFailures;
    body += ",\"txNoMemoryFailures\":";
    body += diagnostics.txNoMemoryFailures;
    body += ",\"txOtherImmediateFailures\":";
    body += diagnostics.txOtherImmediateFailures;
    body += ",\"txDeliveryFailures\":";
    body += diagnostics.txDeliveryFailures;
    body += ",\"pairRequests\":";
    body += diagnostics.pairRequests;
    body += ",\"reconnects\":";
    body += diagnostics.reconnects;
    body += ",\"disconnects\":";
    body += diagnostics.disconnects;
    body += ",\"duplicateActionReplays\":";
    body += diagnostics.duplicateActionReplays;
    body += ",\"fullResyncRequests\":";
    body += diagnostics.fullResyncRequests;
    body += '}';
  }
  body += '}';
  http_.sendString(200, "application/json", body);
}

void ServerApp::handleSettings(const HttpRequest& request) {
  ++httpRequestCount_;
  if (request.method == HttpMethod::Post) {
    const auto intervalMs = request.argUnsigned("botIntervalMs");
    if (intervalMs < kMinimumBotActionIntervalMs || intervalMs > kMaximumBotActionIntervalMs) {
      String rejected;
      rejected.reserve(112);
      rejected += "{\"ok\":false,\"error\":\"bot_interval_out_of_range\",\"minimumMs\":";
      rejected += kMinimumBotActionIntervalMs;
      rejected += ",\"maximumMs\":";
      rejected += kMaximumBotActionIntervalMs;
      rejected += '}';
      http_.sendString(400, "application/json", rejected);
      return;
    }
    if (!store_.saveBotActionIntervalMs(intervalMs)) {
      static constexpr char kPersistFailed[] =
          "{\"ok\":false,\"error\":\"settings_persist_failed\"}";
      http_.send(500, "application/json", kPersistFailed, sizeof(kPersistFailed) - 1);
      return;
    }
    botActionIntervalMs_ = intervalMs;
    lastBotAt_ = millis();
  }
  String body;
  body.reserve(96);
  body += "{\"ok\":true,\"botActionIntervalMs\":";
  body += botActionIntervalMs_;
  body += ",\"minimumMs\":";
  body += kMinimumBotActionIntervalMs;
  body += ",\"maximumMs\":";
  body += kMaximumBotActionIntervalMs;
  body += '}';
  http_.sendString(200, "application/json", body);
}

void ServerApp::handleState(const HttpRequest& request) {
  ++httpRequestCount_;
  const auto currentVersion = engine_.state().stateVersion;
  const auto currentPeers = espNowPeerCount();
  const auto currentNetwork = networkFingerprint(WiFi.status() == WL_CONNECTED, WiFi.localIP());
  const bool sameVersion = request.hasArg("since") && request.argUnsigned("since") == currentVersion;
  const bool samePeers = request.hasArg("peers") && request.argUnsigned("peers") == currentPeers;
  const bool sameRoom = request.hasArg("room") && request.argUnsigned("room") == roomId_;
  const bool sameNetwork = request.hasArg("network") && request.argUnsigned("network") == currentNetwork;
  if (sameVersion && samePeers && sameRoom && sameNetwork) {
    ++conditionalStateHits_;
    http_.sendEmpty(204);
    return;
  }
  http_.sendString(200, "application/json; charset=utf-8", stateJson());
}

void ServerApp::handleSync(const HttpRequest& request) {
  ++httpRequestCount_;
  if (network_ != nullptr) network_->observeWebPoll(request.remoteIpv4);
  const auto currentVersion = engine_.state().stateVersion;
  const auto currentPeers = espNowPeerCount();
  const auto currentNetwork = networkFingerprint(WiFi.status() == WL_CONNECTED, WiFi.localIP());
  const bool sameVersion = request.hasArg("since") && request.argUnsigned("since") == currentVersion;
  const bool samePeers = request.hasArg("peers") && request.argUnsigned("peers") == currentPeers;
  const bool sameRoom = request.hasArg("room") && request.argUnsigned("room") == roomId_;
  const bool sameNetwork = request.hasArg("network") && request.argUnsigned("network") == currentNetwork;
  if (sameVersion && samePeers && sameRoom && sameNetwork) {
    ++conditionalStateHits_;
    http_.sendEmpty(204);
    return;
  }
  http_.sendString(200, "application/json; charset=utf-8", syncJson());
}

void ServerApp::handleBoard(const HttpRequest& request) {
  ++httpRequestCount_;
  const auto expectedRoom = request.argUnsigned("room", roomId_);
  if (expectedRoom != roomId_) {
    String body;
    body.reserve(80);
    body += "{\"ok\":false,\"error\":\"room_changed\",\"roomId\":";
    body += roomId_;
    body += '}';
    http_.sendString(409, "application/json; charset=utf-8", body);
    return;
  }
  http_.sendString(200, "application/json; charset=utf-8", boardJson(), "private, max-age=300");
}

void ServerApp::handleAction(const HttpRequest& request) {
  ++httpRequestCount_;
  char actionText[24]{};
  request.arg("action", actionText, sizeof(actionText));
  const auto action = parseAction(actionText);
  const auto playerId = static_cast<std::uint8_t>(request.argUnsigned("player"));
  const auto assetIndex = static_cast<std::uint8_t>(request.argUnsigned("asset"));
  auto argument = request.argSigned("arg", -1);
  if (action == ActionCode::ConfirmPosition && argument < 0) argument = engine_.state().pendingMove.target;
  const auto expectedVersion = request.argUnsigned("expected", 0);
  const auto result = execute(action, playerId, assetIndex, argument, expectedVersion);
  sendResult(result);
}

void ServerApp::handleNewGame(const HttpRequest& request) {
  ++httpRequestCount_;
  const auto size = static_cast<std::uint8_t>(request.argUnsigned("size"));
  const auto bots = static_cast<std::uint8_t>(request.argUnsigned("bots"));
  sendResult(newGame(size, bots));
}

void ServerApp::sendResult(const Result& result) {
  String body;
  body.reserve(128);
  body += "{\"ok\":";
  body += result ? "true" : "false";
  body += ",\"code\":";
  body += static_cast<unsigned>(result.code);
  body += ",\"message\":";
  appendJsonString(body, result.message);
  body += ",\"version\":";
  body += engine_.state().stateVersion;
  body += '}';
  http_.sendString(result ? 200 : 409, "application/json", body);
}

const String& ServerApp::stateJson() {
  const auto& state = engine_.state();
  const auto wifiConnected = WiFi.status() == WL_CONNECTED;
  const auto ip = WiFi.localIP();
  const auto wifiIp = static_cast<std::uint32_t>(ip[0]) | (static_cast<std::uint32_t>(ip[1]) << 8) |
                      (static_cast<std::uint32_t>(ip[2]) << 16) | (static_cast<std::uint32_t>(ip[3]) << 24);
  const auto peerCount = espNowPeerCount();
  if (stateJsonCacheValid_ && cachedStateVersion_ == state.stateVersion && cachedRoomId_ == roomId_ &&
      cachedWifiConnected_ == wifiConnected && cachedWifiIp_ == wifiIp && cachedPeerCount_ == peerCount) {
    ++stateCacheHits_;
    return stateJsonCache_;
  }
  ++stateCacheMisses_;
  stateJsonCache_.remove(0);
  auto& out = stateJsonCache_;
  out += "{\"version\":";
  out += state.stateVersion;
  out += ",\"roomId\":";
  out += roomId_;
  out += ",\"network\":";
  out += networkFingerprint(wifiConnected, ip);
  out += ",\"phase\":";
  out += static_cast<unsigned>(state.phase);
  out += ",\"round\":";
  out += static_cast<unsigned>(state.roundNumber);
  out += ",\"activePlayer\":";
  out += static_cast<unsigned>(state.activePlayerId);
  out += ",\"decisionPlayer\":";
  out += static_cast<unsigned>(engine_.decisionPlayerId());
  out += ",\"actions\":";
  out += engine_.actionsFor(engine_.decisionPlayerId());
  out += ",\"espnowPeers\":";
  out += static_cast<unsigned>(peerCount);
  out += ",\"wifi\":{\"connected\":";
  out += wifiConnected ? "true" : "false";
  out += ",\"ip\":";
  char ipText[16]{};
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  appendJsonString(out, ipText);
  out += "},\"board\":{\"id\":";
  appendJsonString(out, state.board->id);
  out += ",\"size\":";
  out += static_cast<unsigned>(state.board->tileCount);
  out += "},\"debt\":{\"active\":";
  out += state.pendingDebt.active ? "true" : "false";
  out += ",\"debtor\":";
  out += static_cast<unsigned>(state.pendingDebt.debtorId);
  out += ",\"creditor\":";
  out += static_cast<unsigned>(state.pendingDebt.creditorId);
  out += ",\"asset\":";
  out += static_cast<unsigned>(state.pendingDebt.assetIndex);
  out += ",\"amount\":";
  out += state.pendingDebt.amount;
  out += "},\"auction\":{\"active\":";
  out += state.auction.active ? "true" : "false";
  out += ",\"opening\":";
  out += state.auction.active && state.auction.readyMask != state.auction.requiredReadyMask
      ? "true"
      : "false";
  out += ",\"asset\":";
  out += static_cast<unsigned>(state.auction.assetIndex);
  out += ",\"bidder\":";
  out += static_cast<unsigned>(state.auction.currentBidderId);
  out += ",\"highestBidder\":";
  out += static_cast<unsigned>(state.auction.highestBidderId);
  out += ",\"readyMask\":";
  out += static_cast<unsigned>(state.auction.readyMask);
  out += ",\"requiredReadyMask\":";
  out += static_cast<unsigned>(state.auction.requiredReadyMask);
  out += ",\"generation\":";
  out += state.auction.generation;
  out += ",\"currentBid\":";
  out += state.auction.currentBid;
  out += ",\"minimumBid\":";
  out += state.auction.currentBid == 0 ? 10 : state.auction.currentBid + 10;
  out += "},\"players\":[";
  for (std::uint8_t i = 0; i < state.playerCount; ++i) {
    if (i != 0) out += ',';
    const auto& player = state.players[i];
    out += "{\"id\":";
    out += static_cast<unsigned>(player.id);
    out += ",\"name\":";
    appendJsonString(out, player.name);
    out += ",\"controller\":";
    appendJsonString(out, controllerName(player.controller));
    out += ",\"connected\":";
    out += player.connected ? "true" : "false";
    out += ",\"cash\":";
    out += player.cash;
    out += ",\"position\":";
    out += static_cast<unsigned>(player.position);
    out += ",\"held\":";
    out += player.inHold ? "true" : "false";
    out += ",\"bankrupt\":";
    out += player.bankrupt ? "true" : "false";
    out += '}';
  }
  out += "],\"tiles\":[";
  for (std::uint8_t i = 0; i < state.board->tileCount; ++i) {
    if (i != 0) out += ',';
    const auto& tile = state.board->tiles[i];
    out += "{\"i\":";
    out += static_cast<unsigned>(i);
    out += ",\"id\":";
    appendJsonString(out, tile.id);
    out += ",\"kind\":";
    out += static_cast<unsigned>(tile.kind);
    out += ",\"asset\":";
    out += static_cast<unsigned>(tile.assetIndex);
    if (tile.assetIndex != kNoAsset) {
      const auto& definition = state.board->assets[tile.assetIndex];
      const auto& asset = state.assets[tile.assetIndex];
      out += ",\"price\":";
      out += definition.economy.price;
      out += ",\"owner\":";
      out += static_cast<unsigned>(asset.ownerId);
      out += ",\"level\":";
      out += static_cast<unsigned>(asset.buildingLevel);
      out += ",\"mortgaged\":";
      out += asset.mortgaged ? "true" : "false";
    } else {
      out += ",\"price\":0,\"owner\":0,\"level\":0,\"mortgaged\":false";
    }
    out += '}';
  }
  out += "],\"events\":[";
  const auto first = static_cast<std::uint8_t>((state.eventHead + kEventHistory - state.eventCount) % kEventHistory);
  for (std::uint8_t i = 0; i < state.eventCount; ++i) {
    if (i != 0) out += ',';
    const auto& event = state.events[(first + i) % kEventHistory];
    out += "{\"seq\":";
    out += event.sequence;
    out += ",\"kind\":";
    out += static_cast<unsigned>(event.kind);
    out += ",\"actor\":";
    out += static_cast<unsigned>(event.actorId);
    out += ",\"target\":";
    out += static_cast<unsigned>(event.targetId);
    out += ",\"asset\":";
    out += static_cast<unsigned>(event.assetIndex);
    out += ",\"amount\":";
    out += event.amount;
    out += '}';
  }
  out += "]}";
  cachedStateVersion_ = state.stateVersion;
  cachedRoomId_ = roomId_;
  cachedWifiConnected_ = wifiConnected;
  cachedWifiIp_ = wifiIp;
  cachedPeerCount_ = peerCount;
  stateJsonCacheValid_ = true;
  return out;
}

const String& ServerApp::syncJson() {
  const auto& state = engine_.state();
  const auto wifiConnected = WiFi.status() == WL_CONNECTED;
  const auto ip = WiFi.localIP();
  const auto wifiIp = static_cast<std::uint32_t>(ip[0]) | (static_cast<std::uint32_t>(ip[1]) << 8) |
                      (static_cast<std::uint32_t>(ip[2]) << 16) | (static_cast<std::uint32_t>(ip[3]) << 24);
  const auto peerCount = espNowPeerCount();
  if (syncJsonCacheValid_ && cachedSyncStateVersion_ == state.stateVersion &&
      cachedSyncRoomId_ == roomId_ && cachedSyncWifiConnected_ == wifiConnected &&
      cachedSyncWifiIp_ == wifiIp && cachedSyncPeerCount_ == peerCount) {
    return syncJsonCache_;
  }

  syncJsonCache_.remove(0);
  auto& out = syncJsonCache_;
  out += "{\"schema\":2,\"version\":";
  out += state.stateVersion;
  out += ",\"roomId\":";
  out += roomId_;
  out += ",\"network\":";
  out += networkFingerprint(wifiConnected, ip);
  out += ",\"phase\":";
  out += static_cast<unsigned>(state.phase);
  out += ",\"round\":";
  out += static_cast<unsigned>(state.roundNumber);
  out += ",\"activePlayer\":";
  out += static_cast<unsigned>(state.activePlayerId);
  out += ",\"decisionPlayer\":";
  out += static_cast<unsigned>(engine_.decisionPlayerId());
  out += ",\"actions\":";
  out += engine_.actionsFor(engine_.decisionPlayerId());
  out += ",\"espnowPeers\":";
  out += static_cast<unsigned>(peerCount);
  out += ",\"wifi\":{\"connected\":";
  out += wifiConnected ? "true" : "false";
  out += ",\"ip\":";
  char ipText[16]{};
  snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  appendJsonString(out, ipText);
  out += "},\"board\":{\"id\":";
  appendJsonString(out, state.board->id);
  out += ",\"size\":";
  out += static_cast<unsigned>(state.board->tileCount);
  out += "},\"debt\":{\"active\":";
  out += state.pendingDebt.active ? "true" : "false";
  out += ",\"debtor\":";
  out += static_cast<unsigned>(state.pendingDebt.debtorId);
  out += ",\"creditor\":";
  out += static_cast<unsigned>(state.pendingDebt.creditorId);
  out += ",\"asset\":";
  out += static_cast<unsigned>(state.pendingDebt.assetIndex);
  out += ",\"amount\":";
  out += state.pendingDebt.amount;
  out += "},\"auction\":{\"active\":";
  out += state.auction.active ? "true" : "false";
  out += ",\"opening\":";
  out += state.auction.active && state.auction.readyMask != state.auction.requiredReadyMask
      ? "true"
      : "false";
  out += ",\"asset\":";
  out += static_cast<unsigned>(state.auction.assetIndex);
  out += ",\"bidder\":";
  out += static_cast<unsigned>(state.auction.currentBidderId);
  out += ",\"highestBidder\":";
  out += static_cast<unsigned>(state.auction.highestBidderId);
  out += ",\"readyMask\":";
  out += static_cast<unsigned>(state.auction.readyMask);
  out += ",\"requiredReadyMask\":";
  out += static_cast<unsigned>(state.auction.requiredReadyMask);
  out += ",\"generation\":";
  out += state.auction.generation;
  out += ",\"currentBid\":";
  out += state.auction.currentBid;
  out += ",\"minimumBid\":";
  out += state.auction.currentBid == 0 ? 10 : state.auction.currentBid + 10;
  out += "},\"players\":[";
  for (std::uint8_t i = 0; i < state.playerCount; ++i) {
    if (i != 0) out += ',';
    const auto& player = state.players[i];
    out += "{\"id\":";
    out += static_cast<unsigned>(player.id);
    out += ",\"name\":";
    appendJsonString(out, player.name);
    out += ",\"controller\":";
    appendJsonString(out, controllerName(player.controller));
    out += ",\"connected\":";
    out += player.connected ? "true" : "false";
    out += ",\"cash\":";
    out += player.cash;
    out += ",\"position\":";
    out += static_cast<unsigned>(player.position);
    out += ",\"held\":";
    out += player.inHold ? "true" : "false";
    out += ",\"bankrupt\":";
    out += player.bankrupt ? "true" : "false";
    out += '}';
  }
  out += "],\"assets\":[";
  for (std::uint8_t i = 0; i < state.board->assetCount; ++i) {
    if (i != 0) out += ',';
    const auto& asset = state.assets[i];
    out += '[';
    out += static_cast<unsigned>(asset.ownerId);
    out += ',';
    out += static_cast<unsigned>(asset.buildingLevel);
    out += ',';
    out += asset.mortgaged ? '1' : '0';
    out += ']';
  }
  out += "],\"events\":[";
  constexpr std::uint8_t kWebEventWindow = 10;
  const auto visibleCount = state.eventCount < kWebEventWindow ? state.eventCount : kWebEventWindow;
  const auto first = static_cast<std::uint8_t>(
      (state.eventHead + kEventHistory - visibleCount) % kEventHistory);
  for (std::uint8_t i = 0; i < visibleCount; ++i) {
    if (i != 0) out += ',';
    const auto& event = state.events[(first + i) % kEventHistory];
    out += '[';
    out += event.sequence;
    out += ',';
    out += static_cast<unsigned>(event.kind);
    out += ',';
    out += static_cast<unsigned>(event.actorId);
    out += ',';
    out += static_cast<unsigned>(event.targetId);
    out += ',';
    out += static_cast<unsigned>(event.assetIndex);
    out += ',';
    out += event.amount;
    out += ']';
  }
  out += "]}";

  cachedSyncStateVersion_ = state.stateVersion;
  cachedSyncRoomId_ = roomId_;
  cachedSyncWifiConnected_ = wifiConnected;
  cachedSyncWifiIp_ = wifiIp;
  cachedSyncPeerCount_ = peerCount;
  syncJsonCacheValid_ = true;
  return out;
}

const String& ServerApp::boardJson() {
  const auto& state = engine_.state();
  if (boardJsonCacheValid_ && cachedBoardRoomId_ == roomId_) return boardJsonCache_;

  boardJsonCache_.remove(0);
  auto& out = boardJsonCache_;
  out += "{\"schema\":1,\"roomId\":";
  out += roomId_;
  out += ",\"board\":{\"id\":";
  appendJsonString(out, state.board->id);
  out += ",\"size\":";
  out += static_cast<unsigned>(state.board->tileCount);
  out += "},\"tiles\":[";
  for (std::uint8_t i = 0; i < state.board->tileCount; ++i) {
    if (i != 0) out += ',';
    const auto& tile = state.board->tiles[i];
    out += '[';
    appendJsonString(out, tile.id);
    out += ',';
    out += static_cast<unsigned>(tile.kind);
    out += ',';
    out += static_cast<unsigned>(tile.assetIndex);
    out += ',';
    out += tile.assetIndex == kNoAsset ? 0 : state.board->assets[tile.assetIndex].economy.price;
    out += ']';
  }
  out += "]}";
  cachedBoardRoomId_ = roomId_;
  boardJsonCacheValid_ = true;
  return out;
}

void ServerApp::saveIfChanged() {
  const auto version = engine_.state().stateVersion;
  if (version == persistedVersion_) {
    persistPending_ = false;
    return;
  }
  const auto now = millis();
  if (!persistPending_) {
    persistPending_ = true;
    persistDirtySinceAt_ = now;
    pendingPersistVersion_ = version;
    persistDueAt_ = now + 750;
  } else if (pendingPersistVersion_ != version) {
    pendingPersistVersion_ = version;
    persistDueAt_ = now + 750;
  }
  const bool quietWindowReached = static_cast<std::int32_t>(now - persistDueAt_) >= 0;
  const bool maximumDelayReached = now - persistDirtySinceAt_ >= 5000;
  if (quietWindowReached || maximumDelayReached) {
    if (store_.save(engine_.state())) {
      persistedVersion_ = engine_.state().stateVersion;
      persistPending_ = false;
    } else {
      Serial.printf("GRIDOPOLY_STORE save=0 version=%lu retry_ms=1000\n",
                    static_cast<unsigned long>(engine_.state().stateVersion));
      persistDueAt_ = now + 1000;
    }
  }
}

}  // namespace gridopoly::server
