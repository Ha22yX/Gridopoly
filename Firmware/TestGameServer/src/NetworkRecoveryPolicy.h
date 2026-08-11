#pragma once

#include <cstdint>

namespace gridopoly::server {

constexpr std::uint8_t kNetworkServiceRestartFailureCount = 3;

enum class NetworkRecoveryAction : std::uint8_t {
  None = 0,
  RestartServices = 1,
  ReconnectSta = 2,
};

inline NetworkRecoveryAction chooseNetworkRecovery(bool linkUsable,
                                                   std::uint8_t consecutiveProbeFailures,
                                                   bool servicesRestartedForOutage,
                                                   bool probeStalled,
                                                   bool webTrafficStalled = false,
                                                   bool webServicesRestarted = false,
                                                   bool webEscalationReady = false) {
  if (!linkUsable) return NetworkRecoveryAction::None;
  // A loaded control page polls once per second (five seconds while hidden).
  // If that traffic disappears without a page-detach notification, first
  // rebuild the listeners. Only a still-stalled session may escalate to a
  // STA reassociation, repairing "associated but no LAN RX" without reboot.
  if (webTrafficStalled) {
    if (!webServicesRestarted) return NetworkRecoveryAction::RestartServices;
    if (webEscalationReady) return NetworkRecoveryAction::ReconnectSta;
    return NetworkRecoveryAction::None;
  }
  const bool diagnosticFailed = probeStalled ||
      consecutiveProbeFailures >= kNetworkServiceRestartFailureCount;
  if (!servicesRestartedForOutage && diagnosticFailed) {
    return NetworkRecoveryAction::RestartServices;
  }
  // A UDP/DNS probe is advisory: some gateways legitimately rate-limit or
  // ignore it. Never tear down the shared STA radio (and ESP-NOW peers) while
  // association, DHCP address and gateway are all still valid.
  return NetworkRecoveryAction::None;
}

}  // namespace gridopoly::server
