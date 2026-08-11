#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

std::uint32_t bigEndian32(const std::uint8_t* input) {
  return (static_cast<std::uint32_t>(input[0]) << 24) |
      (static_cast<std::uint32_t>(input[1]) << 16) |
      (static_cast<std::uint32_t>(input[2]) << 8) | input[3];
}

}  // namespace

int main() {
  using namespace gridopoly::pi;
  using gridopoly::protocol::AvatarRecipe;

  const auto source = std::filesystem::path(GRIDOPOLY_SOURCE_DIR);
  const auto components = source / "Assets/GridCity/Avatars/V1/runtime/components-v1";
  const auto temporary = std::filesystem::temp_directory_path() / "gridopoly-avatar-renderer-tests";
  std::error_code error;
  std::filesystem::remove_all(temporary, error);
  std::filesystem::create_directories(temporary, error);
  assert(!error);

  AvatarRenderer renderer(components, temporary);
  assert(renderer.valid());
  const AvatarRecipe recipe{1, 7, 18, 4, 6, 9};
  const auto final = renderer.render(123456789u, 2, 1, recipe);
  assert(final.ok && final.contentHash64 != 0);
  assert(final.contentHash64 == 0x6895eae94a41c35full);
  assert(final.key.rfind("p2-a1-", 0) == 0 && final.key.size() == 22);
  assert(final.pngPath.filename() == final.key + ".png");
  assert(final.rgb565Path.filename() == final.key + ".rgb565");
  const auto rgb = readAll(final.rgb565Path);
  assert(rgb.size() == 128u * 128u * 2u);
  const auto png = readAll(final.pngPath);
  const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
  assert(png.size() > 33 && std::equal(signature.begin(), signature.end(), png.begin()));
  assert(std::string(reinterpret_cast<const char*>(png.data() + 12), 4) == "IHDR");
  assert(bigEndian32(png.data() + 16) == 128 && bigEndian32(png.data() + 20) == 128);
  assert(png[24] == 8 && png[25] == 6);

  const auto repeated = renderer.render(123456789u, 2, 1, recipe);
  assert(repeated.ok && repeated.contentHash64 == final.contentHash64);
  assert(repeated.pngPath == final.pngPath && repeated.rgb565Path == final.rgb565Path);

  const auto preview = renderer.renderPreview(recipe);
  assert(preview.ok);
  assert(preview.contentHash64 == 0x2db09660e5c5fdfaull);
  assert(preview.key == "h7-c18-f4-s6-o9");
  assert(preview.path.filename() == preview.key + ".rgb565");
  assert(preview.path.parent_path().filename() == "gavc-v1");
  const auto previewBytes = readAll(preview.path);
  assert(previewBytes.size() == 220u * 300u * 2u);
  assert(previewBytes[0] == 0x82 && previewBytes[1] == 0x00);
  const auto previewTime = std::filesystem::last_write_time(preview.path);
  const auto previewAgain = renderer.renderPreview(recipe);
  assert(previewAgain.ok && previewAgain.contentHash64 == preview.contentHash64);
  assert(std::filesystem::last_write_time(preview.path) == previewTime);

  // Keep the renderer contract aligned with the canonical HTTP preview route
  // exercised by http_asset_integration_tests.
  const AvatarRecipe httpRecipe{1, 2, 5, 3, 4, 6};
  const auto httpPreview = renderer.renderPreview(httpRecipe);
  assert(httpPreview.ok);
  assert(httpPreview.key == "h2-c5-f3-s4-o6");
  assert(readAll(httpPreview.path).size() == 220u * 300u * 2u);

  assert(!renderer.render(1, 0, 1, recipe).ok);
  assert(!renderer.render(1, 1, 0, recipe).ok);
  assert(!renderer.renderPreview({1, 0, 1, 1, 1, 1}).ok);
  assert(!renderer.renderPreview({1, 1, 21, 1, 1, 1}).ok);

  const auto failed = renderer.render(123456789u, 3, 1, {1, 1, 1, 1, 1, 1}, true);
  assert(!failed.ok);
  const auto roomDirectory = temporary / "avatars" / "123456789";
  if (std::filesystem::exists(roomDirectory)) {
    for (const auto& entry : std::filesystem::directory_iterator(roomDirectory)) {
      assert(entry.path().filename().string().rfind("p3-a1-", 0) != 0);
      assert(entry.path().extension() != ".tmp");
    }
  }

  std::cout << "GRIDOPOLY_AVATAR_RENDERER_TESTS_PASS\n";
  return 0;
}
