#include "espnow_player_transport.h"

#include <ESP.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>

#include "authority_snapshot_reducer.h"

#ifndef GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS
#define GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS 0
#endif

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#else
#define GRIDOPOLY_ESPNOW_TEST_PSK "gridopoly-local-test-key-change-me"
#endif

namespace {

using namespace gridopoly::protocol;

constexpr uint32_t kLinkDegradedAfterMs = 9000;
constexpr uint32_t kSessionResetAfterMs = 15000;
static_assert(kPlayerDetailAssetCapacity == kMaxPlayerDetailAssets,
              "transport and wire player-asset capacities must match");
static_assert(kPlayerFinanceCapacity == kMaxPlayerDetailLedgerEntries,
              "transport and wire ledger capacities must match");

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

AvatarRecipe toWireRecipe(const TransportAvatarRecipe &source)
{
    AvatarRecipe target{};
    target.avatarCatalogVersion = source.catalogVersion;
    target.hairPresetId = source.hairPresetId;
    target.hairColorId = source.hairColorId;
    target.facePresetId = source.facePresetId;
    target.skinToneId = source.skinToneId;
    target.outfitPresetId = source.outfitPresetId;
    return target;
}

TransportAvatarRecipe fromWireRecipe(const AvatarRecipe &source)
{
    TransportAvatarRecipe target{};
    target.catalogVersion = source.avatarCatalogVersion;
    target.hairPresetId = source.hairPresetId;
    target.hairColorId = source.hairColorId;
    target.facePresetId = source.facePresetId;
    target.skinToneId = source.skinToneId;
    target.outfitPresetId = source.outfitPresetId;
    return target;
}

TransportIdentityPhase mapIdentityStage(IdentityRoomPhase roomPhase,
                                        IdentitySeatStage seatStage)
{
    if (roomPhase == IdentityRoomPhase::Active) return TransportIdentityPhase::Active;
    switch (seatStage) {
        case IdentitySeatStage::AvatarSetup: return TransportIdentityPhase::AwaitAvatar;
        case IdentitySeatStage::AvatarGenerating:
            return TransportIdentityPhase::GeneratingAvatar;
        case IdentitySeatStage::NameSetup: return TransportIdentityPhase::AwaitName;
        case IdentitySeatStage::Ready: return TransportIdentityPhase::Ready;
        case IdentitySeatStage::Countdown: return TransportIdentityPhase::Countdown;
        case IdentitySeatStage::Active: return TransportIdentityPhase::Active;
    }
    return TransportIdentityPhase::Inactive;
}

} // namespace

EspNowPlayerTransport *EspNowPlayerTransport::instance_ = nullptr;

void EspNowPlayerTransport::begin(uint32_t nowMs)
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
    if (esp_now_register_recv_cb(&EspNowPlayerTransport::onReceive) != ESP_OK ||
        esp_now_register_send_cb(&EspNowPlayerTransport::onSend) != ESP_OK) return;
    ready_ = true;
    resetSession(nowMs);
    TransportEvent connecting{};
    connecting.kind = TransportEventKind::ConnectionLost;
    push(connecting);
    Serial.printf("GRIDOPOLY_LINK scanning device=%08lx\n", static_cast<unsigned long>(deviceId_));
}

bool EspNowPlayerTransport::send(const TransportCommand &command, uint32_t nowMs)
{
    const bool playerDetail = command.kind == TransportCommandKind::PlayerDetailRequest;
    const bool trade = command.kind == TransportCommandKind::TradeQuery ||
                       command.kind == TransportCommandKind::TradeCreate ||
                       command.kind == TransportCommandKind::TradeUpdate ||
                       command.kind == TransportCommandKind::TradeConfirm ||
                       command.kind == TransportCommandKind::TradeReject ||
                       command.kind == TransportCommandKind::TradeCancel;
    const bool identity = command.kind == TransportCommandKind::IdentityRequest;
    if (!ready_ || linkState_ != LinkState::Online || pendingRoomId_ != 0 ||
        pending_.active || pendingTrade_.active || pendingIdentity_.active ||
        (playerDetail && pendingPlayerDetail_.active)) {
        Serial.printf(
            "GRIDOPOLY_COMMAND blocked kind=%u request=%lu ready=%u link=%u room_pending=%lu action_pending=%u query_pending=%u\n",
            static_cast<unsigned>(command.kind), static_cast<unsigned long>(command.requestId),
            ready_ ? 1u : 0u, static_cast<unsigned>(linkState_),
            static_cast<unsigned long>(pendingRoomId_), pending_.active ? 1u : 0u,
            pendingPlayerDetail_.active ? 1u : 0u
        );
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
    if (playerDetail) return beginPlayerDetailQuery(command, nowMs);
    if (trade) return beginTradeRequest(command, nowMs);
    if (identity) return beginIdentityRequest(command, nowMs);
    return beginPendingAction(command, nowMs);
}

void EspNowPlayerTransport::tick(uint32_t nowMs)
{
    if (!ready_) return;
    drainSendResults();
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
                Serial.printf(
                    "GRIDOPOLY_COMMAND resync request=%lu reason=result_timeout attempts=%u\n",
                    static_cast<unsigned long>(pending_.command.requestId), pending_.sendAttempts
                );
                if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
            }
        } else if (pending_.awaitingSnapshot && !pending_.resyncRequested &&
                   sinceLastSendMs >= 1200) {
            eventGapDetected_ = true;
            pending_.resyncRequested = true;
            Serial.printf(
                "GRIDOPOLY_COMMAND resync request=%lu reason=snapshot_timeout\n",
                static_cast<unsigned long>(pending_.command.requestId)
            );
            if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
        }
        if (pendingAgeMs >= 6500) {
            Serial.printf(
                "GRIDOPOLY_COMMAND timeout request=%lu age_ms=%lu\n",
                static_cast<unsigned long>(pending_.command.requestId),
                static_cast<unsigned long>(pendingAgeMs)
            );
            rejectPending(TransportError::StaleState);
        }
    }
    if (pendingPlayerDetail_.active) {
        const uint32_t queryAgeMs = nowMs - pendingPlayerDetail_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingPlayerDetail_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingPlayerDetail_.sendAttempts < 4) {
            resendPlayerDetailQuery(nowMs);
        }
        if (queryAgeMs >= 4000) {
            rejectPlayerDetailQuery(TransportError::StaleState);
        }
    }
    if (pendingTrade_.active) {
        const uint32_t tradeAgeMs = nowMs - pendingTrade_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingTrade_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingTrade_.sendAttempts < 4) {
            sendTradeRequest(nowMs);
        }
        if (tradeAgeMs >= 4000) {
            rejectTradeRequest(TransportError::StaleState);
        }
    }
    if (pendingIdentity_.active) {
        const uint32_t requestAgeMs = nowMs - pendingIdentity_.startedMs;
        const uint32_t sinceLastSendMs = nowMs - pendingIdentity_.lastSendMs;
        if (sinceLastSendMs >= 700 && pendingIdentity_.sendAttempts < 4) {
            sendIdentityRequest(nowMs);
        }
        if (requestAgeMs >= 4000) {
            rejectIdentityRequest(TransportError::StaleState);
        }
    }
    if (!lossReported_ && nowMs - lastServerFrameMs_ >= kLinkDegradedAfterMs) {
        TransportEvent lost{};
        lost.kind = TransportEventKind::ConnectionLost;
        push(lost);
        lossReported_ = true;
        Serial.println("GRIDOPOLY_LINK degraded");
    }
    if (nowMs - lastServerFrameMs_ >= kSessionResetAfterMs) {
        resetSession(nowMs);
        return;
    }
    if ((!pending_.active || pending_.resyncRequested) && nowMs - lastHeartbeatMs_ >= 2000) {
        if (sendHeartbeat()) lastHeartbeatMs_ = nowMs;
    }
}

bool EspNowPlayerTransport::poll(TransportEvent &event)
{
    if (eventCount_ == 0) return false;
    event = events_[eventHead_];
    eventHead_ = static_cast<uint8_t>((eventHead_ + 1) % kEventCapacity);
    --eventCount_;
    if (transportEventAdvancesAppliedStateVersion(event.kind,
                                                  identitySetupActive_)) {
        if (event.stateVersion > appliedStateVersion_ || event.resync) {
            appliedStateVersion_ = event.stateVersion;
        }
    } else if (event.kind == TransportEventKind::GameEventReceived) {
        if (!eventCursor_.markApplied(event.gameEvent.sequence, event.resync)) {
            eventGapDetected_ = true;
        }
    } else if (event.kind == TransportEventKind::PlayerCardDrawn ||
               event.kind == TransportEventKind::PlayerCardEffectApplied) {
        const bool presentationReplay = event.resync &&
            (event.cardFlags & gridopoly::protocol::PlayerCardFlagReplay) != 0;
        if (!presentationReplay &&
            !eventCursor_.markApplied(event.cardEventSequence, event.resync)) {
            eventGapDetected_ = true;
        }
    }
    return true;
}

void EspNowPlayerTransport::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (instance_ != nullptr && info != nullptr && info->src_addr != nullptr) {
        instance_->enqueue(info->src_addr, data, length);
    }
}

void EspNowPlayerTransport::onSend(const esp_now_send_info_t *info,
                                   esp_now_send_status_t status)
{
    if (instance_ != nullptr) {
        instance_->recordSendResult(info == nullptr ? nullptr : info->des_addr, status);
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
    TransportEvent scopedEvent = event;
    if (scopedEvent.roomId == 0) scopedEvent.roomId = roomId_;
    events_[eventTail_] = scopedEvent;
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
    if (!sameMac(item.mac.data(), serverMac_)) return;
    if (pendingRoomId_ != 0 && frame.header.roomId == roomId_) return;
    if (roomId_ != 0 && frame.header.roomId != roomId_) {
        const bool trustedRoomResync =
            (frame.header.type == MessageType::StateSnapshot ||
             frame.header.type == MessageType::IdentitySnapshot) &&
            (frame.header.flags & FlagResync) != 0 &&
            frame.header.deviceId == serverDeviceId_ && frame.header.roomId != 0 &&
            (pendingRoomId_ == 0 || frame.header.roomId == pendingRoomId_);
        if (!trustedRoomResync) return;
        Serial.printf("GRIDOPOLY_LINK room_resync old=%lu new=%lu\n",
                      static_cast<unsigned long>(roomId_),
                      static_cast<unsigned long>(frame.header.roomId));
        roomId_ = frame.header.roomId;
        pendingRoomId_ = 0;
        pendingPlayerDetail_ = PendingPlayerDetailQuery{};
        pendingTrade_ = PendingTradeRequest{};
        pendingIdentity_ = PendingIdentityRequest{};
        clearProjection(true);
    }
#if GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS
    Serial.printf(
        "GRIDOPOLY_RX type=%u sequence=%lu ack=%lu flags=%u room=%lu at_ms=%lu\n",
        static_cast<unsigned>(frame.header.type),
        static_cast<unsigned long>(frame.header.sequence),
        static_cast<unsigned long>(frame.header.acknowledgement), frame.header.flags,
        static_cast<unsigned long>(frame.header.roomId), static_cast<unsigned long>(nowMs)
    );
#endif
    const bool recovering = lossReported_;
    lastServerFrameMs_ = nowMs;
    if (recovering) {
        eventGapDetected_ = true;
        if (pending_.active) pending_.resyncRequested = true;
        lastHeartbeatMs_ = nowMs - 2000;
        Serial.println("GRIDOPOLY_LINK recovered requesting_resync");
    }
    lossReported_ = false;
    if (frame.header.type == MessageType::PairAccept) {
        processPairAccept(item, frame, nowMs);
    } else if (frame.header.type == MessageType::StateSnapshot) {
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

void EspNowPlayerTransport::processDiscover(const RxItem &item, const DecodedFrame &frame,
                                            uint32_t nowMs)
{
    if (frame.header.payloadLength != 16 || frame.payload[0] != 1 ||
        frame.payload[2] != kVersion) return;
    const uint8_t advertisedChannel = frame.payload[1];
    if (advertisedChannel < 1 || advertisedChannel > 13) return;
    const uint32_t advertisedServerId = get32(frame.payload + 4);
    const uint32_t advertisedRoomId = get32(frame.payload + 8);
    const bool scanning = linkState_ == LinkState::Scanning;
    if (!scanning) {
        const bool sameServerNewRoom = sameMac(item.mac.data(), serverMac_) &&
            advertisedServerId == serverDeviceId_ && advertisedRoomId != 0 &&
            advertisedRoomId != roomId_;
        if (!sameServerNewRoom) return;
        if (pendingRoomId_ == advertisedRoomId) return;
        Serial.printf("GRIDOPOLY_LINK room_discovered old=%lu new=%lu\n",
                      static_cast<unsigned long>(roomId_),
                      static_cast<unsigned long>(advertisedRoomId));
        pending_ = PendingAction{};
        pendingPlayerDetail_ = PendingPlayerDetailQuery{};
        pendingIdentity_ = PendingIdentityRequest{};
        pendingRoomId_ = advertisedRoomId;
        lastHeartbeatMs_ = nowMs - 2000;
        return;
    }
    std::memcpy(serverMac_.data(), item.mac.data(), serverMac_.size());
    channel_ = advertisedChannel;
    serverDeviceId_ = advertisedServerId;
    roomId_ = advertisedRoomId;
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
        accept.serverDeviceId != serverDeviceId_ || accept.wifiChannel < 1 ||
        accept.wifiChannel > 13) {
        resetSession(nowMs);
        return;
    }
    seatId_ = accept.seatId;
    channel_ = accept.wifiChannel;
    if (!setChannel(channel_) || !addServerPeer(true)) {
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
    const bool resync = (frame.header.flags & FlagResync) != 0;
    if (snapshotValid_ && next.stateVersion < snapshot_.stateVersion && !resync) return;
    if (resync) eventGapDetected_ = false;
    const bool changed = !snapshotValid_ || !authoritySnapshotsEqual(snapshot_, next);
    snapshot_ = next;
    snapshotValid_ = true;
    linkState_ = LinkState::Online;
    lastServerFrameMs_ = nowMs;
    lastHeartbeatMs_ = nowMs;
    lossReported_ = false;
    if (changed || resync) {
        TransportEvent event{};
        if (authoritySnapshotToEvent(snapshot_, resync, event) && !push(event)) {
            eventGapDetected_ = true;
        }
        Serial.printf("GRIDOPOLY_SNAPSHOT version=%lu seat=%u active=%u players=%u phase=%u cash=%ld position=%u\n",
                      static_cast<unsigned long>(snapshot_.stateVersion), snapshot_.seatId,
                      snapshot_.activePlayerId, snapshot_.playerCount, snapshot_.phase,
                      static_cast<long>(snapshot_.selfCash), snapshot_.selfPosition);
    }
    if (resync) {
        if (pending_.active) pending_ = PendingAction{};
        pendingPlayerDetail_ = PendingPlayerDetailQuery{};
        if (!identitySetupActive_) pendingIdentity_ = PendingIdentityRequest{};
    } else {
        completePendingFromSnapshot();
    }
}

void EspNowPlayerTransport::processAuthoritySnapshot(const DecodedFrame &frame)
{
    if (linkState_ != LinkState::Online) return;
    AuthoritySnapshot next{};
    if (!decodeAuthoritySnapshot(frame.payload, frame.header.payloadLength, next)) return;
    const bool resync = (frame.header.flags & FlagResync) != 0;
    if (authoritySnapshotValid_ && next.stateVersion < authoritySnapshot_.stateVersion && !resync) return;
    if (!resync && (next.pendingCardFlags & 0x01u) != 0 &&
        next.pendingCardPlayerId != seatId_) {
        privateCardDrawSequence_ = next.pendingCardDrawEventSequence;
    }
    const bool changed = !authoritySnapshotValid_ ||
                         !fullAuthoritySnapshotsEqual(authoritySnapshot_, next);
    authoritySnapshot_ = next;
    authoritySnapshotValid_ = true;
    if (resync) {
        eventCursor_.beginResync(next.lastEventSequence);
        privateCardDrawSequence_ = 0;
    }
    if (changed || resync) {
        TransportEvent event{};
        if (fullAuthoritySnapshotToEvent(authoritySnapshot_, resync, event)) {
            if (!push(event)) {
                eventGapDetected_ = true;
            } else {
                Serial.printf(
                    "GRIDOPOLY_AUTH version=%lu events=%lu assets=%u players=%u board=%08lx resync=%u\n",
                    static_cast<unsigned long>(authoritySnapshot_.stateVersion),
                    static_cast<unsigned long>(authoritySnapshot_.lastEventSequence),
                    authoritySnapshot_.assetCount, authoritySnapshot_.playerCount,
                    static_cast<unsigned long>(authoritySnapshot_.boardIdHash),
                    resync ? 1u : 0u
                );
            }
        }
    }
}

void EspNowPlayerTransport::processRosterSnapshot(const DecodedFrame &frame)
{
    if (linkState_ != LinkState::Online) return;
    RosterSnapshot next{};
    if (!decodeRosterSnapshot(frame.payload, frame.header.payloadLength, next)) return;
    const bool resync = (frame.header.flags & FlagResync) != 0;
    if (rosterSnapshotValid_ && next.stateVersion < rosterSnapshot_.stateVersion && !resync) return;
    const bool changed = !rosterSnapshotValid_ || !rosterSnapshotsEqual(rosterSnapshot_, next);
    rosterSnapshot_ = next;
    rosterSnapshotValid_ = true;
    if ((!snapshotValid_ || snapshot_.stateVersion != next.stateVersion) ||
        (!authoritySnapshotValid_ || authoritySnapshot_.stateVersion != next.stateVersion)) {
        eventGapDetected_ = true;
        lastHeartbeatMs_ = millis() - 2000;
        Serial.printf(
            "GRIDOPOLY_LINK projection_gap roster=%lu state=%lu authority=%lu\n",
            static_cast<unsigned long>(next.stateVersion),
            static_cast<unsigned long>(snapshotValid_ ? snapshot_.stateVersion : 0),
            static_cast<unsigned long>(authoritySnapshotValid_ ? authoritySnapshot_.stateVersion : 0)
        );
    }
    if (changed || resync) {
        TransportEvent event{};
        if (rosterSnapshotToEvent(rosterSnapshot_, resync, event)) {
            if (!push(event)) {
                eventGapDetected_ = true;
            } else {
                Serial.printf("GRIDOPOLY_ROSTER version=%lu players=%u resync=%u\n",
                              static_cast<unsigned long>(rosterSnapshot_.stateVersion),
                              rosterSnapshot_.playerCount, resync ? 1u : 0u);
            }
        }
    }
}

void EspNowPlayerTransport::processGameEventBatch(const DecodedFrame &frame)
{
    if (linkState_ != LinkState::Online) return;
    GameEventBatch batch{};
    if (!decodeGameEventBatch(frame.payload, frame.header.payloadLength, batch)) return;
    const bool resync = (frame.header.flags & FlagResync) != 0;
    if (resync && !authoritySnapshotValid_) {
        eventGapDetected_ = true;
        return;
    }
    for (uint8_t index = 0; index < batch.eventCount; ++index) {
        const GameEventRecord &record = batch.events[index];
        if (record.sequence <= eventCursor_.queued) continue;
        if (!queuePrivateCardSkipBefore(record.sequence, batch.stateVersion)) {
            eventGapDetected_ = true;
            return;
        }
        const TransportSequenceDisposition disposition =
            eventCursor_.prepare(record.sequence, resync);
        if (disposition == TransportSequenceDisposition::Duplicate) continue;
        if (disposition == TransportSequenceDisposition::Gap) {
            eventGapDetected_ = true;
            return;
        }
        TransportEvent event{};
        if (!gameEventToTransportEvent(record, batch.stateVersion, resync, event) || !push(event)) {
            eventGapDetected_ = true;
            return;
        }
        eventCursor_.markQueued(record.sequence);
        if (privateCardDrawSequence_ == record.sequence) privateCardDrawSequence_ = 0;
        Serial.printf(
            "GRIDOPOLY_EVENT version=%lu sequence=%lu kind=%u actor=%u target=%u asset=%u amount=%ld\n",
            static_cast<unsigned long>(batch.stateVersion),
            static_cast<unsigned long>(record.sequence), record.kind, record.actorId,
            record.targetId, record.assetIndex, static_cast<long>(record.amount)
        );
    }
}

bool EspNowPlayerTransport::queuePrivateCardSkipBefore(uint32_t nextSequence,
                                                       uint32_t stateVersion)
{
    if (privateCardDrawSequence_ == 0) return true;
    if (privateCardDrawSequence_ <= eventCursor_.queued) {
        privateCardDrawSequence_ = 0;
        return true;
    }
    if (nextSequence <= privateCardDrawSequence_) return true;
    if (privateCardDrawSequence_ != eventCursor_.queued + 1 ||
        nextSequence != privateCardDrawSequence_ + 1) return false;
    TransportEvent skipped{};
    skipped.kind = TransportEventKind::GameEventReceived;
    skipped.stateVersion = stateVersion;
    skipped.gameEvent.sequence = privateCardDrawSequence_;
    skipped.gameEvent.kind = 27;
    if (!push(skipped)) return false;
    eventCursor_.markQueued(privateCardDrawSequence_);
    privateCardDrawSequence_ = 0;
    return true;
}

void EspNowPlayerTransport::processPlayerCardEvent(const DecodedFrame &frame)
{
    if (linkState_ != LinkState::AwaitSnapshot && linkState_ != LinkState::Online) return;
    PlayerCardEvent card{};
    if (!decodePlayerCardEvent(frame.payload, frame.header.payloadLength, card)) return;
    const bool resync = (frame.header.flags & FlagResync) != 0;
    const bool presentationReplay = resync &&
        (card.flags & PlayerCardFlagReplay) != 0;
    if (!presentationReplay) {
        if (card.eventSequence <= eventCursor_.queued) return;
        if (!queuePrivateCardSkipBefore(card.eventSequence, card.stateVersion)) {
            eventGapDetected_ = true;
            return;
        }
        const TransportSequenceDisposition disposition =
            eventCursor_.prepare(card.eventSequence, resync);
        if (disposition == TransportSequenceDisposition::Duplicate) return;
        if (disposition == TransportSequenceDisposition::Gap) {
            eventGapDetected_ = true;
            return;
        }
    }
    TransportEvent event{};
    if (!playerCardEventToTransportEvent(card, resync, event) || !push(event)) {
        eventGapDetected_ = true;
        return;
    }
    if (!presentationReplay) eventCursor_.markQueued(card.eventSequence);
    Serial.printf(
        "GRIDOPOLY_CARD stage=%u instance=%u catalog=%u sequence=%lu replay=%u\n",
        static_cast<unsigned>(card.stage), card.cardInstanceId, card.cardCatalogId,
        static_cast<unsigned long>(card.eventSequence),
        (card.flags & PlayerCardFlagReplay) != 0 ? 1u : 0u
    );
}

void EspNowPlayerTransport::processActionResult(const DecodedFrame &frame)
{
    if (!pending_.active || frame.header.payloadLength != 12 || frame.payload[0] != 1 ||
        frame.header.acknowledgement != pending_.wireSequence ||
        get32(frame.payload + 8) != pending_.wireSequence) return;
    const uint8_t result = frame.payload[1];
    Serial.printf(
        "GRIDOPOLY_COMMAND result request=%lu wire=%lu code=%u version=%lu\n",
        static_cast<unsigned long>(pending_.command.requestId),
        static_cast<unsigned long>(pending_.wireSequence), result,
        static_cast<unsigned long>(get32(frame.payload + 4))
    );
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
    completePendingFromSnapshot();
}

void EspNowPlayerTransport::processPlayerDetailResponse(const DecodedFrame &frame)
{
    if (!pendingPlayerDetail_.active ||
        (frame.header.flags & FlagResponse) == 0 ||
        frame.header.acknowledgement != pendingPlayerDetail_.wireSequence) {
        return;
    }
    PlayerDetailResponse response{};
    if (!decodePlayerDetailResponse(frame.payload, frame.header.payloadLength, response) ||
        response.requestId != pendingPlayerDetail_.command.requestId ||
        response.targetPlayerId != pendingPlayerDetail_.command.targetPlayerId) {
        return;
    }

    playerDetailPayload_ = TransportPlayerDetailPayload{};
    for (uint8_t index = 0; index < response.assetCount; ++index) {
        playerDetailPayload_.assets[index].assetIndex = response.assets[index].assetIndex;
        playerDetailPayload_.assets[index].state = response.assets[index].state;
    }
    for (uint8_t index = 0; index < response.ledgerCount; ++index) {
        const PlayerDetailLedgerEntry &source = response.ledger[index];
        TransportFinancialRecord &target = playerDetailPayload_.financialRecords[index];
        target.sequence = source.sequence;
        target.amount = source.amount;
        target.kind = source.kind;
        target.counterpartyId = source.counterpartyId;
        target.assetIndex = source.assetIndex;
        target.flags = source.flags;
    }

    TransportEvent detail{};
    detail.kind = TransportEventKind::PlayerDetailReceived;
    detail.requestId = response.requestId;
    detail.stateVersion = response.stateVersion;
    detail.detailPlayerId = response.targetPlayerId;
    detail.detailPosition = response.position;
    detail.detailCash = response.cash;
    detail.detailAssetCount = response.assetCount;
    detail.financialRecordCount = response.ledgerCount;
    detail.playerDetail = &playerDetailPayload_;
    if (!push(detail)) return;

    Serial.printf(
        "GRIDOPOLY_DETAIL received request=%lu player=%u version=%lu assets=%u ledger=%u flags=%u\n",
        static_cast<unsigned long>(response.requestId), response.targetPlayerId,
        static_cast<unsigned long>(response.stateVersion), response.assetCount,
        response.ledgerCount, response.flags
    );
    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
}

void EspNowPlayerTransport::processTradeResponse(const DecodedFrame &frame)
{
    TradeResponse response{};
    if (!decodeTradeResponse(frame.payload, frame.header.payloadLength, response)) return;
    if (response.requestId != 0) {
        if (!pendingTrade_.active ||
            response.requestId != pendingTrade_.command.requestId) return;
        pendingTrade_ = PendingTradeRequest{};
    }

    TransportEvent event{};
    event.kind = TransportEventKind::TradeResponseReceived;
    event.roomId = frame.header.roomId;
    event.requestId = response.requestId;
    event.stateVersion = response.stateVersion;
    event.tradeOperation = static_cast<TransportTradeOperation>(response.operation);
    event.tradeResult = static_cast<TransportTradeResult>(response.result);
    event.tradeStatus = static_cast<TransportTradeStatus>(response.status);
    event.tradeFlags = response.flags;
    event.tradeId = response.tradeId;
    event.tradeRevision = response.revision;
    event.tradeExpiresInMs = response.expiresInMs;
    event.tradeCounterpartyId = response.counterpartyId;
    event.tradeConfirmedMask = response.confirmedMask;
    event.tradeOriginatorId = response.originatorId;
    event.tradeSelfGivesCash = response.selfGivesCash;
    event.tradeCounterpartyGivesCash = response.counterpartyGivesCash;
    for (uint8_t index = 0; index < response.selfAssetCount; ++index) {
        event.assetMask |= static_cast<uint32_t>(1u) << response.selfAssets[index];
    }
    for (uint8_t index = 0; index < response.counterpartyAssetCount; ++index) {
        event.counterpartyAssetMask |=
            static_cast<uint32_t>(1u) << response.counterpartyAssets[index];
    }
    if (!push(event)) return;
    Serial.printf(
        "GRIDOPOLY_TRADE response request=%lu trade=%lu revision=%u op=%u status=%u result=%u flags=%u\n",
        static_cast<unsigned long>(response.requestId),
        static_cast<unsigned long>(response.tradeId), response.revision,
        static_cast<unsigned>(response.operation), static_cast<unsigned>(response.status),
        static_cast<unsigned>(response.result), response.flags
    );
}

void EspNowPlayerTransport::processIdentitySnapshot(const DecodedFrame &frame,
                                                     uint32_t nowMs)
{
    if (linkState_ != LinkState::AwaitSnapshot && linkState_ != LinkState::Online) return;
    IdentitySnapshot snapshot{};
    if (!decodeIdentitySnapshot(frame.payload, frame.header.payloadLength, snapshot) ||
        snapshot.selfPlayerId != seatId_) return;

    const bool resync = (frame.header.flags & FlagResync) != 0 ||
        (snapshot.flags & IdentitySnapshotFlagResync) != 0;
    if (identitySnapshotValid_ && snapshot.stateVersion < appliedStateVersion_ && !resync) {
        return;
    }
    if (snapshot.requestId != 0) {
        if (!pendingIdentity_.active ||
            snapshot.requestId != pendingIdentity_.command.requestId) return;
        pendingIdentity_ = PendingIdentityRequest{};
    } else if (pendingIdentity_.active && identitySnapshotCompletesPendingOperation(
                   pendingIdentity_.command.identityOperation, snapshot.selfPlayerId,
                   snapshot.avatarFinalMask, snapshot.nameFinalMask)) {
        // The immediate request response may be lost. A newer authoritative
        // requestId=0 projection is sufficient proof that the mutation completed.
        pendingIdentity_ = PendingIdentityRequest{};
    }

    identityPayload_ = TransportIdentityPayload{};
    for (uint8_t index = 0; index < snapshot.playerCount; ++index) {
        const IdentitySeatRecord &source = snapshot.seats[index];
        TransportIdentitySeat &target = identityPayload_.seats[index];
        target.playerId = source.playerId;
        target.flags = source.flags;
        target.colorIndex = source.seatColorId;
        target.seatRevision = source.seatRevision;
        target.avatarRevision = source.avatarRevision;
        target.avatarCacheTag = source.avatarContentHash64;
        target.recipe = fromWireRecipe(source.recipe);
    }

    TransportEvent event{};
    event.kind = TransportEventKind::IdentitySnapshotReceived;
    event.roomId = frame.header.roomId;
    event.requestId = snapshot.requestId;
    event.stateVersion = snapshot.stateVersion;
    event.selfSeatId = snapshot.selfPlayerId;
    event.identityPhase = mapIdentityStage(snapshot.roomPhase, snapshot.selfStage);
    event.identityResult = static_cast<TransportIdentityResult>(snapshot.result);
    event.identityRevision = snapshot.identityRevision;
    if (snapshot.countdownDeadlineEpochMs > snapshot.serverEpochMs) {
        const uint64_t remainingMs =
            snapshot.countdownDeadlineEpochMs - snapshot.serverEpochMs;
        event.deadlineMs = nowMs + static_cast<uint32_t>(
            remainingMs > 60000u ? 60000u : remainingMs);
    }
    event.identitySeatCount = snapshot.playerCount;
    event.identityHumanMask = snapshot.requiredHumanMask;
    event.identityAvatarReadyMask = snapshot.avatarFinalMask;
    event.identityNameReadyMask = snapshot.nameFinalMask;
    event.identityReadyMask = snapshot.readyMask;
    event.identityOnlineMask = snapshot.onlineMask;
    event.identitySelfStage = static_cast<uint8_t>(snapshot.selfStage);
    event.identity = &identityPayload_;
    event.resync = resync;
    if (!push(event)) {
        eventGapDetected_ = true;
        return;
    }

    identitySnapshotValid_ = true;
    identitySetupActive_ = snapshot.roomPhase != IdentityRoomPhase::Active;
    linkState_ = LinkState::Online;
    lastServerFrameMs_ = nowMs;
    lastHeartbeatMs_ = nowMs;
    lossReported_ = false;
    Serial.printf(
        "GRIDOPOLY_IDENTITY version=%lu revision=%lu phase=%u stage=%u request=%lu players=%u resync=%u\n",
        static_cast<unsigned long>(snapshot.stateVersion),
        static_cast<unsigned long>(snapshot.identityRevision),
        static_cast<unsigned>(snapshot.roomPhase), static_cast<unsigned>(snapshot.selfStage),
        static_cast<unsigned long>(snapshot.requestId), snapshot.playerCount,
        resync ? 1u : 0u
    );
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
    pendingRoomId_ = 0;
    seatId_ = 0;
    nextSequence_ = 1;
    pending_ = PendingAction{};
    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
    pendingTrade_ = PendingTradeRequest{};
    pendingIdentity_ = PendingIdentityRequest{};
    clearProjection(true);
    linkState_ = LinkState::Scanning;
    channel_ = static_cast<uint8_t>(esp_random() % 13u + 1u);
    setChannel(channel_);
    lastScanStepMs_ = nowMs;
    pairStartedMs_ = 0;
    lossReported_ = false;
}

void EspNowPlayerTransport::clearProjection(bool clearQueuedEvents)
{
    snapshot_ = StateSnapshot{};
    authoritySnapshot_ = AuthoritySnapshot{};
    rosterSnapshot_ = RosterSnapshot{};
    identityPayload_ = TransportIdentityPayload{};
    snapshotValid_ = false;
    authoritySnapshotValid_ = false;
    rosterSnapshotValid_ = false;
    identitySnapshotValid_ = false;
    identitySetupActive_ = false;
    appliedStateVersion_ = 0;
    eventCursor_.reset();
    privateCardDrawSequence_ = 0;
    eventGapDetected_ = false;
    if (clearQueuedEvents) {
        eventHead_ = 0;
        eventTail_ = 0;
        eventCount_ = 0;
    }
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
    peer.channel = 0;
    peer.encrypt = encrypted;
    lmkTag_ = 0;
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
        lmkTag_ = get32(digest);
    }
    const esp_err_t result = esp_now_add_peer(&peer);
    Serial.printf(
        "GRIDOPOLY_PEER add encrypted=%u result=%ld device=%08lx server=%08lx nonce=%08lx lmk_tag=%08lx\n",
        encrypted ? 1u : 0u, static_cast<long>(result),
        static_cast<unsigned long>(deviceId_), static_cast<unsigned long>(serverDeviceId_),
        static_cast<unsigned long>(deviceNonce_), static_cast<unsigned long>(lmkTag_)
    );
    if (result == ESP_OK) logPeerState(encrypted ? "encrypted_added" : "plain_added");
    return result == ESP_OK;
}

bool EspNowPlayerTransport::sendFrame(TxKind kind, const uint8_t *data, size_t length)
{
    portENTER_CRITICAL(&txMux_);
    const uint8_t next = static_cast<uint8_t>((txPendingWrite_ + 1) % kTxCapacity);
    if (next == txPendingRead_) {
        portEXIT_CRITICAL(&txMux_);
        if (kind == TxKind::Heartbeat) ++heartbeatEnqueueFail_;
        Serial.println("GRIDOPOLY_TX enqueue=fail reason=callback_queue_full");
        return false;
    }
    txPending_[txPendingWrite_] = kind;
    txPendingWrite_ = next;
    portEXIT_CRITICAL(&txMux_);

    const esp_err_t result = esp_now_send(serverMac_.data(), data, length);
    if (kind == TxKind::Heartbeat) {
        if (result == ESP_OK) ++heartbeatEnqueueOk_;
        else ++heartbeatEnqueueFail_;
    }
    if (result != ESP_OK) {
        portENTER_CRITICAL(&txMux_);
        const uint8_t previous = static_cast<uint8_t>(
            (txPendingWrite_ + kTxCapacity - 1) % kTxCapacity
        );
        if (txPendingWrite_ != txPendingRead_ && txPending_[previous] == kind) {
            txPendingWrite_ = previous;
        }
        portEXIT_CRITICAL(&txMux_);
    }
    if (kind == TxKind::Heartbeat) {
#if GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS
        Serial.printf(
            "GRIDOPOLY_TX heartbeat enqueue=%s result=%ld ok=%lu fail=%lu\n",
            result == ESP_OK ? "ok" : "fail", static_cast<long>(result),
            static_cast<unsigned long>(heartbeatEnqueueOk_),
            static_cast<unsigned long>(heartbeatEnqueueFail_)
        );
        logPeerState("heartbeat");
#else
        if (result != ESP_OK) {
            Serial.printf(
                "GRIDOPOLY_TX heartbeat enqueue=fail result=%ld failures=%lu\n",
                static_cast<long>(result),
                static_cast<unsigned long>(heartbeatEnqueueFail_)
            );
        }
#endif
    }
    return result == ESP_OK;
}

void EspNowPlayerTransport::recordSendResult(const uint8_t *mac,
                                             esp_now_send_status_t status)
{
    portENTER_CRITICAL(&txMux_);
    TxKind kind = TxKind::Unknown;
    if (txPendingRead_ != txPendingWrite_) {
        kind = txPending_[txPendingRead_];
        txPendingRead_ = static_cast<uint8_t>((txPendingRead_ + 1) % kTxCapacity);
    }
    const uint8_t next = static_cast<uint8_t>((txResultWrite_ + 1) % kTxCapacity);
    if (next != txResultRead_) {
        TxResult &result = txResults_[txResultWrite_];
        result.kind = kind;
        result.success = status == ESP_NOW_SEND_SUCCESS;
        if (mac != nullptr) std::memcpy(result.mac.data(), mac, result.mac.size());
        else result.mac.fill(0);
        txResultWrite_ = next;
    }
    if (kind == TxKind::Heartbeat) {
        if (status == ESP_NOW_SEND_SUCCESS) ++heartbeatDeliveryOk_;
        else ++heartbeatDeliveryFail_;
    }
    portEXIT_CRITICAL(&txMux_);
}

void EspNowPlayerTransport::drainSendResults()
{
    while (true) {
        TxResult result{};
        uint32_t heartbeatOk = 0;
        uint32_t heartbeatFail = 0;
        portENTER_CRITICAL(&txMux_);
        if (txResultRead_ == txResultWrite_) {
            portEXIT_CRITICAL(&txMux_);
            break;
        }
        result = txResults_[txResultRead_];
        txResultRead_ = static_cast<uint8_t>((txResultRead_ + 1) % kTxCapacity);
        heartbeatOk = heartbeatDeliveryOk_;
        heartbeatFail = heartbeatDeliveryFail_;
        portEXIT_CRITICAL(&txMux_);
#if !GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS
        if (result.kind == TxKind::Heartbeat && result.success) continue;
#endif
        Serial.printf(
            "GRIDOPOLY_TX delivery kind=%u status=%s target=%02X:%02X:%02X:%02X:%02X:%02X hb_ok=%lu hb_fail=%lu\n",
            static_cast<unsigned>(result.kind), result.success ? "success" : "fail",
            result.mac[0], result.mac[1], result.mac[2], result.mac[3], result.mac[4],
            result.mac[5], static_cast<unsigned long>(heartbeatOk),
            static_cast<unsigned long>(heartbeatFail)
        );
    }
}

void EspNowPlayerTransport::logPeerState(const char *reason) const
{
    esp_now_peer_info_t peer{};
    const bool exists = esp_now_get_peer(serverMac_.data(), &peer) == ESP_OK;
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &secondary);
    Serial.printf(
        "GRIDOPOLY_PEER state=%s target=%02X:%02X:%02X:%02X:%02X:%02X exists=%u radio_ch=%u cfg_ch=%u peer_ch=%u encrypted=%u lmk_tag=%08lx\n",
        reason, serverMac_[0], serverMac_[1], serverMac_[2], serverMac_[3],
        serverMac_[4], serverMac_[5], exists ? 1u : 0u, primary, channel_,
        exists ? peer.channel : 0u, exists && peer.encrypt ? 1u : 0u,
        static_cast<unsigned long>(lmkTag_)
    );
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
        !sendFrame(TxKind::PairRequest, bytes, length)) return false;
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
    header.roomId = pendingRoomId_ != 0 ? pendingRoomId_ : roomId_;
    header.deviceId = deviceId_;
    Heartbeat heartbeat{};
    const bool gameProjectionIncomplete = !snapshotValid_ || !authoritySnapshotValid_ ||
        !rosterSnapshotValid_ || snapshot_.stateVersion != authoritySnapshot_.stateVersion ||
        snapshot_.stateVersion != rosterSnapshot_.stateVersion;
    const bool projectionIncomplete = identitySetupActive_
        ? !identitySnapshotValid_ : gameProjectionIncomplete;
    heartbeat.flags = (eventGapDetected_ || projectionIncomplete || pendingRoomId_ != 0) ? 1u : 0u;
    heartbeat.appliedStateVersion = appliedStateVersion_;
    heartbeat.appliedEventSequence = eventCursor_.applied;
    uint8_t payload[16]{};
    size_t payloadLength = 0;
    if (!encodeHeartbeat(heartbeat, payload, sizeof(payload), payloadLength)) return false;
    uint8_t bytes[kMaxFrameSize]{};
    size_t length = 0;
    return encodeFrame(header, payload, payloadLength, bytes, sizeof(bytes), length) &&
           sendFrame(TxKind::Heartbeat, bytes, length);
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
    size_t length = 0;
    if (!encodeFrame(header, payload, payloadLength, pending_.frame.data(),
                     pending_.frame.size(), length) ||
        !sendFrame(TxKind::Action, pending_.frame.data(), length)) return false;
    pending_.action = action;
    pending_.wireSequence = header.sequence;
    pending_.awaitingSnapshot = false;
    pending_.resyncRequested = false;
    pending_.frameLength = static_cast<uint16_t>(length);
    pending_.lastSendMs = millis();
    pending_.sendAttempts = 1;
    Serial.printf(
        "GRIDOPOLY_COMMAND sent kind=%u action=%u request=%lu wire=%lu version=%lu attempt=1\n",
        static_cast<unsigned>(pending_.command.kind), static_cast<unsigned>(action),
        static_cast<unsigned long>(pending_.command.requestId),
        static_cast<unsigned long>(pending_.wireSequence),
        static_cast<unsigned long>(stateVersion)
    );
    return true;
}

bool EspNowPlayerTransport::beginPlayerDetailQuery(const TransportCommand &command,
                                                   uint32_t nowMs)
{
    if (!snapshotValid_ || command.requestId == 0 || command.targetPlayerId == 0 ||
        command.targetPlayerId > snapshot_.playerCount) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::ActionNotAllowed;
        rejected.requestId = command.requestId;
        rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
        push(rejected);
        return true;
    }

    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
    pendingPlayerDetail_.active = true;
    pendingPlayerDetail_.command = command;
    pendingPlayerDetail_.startedMs = nowMs;

    PlayerDetailRequest request{};
    request.requestId = command.requestId;
    request.targetPlayerId = command.targetPlayerId;
    request.expectedStateVersion = command.stateVersion;
    uint8_t payload[kPlayerDetailRequestSize]{};
    size_t payloadLength = 0;
    if (!encodePlayerDetailRequest(request, payload, sizeof(payload), payloadLength)) {
        rejectPlayerDetailQuery(TransportError::StaleState);
        return true;
    }

    Header header{};
    header.type = MessageType::PlayerDetailRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    size_t frameLength = 0;
    if (!encodeFrame(header, payload, payloadLength, pendingPlayerDetail_.frame.data(),
                     pendingPlayerDetail_.frame.size(), frameLength) ||
        !sendFrame(TxKind::Query, pendingPlayerDetail_.frame.data(), frameLength)) {
        rejectPlayerDetailQuery(TransportError::StaleState);
        return true;
    }

    pendingPlayerDetail_.wireSequence = header.sequence;
    pendingPlayerDetail_.frameLength = static_cast<uint16_t>(frameLength);
    pendingPlayerDetail_.lastSendMs = nowMs;
    pendingPlayerDetail_.sendAttempts = 1;
    Serial.printf(
        "GRIDOPOLY_DETAIL sent request=%lu player=%u wire=%lu version=%lu attempt=1\n",
        static_cast<unsigned long>(command.requestId), command.targetPlayerId,
        static_cast<unsigned long>(header.sequence),
        static_cast<unsigned long>(command.stateVersion)
    );
    return true;
}

bool EspNowPlayerTransport::resendPlayerDetailQuery(uint32_t nowMs)
{
    if (!pendingPlayerDetail_.active || pendingPlayerDetail_.frameLength == 0) return false;
    pendingPlayerDetail_.lastSendMs = nowMs;
    ++pendingPlayerDetail_.sendAttempts;
    const bool sent = sendFrame(TxKind::Query, pendingPlayerDetail_.frame.data(),
                                 pendingPlayerDetail_.frameLength);
    Serial.printf(
        "GRIDOPOLY_DETAIL retry request=%lu wire=%lu attempt=%u sent=%u\n",
        static_cast<unsigned long>(pendingPlayerDetail_.command.requestId),
        static_cast<unsigned long>(pendingPlayerDetail_.wireSequence),
        pendingPlayerDetail_.sendAttempts, sent ? 1u : 0u
    );
    return sent;
}

void EspNowPlayerTransport::rejectPlayerDetailQuery(TransportError error)
{
    if (!pendingPlayerDetail_.active) return;
    Serial.printf(
        "GRIDOPOLY_DETAIL rejected request=%lu error=%u\n",
        static_cast<unsigned long>(pendingPlayerDetail_.command.requestId),
        static_cast<unsigned>(error)
    );
    TransportEvent rejected{};
    rejected.kind = TransportEventKind::CommandRejected;
    rejected.error = error;
    rejected.requestId = pendingPlayerDetail_.command.requestId;
    rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
    push(rejected);
    pendingPlayerDetail_ = PendingPlayerDetailQuery{};
}

bool EspNowPlayerTransport::beginTradeRequest(const TransportCommand &command,
                                               uint32_t nowMs)
{
    const bool mutation = command.tradeOperation != TransportTradeOperation::Query;
    if (!snapshotValid_ || command.requestId == 0 ||
        (mutation && command.stateVersion == 0)) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::StaleState;
        rejected.requestId = command.requestId;
        rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
        push(rejected);
        return true;
    }
    pendingTrade_ = PendingTradeRequest{};
    pendingTrade_.active = true;
    pendingTrade_.command = command;
    pendingTrade_.startedMs = nowMs;
    if (!sendTradeRequest(nowMs)) rejectTradeRequest(TransportError::StaleState);
    return true;
}

bool EspNowPlayerTransport::sendTradeRequest(uint32_t nowMs)
{
    if (!pendingTrade_.active) return false;
    const TransportCommand &command = pendingTrade_.command;
    TradeRequest request{};
    request.operation = static_cast<TradeOperation>(command.tradeOperation);
    request.targetPlayerId = command.targetPlayerId;
    request.expectedRevision = command.tradeRevision;
    request.requestId = command.requestId;
    request.expectedStateVersion = command.stateVersion;
    request.tradeId = command.tradeId;
    request.selfGivesCash = command.argument;
    request.counterpartyGivesCash = command.counterpartyArgument;
    for (uint8_t assetIndex = 0; assetIndex < 28; ++assetIndex) {
        const uint32_t bit = static_cast<uint32_t>(1u) << assetIndex;
        if ((command.assetMask & bit) != 0) {
            request.selfAssets[request.selfAssetCount++] = assetIndex;
        }
        if ((command.counterpartyAssetMask & bit) != 0) {
            request.counterpartyAssets[request.counterpartyAssetCount++] = assetIndex;
        }
    }

    uint8_t payload[kMaxTradeRequestSize]{};
    size_t payloadLength = 0;
    if (!encodeTradeRequest(request, payload, sizeof(payload), payloadLength)) return false;
    Header header{};
    header.type = MessageType::TradeRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    std::array<uint8_t, kMaxFrameSize> frame{};
    size_t frameLength = 0;
    if (!encodeFrame(header, payload, payloadLength, frame.data(), frame.size(), frameLength)) {
        return false;
    }

    pendingTrade_.wireSequence = header.sequence;
    pendingTrade_.lastSendMs = nowMs;
    ++pendingTrade_.sendAttempts;
    const bool sent = sendFrame(TxKind::Trade, frame.data(), frameLength);
    Serial.printf(
        "GRIDOPOLY_TRADE sent request=%lu trade=%lu revision=%u op=%u wire=%lu attempt=%u sent=%u\n",
        static_cast<unsigned long>(command.requestId),
        static_cast<unsigned long>(command.tradeId), command.tradeRevision,
        static_cast<unsigned>(command.tradeOperation),
        static_cast<unsigned long>(header.sequence), pendingTrade_.sendAttempts,
        sent ? 1u : 0u
    );
    return sent;
}

void EspNowPlayerTransport::rejectTradeRequest(TransportError error)
{
    if (!pendingTrade_.active) return;
    TransportEvent rejected{};
    rejected.kind = TransportEventKind::CommandRejected;
    rejected.error = error;
    rejected.requestId = pendingTrade_.command.requestId;
    rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
    push(rejected);
    pendingTrade_ = PendingTradeRequest{};
}

bool EspNowPlayerTransport::beginIdentityRequest(const TransportCommand &command,
                                                  uint32_t nowMs)
{
    const bool mutation = command.identityOperation != TransportIdentityOperation::Query;
    if (command.requestId == 0 || seatId_ == 0 ||
        (mutation && (command.stateVersion == 0 || command.identitySeatRevision == 0 ||
                      command.identitySeatRevision > UINT16_MAX))) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::StaleState;
        rejected.requestId = command.requestId;
        rejected.stateVersion = appliedStateVersion_;
        push(rejected);
        return true;
    }
    pendingIdentity_ = PendingIdentityRequest{};
    pendingIdentity_.active = true;
    pendingIdentity_.command = command;
    pendingIdentity_.startedMs = nowMs;
    if (!sendIdentityRequest(nowMs)) rejectIdentityRequest(TransportError::StaleState);
    return true;
}

bool EspNowPlayerTransport::sendIdentityRequest(uint32_t nowMs)
{
    if (!pendingIdentity_.active) return false;
    const TransportCommand &command = pendingIdentity_.command;
    IdentityRequest request{};
    request.operation = static_cast<IdentityOperation>(command.identityOperation);
    request.playerId = seatId_;
    request.requestId = command.requestId;
    if (command.identityOperation == TransportIdentityOperation::ConfirmAvatar) {
        request.expectedStateVersion = command.stateVersion;
        request.expectedSeatRevision = static_cast<uint16_t>(command.identitySeatRevision);
        request.avatarCatalogVersion = command.avatarRecipe.catalogVersion;
        request.recipe = toWireRecipe(command.avatarRecipe);
    } else if (command.identityOperation == TransportIdentityOperation::ConfirmName) {
        request.expectedStateVersion = command.stateVersion;
        request.expectedSeatRevision = static_cast<uint16_t>(command.identitySeatRevision);
        const size_t nameLength = strnlen(command.identityName, 16);
        request.nameLength = static_cast<uint8_t>(nameLength);
        memcpy(request.name.data(), command.identityName, nameLength);
    }

    uint8_t payload[kIdentityRequestSize]{};
    size_t payloadLength = 0;
    if (!encodeIdentityRequest(request, payload, sizeof(payload), payloadLength)) return false;
    Header header{};
    header.type = MessageType::IdentityRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    std::array<uint8_t, kMaxFrameSize> frame{};
    size_t frameLength = 0;
    if (!encodeFrame(header, payload, payloadLength, frame.data(), frame.size(), frameLength)) {
        return false;
    }

    pendingIdentity_.wireSequence = header.sequence;
    pendingIdentity_.lastSendMs = nowMs;
    ++pendingIdentity_.sendAttempts;
    const bool sent = sendFrame(TxKind::Identity, frame.data(), frameLength);
    Serial.printf(
        "GRIDOPOLY_IDENTITY sent request=%lu op=%u seat_revision=%u wire=%lu attempt=%u sent=%u\n",
        static_cast<unsigned long>(command.requestId),
        static_cast<unsigned>(command.identityOperation),
        static_cast<unsigned>(command.identitySeatRevision),
        static_cast<unsigned long>(header.sequence), pendingIdentity_.sendAttempts,
        sent ? 1u : 0u
    );
    return sent;
}

void EspNowPlayerTransport::rejectIdentityRequest(TransportError error)
{
    if (!pendingIdentity_.active) return;
    TransportEvent rejected{};
    rejected.kind = TransportEventKind::CommandRejected;
    rejected.error = error;
    rejected.requestId = pendingIdentity_.command.requestId;
    rejected.stateVersion = appliedStateVersion_;
    push(rejected);
    pendingIdentity_ = PendingIdentityRequest{};
}

bool EspNowPlayerTransport::beginPendingAction(const TransportCommand &command, uint32_t nowMs)
{
    pending_ = PendingAction{};
    pending_.active = true;
    pending_.command = command;
    pending_.previousPosition = snapshot_.selfPosition;
    pending_.startedMs = nowMs;
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
        case TransportCommandKind::BuyRequest:
            sent = sendAction(ActionCode::Buy, 0xFF, 0, snapshot_.stateVersion);
            break;
        case TransportCommandKind::DeclinePurchaseRequest:
            sent = sendAction(ActionCode::Decline, 0xFF, 0, snapshot_.stateVersion);
            break;
        case TransportCommandKind::EndTurnRequest:
            sent = sendAction(ActionCode::EndTurn, 0xFF, 0, snapshot_.stateVersion);
            break;
        case TransportCommandKind::UnmortgageRequest:
            sent = sendAction(ActionCode::Unmortgage, command.assetIndex, 0,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::BuildRequest:
            sent = sendAction(ActionCode::Build, command.assetIndex, 0,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::SellBuildingRequest:
            sent = sendAction(ActionCode::SellBuilding, command.assetIndex, 0,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::DeclareBankruptcyRequest:
            sent = sendAction(ActionCode::DeclareBankruptcy, 0xFF, 0,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::AuctionBidRequest:
            sent = sendAction(ActionCode::AuctionBid, command.assetIndex,
                              command.argument, snapshot_.stateVersion);
            break;
        case TransportCommandKind::AuctionPassRequest:
            sent = sendAction(ActionCode::AuctionPass, command.assetIndex, 0,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::AuctionReadyRequest:
            sent = sendAction(ActionCode::AuctionReady, command.assetIndex,
                              command.argument, command.stateVersion);
            break;
        case TransportCommandKind::CardContinueRequest:
            sent = sendAction(ActionCode::CardContinue, 0xFF, command.argument,
                              snapshot_.stateVersion);
            break;
        case TransportCommandKind::PlayerDetailRequest:
        case TransportCommandKind::TradeQuery:
        case TransportCommandKind::ClaimRent:
        case TransportCommandKind::TradeCreate:
        case TransportCommandKind::TradeUpdate:
        case TransportCommandKind::TradeConfirm:
        case TransportCommandKind::TradeReject:
        case TransportCommandKind::TradeCancel:
        case TransportCommandKind::IdentityRequest:
            rejectPending(TransportError::ActionNotAllowed);
            return true;
    }
    if (!sent) rejectPending(TransportError::StaleState);
    return true;
}

bool EspNowPlayerTransport::resendPendingAction(uint32_t nowMs)
{
    if (!pending_.active || pending_.awaitingSnapshot || pending_.frameLength == 0) return false;
    if (!sendFrame(TxKind::Action, pending_.frame.data(), pending_.frameLength)) {
        pending_.lastSendMs = nowMs;
        Serial.printf(
            "GRIDOPOLY_COMMAND retry_failed request=%lu attempt=%u\n",
            static_cast<unsigned long>(pending_.command.requestId),
            static_cast<unsigned>(pending_.sendAttempts + 1)
        );
        ++pending_.sendAttempts;
        return false;
    }
    pending_.lastSendMs = nowMs;
    ++pending_.sendAttempts;
    Serial.printf(
        "GRIDOPOLY_COMMAND retry request=%lu wire=%lu attempt=%u\n",
        static_cast<unsigned long>(pending_.command.requestId),
        static_cast<unsigned long>(pending_.wireSequence), pending_.sendAttempts
    );
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
        // A destination distance cannot recover the original pair of dice:
        // hold release, wrapping and movement cards all break that inference.
        // Exact dice arrive through GameEvent or full Authority metadata.
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
    Serial.printf(
        "GRIDOPOLY_COMMAND rejected request=%lu error=%u\n",
        static_cast<unsigned long>(pending_.command.requestId), static_cast<unsigned>(error)
    );
    TransportEvent rejected{};
    rejected.kind = TransportEventKind::CommandRejected;
    rejected.error = error;
    rejected.requestId = pending_.command.requestId;
    rejected.transactionId = pending_.command.transactionId;
    rejected.stateVersion = snapshotValid_ ? snapshot_.stateVersion : 0;
    push(rejected);
    pending_ = PendingAction{};
}
