#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gridopoly/protocol/Protocol.h>
#include <gridopoly/protocol/UdpEnvelope.h>
#include <gridopoly/core/BoardCatalog.h>
#include <gridopoly/core/GameEngine.h>

#include "../../Server/RaspberryPi/src/AuthorityService.h"
#include "../../Server/RaspberryPi/src/FileStateStore.h"
#include "../../Server/RaspberryPi/src/UdpPlayerServer.h"

using namespace gridopoly::protocol;

namespace {

struct Client {
  int socket{-1};
  sockaddr_in server{};
  std::uint32_t deviceId{0x13572468u};
  std::uint32_t nonce{0x24681357u};
  std::uint32_t roomId{};
  std::uint32_t sessionId{};
  std::uint64_t packetSequence{1};
  std::uint32_t frameSequence{1};
  std::array<std::uint8_t, kUdpKeySize> pairKey{};
  std::array<std::uint8_t, kUdpKeySize> sessionKey{};
};

struct Received {
  UdpEnvelopeHeader envelope{};
  Header header{};
  std::array<std::uint8_t, kMaxPayloadSize> payload{};
  std::size_t payloadLength{};
};

struct EventStream {
  std::vector<std::uint32_t> sequences{};
  std::size_t compactBatchCount{};
  std::size_t drawnFrameCount{};
  std::size_t appliedFrameCount{};
  bool sawRedactedDrawn{};
  PlayerCardEvent drawn{};
  PlayerCardEvent applied{};
};

int openClient() {
  const auto descriptor = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  assert(descriptor >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
  return descriptor;
}

std::vector<std::uint8_t> makeDatagram(Client& client, MessageType type,
                                       const std::uint8_t* payload, std::size_t payloadLength,
                                       bool pairing, std::uint32_t forcedFrameSequence = 0) {
  Header header{};
  header.type = type;
  header.flags = FlagAckRequired;
  header.sequence = forcedFrameSequence == 0 ? client.frameSequence++ : forcedFrameSequence;
  header.roomId = client.roomId;
  header.deviceId = client.deviceId;
  std::uint8_t frame[kMaxFrameSize]{};
  std::size_t frameLength = 0;
  assert(encodeFrame(header, payload, payloadLength, frame, sizeof(frame), frameLength));
  UdpEnvelopeHeader envelope{};
  envelope.flags = pairing ? UdpFlagPairingKey : UdpFlagNone;
  envelope.sessionId = pairing ? 0 : client.sessionId;
  envelope.senderDeviceId = client.deviceId;
  envelope.packetSequence = client.packetSequence++;
  std::vector<std::uint8_t> datagram(kMaxUdpDatagramSize);
  std::size_t written = 0;
  assert(encodeUdpDatagram(envelope, pairing ? client.pairKey : client.sessionKey,
                           frame, frameLength, datagram.data(), datagram.size(), written));
  datagram.resize(written);
  return datagram;
}

void sendDatagram(int descriptor, const sockaddr_in& server,
                  const std::vector<std::uint8_t>& datagram) {
  assert(::sendto(descriptor, datagram.data(), datagram.size(), 0,
      reinterpret_cast<const sockaddr*>(&server), sizeof(server)) ==
      static_cast<ssize_t>(datagram.size()));
}

bool receiveOne(Client& client, int descriptor, Received& output, int timeoutMs,
                bool pairingKey = false) {
  pollfd pollDescriptor{descriptor, POLLIN, 0};
  if (::poll(&pollDescriptor, 1, timeoutMs) <= 0) return false;
  std::uint8_t bytes[kMaxUdpDatagramSize]{};
  const auto length = ::recv(descriptor, bytes, sizeof(bytes), 0);
  if (length <= 0) return false;
  DecodedUdpDatagram datagram{};
  if (!decodeUdpDatagram(bytes, static_cast<std::size_t>(length),
                         pairingKey ? client.pairKey : client.sessionKey, datagram)) return false;
  DecodedFrame frame{};
  if (!decodeFrame(datagram.frame, datagram.header.frameLength, frame)) return false;
  output = Received{};
  output.envelope = datagram.header;
  output.header = frame.header;
  output.payloadLength = frame.header.payloadLength;
  std::copy(frame.payload, frame.payload + frame.header.payloadLength, output.payload.begin());
  return true;
}

bool receiveType(Client& client, int descriptor, MessageType type, Received& output,
                 int timeoutMs, bool pairingKey = false) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    Received candidate{};
    if (!receiveOne(client, descriptor, candidate, static_cast<int>(remaining), pairingKey)) return false;
    if (candidate.header.type == type) {
      output = candidate;
      return true;
    }
  }
  return false;
}

bool receiveEventStream(Client& client, EventStream& stream,
                        std::uint32_t firstSequence, std::uint32_t lastSequence,
                        int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline &&
         stream.sequences.size() < lastSequence - firstSequence + 1u) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    Received received{};
    if (!receiveOne(client, client.socket, received, static_cast<int>(remaining))) return false;
    if (received.header.type == MessageType::GameEvent) {
      GameEventBatch batch{};
      if (!decodeGameEventBatch(received.payload.data(), received.payloadLength, batch)) return false;
      ++stream.compactBatchCount;
      for (std::uint8_t index = 0; index < batch.eventCount; ++index) {
        const auto& event = batch.events[index];
        stream.sequences.push_back(event.sequence);
        if (event.kind == static_cast<std::uint8_t>(gridopoly::core::EventKind::CardDrawn)) {
          if (event.actorId != 0 || event.targetId != 0 ||
              event.assetIndex != gridopoly::core::kNoAsset || event.amount != 0 ||
              event.detail != 0) {
            return false;
          }
          stream.sawRedactedDrawn = true;
        }
      }
    } else if (received.header.type == MessageType::PlayerCardEvent) {
      PlayerCardEvent card{};
      if (!decodePlayerCardEvent(received.payload.data(), received.payloadLength, card)) return false;
      stream.sequences.push_back(card.eventSequence);
      if (card.stage == PlayerCardStage::Drawn) {
        ++stream.drawnFrameCount;
        stream.drawn = card;
      } else if (card.stage == PlayerCardStage::EffectApplied) {
        ++stream.appliedFrameCount;
        stream.applied = card;
      }
    }
  }
  if (stream.sequences.size() != lastSequence - firstSequence + 1u) return false;
  for (std::size_t index = 0; index < stream.sequences.size(); ++index) {
    if (stream.sequences[index] != firstSequence + index) return false;
  }
  return true;
}

bool receivesNoPrivateDrawn(Client& client, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    Received received{};
    if (!receiveOne(client, client.socket, received,
                    static_cast<int>(std::min<std::int64_t>(remaining, 10)))) {
      continue;
    }
    if (received.header.type != MessageType::PlayerCardEvent) continue;
    PlayerCardEvent card{};
    if (!decodePlayerCardEvent(received.payload.data(), received.payloadLength, card)) return false;
    if (card.stage == PlayerCardStage::Drawn) return false;
  }
  return true;
}

}  // namespace

int main() {
  const auto temporary = std::filesystem::temp_directory_path() /
      ("gridopoly-udp-integration-" + std::to_string(::getpid()));
  std::filesystem::remove_all(temporary);
  std::filesystem::create_directories(temporary);
  constexpr char psk[] = "gridopoly-host-integration-psk";

  // Restore a full 32-event history so pairing must stream three compact
  // batches around one private CardDrawn and its public CardApplied result.
  // This catches both partial full-sync delivery and private-event sequence
  // gaps before the real clients are introduced.
  {
    gridopoly::core::GameEngine fixture{};
    const auto* board = gridopoly::core::BoardCatalog::findBySize(32);
    assert(board != nullptr);
    assert(fixture.reset(*board, 0x10203040u));
    assert(fixture.addPlayer("Player Console", gridopoly::core::ControllerKind::RealConsole, false));
    assert(fixture.addPlayer("Player Console 2",
                             gridopoly::core::ControllerKind::RealConsole, false));
    assert(fixture.addPlayer("Bot 1", gridopoly::core::ControllerKind::Bot, true));
    assert(fixture.addPlayer("Bot 2", gridopoly::core::ControllerKind::Bot, true));
    assert(fixture.start());
    auto& fixtureState = fixture.mutableStateForRestore();
    constexpr std::uint32_t firstFixtureSequence = 100;
    for (std::size_t index = 0; index < fixtureState.events.size(); ++index) {
      fixtureState.events[index] = {
          firstFixtureSequence + static_cast<std::uint32_t>(index),
          gridopoly::core::EventKind::TurnStarted,
          static_cast<std::uint8_t>((index % fixtureState.playerCount) + 1u),
          0,
          gridopoly::core::kNoAsset,
          0,
          0};
    }
    const auto cardDetail = gridopoly::core::packCardEventDetail(
        0x1234u, 1, 0, gridopoly::core::CardEffectOutcome::Applied, 7);
    fixtureState.events[10] = {110, gridopoly::core::EventKind::CardDrawn, 1, 0,
                               gridopoly::core::kNoAsset, 100, cardDetail};
    fixtureState.events[11] = {111, gridopoly::core::EventKind::CardApplied, 1, 0,
                               gridopoly::core::kNoAsset, 100, cardDetail};
    fixtureState.eventHead = 0;
    fixtureState.eventCount = static_cast<std::uint8_t>(fixtureState.events.size());
    fixtureState.nextEventSequence = 132;
    gridopoly::pi::FileStateStore fixtureStore(temporary / "state.bin");
    assert(fixtureStore.save(fixtureState));
  }

  gridopoly::pi::AuthorityService authority(temporary / "state.bin", temporary / "authority.meta", 0xABCDEF01u);
  assert(authority.initialize());
  gridopoly::pi::UdpPlayerServer server(authority, psk, temporary / "registry.bin",
                                         "127.0.0.1", "127.255.255.255", 0);
  assert(server.start());

  Client client{};
  client.socket = openClient();
  client.server.sin_family = AF_INET;
  client.server.sin_port = htons(server.port());
  client.server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  client.roomId = authority.roomId();
  deriveUdpPairKey(psk, client.pairKey);

  PairRequest pair{};
  pair.deviceNonce = client.nonce;
  pair.capabilities = 1;
  const std::string name = "UDP Console";
  std::copy(name.begin(), name.end(), pair.displayName);
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  assert(encodePairRequest(pair, payload, sizeof(payload), payloadLength));
  const auto pairDatagram = makeDatagram(client, MessageType::PairRequest,
                                         payload, payloadLength, true);
  sendDatagram(client.socket, client.server, pairDatagram);
  Received received{};
  assert(receiveType(client, client.socket, MessageType::PairAccept, received, 1500, true));
  PairAccept accepted{};
  assert(decodePairAccept(received.payload.data(), received.payloadLength, accepted));
  assert(accepted.accepted == 1 && accepted.seatId == 1 && accepted.sessionId != 0);
  client.sessionId = accepted.sessionId;
  deriveUdpSessionKey(client.pairKey, accepted.serverDeviceId, client.deviceId, client.nonce,
                      client.roomId, client.sessionId, client.sessionKey);

  IdentitySnapshot identity{};
  assert(receiveOne(client, client.socket, received, 1500));
  assert(received.header.type == MessageType::IdentitySnapshot);
  assert(decodeIdentitySnapshot(received.payload.data(), received.payloadLength, identity));
  assert(identity.roomPhase == IdentityRoomPhase::Active && identity.selfPlayerId == 1);
  StateSnapshot state{};
  AuthoritySnapshot authoritySnapshot{};
  RosterSnapshot roster{};
  assert(receiveType(client, client.socket, MessageType::StateSnapshot, received, 1500));
  assert(decodeStateSnapshot(received.payload.data(), received.payloadLength, state));
  assert(receiveType(client, client.socket, MessageType::AuthoritySnapshot, received, 1500));
  assert(decodeAuthoritySnapshot(received.payload.data(), received.payloadLength, authoritySnapshot));
  assert(receiveType(client, client.socket, MessageType::RosterSnapshot, received, 1500));
  assert(decodeRosterSnapshot(received.payload.data(), received.payloadLength, roster));
  assert(state.stateVersion == authoritySnapshot.stateVersion);
  assert(state.stateVersion == roster.stateVersion);
  assert(state.seatId == 1 &&
         std::string(roster.displayNames[0].data()) == "Player Console");
  TradeResponse noTradeResync{};
  assert(receiveType(client, client.socket, MessageType::TradeResponse, received, 1500));
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, noTradeResync));
  assert(noTradeResync.requestId == 0);
  assert(noTradeResync.result == TradeResultCode::NoActiveTrade);
  assert((noTradeResync.flags & TradeResponseFlagResync) != 0);

  Heartbeat heartbeat{0, state.stateVersion, 0};
  assert(encodeHeartbeat(heartbeat, payload, sizeof(payload), payloadLength));
  auto heartbeatDatagram = makeDatagram(client, MessageType::Heartbeat,
                                        payload, payloadLength, false);
  sendDatagram(client.socket, client.server, heartbeatDatagram);
  assert(receiveType(client, client.socket, MessageType::Ack, received, 1000));

  // Pair a second authenticated seat, then exercise the complete revisioned
  // trade path without adding trade state to the regular projections.
  Client second{};
  second.deviceId = 0x13572469u;
  second.nonce = 0x24681358u;
  second.socket = openClient();
  second.server = client.server;
  second.roomId = client.roomId;
  deriveUdpPairKey(psk, second.pairKey);
  PairRequest secondPair{};
  secondPair.deviceNonce = second.nonce;
  secondPair.capabilities = 1;
  const std::string secondName = "UDP Console 2";
  std::copy(secondName.begin(), secondName.end(), secondPair.displayName);
  assert(encodePairRequest(secondPair, payload, sizeof(payload), payloadLength));
  auto secondPairDatagram = makeDatagram(second, MessageType::PairRequest,
                                         payload, payloadLength, true);
  sendDatagram(second.socket, second.server, secondPairDatagram);
  assert(receiveType(second, second.socket, MessageType::PairAccept, received, 1500, true));
  PairAccept secondAccepted{};
  assert(decodePairAccept(received.payload.data(), received.payloadLength, secondAccepted));
  assert(secondAccepted.accepted == 1 && secondAccepted.seatId == 2 && secondAccepted.sessionId != 0);
  second.sessionId = secondAccepted.sessionId;
  deriveUdpSessionKey(second.pairKey, secondAccepted.serverDeviceId, second.deviceId, second.nonce,
                      second.roomId, second.sessionId, second.sessionKey);
  assert(receiveOne(second, second.socket, received, 1500));
  assert(received.header.type == MessageType::IdentitySnapshot);
  assert(decodeIdentitySnapshot(received.payload.data(), received.payloadLength, identity));
  assert(identity.roomPhase == IdentityRoomPhase::Active && identity.selfPlayerId == 2);
  assert(receiveType(second, second.socket, MessageType::StateSnapshot, received, 1500));
  assert(receiveType(second, second.socket, MessageType::AuthoritySnapshot, received, 1500));
  assert(receiveType(second, second.socket, MessageType::RosterSnapshot, received, 1500));
  assert(receiveType(second, second.socket, MessageType::TradeResponse, received, 1500));
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, noTradeResync));
  assert(noTradeResync.requestId == 0);
  assert(noTradeResync.result == TradeResultCode::NoActiveTrade);

  // The room has exactly two frozen human seats. A third physical console is
  // rejected instead of replacing either Bot seat.
  Client rejected{};
  rejected.deviceId = 0x13572470u;
  rejected.nonce = 0x24681359u;
  rejected.socket = openClient();
  rejected.server = client.server;
  rejected.roomId = client.roomId;
  deriveUdpPairKey(psk, rejected.pairKey);
  PairRequest rejectedPair{};
  rejectedPair.deviceNonce = rejected.nonce;
  rejectedPair.capabilities = 1;
  assert(encodePairRequest(rejectedPair, payload, sizeof(payload), payloadLength));
  const auto rejectedPairDatagram = makeDatagram(
      rejected, MessageType::PairRequest, payload, payloadLength, true);
  sendDatagram(rejected.socket, rejected.server, rejectedPairDatagram);
  assert(receiveType(rejected, rejected.socket, MessageType::PairAccept,
                     received, 1500, true));
  PairAccept rejectedAccept{};
  assert(decodePairAccept(received.payload.data(), received.payloadLength, rejectedAccept));
  assert(rejectedAccept.accepted == 0 && rejectedAccept.seatId == 0);
  ::close(rejected.socket);

  // A reboot keeps the persistent deviceId but changes deviceNonce. The
  // server must retire the stale session immediately, reuse the frozen human
  // seat, and start the new session with Identity first. Waiting for the peer
  // timeout would make a normal console reboot look like a full room.
  Client rebooted{};
  rebooted.deviceId = client.deviceId;
  rebooted.nonce = client.nonce + 100u;
  rebooted.socket = openClient();
  rebooted.server = client.server;
  rebooted.roomId = client.roomId;
  deriveUdpPairKey(psk, rebooted.pairKey);
  PairRequest rebootPair{};
  rebootPair.deviceNonce = rebooted.nonce;
  rebootPair.capabilities = 1;
  assert(encodePairRequest(rebootPair, payload, sizeof(payload), payloadLength));
  const auto rebootPairDatagram = makeDatagram(
      rebooted, MessageType::PairRequest, payload, payloadLength, true);
  sendDatagram(rebooted.socket, rebooted.server, rebootPairDatagram);
  assert(receiveType(rebooted, rebooted.socket, MessageType::PairAccept,
                     received, 1500, true));
  PairAccept rebootAccept{};
  assert(decodePairAccept(received.payload.data(), received.payloadLength, rebootAccept));
  assert(rebootAccept.accepted == 1 && rebootAccept.seatId == 1 &&
         rebootAccept.sessionId != 0 && rebootAccept.sessionId != client.sessionId);
  rebooted.sessionId = rebootAccept.sessionId;
  deriveUdpSessionKey(rebooted.pairKey, rebootAccept.serverDeviceId,
                      rebooted.deviceId, rebooted.nonce, rebooted.roomId,
                      rebooted.sessionId, rebooted.sessionKey);
  assert(receiveOne(rebooted, rebooted.socket, received, 1500));
  assert(received.header.type == MessageType::IdentitySnapshot);
  assert(decodeIdentitySnapshot(received.payload.data(), received.payloadLength, identity));
  assert(identity.selfPlayerId == 1);
  assert(receiveType(rebooted, rebooted.socket, MessageType::StateSnapshot,
                     received, 1500));
  assert(receiveType(rebooted, rebooted.socket, MessageType::AuthoritySnapshot,
                     received, 1500));
  assert(receiveType(rebooted, rebooted.socket, MessageType::RosterSnapshot,
                     received, 1500));
  assert(receiveType(rebooted, rebooted.socket, MessageType::TradeResponse,
                     received, 1500));
  ::close(client.socket);
  client = rebooted;

  EventStream targetEvents{};
  EventStream observerEvents{};
  assert(receiveEventStream(client, targetEvents, 100, 131, 3000));
  assert(receiveEventStream(second, observerEvents, 100, 131, 3000));
  assert(targetEvents.compactBatchCount >= 3);
  assert(targetEvents.drawnFrameCount == 1);
  assert(targetEvents.appliedFrameCount == 1);
  assert(!targetEvents.sawRedactedDrawn);
  assert(targetEvents.drawn.domainEventType == kDomainEventCardDrawn);
  assert(targetEvents.drawn.eventSequence == 110);
  assert(targetEvents.drawn.playerId == 1);
  assert(targetEvents.drawn.deckId == 1 && targetEvents.drawn.cardIndex == 0);
  assert(targetEvents.drawn.cardInstanceId == 0x1234u);
  assert(targetEvents.drawn.cardCatalogId == 1 && targetEvents.drawn.effectId == 1);
  assert(targetEvents.drawn.amount == 100 && targetEvents.drawn.targetPosition == 7);
  assert(targetEvents.applied.domainEventType == kDomainEventCardEffectApplied);
  assert(targetEvents.applied.eventSequence == 111);
  assert(targetEvents.applied.cardInstanceId == targetEvents.drawn.cardInstanceId);
  assert(targetEvents.applied.outcome ==
         static_cast<std::uint8_t>(gridopoly::core::CardEffectOutcome::Applied));
  assert(observerEvents.compactBatchCount >= 3);
  assert(observerEvents.drawnFrameCount == 0);
  assert(observerEvents.appliedFrameCount == 1);
  assert(observerEvents.sawRedactedDrawn);
  assert(observerEvents.applied.eventSequence == 111);
  assert(observerEvents.applied.cardInstanceId == 0x1234u);
  assert(receivesNoPrivateDrawn(second, 100));

  IdentityRequest identityQuery{};
  identityQuery.operation = IdentityOperation::Query;
  identityQuery.playerId = 1;
  identityQuery.requestId = 8801;
  assert(encodeIdentityRequest(identityQuery, payload, sizeof(payload), payloadLength));
  auto identityQueryDatagram = makeDatagram(client, MessageType::IdentityRequest,
                                            payload, payloadLength, false);
  sendDatagram(client.socket, client.server, identityQueryDatagram);
  assert(receiveType(client, client.socket, MessageType::IdentitySnapshot, received, 1000));
  IdentitySnapshot queriedIdentity{};
  assert(decodeIdentitySnapshot(received.payload.data(), received.payloadLength, queriedIdentity));
  assert(queriedIdentity.result == IdentityResultCode::Ok);
  assert(queriedIdentity.operationEcho == IdentityOperation::Query);
  assert(queriedIdentity.requestId == 8801 && queriedIdentity.selfPlayerId == 1);
  assert((queriedIdentity.flags & IdentitySnapshotFlagReplay) == 0);

  // The identical logical request can use a fresh authenticated frame and is
  // replayed from the authority cache apart from the explicit replay flag.
  identityQueryDatagram = makeDatagram(client, MessageType::IdentityRequest,
                                       payload, payloadLength, false);
  sendDatagram(client.socket, client.server, identityQueryDatagram);
  assert(receiveType(client, client.socket, MessageType::IdentitySnapshot, received, 1000));
  assert(decodeIdentitySnapshot(received.payload.data(), received.payloadLength, queriedIdentity));
  assert(queriedIdentity.requestId == 8801);
  assert((queriedIdentity.flags & IdentitySnapshotFlagReplay) != 0);

  // Cumulative acknowledgement advances only after both clients have consumed
  // the complete history. An exact version/event heartbeat receives a direct
  // Ack and must not schedule another resync.
  const auto resyncsBeforeAppliedAck = server.diagnostics().resyncs;
  heartbeat.appliedStateVersion = authority.stateVersion();
  heartbeat.appliedEventSequence = 131;
  assert(encodeHeartbeat(heartbeat, payload, sizeof(payload), payloadLength));
  heartbeatDatagram = makeDatagram(client, MessageType::Heartbeat,
                                   payload, payloadLength, false);
  sendDatagram(client.socket, client.server, heartbeatDatagram);
  assert(receiveType(client, client.socket, MessageType::Ack, received, 1000));
  auto secondHeartbeatDatagram = makeDatagram(second, MessageType::Heartbeat,
                                              payload, payloadLength, false);
  sendDatagram(second.socket, second.server, secondHeartbeatDatagram);
  assert(receiveType(second, second.socket, MessageType::Ack, received, 1000));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  assert(server.diagnostics().resyncs == resyncsBeforeAppliedAck);

  const auto beforeTrade = authority.stateCopy();
  TradeRequest createTrade{};
  createTrade.operation = TradeOperation::Create;
  createTrade.targetPlayerId = 2;
  createTrade.requestId = 9001;
  createTrade.expectedStateVersion = authority.stateVersion();
  createTrade.selfGivesCash = 100;
  createTrade.counterpartyGivesCash = 25;
  assert(encodeTradeRequest(createTrade, payload, sizeof(payload), payloadLength));
  auto createTradeDatagram = makeDatagram(client, MessageType::TradeRequest,
                                          payload, payloadLength, false);
  sendDatagram(client.socket, client.server, createTradeDatagram);
  assert(receiveType(client, client.socket, MessageType::TradeResponse, received, 1000));
  TradeResponse createdTrade{};
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, createdTrade));
  assert(createdTrade.result == TradeResultCode::Ok);
  assert(createdTrade.status == TradeStatus::Offered);
  assert(createdTrade.tradeId != 0 && createdTrade.revision == 1);
  assert(receiveType(second, second.socket, MessageType::TradeResponse, received, 1000));
  TradeResponse incomingTrade{};
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, incomingTrade));
  assert(incomingTrade.requestId == 0);
  assert((incomingTrade.flags & TradeResponseFlagResync) != 0);
  assert(incomingTrade.tradeId == createdTrade.tradeId);
  assert(incomingTrade.selfPlayerId == 2 && incomingTrade.counterpartyId == 1);

  // New wire sequence with the same requestId and identical bytes replays the
  // cached response and cannot create a second trade.
  auto createTradeRetry = makeDatagram(client, MessageType::TradeRequest,
                                       payload, payloadLength, false);
  sendDatagram(client.socket, client.server, createTradeRetry);
  assert(receiveType(client, client.socket, MessageType::TradeResponse, received, 1000));
  TradeResponse replayedTrade{};
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, replayedTrade));
  assert(replayedTrade.tradeId == createdTrade.tradeId);
  assert(authority.stateVersion() == createdTrade.stateVersion);

  createTrade.selfGivesCash = 101;
  assert(encodeTradeRequest(createTrade, payload, sizeof(payload), payloadLength));
  auto requestIdCollision = makeDatagram(client, MessageType::TradeRequest,
                                         payload, payloadLength, false);
  sendDatagram(client.socket, client.server, requestIdCollision);
  assert(receiveType(client, client.socket, MessageType::TradeResponse, received, 1000));
  TradeResponse collision{};
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, collision));
  assert(collision.result == TradeResultCode::RequestIdConflict);
  assert(authority.stateVersion() == createdTrade.stateVersion);

  TradeRequest confirmTrade{};
  confirmTrade.operation = TradeOperation::Confirm;
  confirmTrade.targetPlayerId = 1;
  confirmTrade.requestId = 9002;
  confirmTrade.expectedStateVersion = authority.stateVersion();
  confirmTrade.tradeId = createdTrade.tradeId;
  confirmTrade.expectedRevision = createdTrade.revision;
  assert(encodeTradeRequest(confirmTrade, payload, sizeof(payload), payloadLength));
  auto confirmTradeDatagram = makeDatagram(second, MessageType::TradeRequest,
                                           payload, payloadLength, false);
  sendDatagram(second.socket, second.server, confirmTradeDatagram);
  assert(receiveType(second, second.socket, MessageType::TradeResponse, received, 1000));
  TradeResponse settledTrade{};
  assert(decodeTradeResponse(received.payload.data(), received.payloadLength, settledTrade));
  assert(settledTrade.result == TradeResultCode::Ok);
  assert(settledTrade.status == TradeStatus::Settled);
  const auto afterTrade = authority.stateCopy();
  assert(afterTrade.players[0].cash == beforeTrade.players[0].cash - 75);
  assert(afterTrade.players[1].cash == beforeTrade.players[1].cash + 75);
  {
    auto tradeDiagnostics = server.diagnostics();
    const auto diagnosticsDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (tradeDiagnostics.tradeResponses < 8 &&
           std::chrono::steady_clock::now() < diagnosticsDeadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      tradeDiagnostics = server.diagnostics();
    }
    assert(tradeDiagnostics.tradeRequests == 4);
    // Two explicit NoActiveTrade full-resync frames plus direct/replayed/
    // notification responses are mandatory; background sync may add more.
    assert(tradeDiagnostics.tradeResponses >= 8);
    assert(tradeDiagnostics.tradeReplays == 1);
    assert(tradeDiagnostics.tradeErrors == 1);
    assert(tradeDiagnostics.lastTradeRequestId == 9002);
    assert(tradeDiagnostics.lastTradeId == createdTrade.tradeId);
    assert(tradeDiagnostics.lastTradeResult == static_cast<std::uint8_t>(TradeResultCode::Ok));
  }

  const auto beforeRoll = authority.stateVersion();
  ActionRequest roll{ActionCode::Roll, 1, 0xFF, 0, beforeRoll};
  assert(encodeActionRequest(roll, payload, sizeof(payload), payloadLength));
  const auto rollSequence = client.frameSequence;
  auto actionDatagram = makeDatagram(client, MessageType::ActionRequest,
                                     payload, payloadLength, false);
  sendDatagram(client.socket, client.server, actionDatagram);
  assert(receiveType(client, client.socket, MessageType::ActionResult, received, 1000));
  assert(received.payload[1] == 0);
  const auto afterRoll = authority.stateVersion();
  assert(afterRoll > beforeRoll);

  // Same action wire sequence in a fresh authenticated UDP packet replays the
  // cached result without executing the roll twice.
  auto retryDatagram = makeDatagram(client, MessageType::ActionRequest,
                                    payload, payloadLength, false, rollSequence);
  sendDatagram(client.socket, client.server, retryDatagram);
  assert(receiveType(client, client.socket, MessageType::ActionResult, received, 1000));
  assert(authority.stateVersion() == afterRoll);

  PlayerDetailRequest query{77, 1, afterRoll};
  assert(encodePlayerDetailRequest(query, payload, sizeof(payload), payloadLength));
  auto queryDatagram = makeDatagram(client, MessageType::PlayerDetailRequest,
                                    payload, payloadLength, false);
  sendDatagram(client.socket, client.server, queryDatagram);
  assert(receiveType(client, client.socket, MessageType::PlayerDetailResponse, received, 1000));
  PlayerDetailResponse detail{};
  assert(decodePlayerDetailResponse(received.payload.data(), received.payloadLength, detail));
  assert(detail.requestId == 77 && detail.targetPlayerId == 1);

  // A retry carrying the same request identity is served from the cached
  // response and never regenerates or mutates authoritative state.
  const auto detailVersion = authority.stateVersion();
  auto queryRetry = makeDatagram(client, MessageType::PlayerDetailRequest,
                                 payload, payloadLength, false);
  sendDatagram(client.socket, client.server, queryRetry);
  assert(receiveType(client, client.socket, MessageType::PlayerDetailResponse, received, 1000));
  assert(decodePlayerDetailResponse(received.payload.data(), received.payloadLength, detail));
  assert(detail.requestId == 77 && detail.targetPlayerId == 1);
  assert(authority.stateVersion() == detailVersion);
  {
    const auto detailDiagnostics = server.diagnostics();
    assert(detailDiagnostics.detailRequests == 2);
    assert(detailDiagnostics.detailResponses == 2);
    assert(detailDiagnostics.detailReplays == 1);
    assert(detailDiagnostics.detailErrors == 0);
    assert(detailDiagnostics.lastDetailRequestId == 77);
    assert(detailDiagnostics.lastDetailTargetId == 1);
    assert(detailDiagnostics.lastDetailExpectedVersion == afterRoll);
    assert(detailDiagnostics.lastDetailResponseBytes >= kPlayerDetailResponseBaseSize);
  }

  // Endpoint migration is accepted only after session HMAC and replay checks.
  const auto migratedSocket = openClient();
  heartbeat.appliedStateVersion = authority.stateVersion();
  heartbeat.appliedEventSequence = authority.latestEventSequence();
  assert(encodeHeartbeat(heartbeat, payload, sizeof(payload), payloadLength));
  auto migratedHeartbeat = makeDatagram(client, MessageType::Heartbeat,
                                        payload, payloadLength, false);
  sendDatagram(migratedSocket, client.server, migratedHeartbeat);
  assert(receiveType(client, migratedSocket, MessageType::Ack, received, 1000));
  assert(server.diagnostics().endpointMigrations == 1);

  // Replaying the exact outer packet is dropped, while a damaged tag fails
  // authentication. Neither changes authoritative state.
  sendDatagram(migratedSocket, client.server, migratedHeartbeat);
  auto damaged = migratedHeartbeat;
  damaged[32] ^= 0x80;
  sendDatagram(migratedSocket, client.server, damaged);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  const auto diagnostics = server.diagnostics();
  assert(diagnostics.replayDrops >= 1);
  assert(diagnostics.authFailures >= 1);
  assert(diagnostics.duplicateActions == 1);
  assert(diagnostics.heartbeats >= 2);

  ::close(migratedSocket);
  ::close(second.socket);
  ::close(client.socket);
  server.stop();
  assert(authority.flush());
  std::filesystem::remove_all(temporary);
  std::cout << "GRIDOPOLY_UDP_SERVER_INTEGRATION_TESTS_PASS\n";
  return 0;
}
