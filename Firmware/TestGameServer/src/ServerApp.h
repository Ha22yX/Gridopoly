#pragma once

#include <Arduino.h>
#include <gridopoly/core/GameEngine.h>
#include <gridopoly/protocol/Protocol.h>

#include "HttpServer.h"
#include "NetworkSupervisor.h"
#include "StateStore.h"

namespace gridopoly::server {

class EspNowTransport;

class ServerApp {
 public:
  ServerApp();
  void begin(EspNowTransport* transport, NetworkSupervisor* network);
  void loop();
  void suspendNetworkServices();
  void restartNetworkServices();

  gridopoly::core::Result newGame(std::uint8_t boardSize, std::uint8_t botCount);
  gridopoly::core::Result execute(gridopoly::protocol::ActionCode action, std::uint8_t playerId,
                                  std::uint8_t assetIndex, std::int32_t argument,
                                  std::uint32_t expectedStateVersion = 0);
  bool makeSnapshot(std::uint8_t seatId, gridopoly::protocol::StateSnapshot& output) const;
  bool makeAuthoritySnapshot(gridopoly::protocol::AuthoritySnapshot& output) const;
  bool makeRosterSnapshot(gridopoly::protocol::RosterSnapshot& output) const;
  bool makePlayerDetail(std::uint32_t requestId, std::uint8_t targetPlayerId,
                        std::uint32_t requestedStateVersion,
                        gridopoly::protocol::PlayerDetailResponse& output) const;
  bool activateConsoleSeat(std::uint8_t seatId, const char* displayName);
  void setConsoleConnected(std::uint8_t seatId, bool connected);

  const gridopoly::core::GameState& state() const { return engine_.state(); }
  std::uint32_t roomId() const { return roomId_; }
  std::uint8_t espNowPeerCount() const;
  HttpServerDiagnostics httpDiagnostics() const { return http_.diagnostics(); }

 private:
  gridopoly::core::GameEngine engine_;
  StateStore store_;
  HttpServer http_{};
  EspNowTransport* transport_{};
  NetworkSupervisor* network_{};
  std::uint32_t roomId_{1};
  std::uint32_t persistedVersion_{};
  std::uint32_t lastBotAt_{};
  std::uint32_t botActionIntervalMs_{1200};
  String stateJsonCache_{};
  String syncJsonCache_{};
  String boardJsonCache_{};
  std::uint32_t cachedStateVersion_{0xFFFFFFFFu};
  std::uint32_t cachedRoomId_{};
  std::uint32_t cachedWifiIp_{};
  std::uint8_t cachedPeerCount_{0xFF};
  std::uint32_t cachedSyncStateVersion_{0xFFFFFFFFu};
  std::uint32_t cachedSyncRoomId_{};
  std::uint32_t cachedSyncWifiIp_{};
  std::uint8_t cachedSyncPeerCount_{0xFF};
  std::uint32_t cachedBoardRoomId_{};
  bool cachedWifiConnected_{};
  bool cachedSyncWifiConnected_{};
  bool stateJsonCacheValid_{};
  bool syncJsonCacheValid_{};
  bool boardJsonCacheValid_{};
  std::uint32_t pendingPersistVersion_{};
  std::uint32_t persistDueAt_{};
  std::uint32_t persistDirtySinceAt_{};
  std::uint32_t httpRequestCount_{};
  std::uint32_t conditionalStateHits_{};
  std::uint32_t stateCacheHits_{};
  std::uint32_t stateCacheMisses_{};
  bool mdnsStarted_{};
  bool wifiWasConnected_{};
  bool persistPending_{};

  void handleHttp(const HttpRequest& request);
  void maintainMdns();
  void handleHealth();
  void handleState(const HttpRequest& request);
  void handleSync(const HttpRequest& request);
  void handleBoard(const HttpRequest& request);
  void handleSettings(const HttpRequest& request);
  void handleAction(const HttpRequest& request);
  void handleNewGame(const HttpRequest& request);
  void sendResult(const gridopoly::core::Result& result);
  const String& stateJson();
  const String& syncJson();
  const String& boardJson();
  void saveIfChanged();
};

}  // namespace gridopoly::server
