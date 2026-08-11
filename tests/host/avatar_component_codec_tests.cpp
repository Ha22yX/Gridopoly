#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "../../Server/RaspberryPi/src/AvatarComponentCodec.h"

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

}  // namespace

int main() {
  using namespace gridopoly::pi;
  const auto root = std::filesystem::path(GRIDOPOLY_SOURCE_DIR) /
      "Assets/GridCity/Avatars/V1/runtime/components-v1";

  AvatarComponent hair{};
  const auto hairBytes = readAll(root / "hair/h1.gavc");
  assert(decodeAvatarComponent(hairBytes, AvatarComponentKind::Hair, 1, hair));
  assert(hair.header.schema == 1);
  assert(hair.header.canvasWidth == 220 && hair.header.canvasHeight == 300);
  assert(hair.header.width > 0 && hair.header.height > 0);
  assert(hair.rgba.size() == hair.header.decodedBytes);

  AvatarComponent face{};
  assert(decodeAvatarComponent(readAll(root / "face/f10.gavc"),
                               AvatarComponentKind::Face, 10, face));
  AvatarComponent outfit{};
  assert(decodeAvatarComponent(readAll(root / "outfit/o7.gavc"),
                               AvatarComponentKind::Outfit, 7, outfit));

  auto malformed = hairBytes;
  AvatarComponent rejected{};
  malformed[0] = 'X';
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed.back() ^= 0x01;
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed.resize(malformed.size() - 1);
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed[32] = 0;
  malformed[33] = 0;
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed[24] ^= 0x01;
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed[12] = 219;
  malformed[13] = 0;
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  malformed = hairBytes;
  malformed[20] ^= 0x04;
  assert(!decodeAvatarComponent(malformed, AvatarComponentKind::Hair, 1, rejected));
  assert(!decodeAvatarComponent(hairBytes, AvatarComponentKind::Face, 1, rejected));
  assert(!decodeAvatarComponent(hairBytes, AvatarComponentKind::Hair, 2, rejected));

  AvatarComponentKind parsedKind{};
  std::uint8_t parsedPreset = 0;
  assert(parseAvatarComponentRelative("hair/h1.gavc", parsedKind, parsedPreset));
  assert(parsedKind == AvatarComponentKind::Hair && parsedPreset == 1);
  assert(parseAvatarComponentRelative("face/f10.gavc", parsedKind, parsedPreset));
  assert(parsedKind == AvatarComponentKind::Face && parsedPreset == 10);
  assert(parseAvatarComponentRelative("outfit/o7.gavc", parsedKind, parsedPreset));
  assert(parsedKind == AvatarComponentKind::Outfit && parsedPreset == 7);
  for (const auto invalid : {"hair/h0.gavc", "hair/h11.gavc", "hair/h01.gavc",
                             "hair/f1.gavc", "face/h1.gavc", "outfit/o1.gavc.png",
                             "hair/../h1.gavc", "hair/..%2Fidentity.bin", "Hair/h1.gavc"}) {
    assert(!parseAvatarComponentRelative(invalid, parsedKind, parsedPreset));
  }

  constexpr std::array<std::array<std::uint8_t, 3>, 20> expectedHair{{
      {104, 116, 124}, {176, 83, 43}, {40, 48, 58}, {80, 55, 47},
      {142, 90, 60}, {209, 164, 79}, {216, 204, 176}, {45, 132, 138},
      {112, 92, 82}, {139, 54, 42}, {104, 42, 44}, {200, 151, 78},
      {213, 139, 92}, {166, 178, 188}, {235, 238, 234}, {117, 37, 63},
      {194, 85, 119}, {111, 78, 160}, {55, 93, 168}, {48, 125, 91},
  }};
  constexpr std::array<std::array<std::uint8_t, 3>, 8> expectedSkin{{
      {239, 202, 173}, {232, 181, 139}, {224, 158, 100}, {202, 141, 82},
      {173, 128, 83}, {161, 96, 62}, {137, 83, 56}, {91, 53, 42},
  }};
  for (std::uint8_t id = 1; id <= expectedHair.size(); ++id) {
    assert(hairPaletteRgb(id) == expectedHair[id - 1]);
  }
  for (std::uint8_t id = 1; id <= expectedSkin.size(); ++id) {
    assert(skinPaletteRgb(id) == expectedSkin[id - 1]);
  }
  assert((hairPaletteRgb(0) == std::array<std::uint8_t, 3>{0, 0, 0}));
  assert((skinPaletteRgb(9) == std::array<std::uint8_t, 3>{0, 0, 0}));

  std::array<std::uint8_t, 4> pixel{100, 120, 140, 173};
  tintHairPixel(pixel.data(), {176, 83, 43});
  assert((pixel == std::array<std::uint8_t, 4>{147, 69, 36, 173}));
  pixel = {5, 7, 9, 255};
  tintHairPixel(pixel.data(), {176, 83, 43});
  assert((pixel == std::array<std::uint8_t, 4>{5, 7, 9, 255}));
  pixel = {190, 140, 100, 211};
  tintSkinPixel(pixel.data(), {202, 141, 82});
  assert((pixel == std::array<std::uint8_t, 4>{167, 117, 68, 211}));
  pixel = {240, 240, 240, 255};
  tintSkinPixel(pixel.data(), {202, 141, 82});
  assert((pixel == std::array<std::uint8_t, 4>{240, 240, 240, 255}));

  std::array<std::uint8_t, 4> destination{10, 30, 50, 128};
  const std::array<std::uint8_t, 4> source{200, 100, 20, 96};
  blendAvatarPixel(destination.data(), source.data());
  assert((destination == std::array<std::uint8_t, 4>{114, 68, 34, 176}));

  std::vector<std::uint8_t> canvas(220u * 300u * 4u, 0);
  assert(placeAvatarComponent(face, skinPaletteRgb(6), canvas));
  assert(placeAvatarComponent(outfit, {0, 0, 0}, canvas));
  assert(placeAvatarComponent(hair, hairPaletteRgb(18), canvas));
  assert(canvas.size() == 220u * 300u * 4u);

  std::cout << "GRIDOPOLY_AVATAR_COMPONENT_CODEC_TESTS_PASS\n";
  return 0;
}
