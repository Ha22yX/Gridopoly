#include "NetworkSupervisor.h"

#include <cerrno>
#include <cstring>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <unistd.h>

namespace gridopoly::server {
namespace {

constexpr std::uint32_t kProbeIntervalMs = 5000;
constexpr std::uint32_t kProbeTimeoutMs = 2000;
constexpr std::uint32_t kInitialProbeGraceMs = 30000;
constexpr std::uint32_t kReconnectRetryMs = 15000;
constexpr std::uint32_t kReconnectSettleMs = 1500;
constexpr std::uint32_t kGratuitousArpIntervalMs = 15000;
constexpr std::uint32_t kClientArpIntervalMs = 5000;
constexpr std::uint32_t kWebTrafficStallMs = 12000;
constexpr std::uint32_t kWebRecoveryEscalationMs = 6000;
constexpr std::uint32_t kWebStaRecoveryCooldownMs = 60000;
constexpr std::uint16_t kGatewayProbePort = 53;

constexpr std::uint8_t kDnsQueryTemplate[] = {
    0x00, 0x00,  // Transaction ID is filled per probe.
    0x01, 0x00,  // Standard recursive query.
    0x00, 0x01,  // One question.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 'h', 'e', 'a', 'l', 't', 'h',
    0x07, 'i', 'n', 'v', 'a', 'l', 'i', 'd',
    0x00,
    0x00, 0x01,  // A record.
    0x00, 0x01,  // Internet class.
};

bool hasUsableAddress(const IPAddress& address) {
  return address != IPAddress(0, 0, 0, 0);
}

void sendGratuitousArp(void* context) {
  auto* interface = static_cast<netif*>(context);
  if (interface != nullptr && netif_is_up(interface) && netif_is_link_up(interface)) {
    etharp_gratuitous(interface);
  }
}

struct TargetedArpRequest {
  netif* interface{};
  ip4_addr_t target{};
  err_t result{ERR_ARG};
};

void sendTargetedArp(void* context) {
  auto* request = static_cast<TargetedArpRequest*>(context);
  if (request != nullptr && request->interface != nullptr) {
    request->result = etharp_request(request->interface, &request->target);
  }
}

}  // namespace

void NetworkSupervisor::begin(const char* ssid, const char* password) {
  ssid_ = ssid;
  password_ = password;
  configured_ = ssid_ != nullptr && ssid_[0] != '\0';
  lastReconnectAttemptAt_ = millis();
}

void NetworkSupervisor::observeWebPoll(std::uint32_t remoteIpv4) {
  lastHttpProgressAt_ = millis();
  if (remoteIpv4 != 0) webClientIpv4_ = remoteIpv4;
  webWatchArmed_ = true;
  webServicesRestarted_ = false;
  webServiceRestartedAt_ = 0;
}

void NetworkSupervisor::detachWebClient() {
  webWatchArmed_ = false;
  webServicesRestarted_ = false;
  webServiceRestartedAt_ = 0;
  webClientIpv4_ = 0;
}

NetworkRecoveryAction NetworkSupervisor::loop() {
  const auto now = millis();
  startPendingConnect(now);
  wifi_ap_record_t accessPoint{};
  const bool associated = WiFi.status() == WL_CONNECTED &&
      esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK;
  const auto localIp = WiFi.localIP();
  const auto gateway = WiFi.gatewayIP();
  const bool usable = associated && hasUsableAddress(localIp) && hasUsableAddress(gateway);
  associated_ = associated;
  rssi_ = associated ? accessPoint.rssi : 0;
  if (associated) {
    std::memcpy(preferredBssid_.data(), accessPoint.bssid, preferredBssid_.size());
    preferredChannel_ = accessPoint.primary;
    hasPreferredAccessPoint_ = preferredChannel_ != 0;
  }

  if (usable != linkWasUsable_) {
    linkWasUsable_ = usable;
    if (usable) {
      connectPending_ = false;
      linkUsableSince_ = now;
      lastProbeAttemptAt_ = now;
      consecutiveFailures_ = 0;
      probeEstablishedForLink_ = false;
      lastGratuitousArpAt_ = now;
      Serial.printf("GRIDOPOLY_WIFI recovered=1 channel=%ld ip=%s gateway=%s\n",
                    static_cast<long>(WiFi.channel()), localIp.toString().c_str(),
                    gateway.toString().c_str());
    } else {
      Serial.println("GRIDOPOLY_WIFI lost=1 retrying=1");
    }
  }

  if (!usable) {
    linkUsable_ = false;
    servicesRestartedForOutage_ = false;
    closeProbe();
    consecutiveFailures_ = 0;
    probeEstablishedForLink_ = false;
    if (configured_ && now - lastReconnectAttemptAt_ >= kReconnectRetryMs) {
      reconnectSta(now);
      return NetworkRecoveryAction::ReconnectSta;
    }
    return NetworkRecoveryAction::None;
  }

  linkUsable_ = true;
  pollProbe(now);
  if (probeSocket_ < 0 && now - lastProbeAttemptAt_ >= kProbeIntervalMs) {
    startProbe(gateway, now);
  }
  if (now - lastGratuitousArpAt_ >= kGratuitousArpIntervalMs) {
    queueGratuitousArp(now);
  }
  if (webWatchArmed_ && webClientIpv4_ != 0 &&
      now - lastClientArpAt_ >= kClientArpIntervalMs) {
    requestClientArp(now);
  }

  const auto failures = consecutiveFailures_;
  if (failures == 0) servicesRestartedForOutage_ = false;

  // Some gateways do not answer DNS immediately after DHCP. During each new
  // association, allow probes to establish a baseline before remediation;
  // a genuinely unreachable gateway is still recovered after the hard grace.
  if (!probeEstablishedForLink_ && now - linkUsableSince_ < kInitialProbeGraceMs) {
    return NetworkRecoveryAction::None;
  }

  const bool webTrafficStalled = webWatchArmed_ &&
      now - lastHttpProgressAt_ >= kWebTrafficStallMs;
  const bool webCooldownReady = lastWebStaRecoveryAt_ == 0 ||
      now - lastWebStaRecoveryAt_ >= kWebStaRecoveryCooldownMs;
  const bool webEscalationReady = webServicesRestarted_ && webCooldownReady &&
      now - webServiceRestartedAt_ >= kWebRecoveryEscalationMs;
  const auto action = chooseNetworkRecovery(true, failures, servicesRestartedForOutage_, false,
                                             webTrafficStalled, webServicesRestarted_,
                                             webEscalationReady);
  if (action == NetworkRecoveryAction::RestartServices) {
    if (webTrafficStalled) {
      webServicesRestarted_ = true;
      webServiceRestartedAt_ = now;
      ++webServiceRestarts_;
      Serial.printf("GRIDOPOLY_NET_HEAL stage=web_restart silence=%lu\n",
                    static_cast<unsigned long>(now - lastHttpProgressAt_));
    } else {
      servicesRestartedForOutage_ = true;
      Serial.printf("GRIDOPOLY_NET_HEAL stage=http_restart failures=%u\n", failures);
    }
    ++serviceRestarts_;
  } else if (action == NetworkRecoveryAction::ReconnectSta) {
    ++webStaRecoveries_;
    lastWebStaRecoveryAt_ = now;
    webWatchArmed_ = false;
    webServicesRestarted_ = false;
    Serial.printf("GRIDOPOLY_NET_HEAL stage=web_sta_reconnect silence=%lu\n",
                  static_cast<unsigned long>(now - lastHttpProgressAt_));
    reconnectSta(now);
  }
  return action;
}

NetworkDiagnostics NetworkSupervisor::diagnostics() const {
  NetworkDiagnostics output{};
  output.probeSuccesses = probeSuccesses_;
  output.probeTimeouts = probeTimeouts_;
  output.probeErrors = probeErrors_;
  output.lastSuccessAt = lastSuccessAt_;
  output.lastRttMs = lastRttMs_;
  output.consecutiveFailures = consecutiveFailures_;
  output.serviceRestarts = serviceRestarts_;
  output.staReconnects = staReconnects_;
  output.reconnectAttempts = reconnectAttempts_;
  output.connectStarts = connectStarts_;
  output.gratuitousArpQueued = gratuitousArpQueued_;
  output.gratuitousArpErrors = gratuitousArpErrors_;
  output.clientArpRequests = clientArpRequests_;
  output.clientArpErrors = clientArpErrors_;
  output.webServiceRestarts = webServiceRestarts_;
  output.webStaRecoveries = webStaRecoveries_;
  output.webSilenceMs = webWatchArmed_ ? millis() - lastHttpProgressAt_ : 0;
  output.rssi = rssi_;
  output.associated = associated_;
  output.ipUsable = linkUsable_;
  return output;
}

void NetworkSupervisor::queueGratuitousArp(std::uint32_t now) {
  lastGratuitousArpAt_ = now;
  auto* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  auto* interface = station == nullptr
      ? nullptr
      : static_cast<netif*>(esp_netif_get_netif_impl(station));
  if (interface == nullptr || tcpip_callback(sendGratuitousArp, interface) != ERR_OK) {
    ++gratuitousArpErrors_;
    return;
  }
  ++gratuitousArpQueued_;
}

void NetworkSupervisor::requestClientArp(std::uint32_t now) {
  lastClientArpAt_ = now;
  auto* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  auto* interface = station == nullptr
      ? nullptr
      : static_cast<netif*>(esp_netif_get_netif_impl(station));
  if (interface == nullptr) {
    ++clientArpErrors_;
    return;
  }
  TargetedArpRequest request{};
  request.interface = interface;
  request.target.addr = webClientIpv4_;
  const auto dispatch = tcpip_callback_wait(sendTargetedArp, &request);
  if (dispatch != ERR_OK || request.result != ERR_OK) {
    ++clientArpErrors_;
    return;
  }
  ++clientArpRequests_;
}

void NetworkSupervisor::startProbe(const IPAddress& gateway, std::uint32_t now) {
  lastProbeAttemptAt_ = now;
  const int socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket < 0) {
    recordProbeFailure(now, false);
    return;
  }
  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(kGatewayProbePort);
  const auto hostAddress = (static_cast<std::uint32_t>(gateway[0]) << 24) |
                           (static_cast<std::uint32_t>(gateway[1]) << 16) |
                           (static_cast<std::uint32_t>(gateway[2]) << 8) |
                           static_cast<std::uint32_t>(gateway[3]);
  target.sin_addr.s_addr = htonl(hostAddress);
  const int result = ::connect(socket, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
  if (result != 0) {
    ::close(socket);
    recordProbeFailure(now, false);
    return;
  }

  std::uint8_t query[sizeof(kDnsQueryTemplate)];
  memcpy(query, kDnsQueryTemplate, sizeof(query));
  if (++probeTransactionId_ == 0) ++probeTransactionId_;
  query[0] = static_cast<std::uint8_t>(probeTransactionId_ >> 8);
  query[1] = static_cast<std::uint8_t>(probeTransactionId_ & 0xFF);
  const int sent = ::send(socket, query, sizeof(query), MSG_DONTWAIT);
  if (sent != static_cast<int>(sizeof(query))) {
    ::close(socket);
    recordProbeFailure(now, false);
    return;
  }
  probeSocket_ = socket;
  probeStartedAt_ = now;
}

void NetworkSupervisor::pollProbe(std::uint32_t now) {
  if (probeSocket_ < 0) return;
  std::uint8_t response[96]{};
  const int received = ::recv(probeSocket_, response, sizeof(response), MSG_DONTWAIT);
  if (received >= 12) {
    const auto transactionId = static_cast<std::uint16_t>(response[0] << 8) | response[1];
    const bool isResponse = (response[2] & 0x80) != 0;
    if (transactionId == probeTransactionId_ && isResponse) {
      closeProbe();
      recordProbeSuccess(now);
    }
  } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    closeProbe();
    recordProbeFailure(now, false);
  } else if (now - probeStartedAt_ >= kProbeTimeoutMs) {
    closeProbe();
    recordProbeFailure(now, true);
  }
}

void NetworkSupervisor::closeProbe() {
  if (probeSocket_ < 0) return;
  ::close(probeSocket_);
  probeSocket_ = -1;
}

void NetworkSupervisor::recordProbeSuccess(std::uint32_t now) {
  ++probeSuccesses_;
  consecutiveFailures_ = 0;
  probeEstablishedForLink_ = true;
  lastSuccessAt_ = now;
  lastRttMs_ = now - probeStartedAt_;
}

void NetworkSupervisor::recordProbeFailure(std::uint32_t, bool timeout) {
  if (timeout) ++probeTimeouts_;
  else ++probeErrors_;
  if (consecutiveFailures_ < 0xFF) ++consecutiveFailures_;
}

void NetworkSupervisor::reconnectSta(std::uint32_t now) {
  closeProbe();
  consecutiveFailures_ = 0;
  ++staReconnects_;
  ++reconnectAttempts_;
  lastReconnectAttemptAt_ = now;
  connectDueAt_ = now + kReconnectSettleMs;
  connectPending_ = true;
  linkUsable_ = false;
  probeEstablishedForLink_ = false;
  servicesRestartedForOutage_ = false;
  WiFi.disconnect(false, false);
  Serial.printf("GRIDOPOLY_NET_HEAL stage=sta_disconnect reconnect_in=%lu channel=%u locked=%u\n",
                static_cast<unsigned long>(kReconnectSettleMs),
                static_cast<unsigned>(preferredChannel_), hasPreferredAccessPoint_ ? 1u : 0u);
}

void NetworkSupervisor::startPendingConnect(std::uint32_t now) {
  if (!connectPending_ || static_cast<std::int32_t>(now - connectDueAt_) < 0) return;
  connectPending_ = false;
  if (!configured_ || WiFi.status() == WL_CONNECTED) return;
  ++connectStarts_;
  if (hasPreferredAccessPoint_) {
    WiFi.begin(ssid_, password_, preferredChannel_, preferredBssid_.data(), true);
  } else {
    WiFi.begin(ssid_, password_);
  }
  Serial.printf("GRIDOPOLY_NET_HEAL stage=sta_connect attempt=%lu channel=%u locked=%u\n",
                static_cast<unsigned long>(connectStarts_),
                static_cast<unsigned>(preferredChannel_), hasPreferredAccessPoint_ ? 1u : 0u);
}

}  // namespace gridopoly::server
