#include "IdentityModel.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gridopoly::pi {
namespace {

using gridopoly::protocol::AvatarRecipe;
using gridopoly::protocol::IdentityRoomPhase;
using gridopoly::protocol::kAvatarCatalogVersionV1;

bool zeroRecipe(const AvatarRecipe& recipe) {
  return recipe.avatarCatalogVersion == 0 && recipe.hairPresetId == 0 &&
      recipe.hairColorId == 0 && recipe.facePresetId == 0 && recipe.skinToneId == 0 &&
      recipe.outfitPresetId == 0;
}

bool nextCodepoint(std::string_view bytes, std::size_t& offset, std::uint32_t& codepoint) {
  if (offset >= bytes.size()) return false;
  const auto first = static_cast<std::uint8_t>(bytes[offset++]);
  if (first <= 0x7Fu) {
    codepoint = first;
    return true;
  }
  std::uint8_t count = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if (first >= 0xC2u && first <= 0xDFu) {
    count = 1;
    value = first & 0x1Fu;
    minimum = 0x80u;
  } else if (first >= 0xE0u && first <= 0xEFu) {
    count = 2;
    value = first & 0x0Fu;
    minimum = 0x800u;
  } else if (first >= 0xF0u && first <= 0xF4u) {
    count = 3;
    value = first & 0x07u;
    minimum = 0x10000u;
  } else {
    return false;
  }
  if (offset + count > bytes.size()) return false;
  for (std::uint8_t index = 0; index < count; ++index) {
    const auto continuation = static_cast<std::uint8_t>(bytes[offset++]);
    if ((continuation & 0xC0u) != 0x80u) return false;
    value = (value << 6) | (continuation & 0x3Fu);
  }
  if (value < minimum || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
    return false;
  }
  codepoint = value;
  return true;
}

void appendCodepoint(std::string& output, std::uint32_t codepoint) {
  if (codepoint <= 0x7Fu) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFu) {
    output.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else if (codepoint <= 0xFFFFu) {
    output.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else {
    output.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  }
}

std::uint32_t simpleCaseFold(std::uint32_t value) {
  if (value >= 'A' && value <= 'Z') return value + 0x20u;
  if ((value >= 0x00C0u && value <= 0x00D6u) ||
      (value >= 0x00D8u && value <= 0x00DEu)) return value + 0x20u;
  if (value == 0x0178u) return 0x00FFu;
  if (value >= 0x0100u && value <= 0x0177u && (value & 1u) == 0) return value + 1u;
  if (value >= 0x0391u && value <= 0x03A1u) return value + 0x20u;
  if (value >= 0x03A3u && value <= 0x03ABu) return value + 0x20u;
  if (value == 0x03C2u) return 0x03C3u;
  if (value >= 0x0410u && value <= 0x042Fu) return value + 0x20u;
  if (value >= 0x0400u && value <= 0x040Fu) return value + 0x50u;
  if (value >= 0xFF21u && value <= 0xFF3Au) return value + 0x20u;
  if (value == 0x1E9Eu) return 0x00DFu;
  if (value == 0x212Au) return 'k';
  return value;
}

bool isControl(std::uint32_t value) {
  return value <= 0x1Fu || (value >= 0x7Fu && value <= 0x9Fu);
}

std::uint32_t nextRandom(std::uint32_t& state) {
  state += 0x9E3779B9u;
  auto value = state;
  value = (value ^ (value >> 16)) * 0x85EBCA6Bu;
  value = (value ^ (value >> 13)) * 0xC2B2AE35u;
  return value ^ (value >> 16);
}

bool validStoredName(const IdentitySeatState& seat) {
  if (!seat.nameFinal) return seat.name[0] == '\0';
  std::array<char, 17> display{};
  std::string folded;
  if (!validateIdentityName(std::string_view(seat.name.data()), display, folded)) return false;
  return display == seat.name;
}

}  // namespace

bool validAvatarRecipe(const AvatarRecipe& recipe) {
  return recipe.avatarCatalogVersion == kAvatarCatalogVersionV1 &&
      recipe.hairPresetId >= 1 && recipe.hairPresetId <= 10 &&
      recipe.hairColorId >= 1 && recipe.hairColorId <= 20 &&
      recipe.facePresetId >= 1 && recipe.facePresetId <= 10 &&
      recipe.skinToneId >= 1 && recipe.skinToneId <= 8 &&
      recipe.outfitPresetId >= 1 && recipe.outfitPresetId <= 10;
}

std::string caseFoldIdentityName(std::string_view validUtf8) {
  std::string result;
  result.reserve(validUtf8.size());
  std::size_t offset = 0;
  while (offset < validUtf8.size()) {
    std::uint32_t codepoint = 0;
    if (!nextCodepoint(validUtf8, offset, codepoint)) return {};
    appendCodepoint(result, simpleCaseFold(codepoint));
  }
  return result;
}

bool validateIdentityName(std::string_view input, std::array<char, 17>& display,
                          std::string& folded) {
  display.fill('\0');
  folded.clear();
  std::size_t start = 0;
  std::size_t end = input.size();
  while (start < end && (input[start] == ' ' || input[start] == '\t')) ++start;
  while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t')) --end;
  const auto trimmed = input.substr(start, end - start);
  if (trimmed.empty() || trimmed.size() > 16) return false;
  std::size_t offset = 0;
  while (offset < trimmed.size()) {
    std::uint32_t codepoint = 0;
    if (!nextCodepoint(trimmed, offset, codepoint) || isControl(codepoint)) return false;
  }
  std::memcpy(display.data(), trimmed.data(), trimmed.size());
  folded = caseFoldIdentityName(trimmed);
  return !folded.empty();
}

bool identityNameExists(const IdentityRoomState& room, std::string_view candidate,
                        std::uint8_t exceptPlayerId) {
  std::array<char, 17> normalized{};
  std::string folded;
  if (!validateIdentityName(candidate, normalized, folded)) return false;
  for (std::uint8_t index = 0; index < room.playerCount; ++index) {
    const auto& seat = room.seats[index];
    if (!seat.nameFinal || seat.playerId == exceptPlayerId) continue;
    const auto existing = caseFoldIdentityName(std::string_view(seat.name.data()));
    if (!existing.empty() && existing == folded) return true;
  }
  return false;
}

AvatarRecipe deterministicBotRecipe(std::uint32_t roomSeed, std::uint8_t playerId,
                                    std::uint16_t catalogVersion) {
  if (catalogVersion != kAvatarCatalogVersionV1 || playerId == 0) return {};
  std::uint32_t random = roomSeed ^ (static_cast<std::uint32_t>(playerId) * 0xA511E9B3u) ^
      (static_cast<std::uint32_t>(catalogVersion) * 0x63D83595u);
  AvatarRecipe recipe{};
  recipe.avatarCatalogVersion = catalogVersion;
  recipe.hairPresetId = static_cast<std::uint8_t>(nextRandom(random) % 10u + 1u);
  recipe.hairColorId = static_cast<std::uint8_t>(nextRandom(random) % 20u + 1u);
  recipe.facePresetId = static_cast<std::uint8_t>(nextRandom(random) % 10u + 1u);
  recipe.skinToneId = static_cast<std::uint8_t>(nextRandom(random) % 8u + 1u);
  recipe.outfitPresetId = static_cast<std::uint8_t>(nextRandom(random) % 10u + 1u);
  return recipe;
}

bool initializeIdentityRoom(IdentityRoomState& room, std::uint32_t roomId,
                            std::uint32_t roomSeed, std::uint8_t humanCount,
                            std::uint8_t botCount) {
  const auto totalWide = static_cast<unsigned>(humanCount) + botCount;
  if (roomId == 0 || roomSeed == 0 || humanCount == 0 || humanCount > 6 || botCount > 5 ||
      totalWide < 2 || totalWide > 6) return false;
  const auto total = static_cast<std::uint8_t>(totalWide);
  room = IdentityRoomState{};
  room.roomId = roomId;
  room.roomSeed = roomSeed;
  room.identityRevision = 1;
  room.phase = IdentityRoomPhase::AvatarSetup;
  room.avatarCatalogVersion = kAvatarCatalogVersionV1;
  room.humanCount = humanCount;
  room.botCount = botCount;
  room.playerCount = total;
  for (std::uint8_t index = 0; index < total; ++index) {
    auto& seat = room.seats[index];
    seat.playerId = static_cast<std::uint8_t>(index + 1);
    seat.human = index < humanCount;
    seat.bot = !seat.human;
    seat.connected = seat.bot;
    seat.seatColorId = static_cast<std::uint8_t>(index + 1);
    seat.seatRevision = 1;
    if (seat.bot) {
      const auto botIndex = static_cast<unsigned>(index - humanCount + 1);
      std::snprintf(seat.name.data(), seat.name.size(), "Bot %u", botIndex);
      seat.nameFinal = true;
      seat.avatarGenerating = true;
      seat.pendingRecipe = deterministicBotRecipe(roomSeed, seat.playerId,
                                                   kAvatarCatalogVersionV1);
    }
  }
  return validIdentityRoomState(room);
}

bool validIdentityRoomState(const IdentityRoomState& room) {
  const auto phase = static_cast<std::uint8_t>(room.phase);
  if (room.roomId == 0 || room.roomSeed == 0 || room.identityRevision == 0 ||
      phase < 1 || phase > 3 || room.avatarCatalogVersion != kAvatarCatalogVersionV1 ||
      room.humanCount == 0 || room.playerCount < 2 || room.playerCount > 6 ||
      room.playerCount != static_cast<std::uint8_t>(room.humanCount + room.botCount) ||
      (room.phase == IdentityRoomPhase::Countdown) != (room.countdownDeadlineEpochMs != 0)) {
    return false;
  }
  for (std::size_t index = 0; index < room.seats.size(); ++index) {
    const auto& seat = room.seats[index];
    if (index >= room.playerCount) {
      if (seat.playerId != 0 || seat.human || seat.bot || seat.connected ||
          seat.avatarGenerating || seat.avatarFinal || seat.nameFinal || seat.ready ||
          seat.seatColorId != 0 || seat.seatRevision != 0 || seat.avatarRevision != 0 ||
          seat.avatarContentHash64 != 0 || !zeroRecipe(seat.recipe) ||
          !zeroRecipe(seat.pendingRecipe) || seat.name[0] != '\0' ||
          seat.hasCachedRequest || seat.lastRequestId != 0) return false;
      continue;
    }
    if (seat.playerId != index + 1 || seat.human == seat.bot ||
        seat.human != (index < room.humanCount) || seat.seatColorId == 0 ||
        seat.seatRevision == 0 || !validStoredName(seat)) return false;
    if (seat.avatarFinal != (seat.avatarRevision != 0 && seat.avatarContentHash64 != 0 &&
                             validAvatarRecipe(seat.recipe))) return false;
    if (!seat.avatarFinal && (seat.avatarRevision != 0 || seat.avatarContentHash64 != 0 ||
                              !zeroRecipe(seat.recipe))) return false;
    if (seat.avatarGenerating != validAvatarRecipe(seat.pendingRecipe)) return false;
    if (room.phase != IdentityRoomPhase::Active && seat.human && seat.nameFinal &&
        (!seat.avatarFinal || seat.avatarGenerating || !seat.ready)) return false;
    if (seat.ready && (!seat.avatarFinal || !seat.nameFinal || seat.avatarGenerating)) return false;
    if (seat.hasCachedRequest != (seat.lastRequestId != 0)) return false;
  }
  return true;
}

}  // namespace gridopoly::pi
