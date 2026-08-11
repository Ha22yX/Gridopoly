#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <GridopolyCore.h>
#include <GridopolyProtocol.h>

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#define GRIDOPOLY_HAS_LOCAL_SECRETS 1
#else
#define GRIDOPOLY_HAS_LOCAL_SECRETS 0
#define GRIDOPOLY_WIFI_SSID ""
#define GRIDOPOLY_WIFI_PASSWORD ""
#define GRIDOPOLY_ESPNOW_TEST_PSK "gridopoly-local-test-key-change-me"
#endif

#include "src/EspNowTransport.h"
#include "src/NetworkSupervisor.h"
#include "src/ServerApp.h"

namespace {

gridopoly::server::ServerApp app;
gridopoly::server::EspNowTransport radio(app);
gridopoly::server::NetworkSupervisor network;
std::uint32_t lastAliveAt{};
volatile std::uint8_t lastWifiDisconnectReason{};

template <typename SerialType>
auto disableBlockingUsbSerialTx(SerialType& serial, int)
    -> decltype(serial.setTxTimeoutMs(0), void()) {
  serial.setTxTimeoutMs(0);
}

template <typename SerialType>
void disableBlockingUsbSerialTx(SerialType&, long) {}

void connectWifi() {
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
        Serial.printf("GRIDOPOLY_WIFI_EVENT disconnected=1 reason=%u\n",
                      static_cast<unsigned>(lastWifiDisconnectReason));
      },
      WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
#if GRIDOPOLY_HAS_LOCAL_SECRETS
  WiFi.begin(GRIDOPOLY_WIFI_SSID, GRIDOPOLY_WIFI_PASSWORD);
  const auto started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(200);
  }
#endif
}

}  // namespace

void setup() {
  Serial.begin(115200);
  disableBlockingUsbSerialTx(Serial, 0);
  delay(350);
  Serial.println("GRIDOPOLY_BOOT schema=1 target=esp32s3");
  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("GRIDOPOLY_WIFI connected=1 channel=%ld ip=%s bssid=%s\n",
                  static_cast<long>(WiFi.channel()), WiFi.localIP().toString().c_str(),
                  WiFi.BSSIDstr().c_str());
  } else {
    Serial.println("GRIDOPOLY_WIFI connected=0 diagnostic=local_secrets_or_network_unavailable");
  }
  network.begin(GRIDOPOLY_WIFI_SSID, GRIDOPOLY_WIFI_PASSWORD);
  app.begin(&radio, &network);
  const bool radioReady = radio.begin(GRIDOPOLY_ESPNOW_TEST_PSK);
  Serial.printf("GRIDOPOLY_ESPNOW ready=%d device=%08lx peers=0\n", radioReady ? 1 : 0,
                static_cast<unsigned long>(radio.serverDeviceId()));
  Serial.printf("GRIDOPOLY_READY version=%lu heap=%lu psram=%lu web=%s\n",
                static_cast<unsigned long>(app.state().stateVersion),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()),
                WiFi.status() == WL_CONNECTED ? "http://gridopoly-test.local/" : "offline");
}

void loop() {
  const auto networkAction = network.loop();
  if (networkAction == gridopoly::server::NetworkRecoveryAction::RestartServices) {
    app.restartNetworkServices();
  } else if (networkAction == gridopoly::server::NetworkRecoveryAction::ReconnectSta) {
    app.suspendNetworkServices();
  }
  app.loop();
  const auto now = millis();
  if (now - lastAliveAt >= 5000) {
    lastAliveAt = now;
    const auto networkDiagnostics = network.diagnostics();
    const auto httpDiagnostics = app.httpDiagnostics();
    const auto radioDiagnostics = radio.diagnostics();
    Serial.printf("GRIDOPOLY_ALIVE uptime=%lu room=%lu version=%lu wifi=%d ip=%s bssid=%s rssi=%d peers=%u heap=%lu probe_fail=%u net_heal=%lu/%lu/%lu garp=%lu/%lu client_arp=%lu/%lu web_heal=%lu/%lu web_silence=%lu http=%lu/%lu/%u espnow_rx=%lu/%lu espnow_tx=%lu/%lu/%lu(%lu/%lu)/%lu/%lu discover_fail=%lu txq=%u/%u/%u pair=%lu/%lu/%lu heartbeat=%lu/%lu/%lu/%lu/%lu silence=%lu wifi_reason=%u\n",
                  static_cast<unsigned long>(now),
                  static_cast<unsigned long>(app.roomId()),
                  static_cast<unsigned long>(app.state().stateVersion),
                  WiFi.status() == WL_CONNECTED ? 1 : 0,
                  WiFi.localIP().toString().c_str(),
                  WiFi.BSSIDstr().c_str(),
                  static_cast<int>(networkDiagnostics.rssi),
                  static_cast<unsigned>(radio.peerCount()),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned>(networkDiagnostics.consecutiveFailures),
                  static_cast<unsigned long>(networkDiagnostics.serviceRestarts),
                  static_cast<unsigned long>(networkDiagnostics.staReconnects),
                  static_cast<unsigned long>(networkDiagnostics.connectStarts),
                  static_cast<unsigned long>(networkDiagnostics.gratuitousArpQueued),
                  static_cast<unsigned long>(networkDiagnostics.gratuitousArpErrors),
                  static_cast<unsigned long>(networkDiagnostics.clientArpRequests),
                  static_cast<unsigned long>(networkDiagnostics.clientArpErrors),
                  static_cast<unsigned long>(networkDiagnostics.webServiceRestarts),
                  static_cast<unsigned long>(networkDiagnostics.webStaRecoveries),
                  static_cast<unsigned long>(networkDiagnostics.webSilenceMs),
                  static_cast<unsigned long>(httpDiagnostics.accepted),
                  static_cast<unsigned long>(httpDiagnostics.completed),
                  static_cast<unsigned>(httpDiagnostics.activeConnections),
                  static_cast<unsigned long>(radioDiagnostics.rxFrames),
                  static_cast<unsigned long>(radioDiagnostics.validRxFrames),
                  static_cast<unsigned long>(radioDiagnostics.txFrames),
                  static_cast<unsigned long>(radioDiagnostics.txAttempts),
                  static_cast<unsigned long>(radioDiagnostics.txQueueFailures),
                  static_cast<unsigned long>(radioDiagnostics.txNoMemoryFailures),
                  static_cast<unsigned long>(radioDiagnostics.txOtherImmediateFailures),
                  static_cast<unsigned long>(radioDiagnostics.txDeliveryFailures),
                  static_cast<unsigned long>(radioDiagnostics.txDeliveryRetries),
                  static_cast<unsigned long>(radioDiagnostics.discoverDeliveryFailures),
                  static_cast<unsigned>(radioDiagnostics.priorityQueueDepth),
                  static_cast<unsigned>(radioDiagnostics.normalQueueDepth),
                  radioDiagnostics.txInFlight ? 1u : 0u,
                  static_cast<unsigned long>(radioDiagnostics.pairAccepts),
                  static_cast<unsigned long>(radioDiagnostics.pairAcceptDeliveries),
                  static_cast<unsigned long>(radioDiagnostics.pairAcceptDeliveryFailures),
                  static_cast<unsigned long>(radioDiagnostics.heartbeatRx),
                  static_cast<unsigned long>(radioDiagnostics.heartbeatAcks),
                  static_cast<unsigned long>(radioDiagnostics.heartbeatAckFailures),
                  static_cast<unsigned long>(radioDiagnostics.heartbeatAckDeliveries),
                  static_cast<unsigned long>(radioDiagnostics.heartbeatAckDeliveryFailures),
                  static_cast<unsigned long>(radioDiagnostics.maxPeerSilenceMs),
                  static_cast<unsigned>(lastWifiDisconnectReason));
  }
  delay(2);
}
