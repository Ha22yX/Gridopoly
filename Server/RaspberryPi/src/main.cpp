#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "AuthorityService.h"
#include "HttpServer.h"
#include "UdpPlayerServer.h"

namespace {

std::atomic<bool> running{true};

void stopSignal(int) { running = false; }

std::string environment(const char* name, const char* fallback) {
  const auto* value = std::getenv(name);
  return value == nullptr || value[0] == '\0' ? fallback : value;
}

std::uint16_t portEnvironment(const char* name, std::uint16_t fallback) {
  const auto text = environment(name, "");
  if (text.empty()) return fallback;
  char* end = nullptr;
  const auto value = std::strtoul(text.c_str(), &end, 10);
  return end != text.c_str() && *end == '\0' && value > 0 && value <= 65535
      ? static_cast<std::uint16_t>(value) : fallback;
}

std::chrono::milliseconds millisecondsEnvironment(const char* name, std::uint32_t fallback,
                                                   std::uint32_t minimum,
                                                   std::uint32_t maximum) {
  const auto text = environment(name, "");
  if (text.empty()) return std::chrono::milliseconds(fallback);
  char* end = nullptr;
  const auto value = std::strtoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || value < minimum || value > maximum) {
    return std::chrono::milliseconds(fallback);
  }
  return std::chrono::milliseconds(value);
}

}  // namespace

int main() {
  // systemd captures stdout through a pipe; make READY/ALIVE evidence visible
  // immediately instead of waiting for the standard stream buffer to fill.
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  std::signal(SIGINT, stopSignal);
  std::signal(SIGTERM, stopSignal);

  const std::filesystem::path dataDirectory = environment("GRIDOPOLY_DATA_DIR", "/var/lib/gridopoly");
  const auto psk = environment("GRIDOPOLY_UDP_PSK", "");
  if (psk.size() < 16) {
    std::cerr << "GRIDOPOLY_FATAL missing_or_short_udp_psk\n";
    return 2;
  }
  std::error_code error;
  std::filesystem::create_directories(dataDirectory, error);
  if (error) {
    std::cerr << "GRIDOPOLY_FATAL data_directory_unavailable\n";
    return 3;
  }

  gridopoly::pi::AuthorityIdentityOptions identityOptions{};
  identityOptions.identityPath = dataDirectory / "identity.bin";
  identityOptions.avatarComponentRoot = environment(
      "GRIDOPOLY_AVATAR_COMPONENT_ROOT",
      "/usr/local/share/gridopoly/avatar/components-v1");
  identityOptions.avatarAssetRoot = environment(
      "GRIDOPOLY_AVATAR_ASSET_ROOT", "/var/lib/gridopoly/assets");
  gridopoly::pi::AuthorityService authority(dataDirectory / "state.bin",
      dataDirectory / "authority.meta", 0,
      millisecondsEnvironment("GRIDOPOLY_BOT_ACTION_INTERVAL_MS", 1200, 100, 10000),
      std::move(identityOptions));
  if (!authority.initialize()) {
    std::cerr << "GRIDOPOLY_FATAL authority_initialize_failed\n";
    return 4;
  }
  gridopoly::pi::UdpPlayerServer udp(authority, psk, dataDirectory / "device-seats.bin",
      environment("GRIDOPOLY_UDP_BIND", "0.0.0.0"),
      environment("GRIDOPOLY_UDP_BROADCAST", "10.42.0.255"));
  if (!udp.start()) {
    std::cerr << "GRIDOPOLY_FATAL udp_bind_failed\n";
    return 5;
  }
  gridopoly::pi::HttpServer http(authority, udp,
      portEnvironment("GRIDOPOLY_HTTP_PORT", 80),
      environment("GRIDOPOLY_SERVICE_IP", "10.42.0.1"), 32,
      environment("GRIDOPOLY_WEB_ASSET_DIR", "/usr/local/share/gridopoly/tiles"));
  if (!http.start()) {
    std::cerr << "GRIDOPOLY_FATAL http_bind_failed\n";
    udp.stop();
    return 6;
  }

  std::cout << "GRIDOPOLY_READY platform=raspberry-pi room=" << authority.roomId()
            << " version=" << authority.stateVersion()
            << " bot_interval_ms=" << authority.botActionIntervalMs()
            << " udp_port=" << gridopoly::protocol::kGridopolyUdpPort << "\n";
  auto lastAlive = std::chrono::steady_clock::now();
  while (running) {
    authority.tick();
    const auto now = std::chrono::steady_clock::now();
    if (now - lastAlive >= std::chrono::seconds(5)) {
      lastAlive = now;
      const auto udpDiagnostics = udp.diagnostics();
      const auto httpDiagnostics = http.diagnostics();
      std::cout << "GRIDOPOLY_ALIVE room=" << authority.roomId()
                << " version=" << authority.stateVersion()
                << " peers=" << static_cast<unsigned>(authority.peerCount())
                << " udp_rx=" << udpDiagnostics.validDatagrams
                << " udp_tx=" << udpDiagnostics.txDatagrams
                << " http_done=" << httpDiagnostics.completed << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  http.stop();
  udp.stop();
  const bool saved = authority.flush();
  std::cout << "GRIDOPOLY_STOP saved=" << (saved ? 1 : 0) << "\n";
  return saved ? 0 : 7;
}
