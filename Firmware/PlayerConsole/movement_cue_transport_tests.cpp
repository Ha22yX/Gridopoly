#include "app_config.h"
#if GRIDOPOLY_SELF_TEST == 1
#include "espnow_player_transport.h"
#include "test_fixture.h"
#include <cstring>
namespace {
using namespace gridopoly::protocol;
class CueTransportFixture : public EspNowPlayerTransport {
public:
    uint32_t sent = 0;
    size_t lastLength = 0;
    std::array<uint8_t, kMaxFrameSize> lastFrame{};
    void configure() {
        ready_ = snapshotValid_ = true;
        linkState_ = LinkState::Online;
        roomId_ = 123; seatId_ = 1; deviceId_ = 456;
        nextSequence_ = 20;
        snapshot_.seatId = snapshot_.activePlayerId = 1;
        snapshot_.phase = 2; snapshot_.pendingTarget = 7;
        snapshot_.stateVersion = 100;
        pending_ = PendingAction{}; pendingMovementCue_ = PendingAction{};
        eventHead_ = eventTail_ = eventCount_ = 0;
        sent = 0;
    }
    bool active() const { return pendingMovementCue_.active; }
    bool ordinaryActive() const { return pending_.active; }
    void retry(uint32_t now) { tickMovementCueReady(now); }
    void reconnect() {
        // The reset paths preserve cue storage; projections are unavailable until paired.
        clearProjection(true); linkState_ = LinkState::Scanning;
        retry(2000);
        linkState_ = LinkState::Online; snapshotValid_ = true;
        snapshot_.phase = 2; snapshot_.pendingTarget = 7;
        snapshot_.activePlayerId = snapshot_.seatId = 1; snapshot_.stateVersion = 100;
    }
    void heartbeat() { sendHeartbeat(); }
    void resync(uint32_t acknowledged) {
        snapshot_.boardSize = 24; snapshot_.selfPosition = 3; snapshot_.playerCount = 2;
        snapshot_.players[0].playerId = 1; snapshot_.players[1].playerId = 2;
        uint8_t payload[kMaxPayloadSize]{};
        size_t length = 0;
        encodeStateSnapshot(snapshot_, payload, sizeof(payload), length);
        DecodedFrame frame{};
        frame.header.roomId = 123; frame.header.flags = FlagResync;
        frame.header.acknowledgement = acknowledged;
        frame.header.payloadLength = static_cast<uint16_t>(length); frame.payload = payload;
        processSnapshot(frame, 1000);
    }
    bool takeRetry(TransportEvent &retry) {
        TransportEvent next{};
        bool found = false;
        while (poll(next)) if (next.kind == TransportEventKind::MovementCueRetryRequested) {
            retry = next; found = true;
        }
        return found;
    }
    void phase(uint8_t value) { snapshot_.phase = value; }
    void room(uint32_t value) { roomId_ = value; }
    void target(uint8_t value) { snapshot_.pendingTarget = value; }
    void result(uint32_t sequence, uint8_t code = 0) {
        uint8_t payload[12]{1, code, 1, 0, 100, 0, 0, 0};
        for (int i = 0; i < 4; ++i) payload[8+i] = static_cast<uint8_t>(sequence >> (8*i));
        DecodedFrame frame{};
        frame.header.roomId = 123; frame.header.acknowledgement = sequence;
        frame.header.payloadLength = 12; frame.payload = payload;
        processActionResult(frame);
    }
protected:
    bool sendFrame(TxKind, const uint8_t *bytes, size_t length) override {
        ++sent; lastLength = length; std::memcpy(lastFrame.data(), bytes, length); return true;
    }
};
}
bool runMovementCueTransportTests(Stream &out)
{
    TestFixture<CueTransportFixture> storage;
    if (!storage) {
        out.println("[FAIL] cue transport: fixture allocation");
        return false;
    }
    CueTransportFixture &fixture = *storage;
    fixture.configure();
    bool ok = true;
    auto check = [&](bool pass, const char *name) {
        out.printf("[%s] cue transport: %s\n", pass ? "PASS" : "FAIL", name);
        ok &= pass;
    };
    TransportCommand cue{};
    cue.kind = TransportCommandKind::MovementCueReadyRequest;
    cue.requestId = 10; cue.roomId = 123; cue.stateVersion = 100;
    cue.argument = cue.targetPosition = 7;
    fixture.send(cue, 100);
    const auto original = fixture.lastFrame;
    const auto length = fixture.lastLength;
    DecodedFrame decoded{};
    ActionRequest request{};
    check(decodeFrame(original.data(), length, decoded) &&
        decodeActionRequest(decoded.payload, decoded.header.payloadLength, request) &&
        request.action == ActionCode::MovementCueReady && request.assetIndex == 0xFF &&
        request.argument == 7 && request.expectedStateVersion == 100,
        "exact authenticated action fields");
    const uint32_t sequence = decoded.header.sequence;
    fixture.retry(549);
    check(fixture.sent == 1, "bounded retry interval");
    fixture.retry(550);
    check(fixture.sent == 2 && fixture.lastLength == length && fixture.lastFrame == original,
        "lost result retries identical inner sequence and payload");
    fixture.reconnect();
    fixture.retry(2100);
    check(fixture.active() && fixture.lastFrame == original && fixture.sent == 3,
        "reconnect replays same inner request after projection recovery");
    TransportCommand manual{};
    manual.kind = TransportCommandKind::MoveManualConfirmRequest;
    manual.requestId = 11; manual.targetPosition = 7; manual.stateVersion = 100;
    fixture.send(manual, 2150);
    check(fixture.active() && fixture.ordinaryActive(), "manual confirmation has independent pending");
    fixture.result(sequence + 100);
    check(fixture.active(), "unrelated result cannot acknowledge cue");
    fixture.result(sequence);
    TransportEvent event{};
    check(!fixture.active() && fixture.poll(event) &&
        event.kind == TransportEventKind::CommandCompleted && event.requestId == 10,
        "matching result completes without snapshot advance");
    fixture.configure(); fixture.send(cue, 100); fixture.phase(6); fixture.retry(600);
    check(!fixture.active() && fixture.sent == 1, "phase change stops retries");
    fixture.configure(); fixture.send(cue, 100); fixture.room(124); fixture.retry(600);
    check(!fixture.active() && fixture.sent == 1, "room change stops retries");
    fixture.configure(); fixture.send(cue, 100); fixture.target(8); fixture.retry(600);
    check(!fixture.active() && fixture.sent == 1, "target change stops retries");
    fixture.configure(); fixture.send(cue, 100);
    const auto lostOriginal = fixture.lastFrame;
    const size_t lostLength = fixture.lastLength;
    decodeFrame(lostOriginal.data(), lostLength, decoded);
    const uint32_t lostSequence = decoded.header.sequence;
    fixture.heartbeat(); // Newer request overtakes the intentionally lost cue.
    fixture.retry(550);
    fixture.resync(lostSequence + 1);
    TransportEvent retry{};
    check(fixture.active() && !fixture.takeRetry(retry),
        "ordinary resync and newer ack alone cannot renew the request");
    fixture.retry(1000);
    check(fixture.lastFrame == lostOriginal,
        "recovery probe retains the original inner request");
    fixture.resync(lostSequence + 1);
    check(!fixture.active() && fixture.takeRetry(retry) && retry.requestId == cue.requestId,
        "resync after immutable probe retires an overtaken lost request");
    cue.requestId = 12;
    fixture.send(cue, 1500);
    decodeFrame(fixture.lastFrame.data(), fixture.lastLength, decoded);
    const uint32_t renewedSequence = decoded.header.sequence;
    check(renewedSequence > lostSequence + 1,
        "renewed logical operation uses a fresh inner sequence");
    fixture.result(renewedSequence);
    check(!fixture.active(), "renewed request can finish at the same state version");

    fixture.configure(); fixture.send(cue, 100);
    decodeFrame(fixture.lastFrame.data(), fixture.lastLength, decoded);
    const uint32_t cachedSequence = decoded.header.sequence;
    fixture.heartbeat(); fixture.retry(550); fixture.resync(cachedSequence + 1);
    fixture.result(cachedSequence); // Cache hit after an application-lost result.
    check(!fixture.active() && !fixture.takeRetry(retry),
        "cached success wins over a pending recovery probe without renewal");

    fixture.configure(); fixture.send(cue, 100);
    decodeFrame(fixture.lastFrame.data(), fixture.lastLength, decoded);
    const uint32_t rejectedSequence = decoded.header.sequence;
    fixture.result(rejectedSequence, 2);
    fixture.resync(rejectedSequence + 1);
    check(!fixture.active() && fixture.takeRetry(retry),
        "same-version rejection resumes only after authenticated resync");
    return ok;
}
#endif
