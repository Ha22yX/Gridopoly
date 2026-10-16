#include <cctype>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unity.h>
#include <gridopoly/core/BoardCatalog.h>
#include "grid_city_visual_catalog.h"
#include "TileDebugAssignments.h"
#include "TagPairingModel.h"
#include "tile_assignment.h"
#include "tile_pairing_json.h"
#define private public
#include "tile_network.h"
#undef private

struct SilentSerial {
  template <typename... Args> void printf(const char*, Args...) {}
} Serial;
struct WifiBoundary { int RSSI() const { return -50; } } WiFi;
std::uint32_t millis() { return 1000; }
#include "network_fixture.inc"

namespace tile = gridopoly::tile;
namespace pi = gridopoly::pi;
namespace protocol = gridopoly::protocol;
using gridopoly::core::BoardCatalog;

void setUp() {}
void tearDown() {}

pi::TileDebugHeartbeatResponse assignment(unsigned size, unsigned index,
    pi::TileDebugAssignmentSource source = pi::TileDebugAssignmentSource::Order) {
  const auto* board = BoardCatalog::findBySize(static_cast<std::uint8_t>(size));
  // The catalog and display projection are production code, not copied maps.
  const auto visual = pi::projectTile(*board, static_cast<std::uint8_t>(index));
  pi::TileDebugHeartbeatResponse response{};
  response.assigned = true; response.source = source;
  response.serverRevision = 10; response.leaseMs = 15000;
  auto& dto = response.assignment;
  dto.moduleId = "tile-test"; dto.deviceId = "11:22:33:44:55:66";
  dto.tileId = visual.tileId; dto.mapIndex = visual.mapIndex;
  dto.displayName = visual.displayName; dto.kind = visual.kind;
  dto.accentRgb = visual.accentRgb; dto.artworkKey = visual.artworkKey;
  dto.purchasePrice = visual.purchasePrice; dto.source = source;
  dto.revision = 9; dto.orderEpoch = 2; dto.orderOffset = static_cast<int>(index);
  return response;
}

bool parse(const pi::TileDebugHeartbeatResponse& response,
           tile::TileNetworkSnapshot& snapshot,
           const protocol::TagPairSnapshot& pairing = {}) {
  tile::TileNetworkClient client;
  const auto payload = pi::tileModuleHeartbeatJson(response, "tile-test",
                                                  "11:22:33:44:55:66", pairing);
  return client.parsePayload(payload.c_str(), snapshot, false);
}

void all_sixteen_assignments_round_trip_with_artwork_and_economy() {
  const auto* board = BoardCatalog::findBySize(16);
  TEST_ASSERT_NOT_NULL(board);
  for (unsigned index = 0; index < 16; ++index) {
    const auto response = assignment(16, index);
    tile::TileNetworkSnapshot snapshot;
    TEST_ASSERT_TRUE(parse(response, snapshot));
    TEST_ASSERT_TRUE(snapshot.assigned);
    TEST_ASSERT_EQUAL_UINT8(index, snapshot.tile.map_index);
    TEST_ASSERT_EQUAL_STRING(board->tiles[index].id, snapshot.tile.tile_id);
    TEST_ASSERT_EQUAL_STRING(response.assignment.displayName.c_str(), snapshot.tile.display_name);
    TEST_ASSERT_EQUAL_STRING("order", snapshot.source);
    TEST_ASSERT_NOT_EQUAL(tile::ArtworkId::None, snapshot.tile.artwork);
    TEST_ASSERT_NOT_EQUAL(tile::TileKind::Disabled, snapshot.tile.kind);
    TEST_ASSERT_EQUAL_UINT16(response.assignment.purchasePrice, snapshot.tile.purchase_price);
    if (index % 4 == 0) {
      const tile::TileKind corners[] = {tile::TileKind::Start, tile::TileKind::Hold,
                                      tile::TileKind::Rest, tile::TileKind::GoToHold};
      TEST_ASSERT_EQUAL(corners[index / 4], snapshot.tile.kind);
    }
  }
}

void reanchor_then_revoke_and_recover_cannot_keep_old_tile_or_cue() {
  tile::TileNetworkSnapshot snapshot;
  TEST_ASSERT_TRUE(parse(assignment(16, 15), snapshot));
  TEST_ASSERT_EQUAL_STRING("C2", snapshot.tile.tile_id);
  auto rotated = assignment(16, 3, pi::TileDebugAssignmentSource::Manual);
  rotated.assignment.ownerPlayerId = 2;
  rotated.assignment.ownerDisplayName = "Alice";
  rotated.assignment.ownerRgb = 0xEF7168;
  rotated.movementCue.mode = pi::TileMovementCueMode::Destination;
  rotated.movementCue.playerId = 2; rotated.movementCue.revision = 11;
  TEST_ASSERT_TRUE(parse(rotated, snapshot));
  TEST_ASSERT_EQUAL_STRING("A2", snapshot.tile.tile_id);
  TEST_ASSERT_EQUAL_STRING("manual", snapshot.source);
  TEST_ASSERT_EQUAL_UINT8(2, snapshot.tile.owner_player);
  TEST_ASSERT_EQUAL(tile::TileMovementCue::Destination, snapshot.movement.mode);
  pi::TileDebugHeartbeatResponse revoked{};
  revoked.leaseMs = 15000; revoked.serverRevision = 12;
  TEST_ASSERT_TRUE(parse(revoked, snapshot));
  TEST_ASSERT_FALSE(snapshot.assigned);
  TEST_ASSERT_EQUAL_STRING("", snapshot.tile.tile_id);
  TEST_ASSERT_EQUAL(tile::TileMovementCue::None, snapshot.movement.mode);
  TEST_ASSERT_EQUAL(tile::TileNetworkLink::NoFreeTile, snapshot.link);
  TEST_ASSERT_TRUE(parse(assignment(16, 0), snapshot));
  TEST_ASSERT_EQUAL_STRING("CORNER-START", snapshot.tile.tile_id);
  TEST_ASSERT_EQUAL_UINT8(0, snapshot.tile.owner_player);
  TEST_ASSERT_EQUAL_STRING("order", snapshot.source);
}

void unassigned_pairing_survives_and_suppresses_movement_for_every_state() {
  for (unsigned value = 1; value <= 10; ++value) {
    protocol::TagPairSnapshot pairing{};
    pairing.state = static_cast<protocol::TagPairState>(value);
    pairing.roomId = 123; pairing.sessionId = 456; pairing.revision = 7;
    pairing.expiresInMs = 5000; pairing.activePlayerId = 1;
    std::strcpy(pairing.playerName.data(), "Alice");
    pi::TileDebugHeartbeatResponse unassigned{};
    unassigned.movementCue.mode = pi::TileMovementCueMode::Destination;
    unassigned.movementCue.playerId = 1; unassigned.movementCue.revision = 12;
    tile::TileNetworkSnapshot snapshot;
    TEST_ASSERT_TRUE(parse(unassigned, snapshot, pairing));
    TEST_ASSERT_FALSE(snapshot.assigned);
    TEST_ASSERT_TRUE(tile::pairingVisible(snapshot.pairing, 5999));
    TEST_ASSERT_FALSE(tile::pairingVisible(snapshot.pairing, 6000));
    TEST_ASSERT_EQUAL_UINT32(123, snapshot.pairing.room_id);
    TEST_ASSERT_EQUAL_UINT32(456, snapshot.pairing.session_id);
    TEST_ASSERT_EQUAL_STRING("Alice", snapshot.pairing.player_name);
    TEST_ASSERT_EQUAL_STRING(pi::tagPairStateName(pairing.state),
                             tile::tilePairingStateLabel(snapshot.pairing.state));
    TEST_ASSERT_EQUAL(tile::TileMovementCue::None, snapshot.movement.mode);
    TEST_ASSERT_TRUE(parse(unassigned, snapshot));
    TEST_ASSERT_FALSE(snapshot.pairing.active);
  }
}

void building_levels_round_trip_through_real_server_json_and_tile_projection() {
  auto response = assignment(16, 1);  // A1; stable artwork key, not board offset.
  response.assignment.ownerPlayerId = 1;
  tile::TileNetworkSnapshot previous;
  for (unsigned level : {0U, 1U, 2U, 3U, 4U, 5U, 0U}) {
    response.assignment.buildingLevel = static_cast<std::uint8_t>(level);
    tile::TileNetworkSnapshot current;
    TEST_ASSERT_TRUE(parse(response, current));
    TEST_ASSERT_EQUAL_UINT8(level, current.tile.building_level);
    TEST_ASSERT_EQUAL(tile::ArtworkId::RivetRow, current.tile.artwork);
    if (level != previous.tile.building_level)
      TEST_ASSERT_NOT_EQUAL(0, std::memcmp(&previous.tile, &current.tile, sizeof(tile::TileState)));
    previous = current;
  }
  response.assignment.ownerPlayerId = 0; response.assignment.buildingLevel = 5;
  TEST_ASSERT_TRUE(parse(response, previous));
  TEST_ASSERT_EQUAL_UINT8(0, previous.tile.building_level);
  response = assignment(16, 0);
  response.assignment.ownerPlayerId = 1; response.assignment.buildingLevel = 5;
  TEST_ASSERT_TRUE(parse(response, previous));
  TEST_ASSERT_EQUAL_UINT8(0, previous.tile.building_level);
}

void legacy_missing_level_defaults_zero_and_malformed_levels_are_rejected() {
  auto response = assignment(16, 1); response.assignment.ownerPlayerId = 1;
  const std::string payload = pi::tileModuleHeartbeatJson(response, "tile-test",
                                                        "11:22:33:44:55:66", {});
  const std::string field = ",\"buildingLevel\":0";
  const auto offset = payload.find(field);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, offset);
  tile::TileNetworkClient client; tile::TileNetworkSnapshot snapshot;
  std::string legacy = payload; legacy.erase(offset, field.size());
  TEST_ASSERT_TRUE(client.parsePayload(legacy.c_str(), snapshot, false));
  TEST_ASSERT_EQUAL_UINT8(0, snapshot.tile.building_level);
  for (const char *bad : {"-1", "6", "256", "1.5", "\"3\"", "true"}) {
    std::string invalid = payload;
    invalid.replace(offset, field.size(), std::string(",\"buildingLevel\":") + bad);
    TEST_ASSERT_FALSE(client.parsePayload(invalid.c_str(), snapshot, false));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(all_sixteen_assignments_round_trip_with_artwork_and_economy);
  RUN_TEST(reanchor_then_revoke_and_recover_cannot_keep_old_tile_or_cue);
  RUN_TEST(unassigned_pairing_survives_and_suppresses_movement_for_every_state);
  RUN_TEST(building_levels_round_trip_through_real_server_json_and_tile_projection);
  RUN_TEST(legacy_missing_level_defaults_zero_and_malformed_levels_are_rejected);
  return UNITY_END();
}
