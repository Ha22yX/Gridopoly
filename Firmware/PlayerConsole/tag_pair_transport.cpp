#include "espnow_player_transport.h"
#include "tag_pair_wire.h"

using namespace gridopoly::protocol;

bool EspNowPlayerTransport::beginTagPairRequest(const TransportCommand &command, uint32_t nowMs)
{
    if (!ready_ || linkState_ != LinkState::Online || command.requestId == 0 ||
        command.roomId != roomId_ || command.targetPlayerId != seatId_ ||
        (pendingTagPair_.active && !tagPairAutomaticQuery_)) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::StaleState;
        rejected.requestId = command.requestId;
        push(rejected);
        return true;
    }
    pendingTagPair_ = PendingIdentityRequest{};
    pendingTagPair_.active = true;
    pendingTagPair_.command = command;
    pendingTagPair_.startedMs = nowMs;
    tagPairAutomaticQuery_ = false;
    if (!sendTagPairRequest(nowMs)) finishTagPairRequest(false);
    return true;
}

bool EspNowPlayerTransport::sendTagPairRequest(uint32_t nowMs)
{
    if (!pendingTagPair_.active) return false;
    const auto &command = pendingTagPair_.command;
    TagPairRequest request{};
    request.operation = static_cast<TagPairOperation>(command.tagPairOperation);
    request.playerId = seatId_;
    request.requestId = command.requestId;
    request.roomId = command.roomId;
    request.sessionId = command.tagPairSessionId;
    request.expectedRevision = command.tagPairRevision;
    uint8_t payload[kTagPairRequestSize]{};
    size_t payloadLength = 0;
    if (!encodeTagPairRequest(request, payload, sizeof(payload), payloadLength)) return false;
    Header header{};
    header.type = MessageType::TagPairRequest;
    header.flags = FlagAckRequired;
    header.sequence = nextSequence_++;
    header.roomId = roomId_;
    header.deviceId = deviceId_;
    std::array<uint8_t, kMaxFrameSize> frame{};
    size_t length = 0;
    if (!encodeFrame(header, payload, payloadLength, frame.data(), frame.size(), length)) return false;
    pendingTagPair_.lastSendMs = nowMs;
    ++pendingTagPair_.sendAttempts;
    return sendFrame(TxKind::TagPair, frame.data(), length);
}

void EspNowPlayerTransport::finishTagPairRequest(bool accepted)
{
    if (!pendingTagPair_.active) return;
    if (!tagPairAutomaticQuery_) {
        TransportEvent result{};
        result.kind = accepted ? TransportEventKind::CommandCompleted : TransportEventKind::CommandRejected;
        result.error = accepted ? TransportError::None : TransportError::StaleState;
        result.requestId = pendingTagPair_.command.requestId;
        // TagPair revision is independent from game stateVersion.
        push(result);
    }
    pendingTagPair_ = PendingIdentityRequest{};
    tagPairAutomaticQuery_ = false;
}

void EspNowPlayerTransport::tickTagPair(uint32_t nowMs)
{
    if (pendingTagPair_.active) {
        if (nowMs - pendingTagPair_.startedMs >= 4000) {
            finishTagPairRequest(false);
        } else if (nowMs - pendingTagPair_.lastSendMs >= 700 && pendingTagPair_.sendAttempts < 4) {
            sendTagPairRequest(nowMs);
        }
        return;
    }
    if ((!identitySetupActive_ && tagPairSnapshotValid_) ||
        nowMs - lastTagPairQueryMs_ < 2000) return;
    lastTagPairQueryMs_ = nowMs;
    pendingTagPair_ = PendingIdentityRequest{};
    pendingTagPair_.active = true;
    pendingTagPair_.startedMs = nowMs;
    auto &command = pendingTagPair_.command;
    command.kind = TransportCommandKind::TagPairRequest;
    command.tagPairOperation = TransportTagPairOperation::Query;
    command.requestId = 0x80000000u | (nextSequence_ & 0x7fffffffu);
    command.roomId = roomId_;
    command.targetPlayerId = seatId_;
    tagPairAutomaticQuery_ = true;
    if (!sendTagPairRequest(nowMs)) finishTagPairRequest(false);
}

void EspNowPlayerTransport::processTagPairSnapshot(const DecodedFrame &frame, uint32_t nowMs)
{
    if (linkState_ != LinkState::AwaitSnapshot && linkState_ != LinkState::Online) return;
    TagPairSnapshot snapshot{};
    if (frame.header.roomId != roomId_ ||
        !decodeTagPairSnapshot(frame.payload, frame.header.payloadLength, snapshot) ||
        snapshot.roomId != roomId_ || snapshot.selfPlayerId != seatId_) return;
    if (snapshot.requestId != 0) {
        if (!pendingTagPair_.active || snapshot.requestId != pendingTagPair_.command.requestId) return;
        finishTagPairRequest(snapshot.result == TagPairResultCode::Ok);
    }
    const bool newEpoch = tagPairEpochPending_;
    if (tagPairSnapshotValid_ && tagPairRevisionIsOlder(snapshot.revision, tagPairSnapshot_.revision)) return;
    tagPairSnapshot_ = tagPairFromWire(snapshot);
    tagPairSnapshotValid_ = true;
    lastServerFrameMs_ = nowMs;
    TransportEvent event{};
    event.kind = TransportEventKind::TagPairSnapshotReceived;
    event.roomId = roomId_;
    event.selfSeatId = seatId_;
    event.requestId = snapshot.requestId;
    event.tagPair = &tagPairSnapshot_;
    event.tagPairEpochReset = newEpoch;
    event.resync = newEpoch || (snapshot.flags & TagPairFlagResync) != 0 || (frame.header.flags & FlagResync) != 0;
    // One retained payload, one queued event: prevent earlier headers referring
    // to a later payload when multiple snapshots arrive before the app polls.
    for (uint8_t offset = 0; offset < eventCount_; ++offset) {
        auto &queued = events_[(eventHead_ + offset) % kEventCapacity];
        if (queued.kind == TransportEventKind::TagPairSnapshotReceived) {
            event.tagPairEpochReset = event.tagPairEpochReset || queued.tagPairEpochReset;
            event.resync = event.resync || event.tagPairEpochReset;
            queued = event;
            tagPairEpochPending_ = false;
            return;
        }
    }
    if (!push(event)) eventGapDetected_ = true;
    else tagPairEpochPending_ = false;
    Serial.printf("GRIDOPOLY_TAG_PAIR room=%lu revision=%lu session=%lu self=%u active=%u state=%u flags=%u result=%u\n",
                  static_cast<unsigned long>(roomId_), static_cast<unsigned long>(snapshot.revision),
                  static_cast<unsigned long>(snapshot.sessionId), seatId_, snapshot.activePlayerId,
                  static_cast<unsigned>(snapshot.state), snapshot.flags, static_cast<unsigned>(snapshot.result));
}
