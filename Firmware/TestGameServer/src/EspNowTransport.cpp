#include "EspNowTransport.h"

#include <ESP.h>
#include <WiFi.h>
#include <cstring>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>

#include "ServerApp.h"
#include "ReliabilityPolicy.h"

namespace gridopoly::server {
namespace {

using namespace gridopoly::protocol;

constexpr std::uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void put32(std::uint8_t* output, std::uint32_t value) {
  output[0] = static_cast<std::uint8_t>(value);
  output[1] = static_cast<std::uint8_t>(value >> 8);
  output[2] = static_cast<std::uint8_t>(value >> 16);
  output[3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace

EspNowTransport* EspNowTransport::instance_ = nullptr;

EspNowTransport::EspNowTransport(ServerApp& app) : app_(app) {}

bool EspNowTransport::begin(const char* testPsk) {
  if (testPsk == nullptr || std::strlen(testPsk) < 16) return false;
  const auto pskLength = std::strlen(testPsk);
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(testPsk), pskLength, pskHash_.data(), 0);
  serverDeviceId_ = static_cast<std::uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFu);
  if (esp_now_init() != ESP_OK || esp_now_set_pmk(pskHash_.data()) != ESP_OK) return false;

  esp_now_peer_info_t broadcast{};
  std::memcpy(broadcast.peer_addr, kBroadcastMac, sizeof(kBroadcastMac));
  broadcast.ifidx = WIFI_IF_STA;
  broadcast.channel = 0;
  broadcast.encrypt = false;
  if (!esp_now_is_peer_exist(kBroadcastMac) && esp_now_add_peer(&broadcast) != ESP_OK) return false;

  instance_ = this;
  if (esp_now_register_recv_cb(&EspNowTransport::onReceive) != ESP_OK ||
      esp_now_register_send_cb(&EspNowTransport::onSend) != ESP_OK) return false;
  ready_ = true;
  broadcastDiscover();
  return true;
}

void EspNowTransport::loop() {
  if (!ready_) return;
  serviceTx();
  RxItem item{};
  while (dequeue(item)) process(item);
  const auto now = millis();
  for (auto& peer : peers_) {
    if (!peer.active) continue;
    if (shouldRetryPairAccept(peer.encrypted, peer.promoteEncryptionAtMs,
                              peer.pairAcceptAttempts, peer.lastPairAcceptAtMs, now)) {
      sendPairAccept(peer);
    }
    if (!peer.encrypted && peer.promoteEncryptionAtMs != 0 &&
        static_cast<std::int32_t>(now - peer.promoteEncryptionAtMs) >= 0) {
      if (addEncryptedPeer(peer)) {
        peer.encrypted = true;
        peer.promoteEncryptionAtMs = 0;
        peer.disconnectedAtMs = 0;
        app_.setConsoleConnected(peer.seatId, true);
        requestSync(peer, true);
      }
    } else if (peer.encrypted && now - peer.lastSeenMs >= kPeerLivenessTimeoutMs) {
      Serial.printf("GRIDOPOLY_ESPNOW_PEER timeout=1 seat=%u silence=%lu valid_rx=%lu heartbeat_rx=%lu\n",
                    static_cast<unsigned>(peer.seatId),
                    static_cast<unsigned long>(now - peer.lastSeenMs),
                    static_cast<unsigned long>(validRxFrames_),
                    static_cast<unsigned long>(heartbeatRx_));
      esp_now_del_peer(peer.mac.data());
      app_.setConsoleConnected(peer.seatId, false);
      peer.encrypted = false;
      peer.promoteEncryptionAtMs = 0;
      peer.disconnectedAtMs = now;
      peer.syncStage = 0;
      peer.syncResync = false;
      ++disconnects_;
    } else if (!peer.encrypted && peer.promoteEncryptionAtMs == 0 && peer.disconnectedAtMs != 0 &&
               now - peer.disconnectedAtMs >= kPeerSeatReservationMs) {
      peer = Peer{};
    }
  }
  const auto discoverInterval = peerCount() == 0 ? 2000u : 8000u;
  if (now - lastDiscoverAt_ >= discoverInterval) broadcastDiscover();
  if (now - lastSnapshotScanAt_ >= 25) {
    lastSnapshotScanAt_ = now;
    for (auto& peer : peers_) {
      if (peer.active && peer.encrypted && peer.syncStage == 0 &&
          (peer.lastSnapshotVersion != app_.state().stateVersion ||
           peer.lastAuthorityVersion != app_.state().stateVersion)) {
        requestSync(peer, false);
      }
    }
  }
  serviceSync(now);
  serviceTx();
}

std::uint8_t EspNowTransport::peerCount() const {
  std::uint8_t count = 0;
  for (const auto& peer : peers_) if (peer.active && peer.encrypted) count++;
  return count;
}

EspNowTransport::Diagnostics EspNowTransport::diagnostics() const {
  portENTER_CRITICAL(&rxMux_);
  const auto rxFrames = rxFrames_;
  const auto rxDropped = rxDropped_;
  const auto txDeliveryFailures = txDeliveryFailures_;
  const auto discoverDeliveryFailures = discoverDeliveryFailures_;
  const auto txDeliveryRetries = txDeliveryRetries_;
  const auto heartbeatAckDeliveries = heartbeatAckDeliveries_;
  const auto heartbeatAckDeliveryFailures = heartbeatAckDeliveryFailures_;
  const auto pairAcceptDeliveries = pairAcceptDeliveries_;
  const auto pairAcceptDeliveryFailures = pairAcceptDeliveryFailures_;
  const auto priorityQueueDepth = static_cast<std::uint8_t>(
      (priorityTxWrite_ + priorityTxQueue_.size() - priorityTxRead_) % priorityTxQueue_.size());
  const auto normalQueueDepth = static_cast<std::uint8_t>(
      (normalTxWrite_ + normalTxQueue_.size() - normalTxRead_) % normalTxQueue_.size());
  const auto txInFlight = txInFlight_;
  portEXIT_CRITICAL(&rxMux_);
  Diagnostics output{};
  output.rxFrames = rxFrames;
  output.txFrames = txFrames_;
  output.txAttempts = txAttempts_;
  output.rxDropped = rxDropped;
  output.txQueueFailures = txQueueFailures_;
  output.txNoMemoryFailures = txNoMemoryFailures_;
  output.txOtherImmediateFailures = txOtherImmediateFailures_;
  output.txDeliveryFailures = txDeliveryFailures;
  output.txDeliveryRetries = txDeliveryRetries;
  output.discoverDeliveryFailures = discoverDeliveryFailures;
  output.priorityQueueDepth = priorityQueueDepth;
  output.normalQueueDepth = normalQueueDepth;
  output.txInFlight = txInFlight;
  output.pairRequests = pairRequests_;
  output.reconnects = reconnects_;
  output.disconnects = disconnects_;
  output.duplicateActionReplays = duplicateActionReplays_;
  output.fullResyncRequests = fullResyncRequests_;
  output.validRxFrames = validRxFrames_;
  output.heartbeatRx = heartbeatRx_;
  output.heartbeatAcks = heartbeatAcks_;
  output.heartbeatAckFailures = heartbeatAckFailures_;
  output.heartbeatAckDeliveries = heartbeatAckDeliveries;
  output.heartbeatAckDeliveryFailures = heartbeatAckDeliveryFailures;
  output.pairAccepts = pairAccepts_;
  output.pairAcceptDeliveries = pairAcceptDeliveries;
  output.pairAcceptDeliveryFailures = pairAcceptDeliveryFailures;
  const auto now = millis();
  for (const auto& peer : peers_) {
    if (!peer.active || !peer.encrypted) continue;
    const auto silence = now - peer.lastSeenMs;
    if (silence > output.maxPeerSilenceMs) output.maxPeerSilenceMs = silence;
  }
  return output;
}

void EspNowTransport::notifyRoomChanged() {
  if (!ready_) return;
  for (auto& peer : peers_) {
    if (!peer.active || !peer.encrypted) continue;
    peer.lastSnapshotVersion = 0;
    peer.lastAuthorityVersion = 0;
    peer.lastEventSequence = 0;
    peer.hasCachedPlayerDetailResponse = false;
    peer.cachedPlayerDetailRequestId = 0;
    peer.cachedPlayerDetailLength = 0;
    requestSync(peer, true);
  }
  broadcastDiscover();
}

void EspNowTransport::notifyNetworkRecovered() {
  if (!ready_) return;
  // Do not enqueue a full projection for every STA recovery. Heartbeats carry
  // cumulative versions and request exactly the missing projection if needed.
  // A fresh Discover is still required if the AP came back on another channel.
  broadcastDiscover();
}

void EspNowTransport::onReceive(const esp_now_recv_info_t* info, const std::uint8_t* data, int length) {
  if (instance_ != nullptr && info != nullptr && info->src_addr != nullptr) {
    instance_->enqueue(info->src_addr, data, length);
  }
}

void EspNowTransport::onSend(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  (void)info;
  if (instance_ != nullptr) instance_->completeTx(status == ESP_NOW_SEND_SUCCESS);
}

void EspNowTransport::enqueue(const std::uint8_t* mac, const std::uint8_t* data, int length) {
  if (mac == nullptr || data == nullptr || length <= 0 || length > static_cast<int>(kMaxFrameSize)) return;
  portENTER_CRITICAL(&rxMux_);
  const auto next = static_cast<std::uint8_t>((rxWrite_ + 1) % rxQueue_.size());
  if (next != rxRead_) {
    auto& item = rxQueue_[rxWrite_];
    std::memcpy(item.mac.data(), mac, item.mac.size());
    item.length = static_cast<std::uint16_t>(length);
    std::memcpy(item.bytes.data(), data, length);
    rxWrite_ = next;
    rxFrames_ = rxFrames_ + 1;
  } else {
    rxDropped_ = rxDropped_ + 1;
  }
  portEXIT_CRITICAL(&rxMux_);
}

bool EspNowTransport::enqueueTx(const std::uint8_t* mac, MessageType type,
                                const std::uint8_t* data, std::size_t length) {
  if (mac == nullptr || data == nullptr || length == 0 || length > kMaxFrameSize) return false;
  const bool priority = isPriorityResponse(type);
  bool queued = false;
  portENTER_CRITICAL(&rxMux_);
  if (priority) {
    const auto next = static_cast<std::uint8_t>((priorityTxWrite_ + 1) % priorityTxQueue_.size());
    if (next != priorityTxRead_) {
      auto& item = priorityTxQueue_[priorityTxWrite_];
      std::memcpy(item.mac.data(), mac, item.mac.size());
      item.length = static_cast<std::uint16_t>(length);
      std::memcpy(item.bytes.data(), data, length);
      item.type = type;
      item.attempts = 0;
      item.attemptLimit = transmissionAttemptLimit(type);
      priorityTxWrite_ = next;
      queued = true;
    }
  } else {
    const auto next = static_cast<std::uint8_t>((normalTxWrite_ + 1) % normalTxQueue_.size());
    if (next != normalTxRead_) {
      auto& item = normalTxQueue_[normalTxWrite_];
      std::memcpy(item.mac.data(), mac, item.mac.size());
      item.length = static_cast<std::uint16_t>(length);
      std::memcpy(item.bytes.data(), data, length);
      item.type = type;
      item.attempts = 0;
      item.attemptLimit = transmissionAttemptLimit(type);
      normalTxWrite_ = next;
      queued = true;
    }
  }
  if (queued) ++txFrames_;
  else ++txQueueFailures_;
  portEXIT_CRITICAL(&rxMux_);
  return queued;
}

void EspNowTransport::serviceTx() {
  TxItem outgoing{};
  bool available = false;
  portENTER_CRITICAL(&rxMux_);
  if (!txInFlight_) {
    TxItem* item = nullptr;
    if (priorityTxRead_ != priorityTxWrite_) {
      txActiveLane_ = TxLane::Priority;
      item = &priorityTxQueue_[priorityTxRead_];
    } else if (normalTxRead_ != normalTxWrite_) {
      txActiveLane_ = TxLane::Normal;
      item = &normalTxQueue_[normalTxRead_];
    }
    if (item != nullptr) {
      ++item->attempts;
      outgoing = *item;
      txInFlight_ = true;
      ++txAttempts_;
      available = true;
    }
  }
  portEXIT_CRITICAL(&rxMux_);
  if (!available) return;

  const auto sendResult = esp_now_send(outgoing.mac.data(), outgoing.bytes.data(), outgoing.length);
  if (sendResult == ESP_OK) return;

  portENTER_CRITICAL(&rxMux_);
  ++txQueueFailures_;
  if (sendResult == ESP_ERR_ESPNOW_NO_MEM || sendResult == ESP_ERR_NO_MEM) {
    ++txNoMemoryFailures_;
  } else {
    ++txOtherImmediateFailures_;
  }
  TxItem* item = nullptr;
  if (txActiveLane_ == TxLane::Priority) item = &priorityTxQueue_[priorityTxRead_];
  else if (txActiveLane_ == TxLane::Normal) item = &normalTxQueue_[normalTxRead_];
  if (item != nullptr && item->attempts >= item->attemptLimit) {
    if (item->type == MessageType::Ack) {
      heartbeatAckDeliveryFailures_ = heartbeatAckDeliveryFailures_ + 1;
    } else if (item->type == MessageType::PairAccept) {
      pairAcceptDeliveryFailures_ = pairAcceptDeliveryFailures_ + 1;
    }
    if (txActiveLane_ == TxLane::Priority) {
      priorityTxRead_ = static_cast<std::uint8_t>((priorityTxRead_ + 1) % priorityTxQueue_.size());
    } else {
      normalTxRead_ = static_cast<std::uint8_t>((normalTxRead_ + 1) % normalTxQueue_.size());
    }
  } else {
    ++txDeliveryRetries_;
  }
  txActiveLane_ = TxLane::None;
  txInFlight_ = false;
  portEXIT_CRITICAL(&rxMux_);
}

void EspNowTransport::completeTx(bool delivered) {
  portENTER_CRITICAL(&rxMux_);
  if (!txInFlight_) {
    portEXIT_CRITICAL(&rxMux_);
    return;
  }
  TxItem* item = nullptr;
  if (txActiveLane_ == TxLane::Priority) item = &priorityTxQueue_[priorityTxRead_];
  else if (txActiveLane_ == TxLane::Normal) item = &normalTxQueue_[normalTxRead_];
  const bool exhausted = item == nullptr || item->attempts >= item->attemptLimit;
  if (!delivered && item != nullptr) {
    if (item->type == MessageType::Discover) {
      discoverDeliveryFailures_ = discoverDeliveryFailures_ + 1;
    } else {
      txDeliveryFailures_ = txDeliveryFailures_ + 1;
    }
  }
  if (delivered || exhausted) {
    if (item != nullptr && item->type == MessageType::Ack) {
      if (delivered) heartbeatAckDeliveries_ = heartbeatAckDeliveries_ + 1;
      else heartbeatAckDeliveryFailures_ = heartbeatAckDeliveryFailures_ + 1;
    } else if (item != nullptr && item->type == MessageType::PairAccept) {
      if (delivered) pairAcceptDeliveries_ = pairAcceptDeliveries_ + 1;
      else pairAcceptDeliveryFailures_ = pairAcceptDeliveryFailures_ + 1;
    }
    if (txActiveLane_ == TxLane::Priority) {
      priorityTxRead_ = static_cast<std::uint8_t>((priorityTxRead_ + 1) % priorityTxQueue_.size());
    } else if (txActiveLane_ == TxLane::Normal) {
      normalTxRead_ = static_cast<std::uint8_t>((normalTxRead_ + 1) % normalTxQueue_.size());
    }
  } else {
    ++txDeliveryRetries_;
  }
  txActiveLane_ = TxLane::None;
  txInFlight_ = false;
  portEXIT_CRITICAL(&rxMux_);
}

bool EspNowTransport::dequeue(RxItem& item) {
  bool available = false;
  portENTER_CRITICAL(&rxMux_);
  if (rxRead_ != rxWrite_) {
    item = rxQueue_[rxRead_];
    rxRead_ = static_cast<std::uint8_t>((rxRead_ + 1) % rxQueue_.size());
    available = true;
  }
  portEXIT_CRITICAL(&rxMux_);
  return available;
}

void EspNowTransport::process(const RxItem& item) {
  DecodedFrame frame{};
  if (!decodeFrame(item.bytes.data(), item.length, frame)) return;
  if (frame.header.type == MessageType::PairRequest) {
    processPair(item.mac.data(), frame);
    return;
  }
  auto* peer = findPeer(item.mac.data());
  if (peer == nullptr || !peer->encrypted || peer->deviceId != frame.header.deviceId ||
      frame.header.roomId != app_.roomId()) return;
  peer->lastSeenMs = millis();
  ++validRxFrames_;
  if (frame.header.type == MessageType::Heartbeat) ++heartbeatRx_;
  if (frame.header.type == MessageType::PlayerDetailRequest) {
    processPlayerDetail(*peer, frame);
    return;
  }
  const auto disposition = classifyInbound(frame.header.type, frame.header.sequence, peer->lastRxSequence,
                                           peer->hasCachedActionResult, peer->cachedActionSequence);
  if (disposition == InboundDisposition::ReplayCachedAction) {
    ++duplicateActionReplays_;
    sendCachedActionResult(*peer);
    requestSync(*peer, false);
    return;
  }
  if (disposition == InboundDisposition::Resync) {
    requestSync(*peer, true);
    return;
  }
  peer->lastRxSequence = frame.header.sequence;
  if (frame.header.type == MessageType::ActionRequest) {
    processAction(*peer, frame);
  } else if (frame.header.type == MessageType::Heartbeat) {
    Heartbeat heartbeat{};
    HeartbeatResponsePlan response{};
    if (frame.header.payloadLength == 0) {
      // Legacy clients still receive a transport ACK, followed by an incremental projection sync.
      response = planHeartbeatResponse(false, 0, app_.state().stateVersion, 0, 0, 0);
    } else if (!decodeHeartbeat(frame.payload, frame.header.payloadLength, heartbeat)) {
      return;
    } else {
      const auto latestEvent = app_.state().nextEventSequence == 0 ? 0 : app_.state().nextEventSequence - 1;
      if (heartbeat.appliedEventSequence <= latestEvent) {
        peer->lastEventSequence = heartbeat.appliedEventSequence;
      }
      response = planHeartbeatResponse(true, heartbeat.appliedStateVersion, app_.state().stateVersion,
                                       heartbeat.appliedEventSequence, latestEvent, heartbeat.flags);
    }
    if (response.sendAck) sendHeartbeatAck(*peer, frame.header.sequence);
    if (response.requestSync) requestSync(*peer, response.fullResync);
  }
}

void EspNowTransport::processPair(const std::uint8_t* mac, const DecodedFrame& frame) {
  PairRequest request{};
  if (!decodePairRequest(frame.payload, frame.header.payloadLength, request)) return;
  ++pairRequests_;
  auto* peer = findPeer(mac);
  const bool reconnecting = peer != nullptr;
  if (peer == nullptr) peer = allocatePeer(mac, frame.header.deviceId, request.deviceNonce);
  if (peer != nullptr) {
    if (reconnecting) ++reconnects_;
    if (peer->encrypted) app_.setConsoleConnected(peer->seatId, false);
    peer->deviceId = frame.header.deviceId;
    peer->nonce = request.deviceNonce;
    peer->lastSeenMs = millis();
    peer->disconnectedAtMs = 0;
    peer->lastSnapshotVersion = 0;
    peer->lastAuthorityVersion = 0;
    peer->lastEventSequence = 0;
    peer->lastRxSequence = 0;
    peer->nextSequence = 1;
    peer->hasCachedActionResult = false;
    peer->cachedActionSequence = 0;
    peer->hasCachedPlayerDetailResponse = false;
    peer->cachedPlayerDetailRequestId = 0;
    peer->cachedPlayerDetailExpectedVersion = 0;
    peer->cachedPlayerDetailTargetId = 0;
    peer->cachedPlayerDetailLength = 0;
    peer->syncStage = 0;
    peer->syncResync = false;
    peer->encrypted = false;
    peer->pairRequestSequence = frame.header.sequence;
    peer->pairAcceptAttempts = 0;
    peer->lastPairAcceptAtMs = 0;
    if (!addPlainPeer(*peer)) {
      peer->promoteEncryptionAtMs = 0;
      peer->disconnectedAtMs = millis();
      return;
    }
    peer->promoteEncryptionAtMs = millis() + kPairEncryptionPromotionDelayMs;
    app_.activateConsoleSeat(peer->seatId, request.displayName);
  }

  if (peer != nullptr) {
    if (!sendPairAccept(*peer)) {
      esp_now_del_peer(peer->mac.data());
      app_.setConsoleConnected(peer->seatId, false);
      peer->encrypted = false;
      peer->promoteEncryptionAtMs = 0;
      peer->disconnectedAtMs = millis();
    }
    return;
  }

  PairAccept response{};
  response.accepted = 0;
  response.seatId = 0;
  response.wifiChannel = static_cast<std::uint8_t>(WiFi.channel());
  response.serverDeviceId = serverDeviceId_;
  response.stateVersion = app_.state().stateVersion;
  std::uint8_t payload[32]{};
  std::size_t length = 0;
  if (!encodePairAccept(response, payload, sizeof(payload), length)) return;
  Header header{};
  header.type = MessageType::PairAccept;
  header.flags = FlagResponse | FlagAckRequired;
  header.sequence = 1;
  header.acknowledgement = frame.header.sequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  sendFrame(mac, header, payload, length);
}

bool EspNowTransport::sendPairAccept(Peer& peer) {
  PairAccept response{};
  response.accepted = 1;
  response.seatId = peer.seatId;
  response.wifiChannel = static_cast<std::uint8_t>(WiFi.channel());
  response.serverDeviceId = serverDeviceId_;
  response.stateVersion = app_.state().stateVersion;
  std::uint8_t payload[32]{};
  std::size_t length = 0;
  if (!encodePairAccept(response, payload, sizeof(payload), length)) return false;
  Header header{};
  header.type = MessageType::PairAccept;
  header.flags = FlagResponse | FlagAckRequired;
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.pairRequestSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, length)) return false;
  peer.nextSequence++;
  peer.lastPairAcceptAtMs = millis();
  peer.pairAcceptAttempts++;
  ++pairAccepts_;
  return true;
}

void EspNowTransport::processAction(Peer& peer, const DecodedFrame& frame) {
  ActionRequest request{};
  gridopoly::core::Result result{gridopoly::core::ErrorCode::InvalidArgument, "invalid action payload"};
  if (decodeActionRequest(frame.payload, frame.header.payloadLength, request)) {
    if (request.playerId != peer.seatId) {
      result = {gridopoly::core::ErrorCode::InvalidPlayer, "action seat mismatch"};
    } else {
      result = app_.execute(request.action, peer.seatId, request.assetIndex, request.argument,
                            request.expectedStateVersion);
    }
  }
  std::uint8_t payload[12]{};
  payload[0] = 1;
  payload[1] = static_cast<std::uint8_t>(result.code);
  payload[2] = peer.seatId;
  payload[3] = 0;
  put32(payload + 4, app_.state().stateVersion);
  put32(payload + 8, frame.header.sequence);
  std::memcpy(peer.cachedActionResult.data(), payload, sizeof(payload));
  peer.cachedActionSequence = frame.header.sequence;
  peer.hasCachedActionResult = true;
  sendCachedActionResult(peer);
  requestSync(peer, !result);
}

bool EspNowTransport::sendCachedActionResult(Peer& peer) {
  if (!peer.encrypted || !peer.hasCachedActionResult) return false;
  Header header{};
  header.type = MessageType::ActionResult;
  header.flags = FlagResponse | FlagAckRequired;
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.cachedActionSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, peer.cachedActionResult.data(), peer.cachedActionResult.size())) {
    return false;
  }
  peer.nextSequence++;
  return true;
}

void EspNowTransport::processPlayerDetail(Peer& peer, const DecodedFrame& frame) {
  PlayerDetailRequest request{};
  if (!decodePlayerDetailRequest(frame.payload, frame.header.payloadLength, request)) {
    requestSync(peer, true);
    return;
  }
  const auto disposition = classifyPlayerDetailInbound(
      frame.header.sequence, peer.lastRxSequence, request.requestId, request.targetPlayerId,
      request.expectedStateVersion, peer.hasCachedPlayerDetailResponse,
      peer.cachedPlayerDetailRequestId, peer.cachedPlayerDetailTargetId,
      peer.cachedPlayerDetailExpectedVersion);
  if (disposition == PlayerDetailDisposition::ReplayCached) {
    if (isNewerSequence(frame.header.sequence, peer.lastRxSequence)) {
      peer.lastRxSequence = frame.header.sequence;
    }
    sendCachedPlayerDetail(peer, frame.header.sequence);
    return;
  }
  if (disposition == PlayerDetailDisposition::RejectRequestIdCollision) {
    return;
  }
  if (disposition == PlayerDetailDisposition::Resync) {
    requestSync(peer, true);
    return;
  }

  // The encrypted peer record authenticates the source MAC/device and binds
  // it to a live seat. Any authenticated player may inspect the public details
  // of another player in the same room; invalid targets are never disclosed.
  const auto& state = app_.state();
  if (peer.seatId == 0 || peer.seatId > state.playerCount ||
      state.players[peer.seatId - 1].id != peer.seatId) {
    return;
  }
  PlayerDetailResponse response{};
  if (!app_.makePlayerDetail(request.requestId, request.targetPlayerId,
                             request.expectedStateVersion, response)) {
    return;
  }
  std::size_t payloadLength = 0;
  if (!encodePlayerDetailResponse(response, peer.cachedPlayerDetailResponse.data(),
                                  peer.cachedPlayerDetailResponse.size(), payloadLength)) {
    return;
  }
  peer.lastRxSequence = frame.header.sequence;
  peer.cachedPlayerDetailRequestId = request.requestId;
  peer.cachedPlayerDetailExpectedVersion = request.expectedStateVersion;
  peer.cachedPlayerDetailTargetId = request.targetPlayerId;
  peer.cachedPlayerDetailLength = static_cast<std::uint16_t>(payloadLength);
  peer.hasCachedPlayerDetailResponse = true;
  sendCachedPlayerDetail(peer, frame.header.sequence);
}

bool EspNowTransport::sendCachedPlayerDetail(Peer& peer, std::uint32_t acknowledgement) {
  if (!peer.encrypted || !peer.hasCachedPlayerDetailResponse ||
      peer.cachedPlayerDetailLength == 0 ||
      peer.cachedPlayerDetailLength > peer.cachedPlayerDetailResponse.size()) {
    return false;
  }
  Header header{};
  header.type = MessageType::PlayerDetailResponse;
  header.flags = FlagResponse | FlagAckRequired;
  header.sequence = peer.nextSequence;
  header.acknowledgement = acknowledgement;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, peer.cachedPlayerDetailResponse.data(),
                 peer.cachedPlayerDetailLength)) {
    return false;
  }
  ++peer.nextSequence;
  return true;
}

bool EspNowTransport::sendHeartbeatAck(Peer& peer, std::uint32_t acknowledgement) {
  std::uint8_t payload[12]{};
  payload[0] = 1;
  put32(payload + 4, app_.state().stateVersion);
  const auto latestEvent = app_.state().nextEventSequence == 0 ? 0 : app_.state().nextEventSequence - 1;
  put32(payload + 8, latestEvent);
  Header header{};
  header.type = MessageType::Ack;
  header.flags = FlagResponse;
  header.sequence = peer.nextSequence;
  header.acknowledgement = acknowledgement;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, sizeof(payload))) {
    ++heartbeatAckFailures_;
    return false;
  }
  ++heartbeatAcks_;
  peer.nextSequence++;
  return true;
}

EspNowTransport::Peer* EspNowTransport::findPeer(const std::uint8_t* mac) {
  for (auto& peer : peers_) {
    if (peer.active && std::memcmp(peer.mac.data(), mac, peer.mac.size()) == 0) return &peer;
  }
  return nullptr;
}

EspNowTransport::Peer* EspNowTransport::allocatePeer(const std::uint8_t* mac, std::uint32_t deviceId,
                                                     std::uint32_t nonce) {
  const auto seat = nextFreeSeat();
  if (seat == 0) return nullptr;
  for (auto& peer : peers_) {
    if (peer.active) continue;
    peer = Peer{};
    peer.active = true;
    std::memcpy(peer.mac.data(), mac, peer.mac.size());
    peer.deviceId = deviceId;
    peer.nonce = nonce;
    peer.seatId = seat;
    peer.lastSeenMs = millis();
    return &peer;
  }
  return nullptr;
}

bool EspNowTransport::addPlainPeer(Peer& peer) {
  if (esp_now_is_peer_exist(peer.mac.data())) esp_now_del_peer(peer.mac.data());
  esp_now_peer_info_t info{};
  std::memcpy(info.peer_addr, peer.mac.data(), peer.mac.size());
  info.ifidx = WIFI_IF_STA;
  info.channel = 0;
  info.encrypt = false;
  return esp_now_add_peer(&info) == ESP_OK;
}

std::uint8_t EspNowTransport::nextFreeSeat() const {
  for (std::uint8_t seat = 1; seat <= app_.state().playerCount; ++seat) {
    bool used = false;
    for (const auto& peer : peers_) if (peer.active && peer.seatId == seat) used = true;
    if (!used) return seat;
  }
  return 0;
}

bool EspNowTransport::addEncryptedPeer(Peer& peer) {
  if (esp_now_is_peer_exist(peer.mac.data())) esp_now_del_peer(peer.mac.data());
  esp_now_peer_info_t info{};
  std::memcpy(info.peer_addr, peer.mac.data(), peer.mac.size());
  info.ifidx = WIFI_IF_STA;
  info.channel = 0;
  info.encrypt = true;
  deriveLmk(peer, info.lmk);
  return esp_now_add_peer(&info) == ESP_OK;
}

void EspNowTransport::deriveLmk(const Peer& peer, std::uint8_t output[16]) const {
  std::uint8_t material[44]{};
  std::memcpy(material, pskHash_.data(), 32);
  put32(material + 32, serverDeviceId_);
  put32(material + 36, peer.deviceId);
  put32(material + 40, peer.nonce);
  std::uint8_t digest[32]{};
  mbedtls_sha256(material, sizeof(material), digest, 0);
  std::memcpy(output, digest, 16);
}

void EspNowTransport::broadcastDiscover() {
  lastDiscoverAt_ = millis();
  std::uint8_t payload[16]{};
  payload[0] = 1;
  payload[1] = static_cast<std::uint8_t>(WiFi.channel());
  payload[2] = kVersion;
  payload[3] = static_cast<std::uint8_t>(peers_.size());
  put32(payload + 4, serverDeviceId_);
  put32(payload + 8, app_.roomId());
  put32(payload + 12, app_.state().stateVersion);
  Header header{};
  header.type = MessageType::Discover;
  header.flags = FlagBroadcast;
  header.sequence = lastDiscoverAt_;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  sendFrame(kBroadcastMac, header, payload, sizeof(payload));
}

void EspNowTransport::requestSync(Peer& peer, bool resync) {
  if (!peer.active || !peer.encrypted) return;
  if (resync) {
    ++fullResyncRequests_;
    peer.lastSnapshotVersion = 0;
    peer.lastAuthorityVersion = 0;
    peer.lastEventSequence = 0;
    peer.syncResync = true;
    peer.syncStage = 1;
    return;
  }
  if (peer.syncStage == 0) {
    peer.syncResync = false;
    peer.syncStage = 1;
  }
}

void EspNowTransport::serviceSync(std::uint32_t now) {
  constexpr std::uint32_t kSendSpacingMs = 8;
  if (now - lastSyncSendAt_ < kSendSpacingMs) return;
  for (std::size_t checked = 0; checked < peers_.size(); ++checked) {
    const auto index = static_cast<std::uint8_t>((nextSyncPeer_ + checked) % peers_.size());
    auto& peer = peers_[index];
    if (!peer.active || !peer.encrypted || peer.syncStage == 0) continue;
    bool sent = false;
    if (peer.syncStage == 1) {
      sent = sendPendingCard(peer, peer.syncResync);
      if (sent) peer.syncStage = 2;
    } else if (peer.syncStage == 2) {
      sent = sendSnapshot(peer, peer.syncResync);
      if (sent) peer.syncStage = 3;
    } else if (peer.syncStage == 3) {
      sent = sendAuthoritySnapshot(peer, peer.syncResync);
      if (sent) peer.syncStage = 4;
    } else if (peer.syncStage == 4) {
      sent = sendRosterSnapshot(peer, peer.syncResync);
      if (sent) peer.syncStage = 5;
    } else {
      bool complete = false;
      sent = sendEventBatch(peer, peer.syncResync, complete);
      if (complete) {
        peer.syncStage = 0;
        peer.syncResync = false;
      }
    }
    nextSyncPeer_ = static_cast<std::uint8_t>((index + 1) % peers_.size());
    lastSyncSendAt_ = now;
    return;
  }
}

bool EspNowTransport::sendSnapshot(Peer& peer, bool resync) {
  if (!peer.encrypted) return false;
  StateSnapshot snapshot{};
  if (!app_.makeSnapshot(peer.seatId, snapshot)) return false;
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  if (!encodeStateSnapshot(snapshot, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::StateSnapshot;
  header.flags = FlagAckRequired | (resync ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (sendFrame(peer.mac.data(), header, payload, payloadLength)) {
    peer.nextSequence++;
    peer.lastSnapshotVersion = snapshot.stateVersion;
    return true;
  }
  return false;
}

bool EspNowTransport::sendAuthoritySnapshot(Peer& peer, bool resync) {
  AuthoritySnapshot snapshot{};
  if (!app_.makeAuthoritySnapshot(snapshot)) return false;
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  if (!encodeAuthoritySnapshot(snapshot, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::AuthoritySnapshot;
  header.flags = FlagAckRequired | (resync ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, payloadLength)) return false;
  peer.nextSequence++;
  peer.lastAuthorityVersion = snapshot.stateVersion;
  return true;
}

bool EspNowTransport::sendRosterSnapshot(Peer& peer, bool resync) {
  RosterSnapshot roster{};
  if (!app_.makeRosterSnapshot(roster)) return false;
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  if (!encodeRosterSnapshot(roster, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::RosterSnapshot;
  header.flags = FlagAckRequired | (resync ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, payloadLength)) return false;
  peer.nextSequence++;
  return true;
}

bool EspNowTransport::sendPendingCard(Peer& peer, bool resync) {
  const auto& pending = app_.state().pendingCard;
  if (!pending.active || pending.playerId != peer.seatId) return true;
  PlayerCardEvent card{};
  card.stage = PlayerCardStage::Drawn;
  card.domainEventType = kDomainEventCardDrawn;
  card.stateVersion = app_.state().stateVersion;
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
  std::size_t payloadLength = 0;
  if (!encodePlayerCardEvent(card, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::PlayerCardEvent;
  header.flags = FlagAckRequired | FlagResponse | (resync ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, payloadLength)) return false;
  peer.nextSequence++;
  return true;
}

bool EspNowTransport::sendCardEvent(Peer& peer, const gridopoly::core::GameEvent& event,
                                    bool resync) {
  const auto detail = event.detail;
  const auto deckId = gridopoly::core::cardEventDeckId(detail);
  const auto cardIndex = gridopoly::core::cardEventCardIndex(detail);
  const bool drawn = event.kind == gridopoly::core::EventKind::CardDrawn;
  PlayerCardEvent card{};
  card.stage = drawn ? PlayerCardStage::Drawn : PlayerCardStage::EffectApplied;
  card.domainEventType = drawn ? kDomainEventCardDrawn : kDomainEventCardEffectApplied;
  card.stateVersion = app_.state().stateVersion;
  card.eventSequence = event.sequence;
  card.playerId = event.actorId;
  card.deckId = deckId;
  card.cardIndex = cardIndex;
  card.cardInstanceId = gridopoly::core::cardEventInstanceId(detail);
  card.cardCatalogId = gridopoly::core::cardCatalogId(deckId, cardIndex);
  card.effectId = card.cardCatalogId;
  card.amount = event.amount;
  if (!drawn && (cardIndex == 1 || cardIndex == 5 || (deckId == 1 && cardIndex == 7)) &&
      card.amount > 0) {
    card.amount = -card.amount;
  }
  card.targetPlayerId = event.targetId;
  card.targetPosition = gridopoly::core::cardEventTargetPosition(detail);
  card.outcome = drawn ? 0 : static_cast<std::uint8_t>(gridopoly::core::cardEventOutcome(detail));
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  if (!encodePlayerCardEvent(card, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::PlayerCardEvent;
  header.flags = FlagAckRequired | (resync ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, payloadLength)) return false;
  peer.nextSequence++;
  return true;
}

bool EspNowTransport::sendEventBatch(Peer& peer, bool resync, bool& complete) {
  complete = false;
  const auto& state = app_.state();
  GameEventBatch batch{};
  batch.stateVersion = state.stateVersion;
  const auto oldestIndex = static_cast<std::uint8_t>(
      (state.eventHead + gridopoly::core::kEventHistory - state.eventCount) % gridopoly::core::kEventHistory);
  bool historyGap = false;
  if (state.eventCount != 0) {
    const auto oldestSequence = state.events[oldestIndex].sequence;
    if (peer.lastEventSequence != 0 && peer.lastEventSequence + 1 < oldestSequence) {
      peer.lastEventSequence = oldestSequence - 1;
      historyGap = true;
    }
  }
  for (std::uint8_t offset = 0; offset < state.eventCount && batch.eventCount < batch.events.size(); ++offset) {
    const auto index = static_cast<std::uint8_t>((oldestIndex + offset) % gridopoly::core::kEventHistory);
    const auto& source = state.events[index];
    if (source.sequence <= peer.lastEventSequence) continue;
    if (source.kind == gridopoly::core::EventKind::CardDrawn ||
        source.kind == gridopoly::core::EventKind::CardApplied) {
      if (batch.eventCount != 0) break;
      if (source.kind == gridopoly::core::EventKind::CardDrawn && source.actorId != peer.seatId) {
        peer.lastEventSequence = source.sequence;
        complete = peer.lastEventSequence >=
            (state.nextEventSequence == 0 ? 0 : state.nextEventSequence - 1);
        return true;
      }
      if (!sendCardEvent(peer, source, resync || historyGap)) return false;
      peer.lastEventSequence = source.sequence;
      complete = peer.lastEventSequence >=
          (state.nextEventSequence == 0 ? 0 : state.nextEventSequence - 1);
      return true;
    }
    auto& target = batch.events[batch.eventCount++];
    target.sequence = source.sequence;
    target.kind = static_cast<std::uint8_t>(source.kind);
    target.actorId = source.actorId;
    target.targetId = source.targetId;
    target.assetIndex = source.assetIndex;
    target.amount = source.amount;
    target.detail = source.detail;
  }
  if (batch.eventCount == 0) {
    complete = true;
    return false;
  }
  std::uint8_t payload[kMaxPayloadSize]{};
  std::size_t payloadLength = 0;
  if (!encodeGameEventBatch(batch, payload, sizeof(payload), payloadLength)) return false;
  Header header{};
  header.type = MessageType::GameEvent;
  header.flags = FlagAckRequired | ((resync || historyGap) ? FlagResync : FlagNone);
  header.sequence = peer.nextSequence;
  header.acknowledgement = peer.lastRxSequence;
  header.roomId = app_.roomId();
  header.deviceId = serverDeviceId_;
  if (!sendFrame(peer.mac.data(), header, payload, payloadLength)) return false;
  peer.nextSequence++;
  peer.lastEventSequence = batch.events[batch.eventCount - 1].sequence;
  complete = peer.lastEventSequence >= (state.nextEventSequence == 0 ? 0 : state.nextEventSequence - 1);
  return true;
}

bool EspNowTransport::sendFrame(const std::uint8_t* mac, Header header, const std::uint8_t* payload,
                                std::size_t payloadLength) {
  std::uint8_t frame[kMaxFrameSize]{};
  std::size_t frameLength = 0;
  if (!encodeFrame(header, payload, payloadLength, frame, sizeof(frame), frameLength)) return false;
  return enqueueTx(mac, header.type, frame, frameLength);
}

}  // namespace gridopoly::server
