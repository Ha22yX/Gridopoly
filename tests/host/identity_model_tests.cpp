#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "../../Server/RaspberryPi/src/FileIdentityStore.h"
#include "../../Server/RaspberryPi/src/IdentityModel.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {

using namespace gridopoly::pi;
using namespace gridopoly::protocol;

[[noreturn]] void failAssertion(const char* expression, const char* file, int line) {
  std::cerr << file << ':' << line << ": assertion failed: " << expression << '\n';
  std::exit(1);
}

#undef assert
#define assert(expression) \
  ((expression) ? static_cast<void>(0) : failAssertion(#expression, __FILE__, __LINE__))

std::filesystem::path temporaryPath(const char* leaf) {
  auto path = std::filesystem::temp_directory_path() / "gridopoly-identity-tests" / leaf;
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::remove(path, error);
  return path;
}

void expectInvalidName(const std::string& bytes) {
  std::array<char, 17> display{};
  std::string folded;
  assert(!validateIdentityName(bytes, display, folded));
}

}  // namespace

int main() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  assert(validAvatarRecipe({1, 1, 1, 1, 1, 1}));
  assert(validAvatarRecipe({1, 10, 20, 10, 8, 10}));
  assert(!validAvatarRecipe({0, 1, 1, 1, 1, 1}));
  assert(!validAvatarRecipe({1, 11, 1, 1, 1, 1}));
  assert(!validAvatarRecipe({1, 1, 21, 1, 1, 1}));
  assert(!validAvatarRecipe({1, 1, 1, 11, 1, 1}));
  assert(!validAvatarRecipe({1, 1, 1, 1, 9, 1}));
  assert(!validAvatarRecipe({1, 1, 1, 1, 1, 11}));

  std::array<char, 17> display{};
  std::string folded;
  assert(validateIdentityName("  Alice  ", display, folded));
  assert(std::string(display.data()) == "Alice");
  assert(folded == "alice");
  assert(validateIdentityName("\xC3\x84nne", display, folded));
  const auto foldedLatin = folded;
  assert(validateIdentityName("\xC3\xA4NNE", display, folded));
  assert(folded == foldedLatin);
  assert(validateIdentityName("\xE5\xBC\xA0\xE4\xB8\x89", display, folded));

  expectInvalidName("");
  expectInvalidName("       ");
  expectInvalidName("0123456789abcdefg");
  expectInvalidName(std::string("bad\0name", 8));
  expectInvalidName("bad\nname");
  expectInvalidName(std::string("\xC0\xAF", 2));
  expectInvalidName(std::string("\xED\xA0\x80", 3));
  expectInvalidName(std::string("\xF4\x90\x80\x80", 4));

  IdentityRoomState room{};
  assert(initializeIdentityRoom(room, 0x11223344u, 0x55667788u, 3, 3));
  assert(room.roomId == 0x11223344u && room.roomSeed == 0x55667788u);
  assert(room.humanCount == 3 && room.botCount == 3 && room.playerCount == 6);
  assert(room.phase == IdentityRoomPhase::AvatarSetup && room.identityRevision == 1);
  for (std::uint8_t index = 0; index < 3; ++index) {
    const auto& seat = room.seats[index];
    assert(seat.playerId == index + 1 && seat.human && !seat.bot);
    assert(seat.name[0] == '\0' && !seat.nameFinal && !seat.avatarFinal && !seat.ready);
    assert(seat.seatRevision == 1 && seat.seatColorId == index + 1);
  }
  for (std::uint8_t index = 3; index < 6; ++index) {
    const auto& seat = room.seats[index];
    assert(seat.playerId == index + 1 && seat.bot && !seat.human);
    assert(std::string(seat.name.data()) == "Bot " + std::to_string(index - 2));
    assert(seat.nameFinal && seat.avatarGenerating && !seat.avatarFinal && !seat.ready);
    assert(validAvatarRecipe(seat.pendingRecipe));
    const auto repeated = deterministicBotRecipe(room.roomSeed, seat.playerId,
                                                  kAvatarCatalogVersionV1);
    assert(repeated.hairPresetId == seat.pendingRecipe.hairPresetId);
    assert(repeated.hairColorId == seat.pendingRecipe.hairColorId);
    assert(repeated.facePresetId == seat.pendingRecipe.facePresetId);
    assert(repeated.skinToneId == seat.pendingRecipe.skinToneId);
    assert(repeated.outfitPresetId == seat.pendingRecipe.outfitPresetId);
  }
  IdentityRoomState invalid{};
  assert(!initializeIdentityRoom(invalid, 1, 2, 0, 2));
  assert(!initializeIdentityRoom(invalid, 1, 2, 1, 0));
  assert(!initializeIdentityRoom(invalid, 1, 2, 1, 6));
  assert(!initializeIdentityRoom(invalid, 1, 2, 6, 1));
  assert(!initializeIdentityRoom(invalid, 1, 2, 250, 8));

  assert(identityNameExists(room, "bot 1", 0));
  assert(identityNameExists(room, "BOT 3", 0));
  assert(!identityNameExists(room, "BOT 3", 6));
  assert(!identityNameExists(room, "Player", 0));

  auto invalidHumanLifecycle = room;
  std::memcpy(invalidHumanLifecycle.seats[0].name.data(), "Alice", 5);
  invalidHumanLifecycle.seats[0].nameFinal = true;
  assert(!validIdentityRoomState(invalidHumanLifecycle));
  invalidHumanLifecycle.phase = IdentityRoomPhase::Active;
  assert(validIdentityRoomState(invalidHumanLifecycle));

  auto& human = room.seats[0];
  human.connected = true;
  human.avatarFinal = true;
  human.avatarRevision = 1;
  human.avatarContentHash64 = 0x0102030405060708ull;
  human.recipe = {1, 9, 8, 7, 6, 5};
  human.name = {};
  std::memcpy(human.name.data(), "Player One", 10);
  human.nameFinal = true;
  human.ready = true;
  human.hasCachedRequest = true;
  human.lastRequestId = 0xA1B2C3D4u;
  for (std::size_t i = 0; i < human.lastRequest.size(); ++i) {
    human.lastRequest[i] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < human.cachedResponse.size(); ++i) {
    human.cachedResponse[i] = static_cast<std::uint8_t>(255u - i);
  }
  auto& generatingHuman = room.seats[1];
  generatingHuman.avatarGenerating = true;
  generatingHuman.pendingRecipe = {1, 10, 20, 9, 8, 7};
  room.phase = IdentityRoomPhase::Countdown;
  room.countdownDeadlineEpochMs = 1893456005000ull;
  room.identityRevision = 0x10203040u;

  const auto path = temporaryPath("identity.bin");
  FileIdentityStore store(path);
  assert(store.save(room));
  IdentityRoomState restored{};
  assert(store.restore(restored));
  assert(restored.roomId == room.roomId && restored.roomSeed == room.roomSeed);
  assert(restored.phase == IdentityRoomPhase::Countdown);
  assert(restored.countdownDeadlineEpochMs == room.countdownDeadlineEpochMs);
  assert(restored.identityRevision == room.identityRevision);
  assert(restored.seats[0].connected && restored.seats[0].avatarFinal &&
         restored.seats[0].ready);
  assert(restored.seats[1].avatarGenerating);
  assert(restored.seats[1].pendingRecipe.hairColorId == 20);
  assert(std::string(restored.seats[0].name.data()) == "Player One");
  assert(restored.seats[0].lastRequest == human.lastRequest);
  assert(restored.seats[0].cachedResponse == human.cachedResponse);

  std::ifstream validFile(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(validFile)), std::istreambuf_iterator<char>());
  validFile.close();
  assert(bytes.size() > 32);
  {
    std::ofstream corrupted(path, std::ios::binary | std::ios::trunc);
    bytes[bytes.size() / 2] ^= 0x55;
    corrupted.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  assert(!store.restore(restored));
  {
    std::ofstream truncated(path, std::ios::binary | std::ios::trunc);
    truncated.write(bytes.data(), 17);
  }
  assert(!store.restore(restored));
  assert(store.clear());
  assert(!std::filesystem::exists(path));

  std::cout << "GRIDOPOLY_IDENTITY_MODEL_TESTS_PASS\n";
  return 0;
}
