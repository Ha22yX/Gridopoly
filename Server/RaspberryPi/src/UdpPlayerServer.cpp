#include "UdpPlayerServer.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

#include "../../../Firmware/TestGameServer/src/ReliabilityPolicy.h"

namespace gridopoly::pi {
namespace {

using namespace gridopoly::core;
using namespace gridopoly::protocol;
using namespace gridopoly::server;

constexpr std::uint32_t kRegistryMagic = 0x31525047u;
constexpr auto kPeerTimeout = std::chrono::seconds(15);
constexpr auto kSessionRetention = std::chrono::minutes(2);
constexpr auto kDiscoverInterval = std::chrono::seconds(1);
constexpr auto kSyncSpacing = std::chrono::milliseconds(5);

std::uint32_t randomNonZero() {
  std::random_device random;
  const auto value = (static_cast<std::uint32_t>(random()) << 16) ^
      static_cast<std::uint32_t>(random());
  return value == 0 ? 1 : value;
}

bool sameEndpoint(const sockaddr_in& left, const sockaddr_in& right) {
  return left.sin_family == right.sin_family && left.sin_port == right.sin_port &&
      left.sin_addr.s_addr == right.sin_addr.s_addr;
}

void put32(std::uint8_t* output, std::uint32_t value) {
  output[0] = static_cast<std::uint8_t>(value);
  output[1] = static_cast<std::uint8_t>(value >> 8);
  output[2] = static_cast<std::uint8_t>(value >> 16);
  output[3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace

UdpPlayerServer::UdpPlayerServer(AuthorityService& authority, std::string psk,
                                 std::filesystem::path registryPath,
                                 std::string bindAddress, std::string broadcastAddress,
                                 std::uint16_t port)
    : authority_(authority), psk_(std::move(psk)), registryPath_(std::move(registryPath)),
      bindAddress_(std::move(bindAddress)), broadcastAddress_(std::move(broadcastAddress)),
      port_(port) {
  deriveUdpPairKey(psk_.c_str(), pairKey_);
}

UdpPlayerServer::~UdpPlayerServer() { stop(); }

bool UdpPlayerServer::start() {
  if (running_) return true;
  if (!openSocket()) return false;
  loadRegistry();
  observedRoomId_ = authority_.roomId();
  observedIdentityRevision_ = authority_.identityRevision();
  running_ = true;
  thread_ = std::thread(&UdpPlayerServer::run, this);
  return true;
}

void UdpPlayerServer::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  closeSocket();
  for (auto& session : sessions_) {
    if (session.active && session.connected) authority_.setConsoleConnected(session.seatId, false);
  }
  authority_.setPeerCount(0);
}

UdpPlayerServer::Diagnostics UdpPlayerServer::diagnostics() const {
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  return diagnostics_;
}

bool UdpPlayerServer::openSocket() {
  socket_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (socket_ < 0) return false;
  const int enabled = 1;
  ::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  ::setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
  const int flags = ::fcntl(socket_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK) != 0) {
    closeSocket();
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  if (::inet_pton(AF_INET, bindAddress_.c_str(), &address.sin_addr) != 1 ||
      ::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    closeSocket();
    return false;
  }
  if (port_ == 0) {
    socklen_t addressLength = sizeof(address);
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0) {
      closeSocket();
      return false;
    }
    port_ = ntohs(address.sin_port);
  }
  return true;
}

void UdpPlayerServer::closeSocket() {
  if (socket_ >= 0) {
    ::close(socket_);
    socket_ = -1;
  }
}

void UdpPlayerServer::run() {
  lastDiscover_ = std::chrono::steady_clock::now() - kDiscoverInterval;
  while (running_) {
    pollfd descriptor{socket_, POLLIN, 0};
    const auto result = ::poll(&descriptor, 1, 10);
    if (result > 0 && (descriptor.revents & POLLIN) != 0) {
      for (int count = 0; count < 32; ++count) receiveOne();
    }
    const auto room = authority_.roomId();
    if (room != observedRoomId_) resetForNewRoom(room);
    const auto identityRevision = authority_.identityRevision();
    if (identityRevision != observedIdentityRevision_) {
      observedIdentityRevision_ = identityRevision;
      for (auto& session : sessions_) {
        if (session.active && session.connected) requestSync(session, false);
      }
    }
    maintainSessions();
    serviceSync();
    const auto now = std::chrono::steady_clock::now();
    if (now - lastDiscover_ >= kDiscoverInterval) broadcastDiscover();
  }
}

void UdpPlayerServer::receiveOne() {
  std::array<std::uint8_t, kMaxUdpDatagramSize> bytes{};
  sockaddr_in source{};
  socklen_t sourceLength = sizeof(source);
  const auto received = ::recvfrom(socket_, bytes.data(), bytes.size(), 0,
      reinterpret_cast<sockaddr*>(&source), &sourceLength);
  if (received < 0) return;
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.rxDatagrams;
  }
  if (static_cast<std::size_t>(received) < kUdpEnvelopeHeaderSize) return;
  const auto sessionId = static_cast<std::uint32_t>(bytes[8]) |
      (static_cast<std::uint32_t>(bytes[9]) << 8) |
      (static_cast<std::uint32_t>(bytes[10]) << 16) |
      (static_cast<std::uint32_t>(bytes[11]) << 24);
  if (sessionId == 0) {
    processPairing(bytes.data(), static_cast<std::size_t>(received), source);
  } else if (auto* session = findSession(sessionId)) {
    processSession(*session, bytes.data(), static_cast<std::size_t>(received), source);
  }
}

void UdpPlayerServer::processPairing(const std::uint8_t* bytes, std::size_t length,
                                     const sockaddr_in& source) {
  DecodedUdpDatagram datagram{};
  if (!decodeUdpDatagram(bytes, length, pairKey_, datagram) ||
      (datagram.header.flags & UdpFlagPairingKey) == 0) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.authFailures;
    return;
  }
  DecodedFrame frame{};
  if (!decodeFrame(datagram.frame, datagram.header.frameLength, frame) ||
      frame.header.type != MessageType::PairRequest || frame.header.roomId != authority_.roomId() ||
      frame.header.deviceId == 0 || frame.header.deviceId != datagram.header.senderDeviceId) return;
  PairRequest request{};
  if (!decodePairRequest(frame.payload, frame.header.payloadLength, request) || request.deviceNonce == 0) return;
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.validDatagrams;
    ++diagnostics_.pairRequests;
  }
  auto* session = findSessionByDevice(frame.header.deviceId, request.deviceNonce);
  if (session == nullptr) {
    // A device reboot changes its boot nonce.  Retire the previous session for
    // the same persistent device before allocating the new one so the frozen
    // seat is reused immediately instead of waiting for the 15-second timeout
    // or temporarily counting the same physical console twice.
    for (auto& stale : sessions_) {
      if (!stale.active || stale.deviceId != frame.header.deviceId ||
          stale.roomId != authority_.roomId()) continue;
      const auto seat = stale.seatId;
      stale = Session{};
      session = allocateSession(frame.header.deviceId, request.deviceNonce, seat, source);
      break;
    }
  }
  if (session == nullptr) {
    const auto seat = seatForDevice(frame.header.deviceId);
    if (seat != 0) session = allocateSession(frame.header.deviceId, request.deviceNonce, seat, source);
  }
  if (session == nullptr) {
    Session rejected{};
    rejected.endpoint = source;
    rejected.deviceId = frame.header.deviceId;
    rejected.deviceNonce = request.deviceNonce;
    rejected.sessionId = randomNonZero();
    rejected.roomId = authority_.roomId();
    sendPairAccept(rejected, frame.header.sequence);
    return;
  }
  session->endpoint = source;
  session->lastSeen = std::chrono::steady_clock::now();
  if (!authority_.activateConsoleSeat(session->seatId, request.displayName)) {
    Session rejected{};
    rejected.endpoint = source;
    rejected.deviceId = frame.header.deviceId;
    rejected.deviceNonce = request.deviceNonce;
    rejected.sessionId = randomNonZero();
    rejected.roomId = authority_.roomId();
    sendPairAccept(rejected, frame.header.sequence);
    *session = Session{};
    return;
  }
  authority_.setConsoleConnected(session->seatId, true);
  session->connected = true;
  sendPairAccept(*session, frame.header.sequence);
  requestSync(*session, true);
  updatePeerCount();
}

void UdpPlayerServer::processSession(Session& session, const std::uint8_t* bytes,
                                     std::size_t length, const sockaddr_in& source) {
  DecodedUdpDatagram datagram{};
  if (!decodeUdpDatagram(bytes, length, session.key, datagram) ||
      datagram.header.sessionId != session.sessionId ||
      datagram.header.senderDeviceId != session.deviceId) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.authFailures;
    return;
  }
  if (!session.replay.accept(datagram.header.packetSequence)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.replayDrops;
    return;
  }
  DecodedFrame frame{};
  if (!decodeFrame(datagram.frame, datagram.header.frameLength, frame) ||
      frame.header.deviceId != session.deviceId || frame.header.roomId != session.roomId) return;
  if (!sameEndpoint(session.endpoint, source)) {
    session.endpoint = source;
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.endpointMigrations;
  }
  session.lastSeen = std::chrono::steady_clock::now();
  if (!session.connected) {
    session.connected = true;
    authority_.setConsoleConnected(session.seatId, true);
    updatePeerCount();
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.validDatagrams;
  }
  if (frame.header.type == MessageType::PlayerDetailRequest) {
    processPlayerDetail(session, frame);
    return;
  }
  if (frame.header.type == MessageType::TradeRequest) {
    processTrade(session, frame);
    return;
  }
  if (frame.header.type == MessageType::IdentityRequest) {
    processIdentity(session, frame);
    return;
  }
  const auto disposition = classifyInbound(frame.header.type, frame.header.sequence,
      session.lastRxSequence, session.hasCachedAction, session.cachedActionSequence);
  if (disposition == InboundDisposition::ReplayCachedAction) {
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.duplicateActions;
    }
    sendCachedAction(session);
    requestSync(session, false);
    return;
  }
  if (disposition == InboundDisposition::Resync) {
    requestSync(session, true);
    return;
  }
  session.lastRxSequence = frame.header.sequence;
  if (frame.header.type == MessageType::ActionRequest) processAction(session, frame);
  else if (frame.header.type == MessageType::Heartbeat) processHeartbeat(session, frame);
}

void UdpPlayerServer::processAction(Session& session, const DecodedFrame& frame) {
  ActionRequest request{};
  Result result{ErrorCode::InvalidArgument, "invalid action payload"};
  if (decodeActionRequest(frame.payload, frame.header.payloadLength, request)) {
    result = request.playerId == session.seatId
        ? authority_.execute(request.action, session.seatId, request.assetIndex,
                             request.argument, request.expectedStateVersion)
        : Result{ErrorCode::InvalidPlayer, "action seat mismatch"};
  }
  session.cachedActionResult.fill(0);
  session.cachedActionResult[0] = 1;
  session.cachedActionResult[1] = static_cast<std::uint8_t>(result.code);
  session.cachedActionResult[2] = session.seatId;
  put32(session.cachedActionResult.data() + 4, authority_.stateVersion());
  put32(session.cachedActionResult.data() + 8, frame.header.sequence);
  session.cachedActionSequence = frame.header.sequence;
  session.hasCachedAction = true;
  sendCachedAction(session);
  requestSync(session, !result);
}

void UdpPlayerServer::processPlayerDetail(Session& session, const DecodedFrame& frame) {
  PlayerDetailRequest request{};
  if (!decodePlayerDetailRequest(frame.payload, frame.header.payloadLength, request)) {
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.detailErrors;
    }
    requestSync(session, true);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.detailRequests;
    diagnostics_.lastDetailRequestId = request.requestId;
    diagnostics_.lastDetailTargetId = request.targetPlayerId;
    diagnostics_.lastDetailExpectedVersion = request.expectedStateVersion;
  }
  const auto disposition = classifyPlayerDetailInbound(frame.header.sequence, session.lastRxSequence,
      request.requestId, request.targetPlayerId, request.expectedStateVersion,
      session.hasCachedDetail, session.cachedDetailRequestId, session.cachedDetailTargetId,
      session.cachedDetailExpectedVersion);
  if (disposition == PlayerDetailDisposition::ReplayCached) {
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.detailReplays;
    }
    if (isNewerSequence(frame.header.sequence, session.lastRxSequence)) {
      session.lastRxSequence = frame.header.sequence;
    }
    sendCachedDetail(session, frame.header.sequence);
    return;
  }
  if (disposition == PlayerDetailDisposition::RejectRequestIdCollision) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.detailErrors;
    return;
  }
  if (disposition == PlayerDetailDisposition::Resync) {
    requestSync(session, true);
    return;
  }
  PlayerDetailResponse response{};
  if (!authority_.makePlayerDetail(request.requestId, request.targetPlayerId,
                                   request.expectedStateVersion, response)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.detailErrors;
    return;
  }
  std::size_t written = 0;
  if (!encodePlayerDetailResponse(response, session.cachedDetail.data(),
                                  session.cachedDetail.size(), written)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.detailErrors;
    return;
  }
  session.lastRxSequence = frame.header.sequence;
  session.cachedDetailRequestId = request.requestId;
  session.cachedDetailTargetId = request.targetPlayerId;
  session.cachedDetailExpectedVersion = request.expectedStateVersion;
  session.cachedDetailLength = static_cast<std::uint16_t>(written);
  session.hasCachedDetail = true;
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    diagnostics_.lastDetailResponseBytes = session.cachedDetailLength;
  }
  sendCachedDetail(session, frame.header.sequence);
}

void UdpPlayerServer::processTrade(Session& session, const DecodedFrame& frame) {
  TradeRequest request{};
  if (!decodeTradeRequest(frame.payload, frame.header.payloadLength, request)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeErrors;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeRequests;
    diagnostics_.lastTradeRequestId = request.requestId;
    diagnostics_.lastTradeId = request.tradeId;
    diagnostics_.lastTradeRevision = request.expectedRevision;
    diagnostics_.lastTradeOperation = static_cast<std::uint8_t>(request.operation);
  }
  if (session.hasCachedTrade && request.requestId == session.cachedTradeRequestId) {
    if (frame.header.payloadLength != session.cachedTradeRequestLength ||
        std::memcmp(frame.payload, session.cachedTradeRequest.data(),
                    frame.header.payloadLength) != 0) {
      TradeResponse conflict{};
      conflict.operation = request.operation;
      conflict.result = TradeResultCode::RequestIdConflict;
      conflict.selfPlayerId = session.seatId;
      conflict.requestId = request.requestId;
      conflict.stateVersion = authority_.stateVersion();
      std::uint8_t payload[kMaxTradeResponseSize]{};
      std::size_t written = 0;
      const bool sent = encodeTradeResponse(conflict, payload, sizeof(payload), written) &&
          sendFrame(session, MessageType::TradeResponse, FlagResponse | FlagAckRequired,
                    frame.header.sequence, payload, written);
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.tradeErrors;
      if (sent) ++diagnostics_.tradeResponses;
      diagnostics_.lastTradeResult = static_cast<std::uint8_t>(TradeResultCode::RequestIdConflict);
      return;
    }
    if (isNewerSequence(frame.header.sequence, session.lastRxSequence)) {
      session.lastRxSequence = frame.header.sequence;
    }
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.tradeReplays;
    }
    sendCachedTrade(session, frame.header.sequence);
    return;
  }
  if (!isNewerSequence(frame.header.sequence, session.lastRxSequence)) {
    requestSync(session, true);
    return;
  }
  TradeResponse response{};
  authority_.handleTradeRequest(session.seatId, request, response);
  std::size_t written = 0;
  if (!encodeTradeResponse(response, session.cachedTrade.data(),
                           session.cachedTrade.size(), written)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeErrors;
    return;
  }
  session.lastRxSequence = frame.header.sequence;
  session.cachedTradeRequestId = request.requestId;
  session.cachedTradeRequestLength = frame.header.payloadLength;
  std::memcpy(session.cachedTradeRequest.data(), frame.payload, frame.header.payloadLength);
  session.cachedTradeLength = static_cast<std::uint16_t>(written);
  session.hasCachedTrade = true;
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    diagnostics_.lastTradeResponseBytes = session.cachedTradeLength;
    diagnostics_.lastTradeResult = static_cast<std::uint8_t>(response.result);
    diagnostics_.lastTradeId = response.tradeId;
    diagnostics_.lastTradeRevision = response.revision;
  }
  sendCachedTrade(session, frame.header.sequence);
  if (response.tradeId != 0 && response.counterpartyId != 0) {
    for (auto& participant : sessions_) {
      if (participant.active && participant.connected &&
          participant.seatId == response.counterpartyId) {
        sendTradeNotification(participant, response.tradeId);
      }
    }
  }
  requestSync(session, false);
}

void UdpPlayerServer::processIdentity(Session& session, const DecodedFrame& frame) {
  IdentityRequest request{};
  if (!decodeIdentityRequest(frame.payload, frame.header.payloadLength, request)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.identityErrors;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.identityRequests;
    diagnostics_.lastIdentityRequestId = request.requestId;
    diagnostics_.lastIdentityOperation = static_cast<std::uint8_t>(request.operation);
  }
  IdentitySnapshot response{};
  authority_.handleIdentityRequest(session.seatId, request, response);
  std::uint8_t payload[kIdentitySnapshotSize]{};
  std::size_t written = 0;
  const bool sent = encodeIdentitySnapshot(response, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::IdentitySnapshot,
                FlagResponse | FlagAckRequired, frame.header.sequence, payload, written);
  if (isNewerSequence(frame.header.sequence, session.lastRxSequence)) {
    session.lastRxSequence = frame.header.sequence;
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    if (sent) ++diagnostics_.identityResponses;
    else ++diagnostics_.identityErrors;
    diagnostics_.lastIdentityRevision = response.identityRevision;
    diagnostics_.lastIdentityResponseBytes = static_cast<std::uint16_t>(written);
    diagnostics_.lastIdentityResult = static_cast<std::uint8_t>(response.result);
  }
  if (sent && response.result == IdentityResultCode::Ok &&
      request.operation != IdentityOperation::Query) {
    for (auto& participant : sessions_) {
      if (participant.active && participant.connected) requestSync(participant, false);
    }
  }
}

void UdpPlayerServer::processHeartbeat(Session& session, const DecodedFrame& frame) {
  Heartbeat heartbeat{};
  if (!decodeHeartbeat(frame.payload, frame.header.payloadLength, heartbeat)) return;
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.heartbeats;
  }
  const auto version = authority_.stateVersion();
  if (authority_.identityPhase() != IdentityRoomPhase::Active) {
    // Before the game becomes active, IdentitySnapshot is the only projection
    // required for client readiness.  Gameplay event cursors deliberately do
    // not participate in liveness yet; the Active transition changes the
    // state version and schedules a complete gameplay projection afterward.
    const auto plan = planIdentityHeartbeatResponse(
        heartbeat.appliedStateVersion, version, heartbeat.flags);
    if (plan.sendAck) sendHeartbeatAck(session, frame.header.sequence);
    if (plan.requestSync) requestSync(session, plan.fullResync);
    return;
  }
  const auto latestEvent = authority_.latestEventSequence();
  if (heartbeat.appliedEventSequence <= latestEvent) session.lastEventSequence = heartbeat.appliedEventSequence;
  const auto plan = planHeartbeatResponse(true, heartbeat.appliedStateVersion, version,
      heartbeat.appliedEventSequence, latestEvent, heartbeat.flags);
  if (plan.sendAck) sendHeartbeatAck(session, frame.header.sequence);
  if (plan.requestSync) requestSync(session, plan.fullResync);
}

void UdpPlayerServer::maintainSessions() {
  const auto now = std::chrono::steady_clock::now();
  bool changed = false;
  std::uint64_t connectedPeerSilenceMs = 0;
  for (auto& session : sessions_) {
    if (!session.active) continue;
    if (session.connected) {
      const auto silence = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - session.lastSeen).count();
      connectedPeerSilenceMs = std::max<std::uint64_t>(
          connectedPeerSilenceMs, static_cast<std::uint64_t>(std::max<std::int64_t>(silence, 0)));
    }
    if (session.connected && now - session.lastSeen >= kPeerTimeout) {
      session.connected = false;
      authority_.setConsoleConnected(session.seatId, false);
      changed = true;
    }
    if (!session.connected && now - session.lastSeen >= kSessionRetention) session = Session{};
  }
  {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    diagnostics_.connectedPeerSilenceMs = connectedPeerSilenceMs;
    diagnostics_.maxPeerSilenceMs = std::max(diagnostics_.maxPeerSilenceMs,
                                             connectedPeerSilenceMs);
  }
  if (changed) updatePeerCount();
}

void UdpPlayerServer::serviceSync() {
  const auto now = std::chrono::steady_clock::now();
  for (auto& session : sessions_) {
    if (!session.active || !session.connected || session.syncStage == 0 ||
        now - session.lastSyncSend < kSyncSpacing) continue;
    bool sent = false;
    if (session.syncStage == 1) {
      // Identity is first so a freshly paired or resynchronized console can
      // choose Avatar/Name/Ready/Countdown UI without waiting for the larger
      // gameplay projection sequence.
      sent = sendIdentity(session, session.syncResync);
      if (sent) session.syncStage = 2;
    } else if (session.syncStage == 2) {
      sent = sendPendingCard(session, session.syncResync);
      if (sent) session.syncStage = 3;
    } else if (session.syncStage == 3) {
      sent = sendSnapshot(session, session.syncResync);
      if (sent) session.syncStage = 4;
    } else if (session.syncStage == 4) {
      sent = sendAuthority(session, session.syncResync);
      if (sent) session.syncStage = 5;
    } else if (session.syncStage == 5) {
      sent = sendRoster(session, session.syncResync);
      if (sent) session.syncStage = 6;
    } else if (session.syncStage == 6) {
      sent = sendTradeResync(session);
      if (sent) session.syncStage = 7;
    } else {
      bool complete = false;
      sent = sendEvents(session, session.syncResync, complete);
      if (complete) {
        session.syncStage = 0;
        session.syncResync = false;
      }
    }
    session.lastSyncSend = now;
    if (sent) return;
  }
}

void UdpPlayerServer::broadcastDiscover() {
  lastDiscover_ = std::chrono::steady_clock::now();
  sockaddr_in endpoint{};
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(port_);
  if (::inet_pton(AF_INET, broadcastAddress_.c_str(), &endpoint.sin_addr) != 1) return;
  std::uint8_t payload[16]{};
  payload[0] = 2;  // UDP discovery schema.
  payload[1] = 0;
  payload[2] = kVersion;
  payload[3] = 6;
  put32(payload + 4, authority_.serverDeviceId());
  put32(payload + 8, authority_.roomId());
  put32(payload + 12, authority_.stateVersion());
  Header header{};
  header.type = MessageType::Discover;
  header.flags = FlagBroadcast;
  header.sequence = static_cast<std::uint32_t>(broadcastPacketSequence_);
  header.roomId = authority_.roomId();
  header.deviceId = authority_.serverDeviceId();
  sendPairingFrame(endpoint, 0, header, payload, sizeof(payload), true);
}

void UdpPlayerServer::resetForNewRoom(std::uint32_t roomId) {
  for (auto& session : sessions_) {
    if (session.active && session.connected) authority_.setConsoleConnected(session.seatId, false);
    session = Session{};
  }
  observedRoomId_ = roomId;
  observedIdentityRevision_ = authority_.identityRevision();
  updatePeerCount();
  lastDiscover_ = std::chrono::steady_clock::now() - kDiscoverInterval;
}

UdpPlayerServer::Session* UdpPlayerServer::findSession(std::uint32_t sessionId) {
  for (auto& session : sessions_) if (session.active && session.sessionId == sessionId) return &session;
  return nullptr;
}

UdpPlayerServer::Session* UdpPlayerServer::findSessionByDevice(std::uint32_t deviceId,
                                                               std::uint32_t nonce) {
  for (auto& session : sessions_) {
    if (session.active && session.deviceId == deviceId && session.deviceNonce == nonce &&
        session.roomId == authority_.roomId()) return &session;
  }
  return nullptr;
}

UdpPlayerServer::Session* UdpPlayerServer::allocateSession(std::uint32_t deviceId,
                                                           std::uint32_t nonce,
                                                           std::uint8_t seatId,
                                                           const sockaddr_in& endpoint) {
  for (auto& session : sessions_) {
    if (session.active) continue;
    session.active = true;
    session.connected = true;
    session.sessionId = randomNonZero();
    while (findSession(session.sessionId) != &session) session.sessionId = randomNonZero();
    session.roomId = authority_.roomId();
    session.deviceId = deviceId;
    session.deviceNonce = nonce;
    session.seatId = seatId;
    session.endpoint = endpoint;
    session.lastSeen = std::chrono::steady_clock::now();
    deriveUdpSessionKey(pairKey_, authority_.serverDeviceId(), deviceId, nonce,
                        session.roomId, session.sessionId, session.key);
    return &session;
  }
  return nullptr;
}

std::uint8_t UdpPlayerServer::seatForDevice(std::uint32_t deviceId) {
  const auto state = authority_.stateCopy();
  for (const auto& entry : registry_) {
    if (entry.deviceId == deviceId && entry.seatId != 0 &&
        entry.seatId <= state.playerCount && authority_.isHumanSeat(entry.seatId)) {
      return entry.seatId;
    }
  }
  for (std::uint8_t seat = 1; seat <= state.playerCount; ++seat) {
    if (!authority_.isHumanSeat(seat)) continue;
    bool reserved = false;
    for (const auto& entry : registry_) if (entry.deviceId != 0 && entry.seatId == seat) reserved = true;
    if (reserved) continue;
    for (auto& entry : registry_) {
      if (entry.deviceId != 0) continue;
      entry.deviceId = deviceId;
      entry.seatId = seat;
      saveRegistry();
      return seat;
    }
  }
  return 0;
}

bool UdpPlayerServer::loadRegistry() {
  std::ifstream input(registryPath_, std::ios::binary);
  std::uint32_t magic = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(registry_.data()), sizeof(registry_));
  if (!input || magic != kRegistryMagic) {
    registry_ = {};
    return false;
  }
  return true;
}

bool UdpPlayerServer::saveRegistry() const {
  std::error_code error;
  if (!registryPath_.parent_path().empty()) {
    std::filesystem::create_directories(registryPath_.parent_path(), error);
    if (error) return false;
  }
  auto temporary = registryPath_;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(&kRegistryMagic), sizeof(kRegistryMagic));
  output.write(reinterpret_cast<const char*>(registry_.data()), sizeof(registry_));
  output.flush();
  if (!output) return false;
  output.close();
  std::filesystem::rename(temporary, registryPath_, error);
  if (!error) return true;
  std::filesystem::remove(registryPath_, error);
  error.clear();
  std::filesystem::rename(temporary, registryPath_, error);
  return !error;
}

bool UdpPlayerServer::sendPairAccept(Session& session, std::uint32_t acknowledgement) {
  PairAccept response{};
  response.accepted = session.seatId == 0 ? 0 : 1;
  response.seatId = session.seatId;
  response.serverDeviceId = authority_.serverDeviceId();
  response.stateVersion = authority_.stateVersion();
  response.sessionId = session.sessionId;
  std::uint8_t payload[32]{};
  std::size_t written = 0;
  if (!encodePairAccept(response, payload, sizeof(payload), written)) return false;
  Header header{};
  header.type = MessageType::PairAccept;
  header.flags = FlagResponse | FlagAckRequired;
  header.sequence = session.nextFrameSequence++;
  header.acknowledgement = acknowledgement;
  header.roomId = authority_.roomId();
  header.deviceId = authority_.serverDeviceId();
  const auto sent = sendPairingFrame(session.endpoint, session.sessionId, header,
                                     payload, written, false);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.pairAccepts;
  }
  return sent;
}

bool UdpPlayerServer::sendPairingFrame(const sockaddr_in& endpoint, std::uint32_t sessionId,
                                       Header header, const std::uint8_t* payload,
                                       std::size_t payloadLength, bool broadcast) {
  std::uint8_t frame[kMaxFrameSize]{};
  std::size_t frameLength = 0;
  if (!encodeFrame(header, payload, payloadLength, frame, sizeof(frame), frameLength)) return false;
  UdpEnvelopeHeader envelope{};
  envelope.flags = UdpFlagPairingKey | (broadcast ? UdpFlagBroadcast : UdpFlagNone);
  envelope.sessionId = sessionId;
  envelope.senderDeviceId = authority_.serverDeviceId();
  envelope.packetSequence = broadcastPacketSequence_++;
  std::uint8_t datagram[kMaxUdpDatagramSize]{};
  std::size_t datagramLength = 0;
  if (!encodeUdpDatagram(envelope, pairKey_, frame, frameLength,
                         datagram, sizeof(datagram), datagramLength)) return false;
  const auto sent = ::sendto(socket_, datagram, datagramLength, 0,
      reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  if (sent == static_cast<ssize_t>(datagramLength)) {
    ++diagnostics_.txDatagrams;
    return true;
  }
  ++diagnostics_.txErrors;
  return false;
}

bool UdpPlayerServer::sendFrame(Session& session, MessageType type, std::uint16_t flags,
                                std::uint32_t acknowledgement, const std::uint8_t* payload,
                                std::size_t payloadLength) {
  Header header{};
  header.type = type;
  header.flags = flags;
  header.sequence = session.nextFrameSequence++;
  header.acknowledgement = acknowledgement;
  header.roomId = session.roomId;
  header.deviceId = authority_.serverDeviceId();
  std::uint8_t frame[kMaxFrameSize]{};
  std::size_t frameLength = 0;
  if (!encodeFrame(header, payload, payloadLength, frame, sizeof(frame), frameLength)) return false;
  UdpEnvelopeHeader envelope{};
  envelope.sessionId = session.sessionId;
  envelope.senderDeviceId = authority_.serverDeviceId();
  envelope.packetSequence = session.nextPacketSequence++;
  std::uint8_t datagram[kMaxUdpDatagramSize]{};
  std::size_t datagramLength = 0;
  if (!encodeUdpDatagram(envelope, session.key, frame, frameLength,
                         datagram, sizeof(datagram), datagramLength)) return false;
  const auto sent = ::sendto(socket_, datagram, datagramLength, 0,
      reinterpret_cast<const sockaddr*>(&session.endpoint), sizeof(session.endpoint));
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  if (sent == static_cast<ssize_t>(datagramLength)) {
    ++diagnostics_.txDatagrams;
    return true;
  }
  ++diagnostics_.txErrors;
  return false;
}

bool UdpPlayerServer::sendHeartbeatAck(Session& session, std::uint32_t acknowledgement) {
  std::uint8_t payload[12]{};
  payload[0] = 1;
  put32(payload + 4, authority_.stateVersion());
  put32(payload + 8, authority_.latestEventSequence());
  const auto sent = sendFrame(session, MessageType::Ack, FlagResponse,
                              acknowledgement, payload, sizeof(payload));
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.heartbeatAcks;
  }
  return sent;
}

bool UdpPlayerServer::sendCachedAction(Session& session) {
  return session.hasCachedAction && sendFrame(session, MessageType::ActionResult,
      FlagResponse | FlagAckRequired, session.cachedActionSequence,
      session.cachedActionResult.data(), session.cachedActionResult.size());
}

bool UdpPlayerServer::sendCachedDetail(Session& session, std::uint32_t acknowledgement) {
  const auto sent = session.hasCachedDetail && session.cachedDetailLength != 0 &&
      sendFrame(session, MessageType::PlayerDetailResponse, FlagResponse | FlagAckRequired,
                acknowledgement, session.cachedDetail.data(), session.cachedDetailLength);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.detailResponses;
  }
  return sent;
}

bool UdpPlayerServer::sendCachedTrade(Session& session, std::uint32_t acknowledgement,
                                      bool resync) {
  const auto sent = session.hasCachedTrade && session.cachedTradeLength != 0 &&
      sendFrame(session, MessageType::TradeResponse,
                FlagResponse | FlagAckRequired | (resync ? FlagResync : FlagNone),
                acknowledgement, session.cachedTrade.data(), session.cachedTradeLength);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeResponses;
  }
  return sent;
}

bool UdpPlayerServer::sendTradeResync(Session& session) {
  TradeResponse response{};
  if (!authority_.makeTradeResync(session.seatId, response)) return true;
  std::uint8_t payload[kMaxTradeResponseSize]{};
  std::size_t written = 0;
  if (!encodeTradeResponse(response, payload, sizeof(payload), written)) return false;
  const auto sent = sendFrame(session, MessageType::TradeResponse,
      FlagResponse | FlagAckRequired | FlagResync, session.lastRxSequence, payload, written);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeResponses;
  }
  return sent;
}

bool UdpPlayerServer::sendTradeNotification(Session& session, std::uint32_t tradeId) {
  TradeResponse response{};
  if (!authority_.makeTradeNotification(session.seatId, tradeId, response)) return false;
  std::uint8_t payload[kMaxTradeResponseSize]{};
  std::size_t written = 0;
  if (!encodeTradeResponse(response, payload, sizeof(payload), written)) return false;
  const auto sent = sendFrame(session, MessageType::TradeResponse,
      FlagResponse | FlagAckRequired | FlagResync, session.lastRxSequence, payload, written);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.tradeResponses;
  }
  return sent;
}

void UdpPlayerServer::requestSync(Session& session, bool resync) {
  if (resync) {
    session.lastEventSequence = 0;
    session.syncResync = true;
    session.syncStage = 1;
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.resyncs;
  } else if (session.syncStage == 0) {
    session.syncResync = false;
    session.syncStage = 1;
  }
}

bool UdpPlayerServer::sendPendingCard(Session& session, bool resync) {
  const auto state = authority_.stateCopy();
  const auto& pending = state.pendingCard;
  if (!pending.active || pending.playerId != session.seatId) return true;
  PlayerCardEvent card{};
  card.stage = PlayerCardStage::Drawn;
  card.domainEventType = kDomainEventCardDrawn;
  card.stateVersion = state.stateVersion;
  card.eventSequence = pending.drawEventSequence;
  card.playerId = pending.playerId;
  card.deckId = pending.deckId;
  card.cardIndex = pending.cardIndex;
  card.flags = PlayerCardFlagReplay;
  card.cardInstanceId = pending.cardInstanceId;
  card.cardCatalogId = pending.cardCatalogId;
  card.effectId = pending.effectId;
  card.amount = pending.displayAmount;
  card.targetPlayerId = pending.targetPlayerId;
  card.targetPosition = pending.targetPosition;
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  return encodePlayerCardEvent(card, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::PlayerCardEvent,
                FlagResponse | FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
}

bool UdpPlayerServer::sendSnapshot(Session& session, bool resync) {
  StateSnapshot snapshot{};
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  return authority_.makeSnapshot(session.seatId, snapshot) &&
      encodeStateSnapshot(snapshot, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::StateSnapshot,
                FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
}

bool UdpPlayerServer::sendAuthority(Session& session, bool resync) {
  AuthoritySnapshot snapshot{};
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  return authority_.makeAuthoritySnapshot(snapshot) &&
      encodeAuthoritySnapshot(snapshot, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::AuthoritySnapshot,
                FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
}

bool UdpPlayerServer::sendRoster(Session& session, bool resync) {
  RosterSnapshot snapshot{};
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  return authority_.makeRosterSnapshot(snapshot) &&
      encodeRosterSnapshot(snapshot, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::RosterSnapshot,
                FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
}

bool UdpPlayerServer::sendIdentity(Session& session, bool resync) {
  IdentitySnapshot snapshot{};
  std::uint8_t payload[kIdentitySnapshotSize]{};
  std::size_t written = 0;
  const auto sent = authority_.makeIdentitySnapshot(session.seatId, snapshot, resync) &&
      encodeIdentitySnapshot(snapshot, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::IdentitySnapshot,
                FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
  if (sent) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.identityResponses;
    diagnostics_.lastIdentityRevision = snapshot.identityRevision;
    diagnostics_.lastIdentityResponseBytes = static_cast<std::uint16_t>(written);
    diagnostics_.lastIdentityResult = static_cast<std::uint8_t>(snapshot.result);
  }
  return sent;
}

bool UdpPlayerServer::sendCardEvent(Session& session, const GameEvent& event, bool resync) {
  const auto deckId = cardEventDeckId(event.detail);
  const auto index = cardEventCardIndex(event.detail);
  const bool drawn = event.kind == EventKind::CardDrawn;
  PlayerCardEvent card{};
  card.stage = drawn ? PlayerCardStage::Drawn : PlayerCardStage::EffectApplied;
  card.domainEventType = drawn ? kDomainEventCardDrawn : kDomainEventCardEffectApplied;
  card.stateVersion = authority_.stateVersion();
  card.eventSequence = event.sequence;
  card.playerId = event.actorId;
  card.deckId = deckId;
  card.cardIndex = index;
  card.cardInstanceId = cardEventInstanceId(event.detail);
  card.cardCatalogId = gridopoly::core::cardCatalogId(deckId, index);
  card.effectId = card.cardCatalogId;
  card.amount = event.amount;
  if (!drawn && (index == 1 || index == 5 || (deckId == 1 && index == 7)) && card.amount > 0) {
    card.amount = -card.amount;
  }
  card.targetPlayerId = event.targetId;
  card.targetPosition = cardEventTargetPosition(event.detail);
  card.outcome = drawn ? 0 : static_cast<std::uint8_t>(cardEventOutcome(event.detail));
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  return encodePlayerCardEvent(card, payload, sizeof(payload), written) &&
      sendFrame(session, MessageType::PlayerCardEvent,
                FlagAckRequired | (resync ? FlagResync : FlagNone),
                session.lastRxSequence, payload, written);
}

bool UdpPlayerServer::sendEvents(Session& session, bool resync, bool& complete) {
  complete = false;
  const auto state = authority_.stateCopy();
  const auto oldestIndex = static_cast<std::uint8_t>(
      (state.eventHead + kEventHistory - state.eventCount) % kEventHistory);
  bool historyGap = false;
  if (state.eventCount != 0) {
    const auto oldest = state.events[oldestIndex].sequence;
    if (session.lastEventSequence != 0 && session.lastEventSequence + 1 < oldest) {
      session.lastEventSequence = oldest - 1;
      historyGap = true;
    }
  }
  GameEventBatch batch{};
  batch.stateVersion = state.stateVersion;
  for (std::uint8_t offset = 0; offset < state.eventCount && batch.eventCount < batch.events.size(); ++offset) {
    const auto& source = state.events[(oldestIndex + offset) % kEventHistory];
    if (source.sequence <= session.lastEventSequence) continue;
    if (source.kind == EventKind::CardDrawn && source.actorId != session.seatId) {
      // Card faces are private until applied, but the global event sequence is
      // shared by every console. Send a redacted compact record so observers
      // can advance contiguously without learning the card contents.
      auto& target = batch.events[batch.eventCount++];
      target = {source.sequence, static_cast<std::uint8_t>(source.kind),
                0, 0, gridopoly::core::kNoAsset, 0, 0};
      continue;
    }
    if (source.kind == EventKind::CardDrawn || source.kind == EventKind::CardApplied) {
      if (batch.eventCount != 0) break;
      if (sendCardEvent(session, source, resync || historyGap)) {
        session.lastEventSequence = source.sequence;
      } else {
        return false;
      }
      complete = session.lastEventSequence >= authority_.latestEventSequence();
      return true;
    }
    auto& target = batch.events[batch.eventCount++];
    target = {source.sequence, static_cast<std::uint8_t>(source.kind), source.actorId,
              source.targetId, source.assetIndex, source.amount, source.detail};
  }
  if (batch.eventCount == 0) {
    complete = true;
    return false;
  }
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t written = 0;
  if (!encodeGameEventBatch(batch, payload, sizeof(payload), written) ||
      !sendFrame(session, MessageType::GameEvent,
                 FlagAckRequired | ((resync || historyGap) ? FlagResync : FlagNone),
                 session.lastRxSequence, payload, written)) return false;
  session.lastEventSequence = batch.events[batch.eventCount - 1].sequence;
  complete = session.lastEventSequence >= authority_.latestEventSequence();
  return true;
}

void UdpPlayerServer::updatePeerCount() {
  std::uint8_t count = 0;
  for (const auto& session : sessions_) if (session.active && session.connected) ++count;
  authority_.setPeerCount(count);
  std::lock_guard<std::mutex> lock(diagnosticsMutex_);
  diagnostics_.peers = count;
}

}  // namespace gridopoly::pi
