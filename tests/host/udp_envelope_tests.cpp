#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <gridopoly/protocol/Protocol.h>
#include <gridopoly/protocol/UdpEnvelope.h>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

}  // namespace

int main() {
  using namespace gridopoly::protocol;

  std::array<std::uint8_t, kUdpKeySize> pairKey{};
  std::array<std::uint8_t, kUdpKeySize> sessionKey{};
  deriveUdpPairKey("gridopoly", pairKey);
  deriveUdpSessionKey(pairKey, 0x11223344u, 0xaabbccddu, 0x01020304u,
                      0x55667788u, 0x10203040u, sessionKey);
  expect(pairKey != sessionKey, "pair and session keys differ");

  Header frameHeader{};
  frameHeader.type = MessageType::Heartbeat;
  frameHeader.sequence = 7;
  frameHeader.roomId = 0x55667788u;
  frameHeader.deviceId = 0xaabbccddu;
  Heartbeat heartbeat{};
  heartbeat.appliedStateVersion = 42;
  heartbeat.appliedEventSequence = 17;
  std::array<std::uint8_t, kMaxPayloadSize> payload{};
  std::size_t payloadLength = 0;
  expect(encodeHeartbeat(heartbeat, payload.data(), payload.size(), payloadLength),
         "encode heartbeat");
  std::array<std::uint8_t, kMaxFrameSize> frame{};
  std::size_t frameLength = 0;
  expect(encodeFrame(frameHeader, payload.data(), payloadLength, frame.data(), frame.size(),
                     frameLength), "encode inner frame");

  UdpEnvelopeHeader envelope{};
  envelope.sessionId = 0x10203040u;
  envelope.senderDeviceId = 0xaabbccddu;
  envelope.packetSequence = 0x0102030405060708ull;
  std::array<std::uint8_t, kMaxUdpDatagramSize> datagram{};
  std::size_t datagramLength = 0;
  expect(encodeUdpDatagram(envelope, sessionKey, frame.data(), frameLength, datagram.data(),
                           datagram.size(), datagramLength), "encode UDP envelope");
  expect(datagramLength == kUdpEnvelopeHeaderSize + frameLength, "UDP envelope length");

  DecodedUdpDatagram decoded{};
  expect(decodeUdpDatagram(datagram.data(), datagramLength, sessionKey, decoded),
         "decode authenticated UDP envelope");
  expect(decoded.header.sessionId == envelope.sessionId, "session id round trip");
  expect(decoded.header.senderDeviceId == envelope.senderDeviceId, "device id round trip");
  expect(decoded.header.packetSequence == envelope.packetSequence, "packet sequence round trip");
  DecodedFrame decodedFrame{};
  expect(decodeFrame(decoded.frame, decoded.header.frameLength, decodedFrame),
         "decode inner frame after envelope");

  auto corrupted = datagram;
  corrupted[kUdpEnvelopeHeaderSize + 3] ^= 0x40u;
  expect(!decodeUdpDatagram(corrupted.data(), datagramLength, sessionKey, decoded),
         "tampered frame rejected by HMAC");
  corrupted = datagram;
  corrupted[12] ^= 0x01u;
  expect(!decodeUdpDatagram(corrupted.data(), datagramLength, sessionKey, decoded),
         "tampered sender rejected by HMAC");
  std::array<std::uint8_t, kUdpKeySize> wrongKey{};
  deriveUdpPairKey("wrong-password", wrongKey);
  expect(!decodeUdpDatagram(datagram.data(), datagramLength, wrongKey, decoded),
         "wrong key rejected");

  UdpReplayWindow replay;
  expect(replay.accept(10), "accept first packet");
  expect(replay.accept(12), "accept newer packet");
  expect(replay.accept(11), "accept one reordered packet");
  expect(!replay.accept(11), "reject duplicate reordered packet");
  expect(!replay.accept(10), "reject already seen packet");
  expect(replay.accept(80), "accept packet beyond replay window");
  expect(!replay.accept(12), "reject stale packet outside replay window");
  replay.reset();
  expect(replay.accept(1), "reset replay window");

  if (failures != 0) return 1;
  std::cout << "GRIDOPOLY_UDP_ENVELOPE_TESTS_PASS\n";
  return 0;
}
