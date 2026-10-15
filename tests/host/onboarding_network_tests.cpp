#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gridopoly/protocol/Protocol.h>
#include <gridopoly/protocol/UdpEnvelope.h>

#include "settlement_assertions.h"
#include "../../Server/RaspberryPi/src/AuthorityService.h"
#include "../../Server/RaspberryPi/src/HttpServer.h"
#include "../../Server/RaspberryPi/src/UdpPlayerServer.h"

#ifndef GRIDOPOLY_SOURCE_DIR
#error GRIDOPOLY_SOURCE_DIR is required
#endif

using namespace gridopoly::protocol;

namespace {

constexpr const char* kPsk = "gridopoly-isolated-onboarding-test-only";
constexpr int kNetworkTimeoutMs = 2000;

struct HttpResponse { int status{}; std::string body; };

HttpResponse request(std::uint16_t port, const std::string& method,
                     const std::string& target, const std::string& body = {}) {
  assert(port != 0 && port != 80 && port != 4242);
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  assert(fd >= 0);
  timeval timeout{2, 0};
  assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  assert(::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
  const auto text = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
      "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\nConnection: close\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < text.size()) {
    const auto n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
    assert(n > 0); sent += static_cast<std::size_t>(n);
  }
  std::string raw;
  std::array<char, 8192> bytes{};
  for (;;) {
    const auto n = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (n == 0) break;
    assert(n > 0); raw.append(bytes.data(), static_cast<std::size_t>(n));
  }
  ::close(fd);
  const auto split = raw.find("\r\n\r\n");
  assert(split != std::string::npos && raw.compare(0, 9, "HTTP/1.1 ") == 0);
  return {std::stoi(raw.substr(9, 3)), raw.substr(split + 4)};
}

std::size_t occurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0, position = 0;
  while ((position = text.find(needle, position)) != std::string::npos) {
    ++count; position += needle.size();
  }
  return count;
}

std::uint32_t number(const std::string& text, const std::string& key) {
  const auto needle = '"' + key + "\":";
  const auto start = text.find(needle);
  assert(start != std::string::npos);
  return static_cast<std::uint32_t>(std::stoul(text.substr(start + needle.size())));
}

std::string stringValue(const std::string& text, const std::string& key) {
  const auto needle = '"' + key + "\":\"";
  const auto start = text.find(needle);
  assert(start != std::string::npos);
  const auto content = start + needle.size();
  const auto end = text.find('"', content);
  assert(end != std::string::npos);
  return text.substr(content, end - content);
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void waitUntil(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!predicate()) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void assertOnboardingGameplayUnchanged(const gridopoly::core::GameState& before,
                                      const gridopoly::core::GameState& after) {
  // Identity edits/completions legitimately advance stateVersion. They do not
  // move pieces, settle money, change assets, consume dice RNG or emit game events.
  assertSettlementUnchanged(before, after);
  assert(after.board == before.board && after.roundNumber == before.roundNumber);
  assert(after.winnerPlayerId == before.winnerPlayerId);
  assert(after.nextAuctionGeneration == before.nextAuctionGeneration && after.nextTradeId == before.nextTradeId);
  const auto& a = after.pendingMove;
  const auto& b = before.pendingMove;
  assert(a.active == b.active && a.playerId == b.playerId && a.origin == b.origin && a.target == b.target);
  assert(a.dieA == b.dieA && a.dieB == b.dieB && a.passedStart == b.passedStart);
  assert(after.pendingPurchase.active == before.pendingPurchase.active);
  assert(after.pendingPurchase.playerId == before.pendingPurchase.playerId);
  assert(after.pendingPurchase.assetIndex == before.pendingPurchase.assetIndex);
  assert(after.pendingCard.active == before.pendingCard.active && after.pendingCard.stage == before.pendingCard.stage);
  assert(after.auction.active == before.auction.active && after.auction.currentBid == before.auction.currentBid);
  for (std::size_t i = 0; i < before.players.size(); ++i) {
    assert(after.players[i].id == before.players[i].id && after.players[i].controller == before.players[i].controller);
    assert(after.players[i].inHold == before.players[i].inHold);
    assert(after.players[i].failedHoldRolls == before.players[i].failedHoldRolls);
    assert(after.players[i].doublesStreak == before.players[i].doublesStreak);
  }
  for (std::size_t i = 0; i < before.trades.size(); ++i) {
    assert(after.trades[i].status == before.trades[i].status && after.trades[i].tradeId == before.trades[i].tradeId);
  }
}

struct Fixture {
  std::atomic<std::uint64_t> now{100000};
  std::filesystem::path directory;
  std::unique_ptr<gridopoly::pi::AuthorityService> authority;
  std::unique_ptr<gridopoly::pi::UdpPlayerServer> udp;
  std::unique_ptr<gridopoly::pi::HttpServer> http;

  explicit Fixture(const char* name, std::uint8_t initialBoardSize = 32) {
    directory = std::filesystem::temp_directory_path() /
        ("gridopoly-onboarding-network-" + std::to_string(::getpid()) + '-' + name);
    assert(!std::filesystem::exists(directory));
    std::filesystem::create_directories(directory);
    gridopoly::pi::AuthorityIdentityOptions options;
    options.identityPath = directory / "identity.bin";
    options.avatarComponentRoot = std::filesystem::path(GRIDOPOLY_SOURCE_DIR) /
        "Assets/GridCity/Avatars/V1/runtime/components-v1";
    options.avatarAssetRoot = directory / "avatars";
    options.epochClock = [this] { return now.load(); };
    authority = std::make_unique<gridopoly::pi::AuthorityService>(directory / "state.bin",
        directory / "authority.meta", 0xA0102040u, std::chrono::milliseconds(1200), options);
    assert(authority->initialize());
    assert(authority->newGame(initialBoardSize, 1, 3));
    udp = std::make_unique<gridopoly::pi::UdpPlayerServer>(*authority, kPsk,
        directory / "registry.bin", "127.0.0.1", "127.255.255.255", 0);
    http = std::make_unique<gridopoly::pi::HttpServer>(*authority, *udp,
        0, "127.0.0.1", 2, directory / "tiles");
    assert(udp->start() && http->start());
    assert(udp->port() != 0 && udp->port() != 4242 && http->port() != 80);
  }
  ~Fixture() {
    http->stop(); udp->stop(); http.reset(); udp.reset(); authority.reset();
    std::filesystem::remove_all(directory);
  }
  HttpResponse get(const std::string& path) { return request(http->port(), "GET", path); }
  HttpResponse post(const std::string& path, const std::string& body = {}) {
    return request(http->port(), "POST", path, body);
  }
  void advance(std::uint64_t ms) { now.fetch_add(ms); authority->tick(); }
  std::string conditionalSync(std::uint32_t pairing) const {
    return "/api/sync?since=" + std::to_string(authority->stateVersion()) +
        "&peers=" + std::to_string(authority->peerCount()) +
        "&room=" + std::to_string(authority->roomId()) +
        "&network=" + std::to_string(authority->networkId()) +
        "&control=" + std::to_string(authority->controlVersion()) +
        "&tagBindings=" + std::to_string(authority->tagBindingRevision()) +
        "&identity=" + std::to_string(authority->identityRevision()) +
        "&pairing=" + std::to_string(pairing);
  }
};

struct Received {
  Header header{};
  std::array<std::uint8_t, kMaxPayloadSize> payload{};
  std::size_t size{};
};

struct Client {
  int fd{-1};
  sockaddr_in server{};
  std::uint32_t deviceId{}, nonce{0x10203040u}, room{}, session{}, sequence{1};
  std::uint64_t packetSequence{1};
  std::uint32_t capabilities{3};
  std::uint8_t seat{};
  std::string name;
  std::array<std::uint8_t, kUdpKeySize> pairKey{}, sessionKey{};

  Client(Fixture& fixture, std::uint8_t suffix, bool modern = true) {
    const std::array<std::uint8_t, 6> mac{{0xDC, 0xB4, 0xD9, 0x02, 0xD1, suffix}};
    deviceId = 2166136261u;
    std::ostringstream display;
    display << "GP-" << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : mac) {
      deviceId = (deviceId ^ byte) * 16777619u;
      display << std::setw(2) << static_cast<unsigned>(byte);
    }
    if (!deviceId) deviceId = 1;
    name = modern ? display.str() : "Legacy";
    if (!modern) capabilities = 1;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(fixture.udp->port());
    deriveUdpPairKey(kPsk, pairKey);
  }
  ~Client() { if (fd >= 0) ::close(fd); }
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void reopen() {
    if (fd >= 0) ::close(fd);
    fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_in local{};
    local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0);
  }
  std::uint32_t send(MessageType type, const std::uint8_t* payload,
                     std::size_t size, bool pairing = false) {
    Header header;
    header.type = type; header.flags = FlagAckRequired;
    header.sequence = sequence++; header.roomId = room; header.deviceId = deviceId;
    std::array<std::uint8_t, kMaxFrameSize> frame{};
    std::size_t frameSize = 0;
    assert(encodeFrame(header, payload, size, frame.data(), frame.size(), frameSize));
    UdpEnvelopeHeader envelope;
    envelope.flags = pairing ? UdpFlagPairingKey : UdpFlagNone;
    envelope.sessionId = pairing ? 0 : session;
    envelope.senderDeviceId = deviceId; envelope.packetSequence = packetSequence++;
    std::array<std::uint8_t, kMaxUdpDatagramSize> bytes{};
    std::size_t written = 0;
    assert(encodeUdpDatagram(envelope, pairing ? pairKey : sessionKey,
        frame.data(), frameSize, bytes.data(), bytes.size(), written));
    assert(::sendto(fd, bytes.data(), written, 0,
        reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == static_cast<ssize_t>(written));
    return header.sequence;
  }
  Received receive(const std::function<bool(const Received&)>& wanted, bool pairing = false) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kNetworkTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
      pollfd descriptor{fd, POLLIN, 0};
      if (::poll(&descriptor, 1, 20) <= 0) continue;
      std::array<std::uint8_t, kMaxUdpDatagramSize> bytes{};
      const auto size = ::recv(fd, bytes.data(), bytes.size(), 0);
      assert(size > 0);
      DecodedUdpDatagram datagram;
      if (!decodeUdpDatagram(bytes.data(), static_cast<std::size_t>(size),
          pairing ? pairKey : sessionKey, datagram)) continue;
      if (!pairing && datagram.header.sessionId != session) continue;
      DecodedFrame frame;
      assert(decodeFrame(datagram.frame, datagram.header.frameLength, frame));
      if (frame.header.roomId != room) continue;
      Received result;
      result.header = frame.header; result.size = frame.header.payloadLength;
      std::copy_n(frame.payload, result.size, result.payload.begin());
      if (wanted(result)) return result;
    }
    std::cerr << "UDP timeout for " << name << " room=" << room << " session=" << session << '\n';
    assert(false && "expected authenticated response did not arrive");
    return {};
  }
  PairAccept pair(Fixture& fixture, bool reboot = false) {
    reopen(); room = fixture.authority->roomId();
    if (reboot) ++nonce;
    PairRequest input;
    input.deviceNonce = nonce; input.capabilities = capabilities;
    std::strncpy(input.displayName, name.c_str(), 16);
    std::array<std::uint8_t, kMaxPayloadSize> payload{};
    std::size_t written = 0;
    assert(encodePairRequest(input, payload.data(), payload.size(), written));
    const auto sent = send(MessageType::PairRequest, payload.data(), written, true);
    const auto response = receive([sent](const Received& r) {
      return r.header.type == MessageType::PairAccept && r.header.acknowledgement == sent;
    }, true);
    PairAccept accepted;
    assert(decodePairAccept(response.payload.data(), response.size, accepted));
    seat = accepted.seatId;
    if (accepted.accepted) {
      assert(seat != 0 && accepted.sessionId != 0);
      session = accepted.sessionId;
      deriveUdpSessionKey(pairKey, accepted.serverDeviceId, deviceId, nonce, room, session, sessionKey);
    } else assert(seat == 0);
    return accepted;
  }
  void expectOnboardingSync(std::uint8_t humans) {
    bool pairingSeen = false;
    const auto response = receive([&](const Received& r) {
      if (r.header.type == MessageType::TagPairSnapshot) {
        TagPairSnapshot tag;
        assert(decodeTagPairSnapshot(r.payload.data(), r.size, tag));
        assert(tag.selfPlayerId == seat && tag.roomId == room);
        assert(tag.requiredHumanMask == (1u << humans) - 1u);
        assert((tag.flags & (TagPairFlagEnabled | TagPairFlagResync)) ==
            (TagPairFlagEnabled | TagPairFlagResync));
        pairingSeen = true;
      }
      return r.header.type == MessageType::IdentitySnapshot;
    });
    IdentitySnapshot identity;
    assert(decodeIdentitySnapshot(response.payload.data(), response.size, identity));
    assert(pairingSeen && identity.selfPlayerId == seat && identity.playerCount == 4);
    assert(identity.selfStage == IdentitySeatStage::TagPairing);
    assert(identity.roomPhase == IdentityRoomPhase::AvatarSetup);
  }
  TagPairSnapshot tagRequest(const TagPairRequest& input) {
    std::array<std::uint8_t, kTagPairRequestSize> payload{};
    std::size_t written = 0;
    assert(encodeTagPairRequest(input, payload.data(), payload.size(), written));
    const auto sent = send(MessageType::TagPairRequest, payload.data(), written);
    const auto response = receive([sent, &input](const Received& r) {
      if (r.header.type != MessageType::TagPairSnapshot ||
          !(r.header.flags & FlagResponse) || r.header.acknowledgement != sent) return false;
      TagPairSnapshot value;
      assert(decodeTagPairSnapshot(r.payload.data(), r.size, value));
      return value.requestId == input.requestId;
    });
    TagPairSnapshot value;
    assert(decodeTagPairSnapshot(response.payload.data(), response.size, value));
    return value;
  }
  TagPairSnapshot query(std::uint32_t id = 1) {
    TagPairRequest input;
    input.playerId = seat; input.roomId = room; input.requestId = id;
    return tagRequest(input);
  }
  TagPairSnapshot heartbeatSnapshot(std::uint32_t version) {
    Heartbeat heartbeat{}; heartbeat.appliedStateVersion = version;
    std::array<std::uint8_t, kMaxPayloadSize> payload{};
    std::size_t written = 0;
    assert(encodeHeartbeat(heartbeat, payload.data(), payload.size(), written));
    const auto sent = send(MessageType::Heartbeat, payload.data(), written);
    const auto response = receive([sent](const Received& r) {
      return r.header.type == MessageType::TagPairSnapshot && r.header.acknowledgement == sent;
    });
    TagPairSnapshot value;
    assert(decodeTagPairSnapshot(response.payload.data(), response.size, value));
    return value;
  }
  IdentitySnapshot identityRequest(IdentityRequest input) {
    input.playerId = seat;
    std::array<std::uint8_t, kIdentityRequestSize> payload{};
    std::size_t written = 0;
    assert(encodeIdentityRequest(input, payload.data(), payload.size(), written));
    const auto sent = send(MessageType::IdentityRequest, payload.data(), written);
    const auto response = receive([sent, &input](const Received& r) {
      if (r.header.type != MessageType::IdentitySnapshot || r.header.acknowledgement != sent) return false;
      IdentitySnapshot value;
      assert(decodeIdentitySnapshot(r.payload.data(), r.size, value));
      return value.requestId == input.requestId;
    });
    IdentitySnapshot value;
    assert(decodeIdentitySnapshot(response.payload.data(), response.size, value));
    return value;
  }
};

void newAutomaticGame(Fixture& fixture, std::uint8_t humans, std::uint8_t boardSize = 32) {
  const auto priorRoom = fixture.authority->roomId();
  const auto result = fixture.post("/api/new?size=" + std::to_string(boardSize) + "&autoPlayers=1");
  assert(result.status == 200 && number(result.body, "roomId") != priorRoom);
  assert(fixture.authority->stateCopy().playerCount == 4);
  assert(fixture.authority->stateCopy().board->tileCount == boardSize);
  const auto sync = fixture.get("/api/sync");
  assert(sync.status == 200 && sync.body.find("\"frozenDevices\":[") != std::string::npos);
  assert(sync.body.find("\"tagPairing\":{") != std::string::npos);
  assert(number(fixture.get("/api/tag-pairing").body, "requiredHumanMask") == (1u << humans) - 1u);
  for (unsigned seat = 1; seat <= 4; ++seat)
    assert(fixture.authority->isHumanSeat(static_cast<std::uint8_t>(seat)) == (seat <= humans));
  // The next authenticated PairRequest may arrive before the UDP loop notices
  // this HTTP room change. It must survive that room-change boundary.
}

void discoveryAndFrozenSeats() {
  Fixture fixture("discovery");
  const auto initialRoom = fixture.authority->roomId();
  auto result = fixture.post("/api/new?size=32&autoPlayers=1");
  assert(result.status == 409 && result.body.find("no_eligible_players") != std::string::npos);
  assert(fixture.authority->roomId() == initialRoom);
  Client legacy(fixture, 0x10, false);
  assert(legacy.pair(fixture).accepted == 1);
  assert(occurrences(fixture.get("/api/player-devices").body, "\"eligible\":true") == 0);
  assert(fixture.post("/api/new?size=32&autoPlayers=1").status == 409);

  std::vector<std::unique_ptr<Client>> players;
  for (unsigned count = 1; count <= 5; ++count) {
    players.push_back(std::make_unique<Client>(fixture, static_cast<std::uint8_t>(0x20 + count)));
    // Existing seats are full/reserved, including bots in each frozen room.
    assert(players.back()->pair(fixture).accepted == 0);
    const auto devices = fixture.get("/api/player-devices");
    assert(devices.status == 200 && number(devices.body, "maxPlayers") == 4);
    assert(occurrences(devices.body, "\"eligible\":true") == count);
    assert(devices.body.find(players.back()->name) != std::string::npos);
    if (count == 5) {
      const auto room = fixture.authority->roomId();
      const auto version = fixture.authority->stateVersion();
      result = fixture.post("/api/new?size=32&autoPlayers=1");
      assert(result.status == 409 && result.body.find("too_many_players") != std::string::npos);
      assert(fixture.authority->roomId() == room && fixture.authority->stateVersion() == version);
      break;
    }
    newAutomaticGame(fixture, static_cast<std::uint8_t>(count));
    std::vector<std::uint32_t> ids;
    for (const auto& player : players) ids.push_back(player->deviceId);
    std::sort(ids.begin(), ids.end());
    for (const auto& player : players) {
      assert(player->pair(fixture).accepted == 1);
      assert(player->seat == std::find(ids.begin(), ids.end(), player->deviceId) - ids.begin() + 1);
      player->expectOnboardingSync(static_cast<std::uint8_t>(count));
    }
    const auto oldSeat = players.front()->seat;
    const auto oldSession = players.front()->session;
    assert(players.front()->pair(fixture, true).accepted == 1);
    assert(players.front()->seat == oldSeat && players.front()->session != oldSession);
    players.front()->expectOnboardingSync(static_cast<std::uint8_t>(count));
    const auto frozen = fixture.get("/api/sync").body;
    for (const auto& player : players) {
      const auto identity = "\"deviceId\":" + std::to_string(player->deviceId) +
          ",\"displayName\":\"" + player->name + "\",\"online\":true,\"seatId\":" +
          std::to_string(player->seat);
      assert(frozen.find(identity) != std::string::npos);
    }
  }
  // Eligibility expires on the authority clock without changing frozen seats.
  const auto oldRoom = fixture.authority->roomId();
  fixture.advance(15001);
  assert(occurrences(fixture.get("/api/player-devices").body, "\"eligible\":true") == 0);
  assert(fixture.post("/api/new?size=32&autoPlayers=1").status == 409);
  assert(fixture.authority->roomId() == oldRoom);
}

struct Reader {
  Fixture& fixture;
  std::string module, boot, lastBody;
  std::uint32_t scan{}, revision{}, room{}, session{};
  std::uint16_t tx{};
  std::vector<std::uint32_t> inventory;
  Reader(Fixture& f, const char* id, const char* bootId) : fixture(f), module(id), boot(bootId) {}
  HttpResponse report(const std::vector<std::uint32_t>& tags, bool fresh = true,
                      bool newScan = true, std::uint32_t age = 0) {
    const auto gameBeforeReport = fixture.authority->stateCopy();
    fixture.now.fetch_add(5);
    if (newScan) ++scan;
    if (tags != inventory) { ++revision; inventory = tags; }
    std::ostringstream body;
    body << "{\"moduleRole\":\"long\",\"firmwareVersion\":\"V033-test\","
        "\"tagReaderState\":\"stable\",\"tagRevision\":" << revision << ",\"tags\":[";
    for (std::size_t i = 0; i < tags.size(); ++i) {
      if (i) body << ',';
      body << '"' << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
           << tags[i] << '"' << std::dec;
    }
    body << "],\"overflow\":false,\"tagFresh\":" << (fresh ? "true" : "false")
        << ",\"tagScanSeq\":" << scan << ",\"tagAgeMs\":" << age
        << ",\"pairingRoomId\":" << room << ",\"pairingSessionId\":" << session
        << ",\"order\":{\"version\":1,\"bootId\":\"" << boot << "\",\"txSeq\":" << ++tx
        << ",\"upstreamBootId\":null,\"upstreamSeq\":0,\"ageMs\":0,"
        "\"valid\":false,\"inputState\":\"idle\"}}";
    lastBody = body.str();
    const auto result = fixture.post("/api/tile-modules/heartbeat?moduleId=" + module +
        "&deviceId=device-" + module, lastBody);
    if (result.status != 200) std::cerr << "Reader rejected: " << result.body << '\n';
    assert(result.status == 200);
    const auto gameAfterReport = fixture.authority->stateCopy();
    assert(gameAfterReport.stateVersion == gameBeforeReport.stateVersion);
    assertOnboardingGameplayUnchanged(gameBeforeReport, gameAfterReport);
    return result;
  }
  void echo(const TagPairSnapshot& snapshot) { room = snapshot.roomId; session = snapshot.sessionId; }
};

TagPairRequest mutation(Client& client, const TagPairSnapshot& state,
                        TagPairOperation operation, std::uint32_t requestId) {
  TagPairRequest input;
  input.playerId = client.seat; input.roomId = client.room; input.operation = operation;
  input.requestId = requestId; input.sessionId = state.sessionId; input.expectedRevision = state.revision;
  return input;
}

void tagOnboardingAcrossTransports() {
  Fixture fixture("tag-flow");
  Client first(fixture, 0x41), second(fixture, 0x42);
  assert(first.pair(fixture).accepted == 1);
  assert(second.pair(fixture).accepted == 0);
  newAutomaticGame(fixture, 2);
  assert(first.pair(fixture).accepted == 1); first.expectOnboardingSync(2);
  assert(second.pair(fixture).accepted == 1); second.expectOnboardingSync(2);
  Client& one = first.seat == 1 ? first : second;
  Client& two = first.seat == 2 ? first : second;
  const auto room = fixture.authority->roomId();
  const auto gameBeforeOnboarding = fixture.authority->stateCopy();
  const auto version = gameBeforeOnboarding.stateVersion;
  const auto event = fixture.authority->latestEventSequence();
  auto pairing = one.query();
  assert(pairing.state == TagPairState::WaitingReader && pairing.activePlayerId == 1);
  assert(!(pairing.flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)));
  assert(one.query().revision == pairing.revision);  // Query is read-only and repeatable.

  IdentityRequest beforeAvatar; beforeAvatar.requestId = 49;
  auto identity = one.identityRequest(beforeAvatar);
  assert(identity.selfStage == IdentitySeatStage::TagPairing);
  IdentityRequest avatar;
  avatar.operation = IdentityOperation::ConfirmAvatar; avatar.requestId = 50;
  avatar.expectedStateVersion = version; avatar.avatarCatalogVersion = kAvatarCatalogVersionV1;
  avatar.expectedSeatRevision = identity.seats[one.seat - 1].seatRevision;
  avatar.recipe = {kAvatarCatalogVersionV1, 1, 1, 1, 1, 1};
  identity = one.identityRequest(avatar);
  assert(identity.result == IdentityResultCode::NotAllowed && identity.selfStage == IdentitySeatStage::TagPairing);

  // Cancel/retry have a request identity independent of the UDP frame sequence.
  auto cancel = mutation(one, pairing, TagPairOperation::Cancel, 100);
  pairing = one.tagRequest(cancel);
  assert(pairing.result == TagPairResultCode::Ok && pairing.state == TagPairState::Cancelled);
  const auto cancelledRevision = pairing.revision;
  auto replay = one.tagRequest(cancel);
  assert((replay.flags & TagPairFlagReplay) && replay.revision == cancelledRevision);
  auto conflict = cancel; conflict.operation = TagPairOperation::Retry;
  assert(one.tagRequest(conflict).result == TagPairResultCode::RequestIdConflict);
  auto retry = mutation(one, pairing, TagPairOperation::Retry, 101);
  const auto cancelledSession = pairing.sessionId;
  pairing = one.tagRequest(retry);
  assert(pairing.result == TagPairResultCode::Ok && pairing.sessionId != cancelledSession);
  assert(pairing.state == TagPairState::WaitingReader);
  replay = one.tagRequest(retry);
  assert((replay.flags & TagPairFlagReplay) && replay.sessionId == pairing.sessionId && replay.revision == pairing.revision);
  auto wrongRoom = mutation(one, pairing, TagPairOperation::Retry, 102); --wrongRoom.roomId;
  assert(one.tagRequest(wrongRoom).result == TagPairResultCode::RoomStale);
  auto wrongSession = mutation(one, pairing, TagPairOperation::Retry, 103); --wrongSession.sessionId;
  assert(one.tagRequest(wrongSession).result == TagPairResultCode::SessionStale);
  auto unauthorized = mutation(two, pairing, TagPairOperation::Retry, 104);
  unauthorized.playerId = one.seat;
  assert(two.tagRequest(unauthorized).result == TagPairResultCode::Unauthorized);
  // Web and the player console share one mutation identity/cache.
  const auto webCancel = mutation(one, pairing, TagPairOperation::Cancel, 105);
  const auto webTarget = "/api/tag-pairing?operation=cancel&playerId=1&requestId=105&roomId=" +
      std::to_string(room) + "&sessionId=" + std::to_string(webCancel.sessionId) +
      "&expectedRevision=" + std::to_string(webCancel.expectedRevision);
  const auto webResult = fixture.post(webTarget);
  assert(webResult.status == 200 && stringValue(webResult.body, "state") == "cancelled");
  assert(fixture.post(webTarget).body.find("\"replay\":true") != std::string::npos);
  pairing = one.tagRequest(webCancel);
  assert(pairing.result == TagPairResultCode::Ok && (pairing.flags & TagPairFlagReplay));
  assert(pairing.revision == number(webResult.body, "revision"));
  pairing = one.tagRequest(mutation(one, pairing, TagPairOperation::Retry, 106));
  assert(pairing.result == TagPairResultCode::Ok && pairing.state == TagPairState::WaitingReader);

  Reader a(fixture, "reader-a", "ABCDEF0000000001");
  Reader b(fixture, "reader-b", "ABCDEF0000000002");
  const auto occupied = a.report({111});
  assert(occupied.body.find("\"assigned\":false") != std::string::npos);
  assert(occupied.body.find("\"pairing\":{\"active\":true") != std::string::npos);
  pairing = one.query(); a.echo(pairing);
  assert(pairing.state == TagPairState::ClearRequired && pairing.boundMask == 0);
  assert(pairing.readerMapIndex == 255 && std::string(pairing.moduleId.data()) == a.module);
  const auto validBody = a.lastBody;
  auto replace = [&](const std::string& from, const std::string& to) {
    auto edited = validBody;
    const auto position = edited.find(from); assert(position != std::string::npos);
    edited.replace(position, from.size(), to); return edited;
  };
  const auto unchangedBindings = fixture.authority->tagBindingRevision();
  for (const auto& malformed : std::vector<std::string>{
      validBody + " trailing",
      replace("\"tagFresh\":true", "\"tagFresh\":true,\"tagFresh\":false"),
      replace("\"tagFresh\":true", "\"nested\":{\"tagFresh\":true}"),
      replace("\"tagFresh\":true,", ""),
      replace("\"tagAgeMs\":0", "\"tagAgeMs\":0junk"),
      replace("\"tags\":[\"0000006F\"]", "\"tags\":[\"0000006F\",\"0000006F\"]"),
      replace("\"tags\":[\"0000006F\"]", "\"tags\":[111]"),
  }) {
    assert(fixture.post("/api/tile-modules/heartbeat?moduleId=reader-a&deviceId=device-reader-a",
        malformed).status == 400);
    assert(fixture.authority->tagBindingRevision() == unchangedBindings);
  }
  a.report({111});
  assert(one.query().boundMask == 0);  // Occupied inventory never counts as first placement.
  // A changed echo on the same cached scan must not establish the empty fence.
  a.report({}, true, false);
  a.report({111});
  assert(one.query().boundMask == 0);
  a.room = room - 1; a.report({}); a.report({222});
  assert(one.query().boundMask == 0);
  a.room = room; a.session = pairing.sessionId - 1; a.report({}); a.report({222});
  assert(one.query().boundMask == 0);
  a.echo(pairing); a.report({222}, false);  // Cached visible tags are valid JSON but unusable evidence.
  assert(one.query().boundMask == 0);
  a.report({222}); pairing = one.query(); a.echo(pairing);
  assert(pairing.state == TagPairState::ClearRequired);
  a.report({}); assert(one.query().state == TagPairState::AwaitingTag);
  a.report({222, 333}); assert(one.query().state == TagPairState::MultipleTags);
  a.report({222}); assert(one.query().boundMask == 0); // Multi-tag requires clearing again.
  a.report({});
  const auto beforeBindingRevision = one.query().revision;
  a.report({111});
  pairing = one.query();
  assert(pairing.state == TagPairState::SuccessRemove && pairing.boundMask == 1 && pairing.tagUid == 111);
  assert((pairing.flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)) ==
      (TagPairFlagSelfBound | TagPairFlagCanEditIdentity));
  const auto heartbeat = one.heartbeatSnapshot(version);
  assert(heartbeat.boundMask == 1 && (heartbeat.flags & TagPairFlagCanEditIdentity));
  assert(fixture.authority->playerTagBindings().playerTagUids[0] == 111);
  const auto json = fixture.get("/api/tag-pairing").body;
  assert(number(json, "boundMask") == 1 && number(json, "identityEditableMask") == 1);
  assert(stringValue(json, "state") == "success_remove");
  assert(fixture.get(fixture.conditionalSync(beforeBindingRevision)).status == 200);
  assert(fixture.authority->stateVersion() == version); // First complete Tag-only segment.
  assertOnboardingGameplayUnchanged(gameBeforeOnboarding, fixture.authority->stateCopy());

  // The first player's avatar can progress while their Tag is still on reader A.
  IdentityRequest queryIdentity; queryIdentity.requestId = 51;
  identity = one.identityRequest(queryIdentity);
  assert(identity.selfStage == IdentitySeatStage::AvatarSetup);
  avatar.requestId = 52; avatar.expectedSeatRevision = identity.seats[0].seatRevision;
  identity = one.identityRequest(avatar);
  assert(identity.result == IdentityResultCode::Ok);
  assert(identity.selfStage == IdentitySeatStage::AvatarGenerating || identity.selfStage == IdentitySeatStage::NameSetup);
  assert(fixture.authority->stateVersion() > version); // Authorized identity mutation.
  b.report({});
  fixture.advance(1001); // May also apply legitimate avatar completions.
  const auto secondTagVersion = fixture.authority->stateVersion();
  assertOnboardingGameplayUnchanged(gameBeforeOnboarding, fixture.authority->stateCopy());
  pairing = two.query();
  assert(pairing.activePlayerId == 2 && std::string(pairing.moduleId.data()) == b.module);
  const auto oldCue = a.report({111});
  assert(oldCue.body.find("\"state\":\"success_remove\"") != std::string::npos);
  assert(oldCue.body.find("\"playerId\":1,\"playerName\":") != std::string::npos);
  b.echo(pairing); b.report({}); b.report({111});
  assert(two.query().state == TagPairState::AlreadyBound);
  assert(fixture.authority->playerTagBindings().playerTagUids[0] == 111);
  assert(fixture.authority->playerTagBindings().playerTagUids[1] == 0);
  b.report({}); b.report({222});
  pairing = two.query();
  assert(pairing.state == TagPairState::SuccessRemove && pairing.boundMask == 3);
  assert(fixture.authority->playerTagBindings().playerTagUids[1] == 222);
  assert(fixture.authority->stateVersion() == secondTagVersion);
  fixture.advance(1001);
  const auto replayVersion = fixture.authority->stateVersion();
  assert(two.query().state == TagPairState::Complete);
  // Replaying old reader data cannot change bindings or execute a board arrival.
  a.report({333}); b.report({333});
  assert(fixture.authority->playerTagBindings().playerTagUids[0] == 111);
  assert(fixture.authority->playerTagBindings().playerTagUids[1] == 222);
  assert(fixture.authority->stateVersion() == replayVersion);
  assertOnboardingGameplayUnchanged(gameBeforeOnboarding, fixture.authority->stateCopy());
  assert(fixture.authority->latestEventSequence() == event);
  assert(!fixture.authority->stateCopy().pendingMove.active);
  assert(fixture.authority->identityPhase() == IdentityRoomPhase::AvatarSetup);
  waitUntil([&] {
    fixture.authority->tick(); // Apply completed avatar jobs as the main service loop does.
    IdentitySnapshot ready;
    assert(fixture.authority->makeIdentitySnapshot(1, ready));
    return (ready.avatarFinalMask & 0x0Du) == 0x0Du;
  });
  assertOnboardingGameplayUnchanged(gameBeforeOnboarding, fixture.authority->stateCopy());
  assert(fixture.get(fixture.conditionalSync(fixture.authority->tagPairingRevision())).status == 204);
  const auto sync = fixture.get("/api/sync");
  assert(sync.status == 200 && sync.body.find("\"winnerPlayerId\":0") != std::string::npos);
}

void singleUnassignedReaderAcrossSupportedBoards() {
  for (const std::uint8_t boardSize : std::array<std::uint8_t, 4>{{16, 24, 32, 40}}) {
    const auto name = "single-reader-" + std::to_string(boardSize);
    Fixture fixture(name.c_str(), 24);
    // Reproduce the deployed migration: the old truncated identity reserves the
    // only legacy human seat, while the new full-MAC console remains discoverable.
    Client legacy(fixture, 0xDC, false);
    legacy.deviceId = 0x02D9B4DCu;
    assert(legacy.pair(fixture).accepted == 1);
    Client player(fixture, 0xDC);
    assert(player.name == "GP-DCB4D902D1DC");
    assert(player.pair(fixture).accepted == 0);
    const auto devices = fixture.get("/api/player-devices");
    assert(devices.status == 200 && occurrences(devices.body, "\"eligible\":true") == 1);
    assert(devices.body.find("\"displayName\":\"GP-DCB4D902D1DC\",\"online\":true,\"seatId\":0") !=
        std::string::npos);

    // Rejection of an unsupported board is transactional even with an eligible
    // console, so this cannot pass merely because discovery found nobody.
    const auto previousRoom = fixture.authority->roomId();
    const auto previousGame = fixture.authority->stateCopy();
    assert(previousGame.board->tileCount == 24);
    const auto metadata = readFile(fixture.directory / "authority.meta");
    const auto invalid = fixture.post("/api/new?size=23&autoPlayers=1");
    assert(invalid.status == 409 && invalid.body.find("\"ok\":false") != std::string::npos);
    assert(fixture.authority->roomId() == previousRoom);
    assert(fixture.authority->stateVersion() == previousGame.stateVersion);
    assertOnboardingGameplayUnchanged(previousGame, fixture.authority->stateCopy());
    assert(readFile(fixture.directory / "authority.meta") == metadata);

    Reader reader(fixture, "only-reader", "C0FFEE0000000001");
    constexpr std::uint32_t uid = 0x10203040u;
    reader.report({uid}); // Existing inventory, no ORDER neighbor, no manual anchor.
    auto assertUnassignedOpenReader = [&] {
      const auto topology = fixture.get("/api/tile-debug/assignments");
      assert(topology.status == 200);
      assert(occurrences(topology.body, "\"online\":true") == 1);
      assert(occurrences(topology.body, "\"assigned\":false") == 1);
      assert(topology.body.find("\"assignments\":[]") != std::string::npos);
      assert(topology.body.find("\"closedRing\":true") == std::string::npos);
      assert(topology.body.find("\"orderAnchorTileId\":\"\"") != std::string::npos);
      assert(topology.body.find("\"orderUpstreamModuleId\":\"\"") != std::string::npos);
      assert(topology.body.find("\"orderStatus\":\"unanchored\"") != std::string::npos);
    };
    assertUnassignedOpenReader();
    newAutomaticGame(fixture, 1, boardSize);
    assert(player.pair(fixture).accepted == 1 && player.seat == 1);
    player.expectOnboardingSync(1);
    const auto gameBefore = fixture.authority->stateCopy();
    assert(gameBefore.players[0].controller == gridopoly::core::ControllerKind::RealConsole);
    for (std::size_t i = 1; i < 4; ++i)
      assert(gameBefore.players[i].controller == gridopoly::core::ControllerKind::Bot);

    const auto selected = reader.report({uid});
    assert(selected.body.find("\"assigned\":false") != std::string::npos);
    assert(selected.body.find("\"pairing\":{\"active\":true") != std::string::npos);
    auto pairing = player.query();
    assert(pairing.state == TagPairState::ClearRequired && pairing.requiredHumanMask == 1);
    assert(pairing.boundMask == 0 && pairing.readerMapIndex == 255);
    assert(std::string(pairing.moduleId.data()) == reader.module);
    reader.echo(pairing);
    reader.report({uid});
    assert(player.query().boundMask == 0); // Pre-existing UID cannot bind the new room.
    reader.report({});
    assert(player.query().state == TagPairState::AwaitingTag);
    const auto success = reader.report({uid});
    assert(success.body.find("\"state\":\"success_remove\"") != std::string::npos);
    pairing = player.query();
    assert(pairing.state == TagPairState::SuccessRemove && pairing.boundMask == 1 && pairing.tagUid == uid);
    assert((pairing.flags & (TagPairFlagSelfBound | TagPairFlagCanEditIdentity)) ==
        (TagPairFlagSelfBound | TagPairFlagCanEditIdentity));
    assert(fixture.authority->playerTagBindings().playerTagUids[0] == uid);
    IdentityRequest identityQuery; identityQuery.requestId = 200;
    assert(player.identityRequest(identityQuery).selfStage == IdentitySeatStage::AvatarSetup);
    assertUnassignedOpenReader();
    assert(fixture.authority->stateVersion() == gameBefore.stateVersion);
    assertOnboardingGameplayUnchanged(gameBefore, fixture.authority->stateCopy());
    reader.report({}); // Removal is a fresh scan on the same sole reader/session.
    fixture.advance(1001);
    assert(player.query().state == TagPairState::Complete);
    assertOnboardingGameplayUnchanged(gameBefore, fixture.authority->stateCopy());
  }
}

void ringHeartbeatWire(unsigned size) {
  const auto fixtureName = "ring-wire-" + std::to_string(size);
  Fixture fixture(fixtureName.c_str());
  Client player(fixture, 0x51);
  assert(player.pair(fixture).accepted == 1);
  newAutomaticGame(fixture, 1, static_cast<std::uint8_t>(size));
  assert(player.pair(fixture).accepted == 1 && player.seat == 1);
  const auto room = fixture.authority->roomId();
  const auto version = fixture.authority->stateVersion();
  const auto gameBefore = fixture.authority->stateCopy();
  const unsigned spacing = size / 4;
  auto module = [](unsigned i) {
    std::ostringstream out; out << "ring-" << std::setw(2) << std::setfill('0') << i;
    return out.str();
  };
  auto boot = [](unsigned i) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << (0xABCD0000u + i + 1);
    return out.str();
  };
  std::vector<unsigned> sequences(size);
  std::vector<std::string> bootIds(size);
  for (unsigned i = 0; i < size; ++i) bootIds[i] = boot(i);
  auto report = [&](unsigned i, bool wrongRole = false, bool connected = true) {
    const auto sequence = ++sequences[i];
    std::ostringstream body;
    const auto role = !wrongRole && i % spacing == 0 ? "corner" : "long";
    body << "{\"moduleRole\":\"" << role << "\",\"firmwareVersion\":\"V033-test\","
        "\"tagReaderState\":\"stable\",\"tagRevision\":0,\"tags\":[],\"overflow\":false,"
        "\"tagScanSeq\":" << sequence << ",\"tagAgeMs\":0,\"tagFresh\":true,"
        "\"pairingRoomId\":0,\"pairingSessionId\":0,\"order\":{\"version\":1,\"bootId\":\""
        << bootIds[i] << "\",\"txSeq\":" << sequence << ",\"upstreamBootId\":"
        << (connected ? '"' + bootIds[(i + size - 1) % size] + '"' : "null") << ",\"upstreamSeq\":"
        << (connected ? sequence : 0)
        << ",\"ageMs\":0,\"inputState\":\"idle\",\"valid\":"
        << (connected ? "true" : "false") << "}}";
    return fixture.post("/api/tile-modules/heartbeat?moduleId=" + module(i) +
        "&deviceId=device-" + module(i), body.str());
  };
  // Deliberately register in reverse order; map order comes from OUT direction.
  for (unsigned i = size; i > 0; --i) {
    const auto heartbeat = report(i - 1);
    assert(heartbeat.status == 200);
    assert(heartbeat.body.find(i == 1 ? "\"assigned\":true" : "\"assigned\":false") != std::string::npos);
  }
  const auto assignments = fixture.get("/api/tile-debug/assignments");
  assert(assignments.status == 200);
  assert(occurrences(assignments.body, "\"assigned\":true") == size);
  assert(occurrences(assignments.body, "\"closedRing\":true") == size + 1); // Nodes plus one chain.
  assert(occurrences(assignments.body, "\"moduleRole\":\"corner\"") == 4);
  assert(occurrences(assignments.body, "\"moduleRole\":\"long\"") == size - 4);
  auto assertMapping = [&](unsigned phase, bool manual) {
    for (unsigned i = 0; i < size; ++i) {
      const auto heartbeat = report(i);
      assert(heartbeat.status == 200 && heartbeat.body.find("\"assigned\":true") != std::string::npos);
      const auto index = (i + phase) % size;
      assert(number(heartbeat.body, "mapIndex") == index);
      assert(stringValue(heartbeat.body, "tile_id") == gameBefore.board->tiles[index].id);
      assert(heartbeat.body.find(manual && i == 0 ? "\"source\":\"manual\"" :
          "\"source\":\"order\"") != std::string::npos);
    }
  };
  assertMapping(0, false);
  assert(report(spacing, true).status == 200);
  const auto invalid = fixture.get("/api/tile-debug/assignments");
  assert(occurrences(invalid.body, "\"assigned\":true") == 0);
  assert(invalid.body.find("\"assignments\":[]") != std::string::npos);
  assert(report(spacing).status == 200);
  assert(occurrences(fixture.get("/api/tile-debug/assignments").body, "\"assigned\":true") == size);

  auto anchor = [&](unsigned i, unsigned mapIndex) {
    return fixture.post("/api/tile-debug/assignment?moduleId=" + module(i) +
        "&deviceId=device-" + module(i) + "&tileId=" + gameBefore.board->tiles[mapIndex].id);
  };
  assert(anchor(0, spacing).status == 200);
  assertMapping(spacing, true);
  assert(anchor(0, 2 * spacing).status == 200);
  assertMapping(2 * spacing, true);
  const auto revision = number(fixture.get("/api/tile-debug/assignments").body, "revision");
  assert(anchor(spacing, spacing).status == 409);
  assert(number(fixture.get("/api/tile-debug/assignments").body, "revision") == revision);
  assertMapping(2 * spacing, true);

  assert(report(0, false, false).status == 200);
  const auto broken = fixture.get("/api/tile-debug/assignments");
  assert(occurrences(broken.body, "\"assigned\":true") == 1); // Explicit intent only.
  assert(broken.body.find("\"ring_open\"") != std::string::npos);
  assert(broken.body.find("\"source\":\"order\"") == std::string::npos);
  assert(report(0).status == 200);
  assertMapping(2 * spacing, true);
  bootIds[1] = "FFFF000000000001";
  assert(report(1).status == 200);
  assert(occurrences(fixture.get("/api/tile-debug/assignments").body, "\"assigned\":true") == 1);
  assert(report(2).status == 200);
  assertMapping(2 * spacing, true);
  assert(fixture.authority->roomId() == room && fixture.authority->stateVersion() == version);
  assertOnboardingGameplayUnchanged(gameBefore, fixture.authority->stateCopy());

  // HTTP new-game keeps fresh physical evidence, clears manual intent, and
  // withholds all ring assignments when map size and physical size differ.
  const auto otherSize = static_cast<std::uint8_t>(size == 16 ? 32 : 16);
  newAutomaticGame(fixture, 1, otherSize);
  assert(player.pair(fixture).accepted == 1 && player.seat == 1);
  const auto mismatch = fixture.get("/api/tile-debug/assignments");
  assert(mismatch.body.find("\"assignments\":[]") != std::string::npos);
  assert(mismatch.body.find("\"ring_size_mismatch\"") != std::string::npos);
  assert(occurrences(mismatch.body, "\"closedRing\":true") == size + 1);
  assert(occurrences(mismatch.body, "\"orderAnchorTileId\":\"\"") == size);
  const auto mismatchedHeartbeat = report(1);
  assert(mismatchedHeartbeat.status == 200 &&
      mismatchedHeartbeat.body.find("\"assigned\":false") != std::string::npos);
  newAutomaticGame(fixture, 1, static_cast<std::uint8_t>(size));
  assert(player.pair(fixture).accepted == 1 && player.seat == 1);
  assertMapping(0, false);
  // A reader without any ORDER evidence must still invalidate automatic ring
  // projection immediately; each ring heartbeat must explicitly clear its tile.
  const auto stableRevision = number(fixture.get("/api/tile-debug/assignments").body, "revision");
  assert(fixture.post("/api/tile-modules/heartbeat?moduleId=outside&deviceId=outside-device").status == 200);
  const auto extra = fixture.get("/api/tile-debug/assignments");
  assert(occurrences(extra.body, "\"ring_online_set_mismatch\"") == size);
  assert(occurrences(extra.body, "\"closedRing\":true") == size + 1);
  assert(number(extra.body, "revision") > stableRevision);
  for (unsigned i = 0; i < size; ++i) {
    const auto heartbeat = report(i);
    assert(heartbeat.status == 200 && heartbeat.body.find("\"assigned\":false") != std::string::npos);
    assert(heartbeat.body.find("\"mode\":\"none\"") != std::string::npos);
  }
}

}  // namespace

int main() {
  discoveryAndFrozenSeats();
  tagOnboardingAcrossTransports();
  singleUnassignedReaderAcrossSupportedBoards();
  ringHeartbeatWire(16);
  ringHeartbeatWire(24);
  ringHeartbeatWire(32);
  ringHeartbeatWire(40);
  std::cout << "onboarding HTTP/UDP network tests passed\n";
}
