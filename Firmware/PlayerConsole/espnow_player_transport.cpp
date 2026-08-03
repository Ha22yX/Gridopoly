#include "espnow_player_transport.h"

#include <ESP.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>

#include "authority_snapshot_reducer.h"

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#else
#define GRIDOPOLY_ESPNOW_TEST_PSK "gridopoly-local-test-key-change-me"
#endif

namespace {

using namespace gridopoly::protocol;

uint32_t get32(const uint8_t *input)
{
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
}

bool sameMac(const uint8_t *left, const std::array<uint8_t, 6> &right)
{
    return left != nullptr && std::memcmp(left, right.data(), right.size()) == 0;
}

TransportError mapResultError(uint8_t result)
{
    if (result == 4) return TransportError::InsufficientCash;
    if (result == 3 || result == 7) return TransportError::AssetChanged;
    if (result == 1 || result == 2) return TransportError::StaleState;
    return TransportError::ActionNotAllowed;
}

void diceForDistance(uint8_t distance, uint8_t &dieA, uint8_t &dieB)
{
    if (distance < 2 || distance > 12) {
        dieA = 1;
        dieB = 1;
        return;
    }
    dieA = static_cast<uint8_t>(distance / 2);
    if (dieA < 1) dieA = 1;
    if (dieA > 6) dieA = 6;
    dieB = static_cast<uint8_t>(distance - dieA);
    if (dieB > 6) {
        dieB = 6;
        dieA = static_cast<uint8_t>(distance - dieB);
    }
}

} // namespace

EspNowPlayerTransport *EspNowPlayerTransport::instance_ = nullptr;

void EspNowPlayerTransport::begin(uint32_t nowMs)
{
    eventHead_ = eventTail_ = eventCount_ = 0;
    pending_ = PendingAction{};
    snapshot_ = StateSnapshot{};
    snapshotValid_ = false;
    deviceId_ = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFu);
    deviceNonce_ = esp_random();
    if (deviceNonce_ == 0) deviceNonce_ = 1;
    const char *psk = GRIDOPOLY_ESPNOW_TEST_PSK;
    if (psk == nullptr || std::strlen(psk) < 16) return;
    mbedtls_sha256(reinterpret_cast<const unsigned char *>(psk), std::strlen(psk),
                   pskHash_.data(), 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_now_init() != ESP_OK || esp_now_set_pmk(pskHash_.data()) != ESP_OK) return;
    instance_ = this;
    if (esp_now_register_recv_cb(&EspNowPlayerTransport::onReceive) != ESP_OK) return;
    ready_ = true;
    resetSession(nowMs);
    TransportEvent connecting{};
    connecting.kind = TransportEventKind::ConnectionLost;
    push(connecting);
    Serial.printf("GRIDOPOLY_LINK scanning device=%08lx\n", static_cast<unsigned long>(deviceId_));
}

bool EspNowPlayerTransport::send(const TransportCommand &command, uint32_t)
{
    if (!ready_ || linkState_ != LinkState::Online || pending_.active) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = linkState_ == LinkState::Online
                             ? TransportError::ActionNotAllowed
                             : TransportError::StaleState;
        rejected.requestId = command.requestId;
        rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
        push(rejected);
        return true;
    }
    return beginPendingAction(command);
}

void EspNowPlayerTransport::tick(uint32_t nowMs)
{
    if (!ready_) return;
    RxItem item{};
    while (dequeue(item)) process(item, nowMs);

    if (linkState_ == LinkState::Scanning) {
        if (static_cast<int32_t>(nowMs - lastScanStepMs_) >= 320) {
            channel_ = static_cast<uint8_t>(channel_ % 13 + 1);
            setChannel(channel_);
            lastScanStepMs_ = nowMs;
        }
        return;
    }
    if ((linkState_ == LinkState::Pairing && nowMs - pairStartedMs_ >= 3000) ||
        (linkState_ == LinkState::AwaitSnapshot && nowMs - pairStartedMs_ >= 6000)) {
        resetSession(nowMs);
        return;
    }
    if (linkState_ != LinkState::Online) return;
    if (!lossReported_ && nowMs - lastServerFrameMs_ >= 5000) {
        TransportEvent lost{};
        lost.kind = TransportEventKind::ConnectionLost;
        push(lost);
        lossReported_ = true;
        Serial.println("GRIDOPOLY_LINK degraded");
    }
    if (nowMs - lastServerFrameMs_ >= 15000) {
        resetSession(nowMs);
        return;
    }
    if (nowMs - lastHeartbeatMs_ >= 2000) {
        if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
    }
}

bool EspNowPlayerTransport::poll(TransportEvent &event)
{
    if (eventCount_ == 0) return false;
    event = events_[eventHead_];
    eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % kEventCapacity);
    --eventCount_;
    return true;
}

void EspNowPlayerTransport::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (instance_ != nullptr && info != nullptr && info->src_addr != nullptr) {
        instance_->enqueue(info->src_addr, data, length);
    }
}

void EspNowPlayerTransport::enqueue(const uint8_t *mac, const uint8_t *data, int length)
{
    if (mac == nullptr || data == nullptr || length <= 0 ||
        length > static_cast<int>(kMaxFrameSize)) return;
    portENTER_CRITICAL(&rxMux_);
    const uint8_t next = static_cast<uint8_t>((rxWrite_ + 1) % kRxCapacity);
    if (next != rxRead_) {
        RxItem &item = rxQueue_[rxWrite_];
        std::memcpy(item.mac.data(), mac, item.mac.size());
        item.length = static_cast<uint16_t>(length);
        std::memcpy(item.bytes.data(), data, length);
        rxWrite_ = next;
    }
    portEXIT_CRITICAL(&rxMux_);
}

bool EspNowPlayerTransport::dequeue(RxItem &item)
{
    bool available = false;
    portENTER_CRITICAL(&rxMux_);
    if (rxRead_ != rxWrite_) {
        item = rxQueue_[rxRead_];
        rxRead_ = static_cast<uint8_t>((rxRead_ + 1) % kRxCapacity);
        available = true;
    }
    portEXIT_CRITICAL(&rxMux_);
    return available;
}

bool EspNowPlayerTransport::push(const TransportEvent &event)
{
    if (eventCount_ >= kEventCapacity) return false;
    events_[eventTail_] = event;
    eventTail_ = static_cast<uint8_t>((eventTail_ + 1) % kEventCapacity);
    ++eventCount_;
    return true;
}

void EspNowPlayerTransport::process(const RxItem &item, uint32_t nowMs)
{
    DecodedFrame frame{};
    if (!decodeFrame(item.bytes.data(), item.length, frame)) return;
    if (frame.header.type == MessageType::Discover) {
        processDiscover(item, frame, nowMs);
        return;
    }
    if (!sameMac(item.mac.data(), serverMac_) ||
        (roomId_ != 0 && frame.header.roomId != roomId_)) return;
    lastServerFrameMs_ = nowMs;
    lossReported_ = false;
    if (frame.header.type == MessageType::PairAccept) {
        processPairAccept(item, frame, nowMs);
    } else if (frame.header.type == MessageType::StateSnapshot) {
        processSnapshot(frame, nowMs);
    } else if (frame.header.type == MessageType::ActionResult) {
        processActionResult(frame);
    }
}

void EspNowPlayerTransport::processDiscover(const RxItem &item, const DecodedFrame &frame,
                                            uint32_t nowMs)
{
    if (linkState_ != LinkState::Scanning || frame.header.payloadLength != 16 ||
        frame.payload[0] != 1 || frame.payload[2] != kVersion) return;
    const uint8_t advertisedChannel = frame.payload[1];
    if (advertisedChannel < 1 || advertisedChannel > 13) return;
    std::memcpy(serverMac_.data(), item.mac.data(), serverMac_.size());
    channel_ = advertisedChannel;
    serverDeviceId_ = get32(frame.payload + 4);
    roomId_ = get32(frame.payload + 8);
    Serial.printf("GRIDOPOLY_LINK discovered server=%08lx room=%lu channel=%u\n",
                  static_cast<unsigned long>(serverDeviceId_),
                  static_cast<unsigned long>(roomId_), channel_);
    if (serverDeviceId_ == 0 || roomId_ == 0 || !setChannel(channel_) ||
        !addServerPeer(false) || !sendPairRequest(nowMs)) {
        resetSession(nowMs);
    }
}

void EspNowPlayerTransport::processPairAccept(const RxItem &, const DecodedFrame &frame,
                                              uint32_t nowMs)
{
    if (linkState_ != LinkState::Pairing) return;
    PairAccept accept{};
    if (!decodePairAccept(frame.payload, frame.header.payloadLength, accept) ||
        accept.accepted != 1 || accept.seatId == 0 ||
        accept.serverDeviceId != serverDeviceId_) {
        resetSession(nowMs);
        return;
    }
    seatId_ = accept.seatId;
    if (!addServerPeer(true)) {
        resetSession(nowMs);
        return;
    }
    linkState_ = LinkState::AwaitSnapshot;
    pairStartedMs_ = nowMs;
    lastServerFrameMs_ = nowMs;
    Serial.printf("GRIDOPOLY_LINK paired seat=%u\n", seatId_);
}

void EspNowPlayerTransport::processSnapshot(const DecodedFrame &frame, uint32_t nowMs)
{
    if (linkState_ != LinkState::AwaitSnapshot && linkState_ != LinkState::Online) return;
    StateSnapshot next{};
    if (!decodeStateSnapshot(frame.payload, frame.header.payloadLength, next) ||
        next.seatId != seatId_) return;
    if (snapshotValid_ && next.stateVersion < snapshot_.stateVersion) return;
    const bool resync = (frame.header.flags & FlagResync) != 0;
    const bool changed = !snapshotValid_ || !authoritySnapshotsEqual(snapshot_, next);
    snapshot_ = next;
    snapshotValid_ = true;
    linkState_ = LinkState::Online;
    lastServerFrameMs_ = nowMs;
    lastHeartbeatMs_ = nowMs;
    lossReported_ = false;
    if (changed || resync) {
        TransportEvent event{};
        if (authoritySnapshotToEvent(snapshot_, resync, event)) push(event);
        Serial.printf("GRIDOPOLY_SNAPSHOT version=%lu seat=%u active=%u cash=%ld position=%u\n",
                      static_cast<unsigned long>(snapshot_.stateVersion), snapshot_.seatId,
                      snapshot_.activePlayerId, static_cast<long>(snapshot_.selfCash),
                      snapshot_.selfPosition);
    }
    if (resync && pending_.active) pending_ = PendingAction{};
    else completePendingFromSnapshot();
}

void EspNowPlayerTransport::processActionResult(const DecodedFrame &frame)
{
    if (!pending_.active || frame.header.payloadLength != 12 || frame.payload[0] != 1 ||
        frame.header.acknowledgement != pending_.wireSequence ||
        get32(frame.payload + 8) != pending_.wireSequence) return;
    const uint8_t result = frame.payload[1];
    if (result != 0) {
        rejectPending(mapResultError(result));
        return;
    }
    pending_.resultStateVersion = get32(frame.payload + 4);
    pending_.awaitingSnapshot = true;
    if (pending_.action == ActionCode::Mortgage) {
        pending_.completedAssetMask |= pending_.currentAssetMask;
        pending_.currentAssetMask = 0;
    }
}

void EspNowPlayerTransport::resetSession(uint32_t nowMs)
{
    if (serverMac_[0] != 0 || serverMac_[1] != 0 || serverMac_[2] != 0 ||
        serverMac_[3] != 0 || serverMac_[4] != 0 || serverMac_[5] != 0) {
        if (esp_now_is_peer_exist(serverMac_.data())) esp_now_del_peer(serverMac_.data());
    }
    serverMac_.fill(0);
    serverDeviceId_ = 0;
    roomId_ = 0;
    seatId_ = 0;
    nextSequence_ = 1;
    pending_ = PendingAction{};
    snapshotValid_ = false;
    linkState_ = LinkState::Scanning;
    channel_ = static_cast<uint8_t>(esp_random() % 13u + 1u);
    setChannel(channel_);
    lastScanStepMs_ = nowMs;
    pairStartedMs_ = 0;
    lossReported_ = false;
}

bool EspNowPlayerTransport::setChannel(uint8_t channel)
{
    if (channel < 1 || channel > 13) return false;
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

bool EspNowPlayerTransport::addServerPeer(bool encrypted)
{
    if (esp_now_is_peer_exist(serverMac_.data())) esp_now_del_peer(serverMac_.data());
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, serverMac_.data(), serverMac_.size());
    peer.ifidx = WIFI_IF_STA;
    peer.channel = channel_;
    peer.encrypt = encrypted;
    if (encrypted) {
        uint8_t material[44]{};
        std::memcpy(material, pskHash_.data(), 32);
        material[32] = static_cast<uint8_t>(serverDeviceId_);
        material[33] = static_cast<uint8_t>(serverDeviceId_ >> 8);
        material[34] = static_cast<uint8_t>(serverDeviceId_ >> 16);
        material[35] = static_cast<uint8_t>(serverDeviceId_ >> 24);
        material[36] = static_cast<uint8_t>(deviceId_);
        material[37] = static_cast<uint8_t>(deviceId_ >> 8);
        material[38] = static_cast<uint8_t>(deviceId_ >> 16);
        material[39] = static_cast<uint8_t>(deviceId_ >> 24);
        material[40] = static_cast<uint8_t>(deviceNonce_);
        material[41] = static_cast<uint8_t>(deviceNonce_ >> 8);
        material[42] = static_cast<uint8_t>(deviceNonce_ >> 16);
        material[43] = static_cast<uint8_t>(deviceNonce_ >> 24);
        uint8_t digest[32]{};
        mbedtls_sha256(material, sizeof(material), digest, 0);
        std::memcpy(peer.lmk, digest, sizeof(peer.lmk));
    }
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowPlayerTransport::sendPairRequest(uint32_t nowMs)
{
    PairRequest request{};
    request.deviceNonce = deviceNonce_;
    request.capabilities = 0x00000001u;
    std::strncpy(request.displayName, "GRID-P1", sizeof(request.displayName) - 1);
    uint8_t payload[32]{};
    size_t payloadLength = 0;
    if (!encodePairRequest(request, payload, sizeof(payload), payloadLength)) return false;
    Header header{};
    header.type = MessageType::PairRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    uint8_t bytes[kMaxFrameSize]{};
    size_t length = 0;
    if (!encodeFrame(header, payload, payloadLength, bytes, sizeof(bytes), length) ||
        esp_now_send(serverMac_.data(), bytes, length) != ESP_OK) return false;
    linkState_ = LinkState::Pairing;
    pairStartedMs_ = nowMs;
    return true;
}

bool EspNowPlayerTransport::sendHeartbeat()
{
    Header header{};
    header.type = MessageType::Heartbeat;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    uint8_t bytes[kHeaderSize]{};
    size_t length = 0;
    return encodeFrame(header, nullptr, 0, bytes, sizeof(bytes), length) &&
           esp_now_send(serverMac_.data(), bytes, length) == ESP_OK;
}

bool EspNowPlayerTransport::sendAction(ActionCode action, uint8_t assetIndex,
                                       int32_t argument, uint32_t stateVersion)
{
    ActionRequest request{};
    request.action = action;
    request.playerId = seatId_;
    request.assetIndex = assetIndex;
    request.argument = argument;
    request.expectedStateVersion = stateVersion;
    uint8_t payload[16]{};
    size_t payloadLength = 0;
    if (!encodeActionRequest(request, payload, sizeof(payload), payloadLength)) return false;
    Header header{};
    header.type = MessageType::ActionRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    uint8_t bytes[kMaxFrameSize]{};
    size_t length = 0;
    if (!encodeFrame(header, payload, payloadLength, bytes, sizeof(bytes), length) ||
        esp_now_send(serverMac_.data(), bytes, length) != ESP_OK) return false;
    pending_.action = action;
    pending_.wireSequence = header.sequence;
    pending_.awaitingSnapshot = false;
    return true;
}

bool EspNowPlayerTransport::beginPendingAction(const TransportCommand &command)
{
    pending_ = PendingAction{};
    pending_.active = true;
    pending_.command = command;
    pending_.previousPosition = snapshot_.selfPosition;
    bool sent = false;
    switch (command.kind) {
        case TransportCommandKind::RollRequest:
            sent = sendAction(ActionCode::Roll, 0xFF, 0, snapshot_.stateVersion);
            break;
        case TransportCommandKind::PayNow:
            sent = sendAction(ActionCode::PayDebt, 0xFF, 0, snapshot_.stateVersion);
            break;
        case TransportCommandKind::MortgageBatchRequest:
            pending_.remainingAssetMask = command.assetMask;
            sent = sendNextMortgage();
            break;
        case TransportCommandKind::MoveManualConfirmRequest:
            sent = sendAction(ActionCode::ConfirmPosition, 0xFF, command.targetPosition,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::ClaimRent:
        case TransportCommandKind::TradeCreate:
            rejectPending(TransportError::ActionNotAllowed);
            return true;
    }
    if (!sent) rejectPending(TransportError::StaleState);
    return true;
}

bool EspNowPlayerTransport::sendNextMortgage()
{
    if (pending_.remainingAssetMask == 0) return false;
    uint8_t index = 0;
    while (index < 32 && (pending_.remainingAssetMask & (1UL << index)) == 0) ++index;
    if (index >= 32) return false;
    pending_.currentAssetMask = 1UL << index;
    pending_.remainingAssetMask &= ~pending_.currentAssetMask;
    return sendAction(ActionCode::Mortgage, index, 0, snapshot_.stateVersion);
}

void EspNowPlayerTransport::completePendingFromSnapshot()
{
    if (!pending_.active || !pending_.awaitingSnapshot ||
        snapshot_.stateVersion < pending_.resultStateVersion) return;
    const TransportCommand command = pending_.command;
    if (command.kind == TransportCommandKind::MortgageBatchRequest &&
        pending_.remainingAssetMask != 0) {
        pending_.awaitingSnapshot = false;
        if (!sendNextMortgage()) rejectPending(TransportError::StaleState);
        return;
    }

    TransportEvent completed{};
    completed.requestId = command.requestId;
    completed.transactionId = command.transactionId;
    completed.stateVersion = snapshot_.stateVersion;
    completed.cash = snapshot_.selfCash;
    completed.playerPosition = snapshot_.selfPosition;
    if (command.kind == TransportCommandKind::RollRequest) {
        completed.kind = TransportEventKind::RollResult;
        uint8_t target = snapshot_.pendingTarget == 0xFF ? snapshot_.selfPosition : snapshot_.pendingTarget;
        uint8_t distance = snapshot_.boardSize == 0 ? 0 :
            static_cast<uint8_t>((target + snapshot_.boardSize - pending_.previousPosition) % snapshot_.boardSize);
        diceForDistance(distance, completed.dieA, completed.dieB);
        push(completed);
        if (snapshot_.pendingTarget != 0xFF) {
            completed.kind = TransportEventKind::MoveGuidanceStarted;
            completed.targetPosition = snapshot_.pendingTarget;
            completed.targetName = "TARGET TILE";
            push(completed);
        }
    } else if (command.kind == TransportCommandKind::PayNow) {
        completed.kind = TransportEventKind::PaymentCompleted;
        push(completed);
    } else if (command.kind == TransportCommandKind::MortgageBatchRequest) {
        completed.kind = TransportEventKind::MortgageBatchCompleted;
        completed.assetMask = pending_.completedAssetMask;
        push(completed);
    } else if (command.kind == TransportCommandKind::MoveManualConfirmRequest) {
        completed.kind = TransportEventKind::RfidPositionConfirmed;
        completed.targetPosition = command.targetPosition;
        completed.observedPosition = snapshot_.selfPosition;
        completed.manual = true;
        push(completed);
    } else {
        completed.kind = TransportEventKind::CommandCompleted;
        push(completed);
    }
    pending_ = PendingAction{};
}

void EspNowPlayerTransport::rejectPending(TransportError error)
{
    TransportEvent rejected{};
    rejected.kind = TransportEventKind::CommandRejected;
    rejected.error = error;
    rejected.requestId = pending_.command.requestId;
    rejected.transactionId = pending_.command.transactionId;
    rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
    push(rejected);
    pending_ = PendingAction{};
}
