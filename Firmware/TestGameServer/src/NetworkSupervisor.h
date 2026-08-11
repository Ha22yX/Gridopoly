#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <array>
#include <cstdint>

#include "NetworkRecoveryPolicy.h"

namespace gridopoly::server {

struct NetworkDiagnostics {
  std::uint32_t probeSuccesses{};
  std::uint32_t probeTimeouts{};
  std::uint32_t probeErrors{};
  std::uint32_t serviceRestarts{};
  std::uint32_t staReconnects{};
  std::uint32_t reconnectAttempts{};
  std::uint32_t connectStarts{};
  std::uint32_t gratuitousArpQueued{};
  std::uint32_t gratuitousArpErrors{};
  std::uint32_t clientArpRequests{};
  std::uint32_t clientArpErrors{};
  std::uint32_t webServiceRestarts{};
  std::uint32_t webStaRecoveries{};
  std::uint32_t lastSuccessAt{};
  std::uint32_t lastRttMs{};
  std::uint32_t webSilenceMs{};
  std::uint8_t consecutiveFailures{};
  std::int8_t rssi{};
  bool associated{};
  bool ipUsable{};
};

class NetworkSupervisor {
 public:
  void begin(const char* ssid, const char* password);
  void observeWebPoll(std::uint32_t remoteIpv4);
  void detachWebClient();
  NetworkRecoveryAction loop();
  NetworkDiagnostics diagnostics() const;

 private:
  const char* ssid_{};
  const char* password_{};
  int probeSocket_{-1};
  std::uint32_t probeSuccesses_{};
  std::uint32_t probeTimeouts_{};
  std::uint32_t probeErrors_{};
  std::uint32_t serviceRestarts_{};
  std::uint32_t staReconnects_{};
  std::uint32_t reconnectAttempts_{};
  std::uint32_t connectStarts_{};
  std::uint32_t gratuitousArpQueued_{};
  std::uint32_t gratuitousArpErrors_{};
  std::uint32_t clientArpRequests_{};
  std::uint32_t clientArpErrors_{};
  std::uint32_t webServiceRestarts_{};
  std::uint32_t webStaRecoveries_{};
  std::uint32_t lastSuccessAt_{};
  std::uint32_t lastRttMs_{};
  std::uint32_t probeStartedAt_{};
  std::uint32_t lastProbeAttemptAt_{};
  std::uint32_t lastReconnectAttemptAt_{};
  std::uint32_t lastGratuitousArpAt_{};
  std::uint32_t lastClientArpAt_{};
  std::uint32_t webClientIpv4_{};
  std::uint32_t lastHttpProgressAt_{};
  std::uint32_t webServiceRestartedAt_{};
  std::uint32_t lastWebStaRecoveryAt_{};
  std::uint32_t connectDueAt_{};
  std::uint32_t linkUsableSince_{};
  std::uint16_t probeTransactionId_{};
  std::uint8_t consecutiveFailures_{};
  std::int8_t rssi_{};
  std::array<std::uint8_t, 6> preferredBssid_{};
  std::uint8_t preferredChannel_{};
  bool configured_{};
  bool associated_{};
  bool linkUsable_{};
  bool linkWasUsable_{};
  bool probeEstablishedForLink_{};
  bool servicesRestartedForOutage_{};
  bool webWatchArmed_{};
  bool webServicesRestarted_{};
  bool connectPending_{};
  bool hasPreferredAccessPoint_{};

  void startProbe(const IPAddress& gateway, std::uint32_t now);
  void pollProbe(std::uint32_t now);
  void closeProbe();
  void recordProbeSuccess(std::uint32_t now);
  void recordProbeFailure(std::uint32_t now, bool timeout);
  void reconnectSta(std::uint32_t now);
  void startPendingConnect(std::uint32_t now);
  void queueGratuitousArp(std::uint32_t now);
  void requestClientArp(std::uint32_t now);
};

}  // namespace gridopoly::server
