#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::pi {

struct IdentitySeatState {
  std::uint8_t playerId{};
  bool human{};
  bool bot{};
  bool connected{};
  bool avatarGenerating{};
  bool avatarFinal{};
  bool nameFinal{};
  bool ready{};
  std::uint8_t seatColorId{};
  std::uint16_t seatRevision{};
  std::uint16_t avatarRevision{};
  std::uint64_t avatarContentHash64{};
  gridopoly::protocol::AvatarRecipe recipe{};
  gridopoly::protocol::AvatarRecipe pendingRecipe{};
  std::array<char, 17> name{};
  bool hasCachedRequest{};
  std::uint32_t lastRequestId{};
  std::array<std::uint8_t, gridopoly::protocol::kIdentityRequestSize> lastRequest{};
  std::array<std::uint8_t, gridopoly::protocol::kIdentitySnapshotSize> cachedResponse{};
};

struct IdentityRoomState {
  std::uint32_t roomId{};
  std::uint32_t roomSeed{};
  std::uint32_t identityRevision{};
  gridopoly::protocol::IdentityRoomPhase phase{
      gridopoly::protocol::IdentityRoomPhase::AvatarSetup};
  std::uint64_t countdownDeadlineEpochMs{};
  std::uint16_t avatarCatalogVersion{gridopoly::protocol::kAvatarCatalogVersionV1};
  std::uint8_t humanCount{};
  std::uint8_t botCount{};
  std::uint8_t playerCount{};
  std::array<IdentitySeatState, 6> seats{};
};

bool validAvatarRecipe(const gridopoly::protocol::AvatarRecipe& recipe);
bool validateIdentityName(std::string_view input, std::array<char, 17>& display,
                          std::string& folded);
std::string caseFoldIdentityName(std::string_view validUtf8);
bool identityNameExists(const IdentityRoomState& room, std::string_view candidate,
                        std::uint8_t exceptPlayerId);
gridopoly::protocol::AvatarRecipe deterministicBotRecipe(std::uint32_t roomSeed,
                                                         std::uint8_t playerId,
                                                         std::uint16_t catalogVersion);
bool initializeIdentityRoom(IdentityRoomState& room, std::uint32_t roomId,
                            std::uint32_t roomSeed, std::uint8_t humanCount,
                            std::uint8_t botCount);
bool validIdentityRoomState(const IdentityRoomState& room);

}  // namespace gridopoly::pi
