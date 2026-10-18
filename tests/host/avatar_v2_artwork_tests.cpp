#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "../../Server/RaspberryPi/src/AvatarRenderer.h"

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)

namespace {
void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  CHECK(output.good());
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

// Run only against a frozen real V2 catalog, with a new empty output directory.
// The companion exports allow the independent Python and actual player compositor
// to compare source RGBA, background-flattened RGB565 and chroma-keyed RGB565.
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: avatar_v2_artwork_tests <components-v2> <empty-output-directory>\n";
    return 2;
  }
  using namespace gridopoly::pi;
  using gridopoly::protocol::AvatarRecipe;
  const std::filesystem::path components{argv[1]};
  const std::filesystem::path output{argv[2]};
  CHECK(std::filesystem::is_directory(output) && std::filesystem::is_empty(output));
  AvatarRenderer renderer({}, output, components);
  CHECK(renderer.valid(2) && !renderer.valid(1));
  const std::array<AvatarRecipe, 2> recipes{{{2,8,1,7,8,2}, {2,1,1,10,1,7}}};
  std::uint8_t player = 0;
  for (const auto& recipe : recipes) {
    std::vector<std::uint8_t> rgba(220u * 300u * 4u, 0);
    const std::array<AvatarComponentKind,4> order{{AvatarComponentKind::Neck,
        AvatarComponentKind::Face, AvatarComponentKind::Outfit, AvatarComponentKind::Hair}};
    const std::array<std::uint8_t,4> presets{{1, recipe.facePresetId,
                                             recipe.outfitPresetId, recipe.hairPresetId}};
    for (std::size_t index = 0; index < order.size(); ++index) {
      AvatarComponent component{};
      CHECK(loadAvatarComponent(components, order[index], presets[index], component, nullptr, 2));
      const auto tint = order[index] == AvatarComponentKind::Hair
          ? hairPaletteRgb(recipe.hairColorId) : skinPaletteRgb(recipe.skinToneId);
      CHECK(placeAvatarComponent(component, tint, rgba));
    }
    std::vector<std::uint8_t> flat(132000), chroma(132000);
    constexpr std::array<std::uint8_t,3> background{{6,16,23}};
    for (std::size_t pixel = 0; pixel < 220u * 300u; ++pixel) {
      const auto alpha = rgba[pixel * 4u + 3];
      std::array<std::uint8_t,3> color{};
      for (std::size_t channel = 0; channel < 3; ++channel) {
        color[channel] = static_cast<std::uint8_t>((rgba[pixel * 4u + channel] * alpha +
            background[channel] * (255u - alpha) + 127u) / 255u);
      }
      const auto packed = static_cast<std::uint16_t>(((color[0] & 0xF8u) << 8) |
          ((color[1] & 0xFCu) << 3) | (color[2] >> 3));
      flat[pixel * 2] = static_cast<std::uint8_t>(packed);
      flat[pixel * 2 + 1] = static_cast<std::uint8_t>(packed >> 8);
      const auto keyed = static_cast<std::uint16_t>(alpha == 0 ? 0x07E0 : packed);
      chroma[pixel * 2] = static_cast<std::uint8_t>(keyed);
      chroma[pixel * 2 + 1] = static_cast<std::uint8_t>(keyed >> 8);
    }
    const auto preview = renderer.renderPreview(recipe);
    CHECK(preview.ok && read(preview.path) == flat);
    const auto final = renderer.render(200018u, ++player, 1, recipe);
    CHECK(final.ok && read(final.rgb565Path).size() == 32768);
    const auto key = preview.key;
    write(output / (key + ".rgba"), rgba);
    write(output / (key + ".server.rgb565"), flat);
    write(output / (key + ".chroma.rgb565"), chroma);
    std::cout << "AVATAR_V2_REAL_ART_CASE_PASS " << key << '\n';
  }
  std::cout << "GRIDOPOLY_AVATAR_V2_REAL_ART_TESTS_PASS\n";
}
