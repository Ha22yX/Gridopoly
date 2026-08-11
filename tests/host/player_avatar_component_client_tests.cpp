#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "../../Firmware/PlayerConsole/avatar_component_math.h"
#include "../../Server/RaspberryPi/src/AvatarComponentCodec.h"
#include "../../Server/RaspberryPi/src/AvatarRenderer.h"

#ifndef GRIDOPOLY_SOURCE_DIR
#define GRIDOPOLY_SOURCE_DIR "."
#endif

namespace {

[[noreturn]] void failAssertion(const char* expression, const char* file, int line) {
  std::cerr << file << ':' << line << ": assertion failed: " << expression << '\n';
  std::exit(1);
}

#undef assert
#define assert(expression) \
  ((expression) ? static_cast<void>(0) : failAssertion(#expression, __FILE__, __LINE__))

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void placeWithPlayerMath(const gridopoly::pi::AvatarComponent& component,
                         const AvatarComponentRgb& tint,
                         std::vector<std::uint8_t>& canvas) {
  const auto& header = component.header;
  for (std::uint16_t y = 0; y < header.height; ++y) {
    for (std::uint16_t x = 0; x < header.width; ++x) {
      const std::size_t sourceOffset =
          (static_cast<std::size_t>(y) * header.width + x) * 4u;
      std::uint8_t source[4]{
          component.rgba[sourceOffset], component.rgba[sourceOffset + 1],
          component.rgba[sourceOffset + 2], component.rgba[sourceOffset + 3]};
      if (header.kind == gridopoly::pi::AvatarComponentKind::Face) {
        avatarTintSkinPixel(source, tint);
      } else if (header.kind == gridopoly::pi::AvatarComponentKind::Hair) {
        avatarTintHairPixel(source, tint);
      }
      const std::size_t destinationOffset =
          (static_cast<std::size_t>(header.y + y) * header.canvasWidth + header.x + x) * 4u;
      avatarSourceOver(canvas.data() + destinationOffset, source);
    }
  }
}

std::vector<std::uint8_t> flattenPlayerPreview(const std::vector<std::uint8_t>& canvas) {
  constexpr std::array<std::uint8_t, 3> background{0x06, 0x10, 0x17};
  std::vector<std::uint8_t> output(220u * 300u * 2u);
  for (std::size_t pixel = 0; pixel < 220u * 300u; ++pixel) {
    const std::uint8_t* rgba = canvas.data() + pixel * 4u;
    std::uint8_t rgb[3]{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      rgb[channel] = static_cast<std::uint8_t>(avatarRoundHalfUp(
          static_cast<std::uint64_t>(rgba[channel]) * rgba[3] +
              static_cast<std::uint64_t>(background[channel]) * (255u - rgba[3]),
          255u));
    }
    const std::uint16_t rgb565 = static_cast<std::uint16_t>(
        ((rgb[0] & 0xF8u) << 8) | ((rgb[1] & 0xFCu) << 3) | (rgb[2] >> 3));
    output[pixel * 2u] = static_cast<std::uint8_t>(rgb565 & 0xFFu);
    output[pixel * 2u + 1] = static_cast<std::uint8_t>(rgb565 >> 8);
  }
  return output;
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
  // Match the frozen Gridopoly content-hash basis used by AvatarRenderer.
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

int main() {
  using gridopoly::pi::AvatarComponent;
  using gridopoly::pi::AvatarComponentKind;

  const auto source = std::filesystem::path(GRIDOPOLY_SOURCE_DIR);
  const auto componentsRoot =
      source / "Assets/GridCity/Avatars/V1/runtime/components-v1";
  AvatarComponent face{};
  AvatarComponent outfit{};
  AvatarComponent hair{};
  assert(gridopoly::pi::loadAvatarComponent(componentsRoot, AvatarComponentKind::Face, 4,
                                            face));
  assert(gridopoly::pi::loadAvatarComponent(componentsRoot, AvatarComponentKind::Outfit, 9,
                                            outfit));
  assert(gridopoly::pi::loadAvatarComponent(componentsRoot, AvatarComponentKind::Hair, 7,
                                            hair));

  std::vector<std::uint8_t> canvas(220u * 300u * 4u, 0);
  placeWithPlayerMath(face, AvatarComponentRgb{161, 96, 62}, canvas);
  placeWithPlayerMath(outfit, AvatarComponentRgb{}, canvas);
  placeWithPlayerMath(hair, AvatarComponentRgb{111, 78, 160}, canvas);
  const auto playerPreview = flattenPlayerPreview(canvas);
  assert(fnv1a64(playerPreview) == 0x2db09660e5c5fdfaull);

  const auto temporary = std::filesystem::temp_directory_path() /
      "gridopoly-player-avatar-component-tests";
  std::error_code error;
  std::filesystem::remove_all(temporary, error);
  error.clear();
  std::filesystem::create_directories(temporary, error);
  assert(!error);
  gridopoly::pi::AvatarRenderer renderer(componentsRoot, temporary);
  assert(renderer.valid());
  const auto serverPreview = renderer.renderPreview({1, 7, 18, 4, 6, 9});
  assert(serverPreview.ok);
  const auto serverBytes = readAll(serverPreview.path);
  assert(serverBytes.size() == playerPreview.size());
  assert(serverBytes == playerPreview);

  std::cout << "GRIDOPOLY_PLAYER_AVATAR_COMPONENT_CLIENT_TESTS_PASS\n";
  return 0;
}
