#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <gridopoly/protocol/Protocol.h>

// Deliberately tiny synthetic material fixtures, independent of production art.
namespace avatar_v2_fixture {
inline void put16(std::vector<std::uint8_t>& out, std::size_t offset, std::uint16_t value) {
  out[offset] = static_cast<std::uint8_t>(value);
  out[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}
inline void put32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
  put16(out, offset, static_cast<std::uint16_t>(value));
  put16(out, offset + 2, static_cast<std::uint16_t>(value >> 16));
}
inline std::vector<std::uint8_t> component(std::uint8_t kind, std::uint8_t preset,
                                         const std::vector<std::uint8_t>& raw,
                                         bool repeated = false) {
  const auto count = static_cast<std::uint16_t>(raw.size() / 5);
  std::vector<std::uint8_t> bytes(34, 0);
  bytes[0] = 'G'; bytes[1] = 'A'; bytes[2] = 'V'; bytes[3] = 'C';
  bytes[4] = 2; bytes[5] = kind; bytes[6] = preset; bytes[7] = 2;
  put16(bytes, 8, 220); put16(bytes, 10, 300);
  put16(bytes, 12, 100); put16(bytes, 14, 100);
  put16(bytes, 16, count); put16(bytes, 18, 1);
  put32(bytes, 20, static_cast<std::uint32_t>(raw.size()));
  put32(bytes, 24, static_cast<std::uint32_t>(2 + (repeated ? 5 : raw.size())));
  put32(bytes, 28, gridopoly::protocol::crc32(raw.data(), raw.size()));
  put16(bytes, 32, static_cast<std::uint16_t>(count | (repeated ? 0x8000 : 0)));
  bytes.insert(bytes.end(), raw.begin(), repeated ? raw.begin() + 5 : raw.end());
  return bytes;
}
inline void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (!output) throw std::runtime_error("cannot write avatar test fixture");
}
inline void catalog(const std::filesystem::path& root) {
  const std::array<const char*, 4> names{{"face/f", "outfit/o", "hair/h", "neck/n"}};
  const std::array<std::vector<std::uint8_t>, 4> samples{{
      {178,178,178,128,255, 10,20,30,255,0, 255,255,255,128,128},
      {10,40,80,128,255, 20,60,100,255,0},
      {10,20,30,128,255},
      {178,178,178,255,255, 178,178,178,255,255,
       178,178,178,255,255, 178,178,178,255,255}
  }};
  for (std::uint8_t kind = 1; kind <= 4; ++kind) {
    for (std::uint8_t preset = 1; preset <= (kind == 4 ? 1 : 10); ++preset) {
      write(root / (std::string(names[kind - 1]) + std::to_string(preset) + ".gavc"),
            component(kind, preset, samples[kind - 1], kind == 4));
    }
  }
}
}  // namespace avatar_v2_fixture
