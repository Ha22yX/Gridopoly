#include "wifi_udp_player_transport.h"

#include <ESP.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#endif

#ifndef GRIDOPOLY_WIFI_UDP_SSID
#define GRIDOPOLY_WIFI_UDP_SSID "gridopoly"
#endif

#ifndef GRIDOPOLY_WIFI_UDP_PASSWORD
#define GRIDOPOLY_WIFI_UDP_PASSWORD "replace-locally"
#endif

#ifndef GRIDOPOLY_WIFI_UDP_CHANNEL
#define GRIDOPOLY_WIFI_UDP_CHANNEL 1
#endif

#ifndef GRIDOPOLY_WIFI_UDP_PSK
#ifdef GRIDOPOLY_ESPNOW_TEST_PSK
#define GRIDOPOLY_WIFI_UDP_PSK GRIDOPOLY_ESPNOW_TEST_PSK
#else
#define GRIDOPOLY_WIFI_UDP_PSK "gridopoly-local-test-key-change-me"
#endif
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

} // namespace

void WifiUdpPlayerTransport::begin(uint32_t nowMs)
{
    eventHead_ = eventTail_ = eventCount_ = 0;
    pending_ = PendingAction{};
    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
    pendingTrade_ = PendingTradeRequest{};
    pendingIdentity_ = PendingIdentityRequest{};
    playerDetailPayload_ = TransportPlayerDetailPayload{};
    identityPayload_ = TransportIdentityPayload{};
    snapshot_ = StateSnapshot{};
    authoritySnapshot_ = AuthoritySnapshot{};
    rosterSnapshot_ = RosterSnapshot{};
    snapshotValid_ = false;
    authoritySnapshotValid_ = false;
    rosterSnapshotValid_ = false;
    deviceId_ = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFu);
    deviceNonce_ = esp_random();
    if (deviceNonce_ == 0) deviceNonce_ = 1;
    const char *psk = GRIDOPOLY_WIFI_UDP_PSK;
    if (psk == nullptr || std::strlen(psk) < 16) return;
    deriveUdpPairKey(psk, pairKey_);

    nextPacketSequence_ = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    if (nextPacketSequence_ == 0) nextPacketSequence_ = 1;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    ready_ = true;
    resetUdpSession(nowMs);
    beginWifi(nowMs);

    TransportEvent connecting{};
    connecting.kind = TransportEventKind::ConnectionLost;
    push(connecting);
    Serial.printf("GRIDOPOLY_UDP wifi_connecting device=%08lx\n",
                  static_cast<unsigned long>(deviceId_));
}

void WifiUdpPlayerTransport::tick(uint32_t nowMs)
{
    if (!ready_) return;

    const IPAddress localAddress = WiFi.localIP();
    const bool hasDhcpAddress = localAddress != IPAddress(0, 0, 0, 0);
    if (WiFi.status() != WL_CONNECTED && !hasDhcpAddress) {
        if (linkState_ != LinkState::Scanning || udpStarted_ || sessionId_ != 0) {
            resetUdpSession(nowMs);
            TransportEvent lost{};
            lost.kind = TransportEventKind::ConnectionLost;
            push(lost);
            Serial.println("GRIDOPOLY_UDP wifi_lost");
        }
        if (nowMs - lastWifiAttemptMs_ >= kWifiRecoveryMs) recoverWifi(nowMs);
        return;
    }

    if (!udpStarted_ && !startUdp()) return;
    pollDatagrams(nowMs);

    if (linkState_ == LinkState::Scanning) return;
    if ((linkState_ == LinkState::Pairing && nowMs - pairStartedMs_ >= 3000) ||
        (linkState_ == LinkState::AwaitSnapshot && nowMs - pairStartedMs_ >= 6000)) {
        resetUdpSession(nowMs);
        return;
    }
    if (linkState_ != LinkState::Online) return;

    if (pending_.active) {
        const uint32_t pendingAgeMs = nowMs - pending_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pending_.lastSendMs;
        if (!pending_.awaitingSnapshot && !pending_.resyncRequested &&
            sinceLastSendMs >= 450) {
            if (pending_.sendAttempts < 4) {
                resendPendingAction(nowMs);
            } else {
                eventGapDetected_ = true;
                pending_.resyncRequested = true;
                if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
            }
        } else if (pending_.awaitingSnapshot && !pending_.resyncRequested &&
                   sinceLastSendMs >= 1200) {
            eventGapDetected_ = true;
            pending_.resyncRequested = true;
            if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
        }
        if (pendingAgeMs >= 6500) rejectPending(TransportError::StaleState);
    }

    if (pendingPlayerDetail_.active) {
        const uint32_t queryAgeMs = nowMs - pendingPlayerDetail_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingPlayerDetail_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingPlayerDetail_.sendAttempts < 4) {
            resendPlayerDetailQuery(nowMs);
        }
        if (queryAgeMs >= 4000) rejectPlayerDetailQuery(TransportError::StaleState);
    }

    if (pendingTrade_.active) {
        const uint32_t requestAgeMs = nowMs - pendingTrade_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingTrade_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingTrade_.sendAttempts < 4) {
            sendTradeRequest(nowMs);
        }
        if (requestAgeMs >= 4000) rejectTradeRequest(TransportError::StaleState);
    }

    if (pendingIdentity_.active) {
        const uint32_t requestAgeMs = nowMs - pendingIdentity_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingIdentity_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingIdentity_.sendAttempts < 4) {
            sendIdentityRequest(nowMs);
        }
        if (requestAgeMs >= 4000) rejectIdentityRequest(TransportError::StaleState);
    }

    if (!lossReported_ && nowMs - lastServerFrameMs_ >= kLinkDegradedAfterMs) {
        TransportEvent lost{};
        lost.kind = TransportEventKind::ConnectionLost;
        push(lost);
        lossReported_ = true;
        Serial.println("GRIDOPOLY_UDP degraded");
    }
    if (nowMs - lastServerFrameMs_ >= kSessionResetAfterMs) {
        resetUdpSession(nowMs);
        return;
    }
    if ((!pending_.active || pending_.resyncRequested) &&
        nowMs - lastHeartbeatMs_ >= 2000) {
        if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
    }
}

void WifiUdpPlayerTransport::beginWifi(uint32_t nowMs)
{
    lastWifiAttemptMs_ = nowMs;
    if (udpStarted_) {
        udp_.stop();
        udpStarted_ = false;
    }
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("GRIDOPOLY_UDP wifi_mode_failed");
        return;
    }
    const wl_status_t status =
        WiFi.begin(GRIDOPOLY_WIFI_UDP_SSID, GRIDOPOLY_WIFI_UDP_PASSWORD,
                   GRIDOPOLY_WIFI_UDP_CHANNEL);
    Serial.printf("GRIDOPOLY_UDP wifi_begin status=%d\n", static_cast<int>(status));
}

void WifiUdpPlayerTransport::recoverWifi(uint32_t nowMs)
{
    Serial.printf("GRIDOPOLY_UDP wifi_recover status=%d ip=%s\n",
                  static_cast<int>(WiFi.status()),
                  WiFi.localIP().toString().c_str());
    if (udpStarted_) udp_.stop();
    udpStarted_ = false;
    WiFi.mode(WIFI_OFF);
    delay(250);
    beginWifi(nowMs);
}

bool WifiUdpPlayerTransport::startUdp()
{
    if (udpStarted_) return true;
    udpStarted_ = udp_.begin(kGridopolyUdpPort) == 1;
    if (udpStarted_) {
        Serial.printf("GRIDOPOLY_UDP ready ip=%s port=%u\n",
                      WiFi.localIP().toString().c_str(), kGridopolyUdpPort);
    }
    return udpStarted_;
}

void WifiUdpPlayerTransport::resetUdpSession(uint32_t nowMs)
{
    serverDeviceId_ = 0;
    roomId_ = 0;
    pendingRoomId_ = 0;
    sessionId_ = 0;
    seatId_ = 0;
    nextSequence_ = 1;
    pending_ = PendingAction{};
    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
    pendingTrade_ = PendingTradeRequest{};
    pendingIdentity_ = PendingIdentityRequest{};
    sessionKey_.fill(0);
    sessionReplay_.reset();
    clearProjection(true);
    linkState_ = LinkState::Scanning;
    pairStartedMs_ = 0;
    lastServerFrameMs_ = nowMs;
    lastHeartbeatMs_ = nowMs;
    lossReported_ = false;
}

void WifiUdpPlayerTransport::pollDatagrams(uint32_t nowMs)
{
    std::array<uint8_t, kMaxUdpDatagramSize> bytes{};
    for (uint8_t count = 0; count < 8; ++count) {
        // A GameEvent datagram can expand to 13 app events. Leave the packet
        // in the UDP socket until the main loop has drained enough capacity.
        if (eventCount_ + gridopoly::protocol::kMaxEventsPerBatch >
            kEventCapacity) return;
        const int packetLength = udp_.parsePacket();
        if (packetLength <= 0) return;
        const IPAddress remote = udp_.remoteIP();
        const uint16_t remotePort = udp_.remotePort();
        if (packetLength > static_cast<int>(bytes.size())) {
            while (udp_.available() > 0) udp_.read();
            continue;
        }
        const int received = udp_.read(bytes.data(), bytes.size());
        if (received == packetLength) {
            processDatagram(bytes.data(), static_cast<size_t>(received), remote,
                            remotePort, nowMs);
        }
    }
}

void WifiUdpPlayerTransport::processDatagram(const uint8_t *bytes, size_t length,
                                              const IPAddress &remote,
                                              uint16_t remotePort, uint32_t nowMs)
{
    if (bytes == nullptr || length < kUdpEnvelopeHeaderSize + kHeaderSize ||
        remote != serverAddress_ || remotePort != kGridopolyUdpPort) return;

    if ((bytes[5] & ~(UdpFlagPairingKey | UdpFlagBroadcast)) != 0) return;
    const bool pairingPacket = (bytes[5] & UdpFlagPairingKey) != 0;
    DecodedUdpDatagram datagram{};
    const auto &key = pairingPacket ? pairKey_ : sessionKey_;
    if (!decodeUdpDatagram(bytes, length, key, datagram)) return;

    DecodedFrame frame{};
    if (!decodeFrame(datagram.frame, datagram.header.frameLength, frame) ||
        datagram.header.senderDeviceId != frame.header.deviceId) return;

    if (pairingPacket) {
        if (frame.header.type == MessageType::Discover &&
            datagram.header.sessionId == 0 &&
            datagram.header.flags == (UdpFlagPairingKey | UdpFlagBroadcast)) {
            processUdpDiscover(frame, nowMs);
        } else if (frame.header.type == MessageType::PairAccept &&
                   datagram.header.flags == UdpFlagPairingKey) {
            processUdpPairAccept(datagram, frame, nowMs);
        }
        return;
    }

    if (datagram.header.flags != UdpFlagNone || sessionId_ == 0 ||
        datagram.header.sessionId != sessionId_ ||
        datagram.header.senderDeviceId != serverDeviceId_ ||
        !sessionReplay_.accept(datagram.header.packetSequence)) return;
    processUdpFrame(frame, nowMs);
}

void WifiUdpPlayerTransport::processUdpDiscover(const DecodedFrame &frame, uint32_t nowMs)
{
    if (frame.header.payloadLength != 16 ||
        (frame.payload[0] != 1 && frame.payload[0] != 2) ||
        frame.payload[2] != kVersion) return;
    const uint32_t advertisedServerId = get32(frame.payload + 4);
    const uint32_t advertisedRoomId = get32(frame.payload + 8);
    if (advertisedServerId == 0 || advertisedRoomId == 0 ||
        frame.header.roomId != advertisedRoomId ||
        frame.header.deviceId != advertisedServerId) return;

    if (linkState_ != LinkState::Scanning) {
        const bool newRoom = advertisedServerId == serverDeviceId_ &&
                             advertisedRoomId != roomId_;
        if (!newRoom) return;
        TransportEvent lost{};
        lost.kind = TransportEventKind::ConnectionLost;
        resetUdpSession(nowMs);
        push(lost);
    }

    serverDeviceId_ = advertisedServerId;
    roomId_ = advertisedRoomId;
    Serial.printf("GRIDOPOLY_UDP discovered server=%08lx room=%lu schema=%u\n",
                  static_cast<unsigned long>(serverDeviceId_),
                  static_cast<unsigned long>(roomId_), frame.payload[0]);
    if (!sendPairRequest(nowMs)) resetUdpSession(nowMs);
}

void WifiUdpPlayerTransport::processUdpPairAccept(
    const DecodedUdpDatagram &datagram, const DecodedFrame &frame, uint32_t nowMs)
{
    if (linkState_ != LinkState::Pairing || datagram.header.sessionId == 0 ||
        frame.header.roomId != roomId_ || nextSequence_ == 0 ||
        frame.header.acknowledgement != nextSequence_ - 1) return;
    PairAccept accept{};
    if (!decodePairAccept(frame.payload, frame.header.payloadLength, accept) ||
        accept.accepted != 1 || accept.seatId == 0 || accept.wifiChannel != 0 ||
        accept.serverDeviceId != serverDeviceId_ || accept.sessionId == 0 ||
        accept.sessionId != datagram.header.sessionId) {
        resetUdpSession(nowMs);
        return;
    }

    seatId_ = accept.seatId;
    sessionId_ = accept.sessionId;
    deriveUdpSessionKey(pairKey_, serverDeviceId_, deviceId_, deviceNonce_, roomId_,
                        sessionId_, sessionKey_);
    sessionReplay_.reset();
    linkState_ = LinkState::AwaitSnapshot;
    pairStartedMs_ = nowMs;
    lastServerFrameMs_ = nowMs;
    lastHeartbeatMs_ = nowMs - 2000;
    if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
    Serial.printf("GRIDOPOLY_UDP paired seat=%u room=%lu session=%lu\n", seatId_,
                  static_cast<unsigned long>(roomId_),
                  static_cast<unsigned long>(sessionId_));
}

void WifiUdpPlayerTransport::processUdpFrame(const DecodedFrame &frame, uint32_t nowMs)
{
    if (frame.header.roomId != roomId_ || frame.header.deviceId != serverDeviceId_) return;

    const bool recovering = lossReported_;
    lastServerFrameMs_ = nowMs;
    if (recovering) {
        eventGapDetected_ = true;
        if (pending_.active) pending_.resyncRequested = true;
        lastHeartbeatMs_ = nowMs - 2000;
        Serial.println("GRIDOPOLY_UDP recovered requesting_resync");
    }
    lossReported_ = false;

    if (frame.header.type == MessageType::StateSnapshot) {
        processSnapshot(frame, nowMs);
    } else if (frame.header.type == MessageType::AuthoritySnapshot) {
        processAuthoritySnapshot(frame);
    } else if (frame.header.type == MessageType::RosterSnapshot) {
        processRosterSnapshot(frame);
    } else if (frame.header.type == MessageType::GameEvent) {
        processGameEventBatch(frame);
    } else if (frame.header.type == MessageType::PlayerCardEvent) {
        processPlayerCardEvent(frame);
    } else if (frame.header.type == MessageType::ActionResult) {
        processActionResult(frame);
    } else if (frame.header.type == MessageType::PlayerDetailResponse) {
        processPlayerDetailResponse(frame);
    } else if (frame.header.type == MessageType::TradeResponse) {
        processTradeResponse(frame);
    } else if (frame.header.type == MessageType::IdentitySnapshot) {
        processIdentitySnapshot(frame, nowMs);
    }
}

bool WifiUdpPlayerTransport::sendFrame(TxKind kind, const uint8_t *data, size_t length)
{
    if (!udpStarted_ || data == nullptr || length == 0 || length > kMaxFrameSize) return false;
    const bool pairing = kind == TxKind::PairRequest;
    if (!pairing && sessionId_ == 0) return false;

    UdpEnvelopeHeader envelope{};
    envelope.flags = pairing ? UdpFlagPairingKey : UdpFlagNone;
    envelope.sessionId = pairing ? 0 : sessionId_;
    envelope.senderDeviceId = deviceId_;
    envelope.packetSequence = nextPacketSequence_++;
    envelope.frameLength = static_cast<uint16_t>(length);

    std::array<uint8_t, kMaxUdpDatagramSize> datagram{};
    size_t written = 0;
    const auto &key = pairing ? pairKey_ : sessionKey_;
    if (!encodeUdpDatagram(envelope, key, data, length, datagram.data(),
                           datagram.size(), written)) return false;
    if (!udp_.beginPacket(serverAddress_, kGridopolyUdpPort)) return false;
    const size_t sent = udp_.write(datagram.data(), written);
    const bool completed = udp_.endPacket() == 1;
    if (sent != written || !completed) {
        Serial.printf("GRIDOPOLY_UDP send_failed kind=%u bytes=%u/%u\n",
                      static_cast<unsigned>(kind), static_cast<unsigned>(sent),
                      static_cast<unsigned>(written));
        return false;
    }
    return true;
}
