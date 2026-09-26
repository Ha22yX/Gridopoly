#include <array>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <gridopoly/protocol/UdpEnvelope.h>
#include "app_state.h"
#define private public
#define protected public
#include "wifi_udp_player_transport.h"
#undef private
#undef protected
#include <cstdio>

// Extracted production functions handle admission and authenticate the real UDP
// envelope. Only unrelated radio scheduling and other message types are stubbed.
void EspNowPlayerTransport::begin(uint32_t) {}
bool EspNowPlayerTransport::send(const TransportCommand &, uint32_t) { return false; }
void EspNowPlayerTransport::tick(uint32_t) {}
bool EspNowPlayerTransport::sendFrame(TxKind, const uint8_t *, size_t) { return true; }
bool EspNowPlayerTransport::sendHeartbeat() { return true; }
void WifiUdpPlayerTransport::begin(uint32_t) {}
void WifiUdpPlayerTransport::tick(uint32_t) {}
bool WifiUdpPlayerTransport::sendFrame(TxKind, const uint8_t *, size_t) { return true; }
void WifiUdpPlayerTransport::processUdpDiscover(const gridopoly::protocol::DecodedFrame &, uint32_t) {}
void WifiUdpPlayerTransport::processUdpFrame(const gridopoly::protocol::DecodedFrame &, uint32_t) {}

using namespace gridopoly::protocol;
void configure(WifiUdpPlayerTransport &t) {
    t.ready_ = true; t.linkState_ = EspNowPlayerTransport::LinkState::Pairing;
    t.serverDeviceId_ = 900; t.deviceId_ = 400; t.deviceNonce_ = 123;
    t.roomId_ = 700; t.nextSequence_ = 12;
    deriveUdpPairKey("synthetic-admission-test-key", t.pairKey_);
}
void receive(WifiUdpPlayerTransport &t, bool accepted, bool tamper = false,
             uint32_t ack = 11, uint32_t server = 900, uint8_t seat = 2,
             uint32_t room = 700, uint16_t port = kGridopolyUdpPort) {
    PairAccept response{}; response.accepted = accepted ? 1 : 0;
    response.seatId = accepted ? seat : 0; response.serverDeviceId = server; response.sessionId = 88;
    uint8_t payload[kMaxPayloadSize]{}; size_t payloadLength = 0;
    encodePairAccept(response, payload, sizeof(payload), payloadLength);
    Header header{}; header.type = MessageType::PairAccept; header.deviceId = 900;
    header.roomId = room; header.acknowledgement = ack;
    uint8_t frame[kMaxFrameSize]{}; size_t frameLength = 0;
    encodeFrame(header, payload, payloadLength, frame, sizeof(frame), frameLength);
    UdpEnvelopeHeader udp{}; udp.flags = UdpFlagPairingKey; udp.sessionId = 88;
    udp.senderDeviceId = 900; udp.packetSequence = 7;
    uint8_t bytes[kMaxUdpDatagramSize]{}; size_t length = 0;
    encodeUdpDatagram(udp, t.pairKey_, frame, frameLength, bytes, sizeof(bytes), length);
    if (tamper) bytes[length - 1] ^= 0x80;
    t.processDatagram(bytes, length, IPAddress(10, 42, 0, 1), port, 1000);
}
TransportJoinStatus drain(WifiUdpPlayerTransport &t, AppState *app = nullptr) {
    TransportEvent event{}; auto status = TransportJoinStatus::Inactive;
    while (t.poll(event)) {
        if (event.kind == TransportEventKind::JoinStatusChanged) status = event.joinStatus;
        if (app) appHandleTransportEvent(*app, event, 1000);
    }
    return status;
}
int main() {
    int failed = 0, checks = 0;
    auto check = [&](bool value, const char *name) {
        ++checks; if (!value) ++failed;
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
    };
    WifiUdpPlayerTransport t; configure(t);
    TransportEvent old{}; while (t.push(old)) {}
    AppState app{}; appInit(app, 0, true);
    receive(t, false);
    check(t.sessionId_ == 0 && t.linkState_ == EspNowPlayerTransport::LinkState::Scanning && t.eventCount_ == 2,
          "authenticated refusal clears old full queue then reliably queues connecting and refusal");
    check(drain(t, &app) == TransportJoinStatus::WaitingForNewGame &&
          app.page == ScreenPage::WaitingForGame && app.joinStatus == TransportJoinStatus::WaitingForNewGame,
          "real authenticated denial reaches persistent new-game app state after transport reset");
    configure(t); receive(t, false);
    check(drain(t, &app) == TransportJoinStatus::WaitingForNewGame && app.money == 0,
          "repeated authenticated denials never expose initialization cash");
    configure(t); receive(t, false, true);
    check(t.eventCount_ == 0 && t.linkState_ == EspNowPlayerTransport::LinkState::Pairing,
          "tampered UDP authentication cannot display a new-game refusal");
    receive(t, false, false, 10);
    check(t.eventCount_ == 0, "old PairAccept acknowledgement cannot change admission page");
    receive(t, false, false, 11, 900, 2, 699);
    check(t.eventCount_ == 0, "other room PairAccept cannot change admission page");
    receive(t, false, false, 11, 901);
    check(drain(t) == TransportJoinStatus::Connecting,
          "malformed server identity is a reconnect condition rather than an authenticated admission refusal");
    configure(t); receive(t, true);
    check(t.sessionId_ == 88 && t.seatId_ == 2 && t.linkState_ == EspNowPlayerTransport::LinkState::AwaitSnapshot &&
          drain(t, &app) == TransportJoinStatus::Syncing && appAwaitingGame(app),
          "true acceptance establishes session but keeps app waiting until authoritative state arrives");
    configure(t); receive(t, true, false, 11, 900, 0);
    check(drain(t) == TransportJoinStatus::Connecting, "accepted response with invalid seat never implies a new-game refusal");
    configure(t); receive(t, false, false, 11, 900, 2, 700, 4243);
    check(t.eventCount_ == 0, "wrong UDP endpoint cannot change admission page");
    std::printf("%d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
