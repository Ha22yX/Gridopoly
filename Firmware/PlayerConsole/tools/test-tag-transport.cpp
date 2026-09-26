#include "espnow_player_transport.h"
#include <cstdio>

// Only radio I/O and scheduling are stubbed. The production TagPair parser,
// retry logic, codec, event queue push and poll are compiled by the runner.
void EspNowPlayerTransport::begin(uint32_t) {}
bool EspNowPlayerTransport::send(const TransportCommand &, uint32_t) { return false; }
void EspNowPlayerTransport::tick(uint32_t) {}
bool EspNowPlayerTransport::sendFrame(TxKind, const uint8_t *, size_t) { return true; }

class Fixture : public EspNowPlayerTransport {
public:
    Fixture() { ready_ = true; linkState_ = LinkState::Online; roomId_ = 100; seatId_ = 1; }
    void fill() { TransportEvent event{}; while (push(event)) {} }
    void drain() { TransportEvent event{}; while (poll(event)) {} }
    bool epochPending() const { return tagPairEpochPending_; }
    bool gap() const { return eventGapDetected_; }
    uint32_t revision() const { return tagPairSnapshot_.revision; }
    uint8_t count() const { return eventCount_; }
    void receive(uint32_t revision, uint32_t room = 100, uint8_t self = 1,
                 uint32_t request = 0, bool resync = false) {
        using namespace gridopoly::protocol;
        TagPairSnapshot wire{}; wire.roomId = room; wire.selfPlayerId = self;
        wire.revision = revision; wire.sessionId = 200; wire.requestId = request;
        wire.state = TagPairState::AwaitingTag; wire.flags = TagPairFlagEnabled;
        uint8_t payload[kTagPairSnapshotSize]{}; size_t length = 0;
        encodeTagPairSnapshot(wire, payload, sizeof(payload), length);
        DecodedFrame frame{}; frame.header.roomId = room; frame.payload = payload;
        frame.header.payloadLength = static_cast<uint16_t>(length);
        if (resync) frame.header.flags |= FlagResync;
        processTagPairSnapshot(frame, 1000 + revision);
    }
};

int main() {
    int failed = 0, checks = 0;
    auto check = [&](bool value, const char *name) {
        ++checks; if (!value) ++failed;
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
    };
    Fixture congested; congested.fill(); congested.receive(1);
    check(congested.epochPending() && congested.gap() && congested.revision() == 1,
          "queue overflow retains new authenticated epoch marker while caching accepted revision");
    congested.drain(); congested.receive(2); TransportEvent event{};
    check(congested.poll(event) && event.tagPairEpochReset && event.resync && event.tagPair->revision == 2,
          "next accepted snapshot delivers retained epoch reset after congestion clears");
    check(!congested.epochPending(), "successful queue delivery consumes pending epoch marker");
    congested.receive(1, 100, 1, 0, true);
    check(congested.count() == 0 && congested.revision() == 2,
          "same authenticated epoch rejects older revision even with wire Resync");
    Fixture coalesced; coalesced.receive(40); coalesced.receive(41);
    check(coalesced.count() == 1 && coalesced.poll(event) && event.tagPairEpochReset &&
          event.tagPair->revision == 41,
          "coalesced snapshots preserve original epoch marker and expose latest matching payload");
    coalesced.receive(999, 99); coalesced.receive(999, 100, 2); coalesced.receive(999, 100, 1, 123);
    check(coalesced.count() == 0 && coalesced.revision() == 41,
          "old room, wrong seat and unknown request response cannot replace retained projection");
    Fixture pendingOld; pendingOld.fill(); pendingOld.receive(40); pendingOld.receive(39, 100, 1, 0, true);
    check(pendingOld.epochPending() && pendingOld.revision() == 40,
          "undelivered epoch marker never bypasses transport revision ordering");
    std::printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
