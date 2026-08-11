#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Protocol.h"

namespace gridopoly::protocol {

constexpr std::uint32_t kUdpEnvelopeMagic = 0x31555047u;  // "GPU1" on the wire.
constexpr std::uint8_t kUdpEnvelopeVersion = 1;
constexpr std::size_t kUdpEnvelopeHeaderSize = 48;
constexpr std::size_t kUdpAuthTagSize = 16;
constexpr std::size_t kUdpKeySize = 32;
constexpr std::size_t kMaxUdpDatagramSize = kUdpEnvelopeHeaderSize + kMaxFrameSize;
constexpr std::uint16_t kGridopolyUdpPort = 4242;

enum UdpEnvelopeFlags : std::uint8_t {
  UdpFlagNone = 0,
  UdpFlagPairingKey = 1u << 0,
  UdpFlagBroadcast = 1u << 1,
};

struct UdpEnvelopeHeader {
  std::uint8_t flags{};
  std::uint32_t sessionId{};
  std::uint32_t senderDeviceId{};
  std::uint64_t packetSequence{};
  std::uint16_t frameLength{};
};

struct DecodedUdpDatagram {
  UdpEnvelopeHeader header{};
  const std::uint8_t* frame{};
};

void deriveUdpPairKey(const char* psk, std::array<std::uint8_t, kUdpKeySize>& output);
void deriveUdpSessionKey(const std::array<std::uint8_t, kUdpKeySize>& pairKey,
                         std::uint32_t serverDeviceId, std::uint32_t clientDeviceId,
                         std::uint32_t deviceNonce, std::uint32_t roomId,
                         std::uint32_t sessionId,
                         std::array<std::uint8_t, kUdpKeySize>& output);

bool encodeUdpDatagram(const UdpEnvelopeHeader& header,
                       const std::array<std::uint8_t, kUdpKeySize>& key,
                       const std::uint8_t* frame, std::size_t frameLength,
                       std::uint8_t* output, std::size_t capacity, std::size_t& written);
bool decodeUdpDatagram(const std::uint8_t* input, std::size_t length,
                       const std::array<std::uint8_t, kUdpKeySize>& key,
                       DecodedUdpDatagram& output);

class UdpReplayWindow {
 public:
  bool accept(std::uint64_t sequence);
  void reset();
  std::uint64_t highestSequence() const { return highestSequence_; }

 private:
  std::uint64_t highestSequence_{};
  std::uint64_t seenMask_{};
};

}  // namespace gridopoly::protocol
