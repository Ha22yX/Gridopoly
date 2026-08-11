#include "FileIdentityStore.h"

#include <array>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace gridopoly::pi {
namespace {

using gridopoly::protocol::AvatarRecipe;
using gridopoly::protocol::crc32;

constexpr std::uint32_t kMagic = 0x44495047u;
constexpr std::uint16_t kSchema = 1;
constexpr std::uint16_t kHeaderSize = 16;
constexpr std::size_t kSeatBytes = 281;
constexpr std::size_t kPayloadBytes = 28 + 6 * kSeatBytes;

bool writeAtomically(const std::filesystem::path& path, const void* data,
                     std::size_t length) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
  }
  auto temporary = path;
  temporary += ".tmp";
#if defined(__linux__)
  const int descriptor = ::open(temporary.c_str(),
                                O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
  if (descriptor < 0) return false;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::size_t written = 0;
  while (written < length) {
    const auto result = ::write(descriptor, bytes + written, length - written);
    if (result <= 0) {
      ::close(descriptor);
      std::filesystem::remove(temporary, error);
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  const bool synchronized = ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  if (!synchronized || !closed) {
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(static_cast<const char*>(data), static_cast<std::streamsize>(length));
  output.flush();
  if (!output) return false;
  output.close();
#endif
  std::filesystem::rename(temporary, path, error);
  if (!error) return true;
  std::filesystem::remove(path, error);
  error.clear();
  std::filesystem::rename(temporary, path, error);
  return !error;
}

void put16(std::uint8_t* output, std::uint16_t value) {
  output[0] = static_cast<std::uint8_t>(value);
  output[1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::uint8_t* output, std::uint32_t value) {
  output[0] = static_cast<std::uint8_t>(value);
  output[1] = static_cast<std::uint8_t>(value >> 8);
  output[2] = static_cast<std::uint8_t>(value >> 16);
  output[3] = static_cast<std::uint8_t>(value >> 24);
}

void put64(std::uint8_t* output, std::uint64_t value) {
  put32(output, static_cast<std::uint32_t>(value));
  put32(output + 4, static_cast<std::uint32_t>(value >> 32));
}

std::uint16_t get16(const std::uint8_t* input) {
  return static_cast<std::uint16_t>(input[0]) |
      static_cast<std::uint16_t>(input[1] << 8);
}

std::uint32_t get32(const std::uint8_t* input) {
  return static_cast<std::uint32_t>(input[0]) |
      (static_cast<std::uint32_t>(input[1]) << 8) |
      (static_cast<std::uint32_t>(input[2]) << 16) |
      (static_cast<std::uint32_t>(input[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* input) {
  return static_cast<std::uint64_t>(get32(input)) |
      (static_cast<std::uint64_t>(get32(input + 4)) << 32);
}

void putRecipe(std::uint8_t* output, const AvatarRecipe& recipe) {
  put16(output, recipe.avatarCatalogVersion);
  output[2] = recipe.hairPresetId;
  output[3] = recipe.hairColorId;
  output[4] = recipe.facePresetId;
  output[5] = recipe.skinToneId;
  output[6] = recipe.outfitPresetId;
}

AvatarRecipe getRecipe(const std::uint8_t* input) {
  return {get16(input), input[2], input[3], input[4], input[5], input[6]};
}

std::uint8_t flagsFor(const IdentitySeatState& seat) {
  return static_cast<std::uint8_t>((seat.human ? 1u : 0u) |
      (seat.bot ? 2u : 0u) | (seat.connected ? 4u : 0u) |
      (seat.avatarGenerating ? 8u : 0u) | (seat.avatarFinal ? 16u : 0u) |
      (seat.nameFinal ? 32u : 0u) | (seat.ready ? 64u : 0u));
}

std::uint8_t nameLength(const IdentitySeatState& seat) {
  std::uint8_t length = 0;
  while (length < 16 && seat.name[length] != '\0') ++length;
  return length;
}

bool serialize(const IdentityRoomState& state, std::vector<std::uint8_t>& bytes) {
  if (!validIdentityRoomState(state)) return false;
  bytes.assign(kHeaderSize + kPayloadBytes, 0);
  auto* header = bytes.data();
  auto* payload = header + kHeaderSize;
  put32(header, kMagic);
  put16(header + 4, kSchema);
  put16(header + 6, kHeaderSize);
  put32(header + 8, static_cast<std::uint32_t>(kPayloadBytes));
  put32(payload, state.roomId);
  put32(payload + 4, state.roomSeed);
  put32(payload + 8, state.identityRevision);
  payload[12] = static_cast<std::uint8_t>(state.phase);
  payload[13] = state.humanCount;
  payload[14] = state.botCount;
  payload[15] = state.playerCount;
  put64(payload + 16, state.countdownDeadlineEpochMs);
  put16(payload + 24, state.avatarCatalogVersion);
  for (std::size_t index = 0; index < state.seats.size(); ++index) {
    const auto& seat = state.seats[index];
    auto* output = payload + 28 + index * kSeatBytes;
    output[0] = seat.playerId;
    output[1] = flagsFor(seat);
    output[2] = seat.seatColorId;
    output[3] = nameLength(seat);
    put16(output + 4, seat.seatRevision);
    put16(output + 6, seat.avatarRevision);
    put64(output + 8, seat.avatarContentHash64);
    putRecipe(output + 16, seat.recipe);
    putRecipe(output + 23, seat.pendingRecipe);
    put32(output + 30, seat.lastRequestId);
    output[34] = seat.hasCachedRequest ? 1 : 0;
    std::memcpy(output + 38, seat.name.data(), seat.name.size());
    std::memcpy(output + 55, seat.lastRequest.data(), seat.lastRequest.size());
    std::memcpy(output + 99, seat.cachedResponse.data(), seat.cachedResponse.size());
  }
  put32(header + 12, crc32(payload, kPayloadBytes));
  return true;
}

bool deserialize(const std::vector<std::uint8_t>& bytes, IdentityRoomState& state) {
  if (bytes.size() != kHeaderSize + kPayloadBytes || get32(bytes.data()) != kMagic ||
      get16(bytes.data() + 4) != kSchema || get16(bytes.data() + 6) != kHeaderSize ||
      get32(bytes.data() + 8) != kPayloadBytes) return false;
  const auto* payload = bytes.data() + kHeaderSize;
  if (get32(bytes.data() + 12) != crc32(payload, kPayloadBytes) ||
      payload[26] != 0 || payload[27] != 0) return false;
  IdentityRoomState decoded{};
  decoded.roomId = get32(payload);
  decoded.roomSeed = get32(payload + 4);
  decoded.identityRevision = get32(payload + 8);
  decoded.phase = static_cast<gridopoly::protocol::IdentityRoomPhase>(payload[12]);
  decoded.humanCount = payload[13];
  decoded.botCount = payload[14];
  decoded.playerCount = payload[15];
  decoded.countdownDeadlineEpochMs = get64(payload + 16);
  decoded.avatarCatalogVersion = get16(payload + 24);
  for (std::size_t index = 0; index < decoded.seats.size(); ++index) {
    auto& seat = decoded.seats[index];
    const auto* input = payload + 28 + index * kSeatBytes;
    if (input[35] != 0 || input[36] != 0 || input[37] != 0 || input[3] > 16) return false;
    seat.playerId = input[0];
    const auto flags = input[1];
    if ((flags & 0x80u) != 0) return false;
    seat.human = (flags & 1u) != 0;
    seat.bot = (flags & 2u) != 0;
    seat.connected = (flags & 4u) != 0;
    seat.avatarGenerating = (flags & 8u) != 0;
    seat.avatarFinal = (flags & 16u) != 0;
    seat.nameFinal = (flags & 32u) != 0;
    seat.ready = (flags & 64u) != 0;
    seat.seatColorId = input[2];
    seat.seatRevision = get16(input + 4);
    seat.avatarRevision = get16(input + 6);
    seat.avatarContentHash64 = get64(input + 8);
    seat.recipe = getRecipe(input + 16);
    seat.pendingRecipe = getRecipe(input + 23);
    seat.lastRequestId = get32(input + 30);
    if (input[34] > 1) return false;
    seat.hasCachedRequest = input[34] != 0;
    std::memcpy(seat.name.data(), input + 38, seat.name.size());
    if (seat.name[input[3]] != '\0') return false;
    for (std::size_t nameIndex = input[3]; nameIndex < seat.name.size(); ++nameIndex) {
      if (seat.name[nameIndex] != '\0') return false;
    }
    std::memcpy(seat.lastRequest.data(), input + 55, seat.lastRequest.size());
    std::memcpy(seat.cachedResponse.data(), input + 99, seat.cachedResponse.size());
  }
  if (!validIdentityRoomState(decoded)) return false;
  state = decoded;
  return true;
}

}  // namespace

FileIdentityStore::FileIdentityStore(std::filesystem::path path) : path_(std::move(path)) {}

bool FileIdentityStore::restore(IdentityRoomState& state) const {
  std::ifstream input(path_, std::ios::binary);
  if (!input) return false;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  return deserialize(bytes, state);
}

bool FileIdentityStore::save(const IdentityRoomState& state) const {
  std::vector<std::uint8_t> bytes;
  if (!serialize(state, bytes)) return false;
  return writeAtomically(path_, bytes.data(), bytes.size());
}

bool FileIdentityStore::clear() const {
  std::error_code error;
  std::filesystem::remove(path_, error);
  auto temporary = path_;
  temporary += ".tmp";
  std::filesystem::remove(temporary, error);
  return true;
}

}  // namespace gridopoly::pi
