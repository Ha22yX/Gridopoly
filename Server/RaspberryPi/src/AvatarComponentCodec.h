#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace gridopoly::pi {

enum class AvatarComponentKind : std::uint8_t {
  Face = 1,
  Outfit = 2,
  Hair = 3,
};

struct AvatarComponentHeader {
  std::uint8_t schema{};
  AvatarComponentKind kind{};
  std::uint8_t presetId{};
  std::uint8_t encoding{};
  std::uint16_t canvasWidth{};
  std::uint16_t canvasHeight{};
  std::uint16_t x{};
  std::uint16_t y{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint32_t decodedBytes{};
  std::uint32_t encodedBytes{};
  std::uint32_t crc32{};
};

struct AvatarComponent {
  AvatarComponentHeader header{};
  std::vector<std::uint8_t> rgba{};
};

constexpr std::uint16_t kAvatarComponentCanvasWidth = 220;
constexpr std::uint16_t kAvatarComponentCanvasHeight = 300;
constexpr std::size_t kAvatarComponentHeaderSize = 32;

bool decodeAvatarComponent(const std::vector<std::uint8_t>& bytes,
                           AvatarComponentKind expectedKind,
                           std::uint8_t expectedPresetId,
                           AvatarComponent& output);
std::filesystem::path avatarComponentPath(const std::filesystem::path& root,
                                          AvatarComponentKind kind,
                                          std::uint8_t presetId);
bool loadAvatarComponent(const std::filesystem::path& root,
                         AvatarComponentKind kind,
                         std::uint8_t presetId,
                         AvatarComponent& output,
                         std::vector<std::uint8_t>* encodedFile = nullptr);
bool parseAvatarComponentRelative(std::string_view value,
                                  AvatarComponentKind& kind,
                                  std::uint8_t& presetId);

std::array<std::uint8_t, 3> hairPaletteRgb(std::uint8_t id);
std::array<std::uint8_t, 3> skinPaletteRgb(std::uint8_t id);
void tintHairPixel(std::uint8_t* rgba, const std::array<std::uint8_t, 3>& target);
void tintSkinPixel(std::uint8_t* rgba, const std::array<std::uint8_t, 3>& target);
void blendAvatarPixel(std::uint8_t* destination, const std::uint8_t* source);
bool placeAvatarComponent(const AvatarComponent& component,
                          const std::array<std::uint8_t, 3>& tint,
                          std::vector<std::uint8_t>& canvasRgba);

}  // namespace gridopoly::pi
