#include "UdpEnvelope.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace gridopoly::protocol {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

constexpr std::uint32_t rotateRight(std::uint32_t value, std::uint8_t amount) {
  return (value >> amount) | (value << (32u - amount));
}

void put16(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value);
  out[1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value);
  out[1] = static_cast<std::uint8_t>(value >> 8);
  out[2] = static_cast<std::uint8_t>(value >> 16);
  out[3] = static_cast<std::uint8_t>(value >> 24);
}

void put64(std::uint8_t* out, std::uint64_t value) {
  for (std::uint8_t index = 0; index < 8; ++index) {
    out[index] = static_cast<std::uint8_t>(value >> (index * 8u));
  }
}

std::uint16_t get16(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) | (static_cast<std::uint16_t>(in[1]) << 8);
}

std::uint32_t get32(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* in) {
  std::uint64_t value = 0;
  for (std::uint8_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(in[index]) << (index * 8u);
  }
  return value;
}

class Sha256 {
 public:
  Sha256() { reset(); }

  void update(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0) return;
    totalBytes_ += length;
    while (length != 0) {
      const auto take = std::min(length, buffer_.size() - buffered_);
      std::memcpy(buffer_.data() + buffered_, data, take);
      buffered_ += take;
      data += take;
      length -= take;
      if (buffered_ == buffer_.size()) {
        transform(buffer_.data());
        buffered_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> finish() {
    const std::uint64_t totalBits = totalBytes_ * 8u;
    buffer_[buffered_++] = 0x80;
    if (buffered_ > 56) {
      while (buffered_ < buffer_.size()) buffer_[buffered_++] = 0;
      transform(buffer_.data());
      buffered_ = 0;
    }
    while (buffered_ < 56) buffer_[buffered_++] = 0;
    for (std::uint8_t index = 0; index < 8; ++index) {
      buffer_[63u - index] = static_cast<std::uint8_t>(totalBits >> (index * 8u));
    }
    transform(buffer_.data());
    std::array<std::uint8_t, 32> digest{};
    for (std::uint8_t word = 0; word < 8; ++word) {
      digest[word * 4] = static_cast<std::uint8_t>(state_[word] >> 24);
      digest[word * 4 + 1] = static_cast<std::uint8_t>(state_[word] >> 16);
      digest[word * 4 + 2] = static_cast<std::uint8_t>(state_[word] >> 8);
      digest[word * 4 + 3] = static_cast<std::uint8_t>(state_[word]);
    }
    return digest;
  }

 private:
  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t totalBytes_{};
  std::size_t buffered_{};

  void reset() {
    state_ = {{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
               0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}};
    buffer_.fill(0);
    totalBytes_ = 0;
    buffered_ = 0;
  }

  void transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::uint8_t index = 0; index < 16; ++index) {
      const auto offset = static_cast<std::size_t>(index) * 4u;
      words[index] = (static_cast<std::uint32_t>(block[offset]) << 24) |
                     (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
                     (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
                     static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::uint8_t index = 16; index < 64; ++index) {
      const auto s0 = rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^
                      (words[index - 15] >> 3);
      const auto s1 = rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^
                      (words[index - 2] >> 10);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::uint8_t index = 0; index < 64; ++index) {
      const auto sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const auto choice = (e & f) ^ ((~e) & g);
      const auto temporary1 = h + sum1 + choice + kSha256RoundConstants[index] + words[index];
      const auto sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
};

std::array<std::uint8_t, 32> hmacSha256(const std::uint8_t* key, std::size_t keyLength,
                                        const std::uint8_t* data, std::size_t dataLength) {
  std::array<std::uint8_t, 64> normalizedKey{};
  if (keyLength > normalizedKey.size()) {
    Sha256 keyHash;
    keyHash.update(key, keyLength);
    const auto digest = keyHash.finish();
    std::copy(digest.begin(), digest.end(), normalizedKey.begin());
  } else if (key != nullptr && keyLength != 0) {
    std::memcpy(normalizedKey.data(), key, keyLength);
  }
  std::array<std::uint8_t, 64> innerPad{};
  std::array<std::uint8_t, 64> outerPad{};
  for (std::size_t index = 0; index < normalizedKey.size(); ++index) {
    innerPad[index] = normalizedKey[index] ^ 0x36u;
    outerPad[index] = normalizedKey[index] ^ 0x5cu;
  }
  Sha256 inner;
  inner.update(innerPad.data(), innerPad.size());
  inner.update(data, dataLength);
  const auto innerDigest = inner.finish();
  Sha256 outer;
  outer.update(outerPad.data(), outerPad.size());
  outer.update(innerDigest.data(), innerDigest.size());
  return outer.finish();
}

bool constantTimeEqual(const std::uint8_t* left, const std::uint8_t* right, std::size_t length) {
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < length; ++index) difference |= left[index] ^ right[index];
  return difference == 0;
}

std::array<std::uint8_t, 32> datagramTag(
    const std::array<std::uint8_t, kUdpKeySize>& key, const std::uint8_t* prefix,
    const std::uint8_t* frame, std::size_t frameLength) {
  std::array<std::uint8_t, 32 + kMaxFrameSize> signedBytes{};
  std::memcpy(signedBytes.data(), prefix, 32);
  if (frameLength != 0) std::memcpy(signedBytes.data() + 32, frame, frameLength);
  return hmacSha256(key.data(), key.size(), signedBytes.data(), 32 + frameLength);
}

}  // namespace

void deriveUdpPairKey(const char* psk, std::array<std::uint8_t, kUdpKeySize>& output) {
  static constexpr std::uint8_t kLabel[] = "GRIDOPOLY-UDP-PAIR-V1";
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(psk == nullptr ? "" : psk);
  const auto length = psk == nullptr ? 0u : std::strlen(psk);
  output = hmacSha256(bytes, length, kLabel, sizeof(kLabel) - 1);
}

void deriveUdpSessionKey(const std::array<std::uint8_t, kUdpKeySize>& pairKey,
                         std::uint32_t serverDeviceId, std::uint32_t clientDeviceId,
                         std::uint32_t deviceNonce, std::uint32_t roomId,
                         std::uint32_t sessionId,
                         std::array<std::uint8_t, kUdpKeySize>& output) {
  static constexpr std::uint8_t kLabel[] = "GRIDOPOLY-UDP-SESSION-V1";
  std::array<std::uint8_t, sizeof(kLabel) - 1 + 20> material{};
  std::memcpy(material.data(), kLabel, sizeof(kLabel) - 1);
  auto* values = material.data() + sizeof(kLabel) - 1;
  put32(values, serverDeviceId);
  put32(values + 4, clientDeviceId);
  put32(values + 8, deviceNonce);
  put32(values + 12, roomId);
  put32(values + 16, sessionId);
  output = hmacSha256(pairKey.data(), pairKey.size(), material.data(), material.size());
}

bool encodeUdpDatagram(const UdpEnvelopeHeader& header,
                       const std::array<std::uint8_t, kUdpKeySize>& key,
                       const std::uint8_t* frame, std::size_t frameLength,
                       std::uint8_t* output, std::size_t capacity, std::size_t& written) {
  written = 0;
  if (output == nullptr || frame == nullptr || frameLength < kHeaderSize ||
      frameLength > kMaxFrameSize || capacity < kUdpEnvelopeHeaderSize + frameLength ||
      header.packetSequence == 0) {
    return false;
  }
  put32(output, kUdpEnvelopeMagic);
  output[4] = kUdpEnvelopeVersion;
  output[5] = header.flags;
  put16(output + 6, static_cast<std::uint16_t>(kUdpEnvelopeHeaderSize));
  put32(output + 8, header.sessionId);
  put32(output + 12, header.senderDeviceId);
  put64(output + 16, header.packetSequence);
  put16(output + 24, static_cast<std::uint16_t>(frameLength));
  put16(output + 26, 0);
  put32(output + 28, 0);
  std::memset(output + 32, 0, kUdpAuthTagSize);
  std::memcpy(output + kUdpEnvelopeHeaderSize, frame, frameLength);
  const auto tag = datagramTag(key, output, frame, frameLength);
  std::memcpy(output + 32, tag.data(), kUdpAuthTagSize);
  written = kUdpEnvelopeHeaderSize + frameLength;
  return true;
}

bool decodeUdpDatagram(const std::uint8_t* input, std::size_t length,
                       const std::array<std::uint8_t, kUdpKeySize>& key,
                       DecodedUdpDatagram& output) {
  if (input == nullptr || length < kUdpEnvelopeHeaderSize + kHeaderSize ||
      length > kMaxUdpDatagramSize || get32(input) != kUdpEnvelopeMagic ||
      input[4] != kUdpEnvelopeVersion || get16(input + 6) != kUdpEnvelopeHeaderSize ||
      get16(input + 26) != 0 || get32(input + 28) != 0) {
    return false;
  }
  const auto frameLength = get16(input + 24);
  if (frameLength < kHeaderSize || frameLength > kMaxFrameSize ||
      length != kUdpEnvelopeHeaderSize + frameLength) {
    return false;
  }
  const auto packetSequence = get64(input + 16);
  if (packetSequence == 0) return false;
  const auto* frame = input + kUdpEnvelopeHeaderSize;
  const auto tag = datagramTag(key, input, frame, frameLength);
  if (!constantTimeEqual(input + 32, tag.data(), kUdpAuthTagSize)) return false;
  output.header.flags = input[5];
  output.header.sessionId = get32(input + 8);
  output.header.senderDeviceId = get32(input + 12);
  output.header.packetSequence = packetSequence;
  output.header.frameLength = frameLength;
  output.frame = frame;
  return true;
}

bool UdpReplayWindow::accept(std::uint64_t sequence) {
  if (sequence == 0) return false;
  if (highestSequence_ == 0) {
    highestSequence_ = sequence;
    seenMask_ = 1;
    return true;
  }
  if (sequence > highestSequence_) {
    const auto distance = sequence - highestSequence_;
    seenMask_ = distance >= 64 ? 1u : (seenMask_ << distance) | 1u;
    highestSequence_ = sequence;
    return true;
  }
  const auto distance = highestSequence_ - sequence;
  if (distance >= 64) return false;
  const auto bit = static_cast<std::uint64_t>(1) << distance;
  if ((seenMask_ & bit) != 0) return false;
  seenMask_ |= bit;
  return true;
}

void UdpReplayWindow::reset() {
  highestSequence_ = 0;
  seenMask_ = 0;
}

}  // namespace gridopoly::protocol
