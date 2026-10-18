#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "avatar_v2_fixture.h"
#include "../../Server/RaspberryPi/src/AvatarRenderer.h"
#include "../../Server/RaspberryPi/src/IdentityModel.h"
#include "../../Server/RaspberryPi/src/FileIdentityStore.h"

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)

namespace {
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}

int main() {
  using namespace gridopoly::pi;
  using namespace gridopoly::protocol;
  const auto temporary = std::filesystem::temp_directory_path() / "gridopoly-avatar-v2-tests";
  std::filesystem::remove_all(temporary);
  const auto v2 = temporary / "components-v2";
  avatar_v2_fixture::catalog(v2);
  const auto v1 = std::filesystem::path(GRIDOPOLY_SOURCE_DIR) /
      "Assets/GridCity/Avatars/V1/runtime/components-v1";

  AvatarComponent neck{};
  const auto neckBytes = read(v2 / "neck/n1.gavc");
  CHECK(decodeAvatarComponent(neckBytes, AvatarComponentKind::Neck, 1, neck, 2));
  CHECK(neck.rgba.size() == 20 && neck.header.decodedBytes == 20);
  CHECK(!decodeAvatarComponent(neckBytes, AvatarComponentKind::Neck, 1, neck, 1));
  CHECK(!decodeAvatarComponent(neckBytes, AvatarComponentKind::Neck, 2, neck, 2));
  const auto faceBytes = read(v2 / "face/f1.gavc");
  AvatarComponent decoded{};
  CHECK(decodeAvatarComponent(faceBytes, AvatarComponentKind::Face, 1, decoded, 2));
  CHECK(!decodeAvatarComponent(faceBytes, AvatarComponentKind::Face, 1, decoded, 1));
  CHECK(!decodeAvatarComponent(read(v1 / "face/f1.gavc"), AvatarComponentKind::Face, 1, decoded, 2));
  for (const auto offset : {4u, 7u, 16u, 20u, 24u, 28u, 32u, 38u}) {
    auto bad = faceBytes;
    bad[offset] ^= 0x7F;
    CHECK(!decodeAvatarComponent(bad, AvatarComponentKind::Face, 1, decoded, 2));
  }
  auto bad = faceBytes;
  bad[12] = 220;
  CHECK(!decodeAvatarComponent(bad, AvatarComponentKind::Face, 1, decoded, 2));
  bad = faceBytes;
  bad.pop_back();
  CHECK(!decodeAvatarComponent(bad, AvatarComponentKind::Face, 1, decoded, 2));
  bad = faceBytes; bad.push_back(0);
  CHECK(!decodeAvatarComponent(bad, AvatarComponentKind::Face, 1, decoded, 2));
  bad.assign(kAvatarComponentMaximumFileBytes + 1, 0);
  CHECK(!decodeAvatarComponent(bad, AvatarComponentKind::Face, 1, decoded, 2));
  AvatarComponentKind kind{}; std::uint8_t preset{};
  CHECK(parseAvatarComponentRelative("neck/n1.gavc", kind, preset, 2));
  CHECK(kind == AvatarComponentKind::Neck && preset == 1);
  CHECK(!parseAvatarComponentRelative("neck/n1.gavc", kind, preset, 1));
  for (const auto name : {"neck/n2.gavc", "neck/n01.gavc", "neck/f1.gavc", "neck/../n1.gavc"})
    CHECK(!parseAvatarComponentRelative(name, kind, preset, 2));

  auto pixel = std::array<std::uint8_t, 4>{178,178,178,17};
  tintMaskedSkinPixel(pixel.data(), 255, skinPaletteRgb(8));
  CHECK((pixel == std::array<std::uint8_t,4>{91,53,42,17}));
  pixel = {255,255,255,128};
  tintMaskedSkinPixel(pixel.data(), 255, skinPaletteRgb(8));
  CHECK((pixel == std::array<std::uint8_t,4>{130,76,60,128}));
  pixel = {178,178,178,77};
  tintMaskedSkinPixel(pixel.data(), 128, skinPaletteRgb(8));
  CHECK((pixel == std::array<std::uint8_t,4>{134,115,110,77}));
  pixel = {220,230,240,255};
  tintMaskedSkinPixel(pixel.data(), 0, skinPaletteRgb(8));
  CHECK((pixel == std::array<std::uint8_t,4>{220,230,240,255}));
  pixel = {255,255,255,255};
  tintMaskedSkinPixel(pixel.data(), 255, skinPaletteRgb(1));
  CHECK((pixel == std::array<std::uint8_t,4>{255,255,248,255}));

  AvatarRenderer renderer(v1, temporary / "output", v2);
  CHECK(renderer.valid(1) && renderer.valid(2) && !renderer.valid(3));
  const AvatarRecipe recipe1{1,1,1,1,8,1};
  const AvatarRecipe recipe2{2,1,1,1,8,1};
  const auto preview1 = renderer.renderPreview(recipe1);
  const auto preview2 = renderer.renderPreview(recipe2);
  CHECK(preview1.ok && preview2.ok && preview1.path != preview2.path);
  CHECK(preview2.path.parent_path().filename() == "gavc-v2");
  const auto pixels = read(preview2.path);
  const auto offset = (100u * 220u + 100u) * 2u;
  CHECK(pixels.size() == 132000 && pixels[offset] == 5 && pixels[offset + 1] == 25);
  CHECK(pixels[offset + 6] == 0xA5 && pixels[offset + 7] == 0x59); // exposed Ebony neck
  const auto final1 = renderer.render(123, 1, 1, recipe1);
  const auto final2 = renderer.render(123, 1, 1, recipe2);
  CHECK(final1.ok && final2.ok && final1.pngPath != final2.pngPath);
  CHECK(final2.pngPath.parent_path().parent_path().filename() == "v2");
  CHECK(read(final2.rgb565Path).size() == 32768);
  CHECK(renderer.renderPreview(recipe2).contentHash64 == preview2.contentHash64);
  const auto failed = renderer.render(123, 2, 1, recipe2, true);
  CHECK(!failed.ok && !std::filesystem::exists(failed.pngPath));
  std::filesystem::remove(v2 / "neck/n1.gavc");
  AvatarRenderer incomplete(v1, temporary / "other", v2);
  CHECK(incomplete.valid(1) && !incomplete.valid(2));
  CHECK(!incomplete.renderPreview(recipe2).ok);

  IdentityRoomState room{};
  CHECK(initializeIdentityRoom(room, 123, 456, 1, 1, 2));
  CHECK(room.avatarCatalogVersion == 2 && room.seats[1].pendingRecipe.avatarCatalogVersion == 2);
  CHECK(validAvatarRecipe(recipe1) && validAvatarRecipe(recipe2));
  CHECK(!validAvatarRecipe({3,1,1,1,1,1}));
  auto& oldSeat = room.seats[0];
  oldSeat.avatarFinal = true; oldSeat.avatarRevision = 7;
  oldSeat.avatarContentHash64 = final1.contentHash64; oldSeat.recipe = recipe1;
  CHECK(validIdentityRoomState(room));
  FileIdentityStore store(temporary / "identity.bin");
  CHECK(store.save(room));
  IdentityRoomState restored{};
  CHECK(store.restore(restored));
  CHECK(restored.avatarCatalogVersion == 2 && restored.seats[0].recipe.avatarCatalogVersion == 1);

  IdentityRequest request{};
  request.operation = IdentityOperation::ConfirmAvatar; request.playerId = 1;
  request.requestId = 1; request.expectedStateVersion = 1; request.expectedSeatRevision = 1;
  request.avatarCatalogVersion = 2; request.recipe = recipe2;
  std::array<std::uint8_t, kMaxPayloadSize> bytes{}; std::size_t length = 0;
  CHECK(encodeIdentityRequest(request, bytes.data(), bytes.size(), length));
  IdentityRequest requestRead{};
  CHECK(decodeIdentityRequest(bytes.data(), length, requestRead));
  CHECK(requestRead.recipe.avatarCatalogVersion == 2 && length == kIdentityRequestSize);
  request.avatarCatalogVersion = 1;
  CHECK(!encodeIdentityRequest(request, bytes.data(), bytes.size(), length));
  IdentitySnapshot snapshot{};
  snapshot.avatarCatalogVersion = 2; snapshot.playerCount = 2; snapshot.selfPlayerId = 1;
  for (std::uint8_t index = 0; index < 2; ++index) {
    auto& seat = snapshot.seats[index];
    seat.playerId = index + 1; seat.seatRevision = 1; seat.seatColorId = index + 1;
    seat.flags = IdentitySeatPresent | IdentitySeatAvatarFinal |
        (index == 0 ? IdentitySeatHuman : IdentitySeatBot);
    seat.avatarRevision = 1; seat.avatarContentHash64 = 1;
    seat.recipe = index == 0 ? recipe1 : recipe2;
  }
  CHECK(encodeIdentitySnapshot(snapshot, bytes.data(), bytes.size(), length));
  IdentitySnapshot snapshotRead{};
  CHECK(decodeIdentitySnapshot(bytes.data(), length, snapshotRead));
  CHECK(snapshotRead.seats[0].recipe.avatarCatalogVersion == 1 &&
        snapshotRead.seats[1].recipe.avatarCatalogVersion == 2);
  snapshot.seats[1].recipe.avatarCatalogVersion = 3;
  CHECK(!encodeIdentitySnapshot(snapshot, bytes.data(), bytes.size(), length));
  std::cout << "GRIDOPOLY_AVATAR_V2_TESTS_PASS\n";
}
