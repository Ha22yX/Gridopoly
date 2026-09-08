#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <thread>

#include "../../Server/RaspberryPi/src/AuthorityService.h"
#include "../../Server/RaspberryPi/src/FileStateStore.h"
#include "../../Server/RaspberryPi/src/HttpServer.h"
#include "../../Server/RaspberryPi/src/UdpPlayerServer.h"

#ifndef GRIDOPOLY_SOURCE_DIR
#error GRIDOPOLY_SOURCE_DIR is required
#endif

namespace {

struct HttpResponse {
  int status{};
  std::map<std::string, std::string> headers{};
  std::vector<std::uint8_t> body{};
};

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  assert(output);
}

HttpResponse request(std::uint16_t port, const std::string& method,
                     const std::string& target,
                     const std::string& extraHeaders = {},
                     const std::string& body = {}) {
  const auto descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(descriptor >= 0);
  timeval timeout{2, 0};
  assert(::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(::connect(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
  const auto requestText = method + " " + target +
      " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " +
      std::to_string(body.size()) + "\r\n" + extraHeaders +
      "Connection: close\r\n\r\n" + body;
  assert(::send(descriptor, requestText.data(), requestText.size(), 0) ==
         static_cast<ssize_t>(requestText.size()));
  std::vector<std::uint8_t> raw;
  std::array<std::uint8_t, 8192> buffer{};
  while (true) {
    const auto received = ::recv(descriptor, buffer.data(), buffer.size(), 0);
    if (received == 0) break;
    assert(received > 0);
    raw.insert(raw.end(), buffer.begin(), buffer.begin() + received);
  }
  ::close(descriptor);

  constexpr std::array<std::uint8_t, 4> delimiter{'\r', '\n', '\r', '\n'};
  const auto headerEnd = std::search(raw.begin(), raw.end(), delimiter.begin(), delimiter.end());
  assert(headerEnd != raw.end());
  const std::string headerText(raw.begin(), headerEnd);
  const auto firstLineEnd = headerText.find("\r\n");
  assert(firstLineEnd != std::string::npos);
  const auto statusAt = headerText.find(' ');
  assert(statusAt != std::string::npos);
  HttpResponse response{};
  response.status = std::stoi(headerText.substr(statusAt + 1, 3));
  std::size_t cursor = firstLineEnd + 2;
  while (cursor < headerText.size()) {
    const auto end = headerText.find("\r\n", cursor);
    const auto lineEnd = end == std::string::npos ? headerText.size() : end;
    const auto colon = headerText.find(':', cursor);
    assert(colon != std::string::npos && colon < lineEnd);
    auto valueStart = colon + 1;
    while (valueStart < lineEnd && headerText[valueStart] == ' ') ++valueStart;
    response.headers[lower(headerText.substr(cursor, colon - cursor))] =
        headerText.substr(valueStart, lineEnd - valueStart);
    if (end == std::string::npos) break;
    cursor = end + 2;
  }
  response.body.assign(headerEnd + delimiter.size(), raw.end());
  assert(response.headers.count("content-length") == 1);
  assert(std::stoull(response.headers.at("content-length")) == response.body.size());
  return response;
}

HttpResponse get(std::uint16_t port, const std::string& target) {
  return request(port, "GET", target);
}

HttpResponse getWithHeader(std::uint16_t port, const std::string& target,
                           const std::string& header) {
  return request(port, "GET", target, header);
}

HttpResponse post(std::uint16_t port, const std::string& target) {
  return request(port, "POST", target);
}

HttpResponse postJson(std::uint16_t port, const std::string& target,
                      const std::string& body) {
  return request(port, "POST", target,
                 "Content-Type: application/json\r\n", body);
}

HttpResponse remove(std::uint16_t port, const std::string& target) {
  return request(port, "DELETE", target);
}

std::string bodyText(const HttpResponse& response) {
  return std::string(response.body.begin(), response.body.end());
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  const auto source = std::filesystem::path(GRIDOPOLY_SOURCE_DIR);
  const auto temporary = std::filesystem::temp_directory_path() /
      ("gridopoly-http-assets-" + std::to_string(::getpid()));
  std::filesystem::remove_all(temporary);
  std::filesystem::create_directories(temporary / "tiles");
  const auto sourceComponents =
      source / "Assets/GridCity/Avatars/V1/runtime/components-v1";
  const auto testComponents = temporary / "avatar-components";
  std::filesystem::copy(sourceComponents, testComponents,
                        std::filesystem::copy_options::recursive);

  std::vector<std::uint8_t> rgb565(128u * 128u * 2u);
  for (std::size_t index = 0; index < rgb565.size(); ++index) {
    rgb565[index] = static_cast<std::uint8_t>((index * 37u) & 0xFFu);
  }
  auto malformed = rgb565;
  malformed.pop_back();
  const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  writeBytes(temporary / "tiles" / "a1-rivet-row.rgb565", rgb565);
  writeBytes(temporary / "tiles" / "broken-tile.rgb565", malformed);
  writeBytes(temporary / "tiles" / "a1-rivet-row.png", png);

  gridopoly::pi::AuthorityIdentityOptions identityOptions{};
  identityOptions.identityPath = temporary / "identity.bin";
  identityOptions.avatarComponentRoot = testComponents;
  identityOptions.avatarAssetRoot = temporary / "avatar-assets";
  gridopoly::pi::AuthorityService authority(temporary / "state.bin",
      temporary / "authority.meta", 0x10203040u, std::chrono::milliseconds(1200),
      identityOptions);
  assert(authority.initialize());
  gridopoly::pi::UdpPlayerServer udp(authority, "gridopoly-http-asset-test-psk",
      temporary / "registry.bin", "127.0.0.1", "127.255.255.255", 0);
  gridopoly::pi::HttpServer http(authority, udp, 0, "127.0.0.1", 2,
                                 temporary / "tiles");
  assert(http.start());
  assert(http.port() != 0);

  for (const auto& kind : {std::string("hair"), std::string("face"),
                           std::string("outfit")}) {
    const auto prefix = kind[0];
    for (int preset = 1; preset <= 10; ++preset) {
      const auto file = std::string(1, prefix) + std::to_string(preset) + ".gavc";
      const auto route = "/assets/avatar-components/v1/" + kind + "/" + file;
      const auto component = get(http.port(), route);
      assert(component.status == 200);
      assert(component.headers.at("content-type") == "application/octet-stream");
      assert(component.headers.at("cache-control") ==
             "public, max-age=31536000, immutable");
      assert(component.headers.count("etag") == 1);
      assert(component.body == readBytes(identityOptions.avatarComponentRoot / kind / file));
      if (preset == 1) {
        assert(getWithHeader(http.port(), route,
            "If-None-Match: " + component.headers.at("etag") + "\r\n").status == 304);
      }
    }
  }
  for (const auto& target : {
           "/assets/avatar-components/v1/hair/h0.gavc",
           "/assets/avatar-components/v1/hair/h11.gavc",
           "/assets/avatar-components/v1/hair/h01.gavc",
           "/assets/avatar-components/v1/hair/f1.gavc",
           "/assets/avatar-components/v1/face/h1.gavc",
           "/assets/avatar-components/v1/outfit/o1.gavc.png",
           "/assets/avatar-components/v1/hair/../identity.bin",
           "/assets/avatar-components/v1/hair/..%2Fidentity.bin",
       }) {
    assert(get(http.port(), target).status == 404);
  }
  const auto hair10Path = testComponents / "hair/h10.gavc";
  const auto hair10 = readBytes(hair10Path);
  auto truncatedHair10 = hair10;
  truncatedHair10.pop_back();
  writeBytes(hair10Path, truncatedHair10);
  assert(get(http.port(), "/assets/avatar-components/v1/hair/h10.gavc").status == 404);
  writeBytes(hair10Path, hair10);

  const auto previewPath =
      "/assets/avatar-previews/v1/h2-c5-f3-s4-o6.rgb565";
  const auto preview = get(http.port(), previewPath);
  if (preview.status != 200) {
    std::cerr << "avatar preview request failed: status=" << preview.status
              << " body=" << bodyText(preview) << '\n';
  }
  assert(preview.status == 200);
  assert(preview.body.size() == 220u * 300u * 2u);
  assert(preview.headers.at("content-type") == "application/octet-stream");
  assert(preview.headers.at("cache-control") ==
         "public, max-age=31536000, immutable");
  assert(preview.headers.count("etag") == 1);
  const auto preview304 = getWithHeader(
      http.port(), previewPath, "If-None-Match: " + preview.headers.at("etag") + "\r\n");
  assert(preview304.status == 304 && preview304.body.empty());
  for (const auto& target : {
           "/assets/avatar-previews/v1/h0-c5-f3-s4-o6.rgb565",
           "/assets/avatar-previews/v1/h11-c5-f3-s4-o6.rgb565",
           "/assets/avatar-previews/v1/h2-c21-f3-s4-o6.rgb565",
           "/assets/avatar-previews/v1/h02-c5-f3-s4-o6.rgb565",
           "/assets/avatar-previews/v1/h2-c5-f3-s4-o6.rgb565.png",
           "/assets/avatar-previews/v1/..%2Fidentity.bin",
       }) {
    assert(get(http.port(), target).status == 404);
  }

  const auto valid = get(http.port(), "/assets/tiles/a1-rivet-row.rgb565");
  assert(valid.status == 200);
  assert(valid.headers.at("content-type") == "application/octet-stream");
  assert(valid.headers.at("cache-control") == "public, max-age=31536000, immutable");
  assert(valid.headers.at("x-content-type-options") == "nosniff");
  assert(valid.body == rgb565);

  const auto legacyPng = get(http.port(), "/assets/tiles/a1-rivet-row.png");
  assert(legacyPng.status == 200);
  assert(legacyPng.headers.at("content-type") == "image/png");
  assert(legacyPng.headers.at("cache-control") == "public, max-age=31536000, immutable");
  assert(legacyPng.body == png);

  for (const auto& target : {
           "/assets/tiles/broken-tile.rgb565",
           "/assets/tiles/missing-tile.rgb565",
           "/assets/tiles/../state.bin",
           "/assets/tiles/..%2Fstate.bin",
           "/assets/tiles/a1_rivet_row.rgb565",
           "/assets/tiles/A1-rivet-row.rgb565",
           "/assets/tiles/a1-rivet-row.rgb565.png",
           "/assets/tiles/.rgb565",
       }) {
    const auto rejected = get(http.port(), target);
    assert(rejected.status == 404);
    assert(rejected.headers.at("cache-control") == "no-store");
  }

  const auto initialControlVersion = authority.controlVersion();
  const auto gameVersion = authority.stateVersion();
  const auto staleVersion = post(http.port(), "/api/forced-roll?player=1&target=7&expected=" +
      std::to_string(gameVersion - 1));
  assert(staleVersion.status == 409);
  assert(bodyText(staleVersion).find("state version mismatch") != std::string::npos);
  assert(!authority.forcedRollState().active);
  assert(authority.controlVersion() == initialControlVersion);
  const auto armed = post(http.port(), "/api/forced-roll?player=1&target=7&expected=" +
      std::to_string(gameVersion));
  assert(armed.status == 200);
  assert(bodyText(armed).find("\"active\":true") != std::string::npos);
  assert(bodyText(armed).find("\"player\":1") != std::string::npos);
  assert(bodyText(armed).find("\"target\":7") != std::string::npos);
  assert(bodyText(armed).find("\"steps\":7") != std::string::npos);
  assert(authority.stateVersion() == gameVersion);
  assert(authority.controlVersion() == initialControlVersion + 1);

  const auto sync = get(http.port(), "/api/sync");
  assert(sync.status == 200);
  assert(bodyText(sync).find("\"controlVersion\":" +
      std::to_string(initialControlVersion + 1)) != std::string::npos);
  assert(bodyText(sync).find(
      "\"forcedRoll\":{\"active\":true,\"player\":1,\"target\":7,\"steps\":7,\"origin\":0}") !=
      std::string::npos);

  const auto staleControl = get(http.port(), "/api/sync?since=" +
      std::to_string(authority.stateVersion()) + "&peers=0&room=" +
      std::to_string(authority.roomId()) + "&network=" +
      std::to_string(authority.networkId()) + "&control=" +
      std::to_string(initialControlVersion));
  assert(staleControl.status == 200);
  const auto currentControl = get(http.port(), "/api/sync?since=" +
      std::to_string(authority.stateVersion()) + "&peers=0&room=" +
      std::to_string(authority.roomId()) + "&network=" +
      std::to_string(authority.networkId()) + "&control=" +
      std::to_string(authority.controlVersion()) + "&tagBindings=" +
      std::to_string(authority.tagBindingRevision()) + "&identity=" +
      std::to_string(authority.identityRevision()));
  assert(currentControl.status == 204);

  const auto illegal = post(http.port(), "/api/forced-roll?player=1&target=1&expected=" +
      std::to_string(authority.stateVersion()));
  assert(illegal.status == 409);
  assert(bodyText(illegal).find("destination must be 2 to 12 spaces clockwise") !=
      std::string::npos);
  assert(authority.forcedRollState().active);

  const auto cancelled = post(http.port(), "/api/forced-roll?cancel=1");
  assert(cancelled.status == 200);
  assert(bodyText(cancelled).find("\"active\":false") != std::string::npos);
  assert(!authority.forcedRollState().active);

  const auto gameBeforeTileDebug = authority.stateCopy();
  const auto roomBeforeTileDebug = authority.roomId();
  const auto controlBeforeTileDebug = authority.controlVersion();
  const auto identityBeforeTileDebug = authority.identityRevision();
  const auto tileDebugEmpty = get(http.port(), "/api/tile-debug/assignments");
  assert(tileDebugEmpty.status == 200);
  assert(tileDebugEmpty.headers.at("content-type") == "application/json; charset=utf-8");
  assert(tileDebugEmpty.headers.at("cache-control") == "no-store");
  const auto tileDebugEmptyBody = bodyText(tileDebugEmpty);
  assert(tileDebugEmptyBody.find("\"boardId\":\"grid-city-32-v1\"") !=
         std::string::npos);
  assert(tileDebugEmptyBody.find("\"boardSize\":32") != std::string::npos);
  assert(tileDebugEmptyBody.find("\"revision\":0,\"serverRevision\":0") !=
         std::string::npos);
  assert(tileDebugEmptyBody.find("\"modules\":[]") != std::string::npos);
  assert(tileDebugEmptyBody.find("\"assignments\":[]") != std::string::npos);
  assert(tileDebugEmptyBody.find(
      "\"tileId\":\"A1\",\"mapIndex\":1,\"displayName\":\"Rivet Row\","
      "\"kind\":\"PROPERTY\",\"accentRgb\":13203538,"
      "\"artworkKey\":\"a1-rivet-row\",\"purchasePrice\":60") !=
      std::string::npos);
  assert(tileDebugEmptyBody.find("\"rgb\":15692136") != std::string::npos);

  const auto missingHeartbeat = post(http.port(), "/api/tile-modules/heartbeat");
  assert(missingHeartbeat.status == 400);
  assert(bodyText(missingHeartbeat).find("moduleId_and_deviceId_required") !=
         std::string::npos);
  assert(post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a").status == 400);
  assert(post(http.port(), "/api/tile-debug/assignment?tileId=A1").status == 400);
  assert(post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=bad%20id&deviceId=device-a").status ==
         400);
  assert(post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=bad%20id").status ==
         400);
  assert(post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-a&deviceId=device-a").status ==
         400);
  assert(remove(http.port(), "/api/tile-debug/assignment").status == 400);
  assert(get(http.port(), "/api/tile-modules/heartbeat").status == 405);
  assert(get(http.port(), "/api/tile-debug/assignment").status == 405);
  assert(post(http.port(), "/api/tile-debug/assignments").status == 405);

  const auto moduleAHeartbeat = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(moduleAHeartbeat.status == 200);
  const auto moduleAHeartbeatBody = bodyText(moduleAHeartbeat);
  assert(moduleAHeartbeatBody.find(
      "{\"ok\":true,\"assigned\":true,\"source\":\"auto\",\"leaseMs\":15000,"
      "\"serverRevision\":1,\"moduleId\":\"module-a\",\"deviceId\":\"device-a\","
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":"
      ) != std::string::npos);
  assert(moduleAHeartbeatBody.find(
      "\"assignment\":{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\","
      "\"source\":\"auto\",\"tile_id\":\"CORNER-START\",\"mapIndex\":0") !=
      std::string::npos);
  assert(moduleAHeartbeatBody.find("\"owner_player\":0") != std::string::npos);
  assert(moduleAHeartbeatBody.find("\"owner_color\":0") != std::string::npos);
  assert(moduleAHeartbeatBody.find("\"revision\":1") != std::string::npos);
  assert(moduleAHeartbeatBody.find("\"updatedAtMs\":") != std::string::npos);

  const auto moduleAHeartbeatRepeat = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(moduleAHeartbeatRepeat.status == 200);
  assert(bodyText(moduleAHeartbeatRepeat).find("\"serverRevision\":1") !=
         std::string::npos);
  assert(bodyText(moduleAHeartbeatRepeat).find("\"revision\":1") !=
         std::string::npos);

  const auto moduleBHeartbeat = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b");
  assert(moduleBHeartbeat.status == 200);
  assert(bodyText(moduleBHeartbeat).find(
      "{\"ok\":true,\"assigned\":true,\"source\":\"auto\",\"leaseMs\":15000,"
      "\"serverRevision\":2,\"moduleId\":\"module-b\",\"deviceId\":\"device-b\","
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":") !=
      std::string::npos);
  assert(bodyText(moduleBHeartbeat).find(
      "\"assignment\":{\"moduleId\":\"module-b\",\"deviceId\":\"device-b\","
      "\"source\":\"auto\",\"tile_id\":\"A1\",\"mapIndex\":1") !=
      std::string::npos);

  const auto tileDebugModules = get(http.port(), "/api/tile-debug/assignments");
  assert(tileDebugModules.status == 200);
  const auto tileDebugModulesBody = bodyText(tileDebugModules);
  assert(tileDebugModulesBody.find("\"serverRevision\":2") != std::string::npos);
  const auto moduleAAt = tileDebugModulesBody.find(
      "{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\",\"online\":true,"
      "\"assigned\":true,\"source\":\"auto\"");
  assert(moduleAAt != std::string::npos);
  const auto moduleAEnd = tileDebugModulesBody.find('}', moduleAAt);
  assert(moduleAEnd != std::string::npos);
  const auto moduleAText = tileDebugModulesBody.substr(moduleAAt, moduleAEnd - moduleAAt);
  assert(moduleAText.find("\"lastSeenMs\":") != std::string::npos);
  assert(moduleAText.find("\"leaseMs\":15000") != std::string::npos);
  assert(moduleAText.find("\"leaseRemainingMs\":") != std::string::npos);
  assert(moduleAText.find("\"registrationOrder\":1") != std::string::npos);
  assert(moduleAText.find("\"tagReaderState\":\"scanning\"") != std::string::npos);
  assert(moduleAText.find("\"tagRevision\":0") != std::string::npos);
  assert(moduleAText.find("\"tagOverflow\":false") != std::string::npos);
  const auto moduleBAt = tileDebugModulesBody.find(
      "{\"moduleId\":\"module-b\",\"deviceId\":\"device-b\",\"online\":true,"
      "\"assigned\":true,\"source\":\"auto\"", moduleAEnd);
  assert(moduleBAt != std::string::npos);
  const auto moduleBEnd = tileDebugModulesBody.find('}', moduleBAt);
  assert(moduleBEnd != std::string::npos);
  const auto moduleBText = tileDebugModulesBody.substr(moduleBAt, moduleBEnd - moduleBAt);
  assert(moduleBText.find("\"lastSeenMs\":") != std::string::npos);
  assert(moduleBText.find("\"leaseMs\":15000") != std::string::npos);
  assert(moduleBText.find("\"leaseRemainingMs\":") != std::string::npos);
  assert(moduleBText.find("\"registrationOrder\":2") != std::string::npos);

  const auto tileDebugSet = post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-a&deviceId=device-a&tileId=B1&"
      "displayName=FORGED&kind=FORGED&accent=1&artworkKey=FORGED&"
      "purchasePrice=9999&owner_display_name=FORGED&owner_color=1");
  assert(tileDebugSet.status == 200);
  const auto tileDebugSetBody = bodyText(tileDebugSet);
  assert(tileDebugSetBody.find("\"revision\":3,\"serverRevision\":3") !=
         std::string::npos);
  const auto manualAssignmentAt = tileDebugSetBody.find(
      "{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\",\"source\":\"manual\","
      "\"tile_id\":\"B1\",\"mapIndex\":6,\"displayName\":\"Lantern Avenue\","
      "\"kind\":\"PROPERTY\",\"accent\":6538984,"
      "\"artworkKey\":\"b1-lantern-avenue\",\"purchase_price\":100");
  assert(manualAssignmentAt != std::string::npos);
  const auto manualAssignmentEnd = tileDebugSetBody.find('}', manualAssignmentAt);
  assert(manualAssignmentEnd != std::string::npos);
  const auto manualAssignment =
      tileDebugSetBody.substr(manualAssignmentAt, manualAssignmentEnd - manualAssignmentAt);
  assert(manualAssignment.find("\"owner_player\":0") != std::string::npos);
  assert(manualAssignment.find("\"owner_color\":0") != std::string::npos);
  assert(manualAssignment.find("\"revision\":3") != std::string::npos);
  assert(tileDebugSetBody.find("\"tile_id\":\"CORNER-START\"") == std::string::npos);
  assert(tileDebugSetBody.find("FORGED") == std::string::npos);
  assert(tileDebugSetBody.find("\"purchase_price\":9999") == std::string::npos);
  assert(tileDebugSetBody.find("\"owner_color\":1,") == std::string::npos);

  const auto manualHeartbeat = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(manualHeartbeat.status == 200);
  const auto manualHeartbeatBody = bodyText(manualHeartbeat);
  assert(manualHeartbeatBody.find("\"source\":\"manual\",\"leaseMs\":15000") !=
         std::string::npos);
  assert(manualHeartbeatBody.find("\"serverRevision\":3") != std::string::npos);
  assert(manualHeartbeatBody.find(
      "\"assignment\":{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\","
      "\"source\":\"manual\",\"tile_id\":\"B1\",\"mapIndex\":6") !=
      std::string::npos);
  assert(manualHeartbeatBody.find("\"owner_player\":0") != std::string::npos);
  assert(manualHeartbeatBody.find("\"owner_color\":0") != std::string::npos);

  const auto tileConflict = post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-b&deviceId=device-b&tileId=B1");
  assert(tileConflict.status == 409);
  assert(bodyText(tileConflict).find(
      "\"code\":" + std::to_string(static_cast<unsigned>(
          gridopoly::pi::TileDebugResultCode::TileConflict))) != std::string::npos);
  const auto offlineManual = post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-c&deviceId=device-c&tileId=B2");
  assert(offlineManual.status == 409);
  assert(bodyText(offlineManual).find(
      "\"code\":" + std::to_string(static_cast<unsigned>(
          gridopoly::pi::TileDebugResultCode::ModuleOffline))) != std::string::npos);
  const auto deviceMismatch = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-wrong");
  assert(deviceMismatch.status == 409);
  assert(bodyText(deviceMismatch).find(
      "\"code\":" + std::to_string(static_cast<unsigned>(
          gridopoly::pi::TileDebugResultCode::DeviceMismatch))) != std::string::npos);
  assert(post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-a&deviceId=device-a&tileId=NOPE")
      .status == 404);

  const auto tileDebugSetRepeat = post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-a&deviceId=device-a&tileId=B1&"
      "ownerPlayerId=2&owner=7");
  assert(tileDebugSetRepeat.status == 200);
  const auto tileDebugSetRepeatBody = bodyText(tileDebugSetRepeat);
  assert(tileDebugSetRepeatBody.find("\"revision\":3,\"serverRevision\":3") !=
         std::string::npos);
  assert(tileDebugSetRepeatBody.find("\"tile_id\":\"B1\"") != std::string::npos);
  assert(tileDebugSetRepeatBody.find("\"owner_player\":0") != std::string::npos);
  assert(tileDebugSetRepeatBody.find("\"owner_color\":0") != std::string::npos);
  assert(tileDebugSetRepeatBody.find("\"owner_player\":2") == std::string::npos);
  assert(tileDebugSetRepeatBody.find("\"owner_player\":7") == std::string::npos);

  const auto tileDebugCleared = remove(
      http.port(), "/api/tile-debug/assignment?moduleId=module-a");
  assert(tileDebugCleared.status == 200);
  const auto tileDebugClearedBody = bodyText(tileDebugCleared);
  assert(tileDebugClearedBody.find("\"revision\":4,\"serverRevision\":4") !=
         std::string::npos);
  assert(tileDebugClearedBody.find(
      "{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\",\"online\":true,"
      "\"assigned\":false,\"source\":\"none\"") != std::string::npos);
  assert(tileDebugClearedBody.find(
      "\"assignments\":[{\"moduleId\":\"module-b\"") != std::string::npos);
  assert(tileDebugClearedBody.find("\"tile_id\":\"B1\"") == std::string::npos);

  const auto moduleAReclaimed = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(moduleAReclaimed.status == 200);
  const auto moduleAReclaimedBody = bodyText(moduleAReclaimed);
  assert(moduleAReclaimedBody.find(
      "{\"ok\":true,\"assigned\":true,\"source\":\"auto\",\"leaseMs\":15000,"
      "\"serverRevision\":5") != std::string::npos);
  assert(moduleAReclaimedBody.find(
      "\"assignment\":{\"moduleId\":\"module-a\",\"deviceId\":\"device-a\","
      "\"source\":\"auto\",\"tile_id\":\"CORNER-START\",\"mapIndex\":0") !=
      std::string::npos);

  const auto gameAfterTileDebug = authority.stateCopy();
  assert(authority.roomId() == roomBeforeTileDebug);
  assert(gameAfterTileDebug.stateVersion == gameBeforeTileDebug.stateVersion);
  assert(gameAfterTileDebug.assets[0].ownerId == gameBeforeTileDebug.assets[0].ownerId);
  assert(gameAfterTileDebug.players[0].cash == gameBeforeTileDebug.players[0].cash);
  assert(authority.controlVersion() == controlBeforeTileDebug);
  assert(authority.identityRevision() == identityBeforeTileDebug);

  // Stable HITAG S reports are globally deduplicated. Binding is revision
  // gated and a destination heartbeat invokes the ordinary ConfirmPosition
  // transaction exactly once. Overflow keeps the destination cue visible but
  // deliberately suppresses automatic arrival.
  const auto targetAssignment = post(http.port(),
      "/api/tile-debug/assignment?moduleId=module-b&deviceId=device-b&tileId=B2");
  assert(targetAssignment.status == 200);
  const auto firstTagReport = postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":17,"
      "\"tags\":[\"8EFA259D\",\"8EFA259D\"],\"overflow\":false}");
  assert(firstTagReport.status == 200);
  assert(bodyText(firstTagReport).find("\"movementCue\":{\"mode\":\"none\"") !=
         std::string::npos);
  const auto tagsBeforeBinding = get(http.port(), "/api/tile-tags");
  assert(tagsBeforeBinding.status == 200);
  assert(bodyText(tagsBeforeBinding).find(
      "\"uid\":\"8EFA259D\",\"currentlySeen\":true") != std::string::npos);
  assert(bodyText(tagsBeforeBinding).find("\"moduleId\":\"module-b\"") !=
         std::string::npos);
  assert(bodyText(tagsBeforeBinding).find("\"tileId\":\"B2\",\"mapIndex\":7") !=
         std::string::npos);

  const auto staleBinding = post(http.port(),
      "/api/player-tag-binding?playerId=1&uid=8EFA259D&expectedRevision=0");
  assert(staleBinding.status == 409);
  const auto binding = post(http.port(),
      "/api/player-tag-binding?playerId=1&uid=8efa259d&expectedRevision=" +
      std::to_string(authority.tagBindingRevision()));
  assert(binding.status == 200);
  assert(bodyText(binding).find("\"playerId\":1,\"displayName\":") !=
         std::string::npos);
  assert(bodyText(binding).find("\"uid\":\"8EFA259D\"") != std::string::npos);
  assert(authority.playerTagBindings().playerTagUids[0] == 0x8EFA259Du);

  assert(authority.setForcedRollTarget(1, 7, authority.stateVersion()));
  assert(authority.execute(gridopoly::protocol::ActionCode::Roll, 1, 0xFF, 0,
                           authority.stateVersion()));
  const auto waitingForTag = authority.stateCopy();
  assert(waitingForTag.phase == gridopoly::core::GamePhase::AwaitMoveConfirm);
  assert(waitingForTag.pendingMove.target == 7);
  const auto departureHeartbeat = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(departureHeartbeat.status == 200);
  assert(bodyText(departureHeartbeat).find(
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":" +
      std::to_string(waitingForTag.stateVersion)) != std::string::npos);

  const auto overflowDestination = postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":18,"
      "\"tags\":[\"8EFA259D\"],\"overflow\":true}");
  assert(overflowDestination.status == 200);
  assert(bodyText(overflowDestination).find(
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":" +
      std::to_string(waitingForTag.stateVersion)) != std::string::npos);
  assert(authority.stateVersion() == waitingForTag.stateVersion);

  const auto blockedDestination = postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":19,"
      "\"tags\":[\"8EFA259D\"],\"overflow\":false}");
  assert(blockedDestination.status == 200);
  assert(bodyText(blockedDestination).find(
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":" +
      std::to_string(waitingForTag.stateVersion)) != std::string::npos);
  assert(authority.stateVersion() == waitingForTag.stateVersion);

  // The player console releases LEDs and RFID only after its dice result and
  // first MoveGuide frame have actually been presented.
  assert(authority.execute(gridopoly::protocol::ActionCode::MovementCueReady,
                           1, 0xFF, waitingForTag.pendingMove.target,
                           waitingForTag.stateVersion));
  assert(authority.stateVersion() == waitingForTag.stateVersion);
  const auto releasedDeparture = post(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-a&deviceId=device-a");
  assert(releasedDeparture.status == 200);
  assert(bodyText(releasedDeparture).find(
      "\"movementCue\":{\"mode\":\"departure\",\"playerId\":1,\"revision\":" +
      std::to_string(waitingForTag.stateVersion)) != std::string::npos);

  // The tag was already stable while the gate was closed. An unchanged
  // full report must be re-evaluated after readiness, without a new tag edge.
  const auto arrived = postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":19,"
      "\"tags\":[\"8EFA259D\"],\"overflow\":false}");
  assert(arrived.status == 200);
  const auto arrivedState = authority.stateCopy();
  assert(arrivedState.stateVersion == waitingForTag.stateVersion + 1);
  assert(!arrivedState.pendingMove.active);
  assert(arrivedState.players[0].position == 7);
  assert(bodyText(arrived).find(
      "\"movementCue\":{\"mode\":\"none\",\"playerId\":0,\"revision\":" +
      std::to_string(arrivedState.stateVersion)) != std::string::npos);

  const auto duplicateArrival = postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":19,"
      "\"tags\":[\"8EFA259D\"],\"overflow\":false}");
  assert(duplicateArrival.status == 200);
  assert(authority.stateVersion() == arrivedState.stateVersion);
  assert(postJson(http.port(),
      "/api/tile-modules/heartbeat?moduleId=module-b&deviceId=device-b",
      "{\"tagReaderState\":\"stable\",\"tagRevision\":21,"
      "\"tags\":[\"XYZ\"],\"overflow\":false}").status == 400);

  const auto bindingRevision = authority.tagBindingRevision();
  assert(remove(http.port(),
      "/api/player-tag-binding?playerId=1&expectedRevision=" +
      std::to_string(bindingRevision - 1)).status == 409);
  const auto clearedBinding = remove(http.port(),
      "/api/player-tag-binding?playerId=1&expectedRevision=" +
      std::to_string(bindingRevision));
  assert(clearedBinding.status == 200);
  assert(authority.playerTagBindings().playerTagUids[0] == 0);

  const auto previousRoom = authority.roomId();
  assert(post(http.port(), "/api/new?size=16&humans=1&bots=0").status == 409);
  assert(post(http.port(), "/api/new?size=16&humans=0&bots=2").status == 409);
  assert(post(http.port(), "/api/new?size=16&humans=5&bots=2").status == 409);
  assert(post(http.port(), "/api/new?size=272&humans=2&bots=2").status == 409);
  assert(post(http.port(), "/api/new?size=16&humans=257&bots=1").status == 409);
  assert(post(http.port(), "/api/new?size=16&humans=1&bots=258").status == 409);
  assert(authority.roomId() == previousRoom);
  const auto created = post(http.port(), "/api/new?size=16&humans=2&bots=2");
  assert(created.status == 200);
  assert(authority.stateCopy().phase == gridopoly::core::GamePhase::Lobby);
  assert(authority.stateCopy().playerCount == 4);
  assert(bodyText(created).find("\"roomId\":") != std::string::npos);
  const auto identitySync = get(http.port(), "/api/sync");
  assert(identitySync.status == 200);
  assert(bodyText(identitySync).find("\"identity\":{\"revision\":") != std::string::npos);
  assert(bodyText(identitySync).find("\"phase\":1,\"serverEpochMs\":") !=
         std::string::npos);
  assert(bodyText(identitySync).find("\"humanCount\":2,\"botCount\":2") !=
         std::string::npos);
  assert(bodyText(identitySync).find("\"id\":1,\"name\":\"\"") !=
         std::string::npos);
  assert(authority.playerTagBindings().playerTagUids[0] == 0);

  gridopoly::protocol::IdentitySnapshot identity{};
  assert(authority.makeIdentitySnapshot(1, identity, true));
  gridopoly::protocol::IdentityRequest avatar{};
  avatar.operation = gridopoly::protocol::IdentityOperation::ConfirmAvatar;
  avatar.playerId = 1;
  avatar.requestId = 7001;
  avatar.expectedStateVersion = authority.stateVersion();
  avatar.expectedSeatRevision = identity.seats[0].seatRevision;
  avatar.avatarCatalogVersion = gridopoly::protocol::kAvatarCatalogVersionV1;
  avatar.recipe = {gridopoly::protocol::kAvatarCatalogVersionV1, 2, 5, 3, 4, 6};
  authority.handleIdentityRequest(1, avatar, identity);
  assert(identity.result == gridopoly::protocol::IdentityResultCode::Ok);
  for (int attempt = 0; attempt < 200; ++attempt) {
    authority.tick();
    authority.makeIdentitySnapshot(1, identity);
    if ((identity.seats[0].flags & gridopoly::protocol::IdentitySeatAvatarFinal) != 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert((identity.seats[0].flags & gridopoly::protocol::IdentitySeatAvatarFinal) != 0);
  std::ostringstream avatarKey;
  avatarKey << "/assets/avatars/" << authority.roomId() << "/p1-a"
            << identity.seats[0].avatarRevision << '-' << std::hex << std::setfill('0')
            << std::setw(16) << identity.seats[0].avatarContentHash64;
  const auto finalPng = get(http.port(), avatarKey.str() + ".png");
  const auto finalRgb = get(http.port(), avatarKey.str() + ".rgb565");
  assert(finalPng.status == 200 && finalPng.headers.at("content-type") == "image/png");
  assert(finalRgb.status == 200 && finalRgb.body.size() == 128u * 128u * 2u);
  assert(finalPng.headers.count("etag") == 1);
  assert(getWithHeader(http.port(), avatarKey.str() + ".png",
      "If-None-Match: " + finalPng.headers.at("etag") + "\r\n").status == 304);
  assert(get(http.port(), "/assets/avatars/../state.bin").status == 404);
  const auto avatarSync = get(http.port(), "/api/sync");
  assert(avatarSync.status == 200);
  assert(bodyText(avatarSync).find("\"avatarUrl\":\"" + avatarKey.str() +
                                   ".png\"") != std::string::npos);
  assert(bodyText(avatarSync).find("http://127.0.0.1/assets/avatars/") ==
         std::string::npos);

  http.stop();
  std::filesystem::remove_all(temporary);
  std::cout << "GRIDOPOLY_HTTP_ASSET_INTEGRATION_TESTS_PASS\n";
  return 0;
}
