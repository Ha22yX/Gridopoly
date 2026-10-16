#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "gridopoly/assets/DevelopmentArtworkCatalog.h"
#include "gridopoly/assets/RemoteArtworkPolicy.h"

#ifndef GRIDOPOLY_SOURCE_DIR
#define GRIDOPOLY_SOURCE_DIR "."
#endif

using namespace gridopoly::assets;

static void checkTimingAndIntegrity() {
  const auto* sample = reinterpret_cast<const std::uint8_t*>("123456789");
  assert(artworkCrc32(sample, 9) == 0xCBF43926u);
  assert(artworkCrc32(nullptr, 0) == 0u);
  auto partial = artworkCrc32Update(kArtworkCrc32Initial, sample, 4);
  partial = artworkCrc32Update(partial, sample + 4, 5);
  assert(artworkCrc32Finish(partial) == 0xCBF43926u);
  assert(!artworkDeadlineReached(0xFFFFFFF0u, 0x10u));
  assert(!artworkDeadlineReached(0x0Fu, 0x10u));
  assert(artworkDeadlineReached(0x10u, 0x10u));
  assert(artworkDeadlineReached(0x20u, 0xFFFFFFF0u));

  for (unsigned failure = 1; failure <= 255; ++failure) {
    const auto delay = retryArtworkDelayMs(static_cast<std::uint8_t>(failure), 0xABCD1234u);
    assert(delay >= 1000u && delay <= 31000u);
    if (failure >= 6) assert(delay >= 30000u);
  }
  assert(retryArtworkDelayMs(1, 0, 3) >= 3000u);
  assert(retryArtworkDelayMs(1, 0xFFFFFFFFu, 120) <= 121000u);
  assert(retryArtworkDelayMs(255, 0xFFFFFFFFu, 0xFFFFFFFFu) <= 121000u);
  assert(artworkRetryAfterSeconds(nullptr) == 0);
  assert(artworkRetryAfterSeconds("\t3 ") == 3);
  assert(artworkRetryAfterSeconds("120") == 120);
  assert(artworkRetryAfterSeconds("99999999999999999999999") == 120);
  assert(artworkRetryAfterSeconds("-1") == 0);
  assert(artworkRetryAfterSeconds("12x") == 0);
  assert(artworkRetryAfterSeconds("1.5") == 0);
  assert(artworkRetryAfterSeconds("Fri, 16 Oct 2026 10:00:00 GMT") == 0);

  // Model 40 devices assigned the same property after a restored game. Device
  // identity still spreads their first requests even with the same boot salt.
  std::set<std::uint32_t> delays;
  std::array<unsigned, 4> halfSecondWindows{};
  for (std::uint32_t device = 0; device < 40; ++device) {
    const auto entropy = artworkEntropy(0x12345678u ^ device, "a1-rivet-row", 4);
    const auto delay = initialArtworkDelayMs(entropy);
    assert(delay >= 250u && delay <= 2000u);
    delays.insert(delay);
    ++halfSecondWindows[(delay - 250u) / 500u];
    assert(artworkEntropy(device, "a1-rivet-row", 4, 1) !=
           artworkEntropy(device, "a1-rivet-row", 4, 2));
  }
  assert(delays.size() >= 32);
  for (auto count : halfSecondWindows) assert(count < 25);
}

int main() {
  checkTimingAndIntegrity();
  assert(!developmentProperty(nullptr));
  assert(!developmentProperty("../a1-rivet-row"));
  assert(!developmentProperty("a1-rivet-row-l1"));
  assert(!developmentPropertyForTileId("START"));
  assert(!validDevelopmentLevel(0));
  assert(!validDevelopmentLevel(6));
  assert(!validDevelopmentLevel(255));
  std::set<std::string> keys, tileIds;
  unsigned filesChecked = 0;
  const auto root = std::filesystem::path(GRIDOPOLY_SOURCE_DIR) / "Assets/GridCity/DevelopmentV1";
  for (const auto& property : kDevelopmentProperties) {
    assert(keys.insert(property.key).second);
    assert(tileIds.insert(property.tileId).second);
    assert(developmentProperty(property.key) == &property);
    assert(developmentPropertyForTileId(property.tileId) == &property);
    assert(developmentCrc32(&property, 0, DevelopmentTarget::Tile) == 0);
    assert(developmentCrc32(nullptr, 1, DevelopmentTarget::Player) == 0);
    for (std::uint8_t level = 1; level <= 5; ++level) {
      for (unsigned target = 0; target < 3; ++target) {
        const auto folder = target == 0 ? "rgb565-tile" : (target == 1 ? "rgb565-player" : "device");
        const auto extension = target == 2 ? ".png" : ".rgb565";
        const auto name = std::string(property.key) + "-l" + std::to_string(level) + extension;
        std::ifstream stream(root / folder / name, std::ios::binary);
        assert(stream && "Generated derivatives must exist for all 110 states");
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
        const auto expectedLength = target == 0 ? kTileDevelopmentImageBytes :
            (target == 1 ? kPlayerDevelopmentImageBytes : property.webPngBytes[level - 1]);
        const auto expectedCrc = target == 2 ? property.webPngCrc32[level - 1] :
            developmentCrc32(&property, level, target == 0 ? DevelopmentTarget::Tile : DevelopmentTarget::Player);
        assert(bytes.size() == expectedLength);
        assert(artworkCrc32(bytes.data(), bytes.size()) == expectedCrc);
        bytes[bytes.size() / 2] ^= 0x01u;
        assert(artworkCrc32(bytes.data(), bytes.size()) != expectedCrc);
        ++filesChecked;
      }
    }
  }
  assert(keys.size() == 22 && filesChecked == 330);
  std::cout << "GRIDOPOLY_DEVELOPMENT_ARTWORK_PASS properties=22 levels=5 files=330 crc_corruption_rejected=330 devices=40\n";
}
