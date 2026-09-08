#include <unity.h>

#include "demo_engine.h"
#include "tile_assignment.h"
#include "tile_connection_view.h"
#include "tile_model.h"
#include "tile_network.h"
#include "tile_ui_theme.h"

using namespace gridopoly::tile;

void setUp() {}
void tearDown() {}

void test_scenarios_cover_single_tile_ui_states() {
  TEST_ASSERT_EQUAL_UINT32(9, demoScenarioCount());
  TEST_ASSERT_EQUAL(TileKind::Property, makeDemoScenario(0).kind);
  TEST_ASSERT_EQUAL(TileActivity::RentDue, makeDemoScenario(1).activity);
  TEST_ASSERT_EQUAL(TileKind::Transit, makeDemoScenario(3).kind);
  TEST_ASSERT_EQUAL(TileKind::Utility, makeDemoScenario(4).kind);
  TEST_ASSERT_EQUAL(TileKind::Card, makeDemoScenario(5).kind);
  TEST_ASSERT_EQUAL(TileKind::Fee, makeDemoScenario(6).kind);
  TEST_ASSERT_EQUAL(TileKind::Start, makeDemoScenario(7).kind);
  TEST_ASSERT_EQUAL(TileKind::Hold, makeDemoScenario(8).kind);
}

void test_purchase_state_keeps_asset_artwork_and_owner_identity() {
  const TileState available = makeDemoScenario(0);
  const TileState owned = makeDemoScenario(1);
  TEST_ASSERT_EQUAL(ArtworkId::RivetRow, available.artwork);
  TEST_ASSERT_EQUAL(ArtworkId::RivetRow, owned.artwork);
  TEST_ASSERT_FALSE(hasOwner(available));
  TEST_ASSERT_EQUAL_UINT16(60, available.purchase_price);
  TEST_ASSERT_TRUE(hasOwner(owned));
  TEST_ASSERT_EQUAL_UINT8(2, owned.owner_player);
  TEST_ASSERT_EQUAL_STRING("Player 2", owned.owner_display_name);
}
void test_normalize_limits_protocol_facing_values() {
  TileState state = makeDemoScenario(1);
  state.owner_player = 99;
  state.active_player = 99;
  state.building_level = 99;
  state.occupied_players = 0xFF;
  normalizeTileState(state);
  TEST_ASSERT_EQUAL_UINT8(0, state.owner_player);
  TEST_ASSERT_EQUAL_UINT8(0, state.active_player);
  TEST_ASSERT_EQUAL_UINT8(5, state.building_level);
  TEST_ASSERT_EQUAL_HEX8(0x3F, state.occupied_players);
}

void test_non_ownable_tile_cannot_keep_ownership() {
  TileState state = makeDemoScenario(6);
  state.owner_player = 3;
  state.building_level = 4;
  state.mortgaged = true;
  normalizeTileState(state);
  TEST_ASSERT_FALSE(hasOwner(state));
  TEST_ASSERT_EQUAL_UINT8(0, state.owner_player);
  TEST_ASSERT_EQUAL_UINT8(0, state.building_level);
  TEST_ASSERT_FALSE(state.mortgaged);
}

void test_demo_navigation_wraps() {
  DemoEngine demo(1000);
  demo.previous(10);
  TEST_ASSERT_EQUAL_UINT32(demoScenarioCount() - 1, demo.index());
  demo.next(20);
  TEST_ASSERT_EQUAL_UINT32(0, demo.index());
  demo.select(100, 30);
  TEST_ASSERT_EQUAL_UINT32(100 % demoScenarioCount(), demo.index());
}

void test_autoplay_uses_wrap_safe_elapsed_time() {
  DemoEngine demo(100);
  demo.select(0, 0xFFFFFFF0U);
  TEST_ASSERT_FALSE(demo.update(0x00000020U));
  TEST_ASSERT_TRUE(demo.update(0x00000080U));
  TEST_ASSERT_EQUAL_UINT32(1, demo.index());
  demo.setAutoplay(false, 0x00000081U);
  TEST_ASSERT_FALSE(demo.update(0x00001000U));
}

void test_visual_tokens_match_player_console_contract() {
  using namespace gridopoly::tile::theme;
  TEST_ASSERT_EQUAL_HEX16(0x0862, kBackground);
  TEST_ASSERT_EQUAL_HEX16(0x10C3, kPanel);
  TEST_ASSERT_EQUAL_HEX16(0x2186, kLine);
  TEST_ASSERT_EQUAL_HEX16(0xEF9E, kText);
  TEST_ASSERT_EQUAL_HEX16(0x8491, kMuted);
  TEST_ASSERT_EQUAL_HEX16(0x56F6, kGreen);
  TEST_ASSERT_EQUAL_HEX16(0xF62A, kYellow);
  TEST_ASSERT_EQUAL_HEX16(0xEB8D, kRed);
  TEST_ASSERT_EQUAL_HEX16(0x5D3D, kBlue);
  TEST_ASSERT_EQUAL_UINT8(88, kPlayerColors[0].red);
  TEST_ASSERT_EQUAL_UINT8(239, kPlayerColors[1].red);
  TEST_ASSERT_EQUAL_UINT8(194, kPlayerColors[4].red);
}
void test_server_assignment_maps_to_tile_state() {
  TileAssignmentDto assignment{};
  assignment.assigned = true;
  assignment.manual = true;
  assignment.map_index = 1;
  copyTileText(assignment.tile_id, sizeof(assignment.tile_id), "A1");
  copyTileText(assignment.display_name, sizeof(assignment.display_name), "Rivet Row");
  copyTileText(assignment.kind, sizeof(assignment.kind), "PROPERTY");
  assignment.accent_rgb = 0xC97852;
  copyTileText(assignment.artwork_key, sizeof(assignment.artwork_key), "a1-rivet-row");
  assignment.purchase_price = 60;
  assignment.owner_player = 2;
  copyTileText(assignment.owner_display_name,
               sizeof(assignment.owner_display_name), "Player 2");
  assignment.owner_rgb = 0xEF7168;
  assignment.revision = 12;

  TileState state{};
  TEST_ASSERT_TRUE(applyTileAssignment(assignment, state));
  TEST_ASSERT_EQUAL_UINT8(1, state.map_index);
  TEST_ASSERT_EQUAL_STRING("A1", state.tile_id);
  TEST_ASSERT_EQUAL_STRING("Rivet Row", state.display_name);
  TEST_ASSERT_EQUAL_STRING("FORGE QUARTER", state.subtitle);
  TEST_ASSERT_EQUAL(TileKind::Property, state.kind);
  TEST_ASSERT_EQUAL(TileActivity::Owned, state.activity);
  TEST_ASSERT_EQUAL(ArtworkId::RivetRow, state.artwork);
  TEST_ASSERT_EQUAL_UINT8(201, state.accent.red);
  TEST_ASSERT_EQUAL_UINT8(2, state.owner_player);
  TEST_ASSERT_EQUAL_STRING("Player 2", state.owner_display_name);
  TEST_ASSERT_EQUAL_STRING("MANUAL SERVER ASSIGNMENT", state.detail);
}

void test_auto_claim_start_tile_maps_without_owner() {
  TileAssignmentDto assignment{};
  assignment.assigned = true;
  assignment.map_index = 0;
  copyTileText(assignment.tile_id, sizeof(assignment.tile_id), "CORNER-START");
  copyTileText(assignment.display_name, sizeof(assignment.display_name), "Grid Central");
  copyTileText(assignment.kind, sizeof(assignment.kind), "START");
  assignment.accent_rgb = 0xE9EEF0;
  copyTileText(assignment.artwork_key, sizeof(assignment.artwork_key),
               "corner-start");

  TileState state{};
  TEST_ASSERT_TRUE(applyTileAssignment(assignment, state));
  TEST_ASSERT_EQUAL_UINT8(0, state.map_index);
  TEST_ASSERT_EQUAL(TileKind::Start, state.kind);
  TEST_ASSERT_EQUAL(TileActivity::Reward, state.activity);
  TEST_ASSERT_EQUAL(ArtworkId::CentralLaunch, state.artwork);
  TEST_ASSERT_EQUAL_STRING("AUTOMATIC SERVER CLAIM", state.detail);

  assignment.assigned = false;
  TEST_ASSERT_FALSE(applyTileAssignment(assignment, state));
}

void test_complete_grid_city_artwork_catalog_maps_locally() {
  const char *keys[] = {
      "a1-rivet-row", "a2-copper-lane", "b1-lantern-avenue",
      "b2-tideway-drive", "b3-beacon-boulevard", "c1-canvas-street",
      "c2-bloom-terrace", "c3-aurora-avenue", "d1-archive-way",
      "d2-forum-drive", "d3-meridian-avenue", "e1-pulse-street",
      "e2-prism-boulevard", "e3-nova-avenue", "f1-sunstep-terrace",
      "f2-helix-way", "f3-horizon-drive", "g1-canopy-lane",
      "g2-verdant-avenue", "g3-summit-boulevard", "h1-crown-promenade",
      "h2-grand-meridian", "transit-westline-terminal",
      "transit-northloop-station", "transit-eastgate-terminal",
      "transit-southline-depot", "utility-metro-grid",
      "utility-bluewater-works", "cover-chance", "cover-community-fund",
      "cover-income-tax", "cover-luxury-tax", "corner-central-launch",
      "corner-civic-hold", "corner-free-plaza", "corner-hold-order",
  };
  for (const char *key : keys) {
    TEST_ASSERT_NOT_EQUAL(ArtworkId::None, artworkFromServerKey(key));
  }
  TEST_ASSERT_EQUAL(ArtworkId::BeaconBoulevard,
                    artworkFromServerKey("b3-beacon-boulevard"));
  TEST_ASSERT_EQUAL(ArtworkId::None, artworkFromServerKey("unknown-artwork"));
}

void test_unassigned_connection_states_have_explicit_ui_copy() {
  TileConnectionView view =
      tileConnectionView(TileNetworkLink::WifiConnecting);
  TEST_ASSERT_EQUAL_STRING("CONNECTING", view.eyebrow);
  TEST_ASSERT_EQUAL_STRING("WI-FI", view.title);

  view = tileConnectionView(TileNetworkLink::ServerConnecting);
  TEST_ASSERT_EQUAL_STRING("WI-FI CONNECTED", view.eyebrow);
  TEST_ASSERT_EQUAL_STRING("REGISTERING", view.title);

  view = tileConnectionView(TileNetworkLink::NoFreeTile);
  TEST_ASSERT_EQUAL_STRING("SERVER ONLINE", view.eyebrow);
  TEST_ASSERT_EQUAL_STRING("WAITING FOR TILE", view.title);

  view = tileConnectionView(TileNetworkLink::Fault);
  TEST_ASSERT_EQUAL_STRING("RECONNECTING", view.eyebrow);
  TEST_ASSERT_EQUAL_STRING("SERVER ERROR", view.title);
}

void test_tag_reader_and_movement_wire_labels_are_frozen() {
  TEST_ASSERT_EQUAL_STRING("scanning",
                           tileTagReaderStateLabel(TileTagReaderState::Scanning));
  TEST_ASSERT_EQUAL_STRING("stable",
                           tileTagReaderStateLabel(TileTagReaderState::Stable));
  TEST_ASSERT_EQUAL_STRING("fault",
                           tileTagReaderStateLabel(TileTagReaderState::Fault));

  TileMovementCue cue = TileMovementCue::None;
  TEST_ASSERT_TRUE(parseTileMovementCue("none", cue));
  TEST_ASSERT_EQUAL(TileMovementCue::None, cue);
  TEST_ASSERT_TRUE(parseTileMovementCue("departure", cue));
  TEST_ASSERT_EQUAL(TileMovementCue::Departure, cue);
  TEST_ASSERT_TRUE(parseTileMovementCue("destination", cue));
  TEST_ASSERT_EQUAL(TileMovementCue::Destination, cue);
  TEST_ASSERT_FALSE(parseTileMovementCue("arriving", cue));
  TEST_ASSERT_EQUAL_STRING("departure",
                           tileMovementCueLabel(TileMovementCue::Departure));
  TEST_ASSERT_EQUAL_STRING("destination",
                           tileMovementCueLabel(TileMovementCue::Destination));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_scenarios_cover_single_tile_ui_states);
  RUN_TEST(test_purchase_state_keeps_asset_artwork_and_owner_identity);
  RUN_TEST(test_normalize_limits_protocol_facing_values);
  RUN_TEST(test_non_ownable_tile_cannot_keep_ownership);
  RUN_TEST(test_demo_navigation_wraps);
  RUN_TEST(test_autoplay_uses_wrap_safe_elapsed_time);
  RUN_TEST(test_visual_tokens_match_player_console_contract);
  RUN_TEST(test_server_assignment_maps_to_tile_state);
  RUN_TEST(test_auto_claim_start_tile_maps_without_owner);
  RUN_TEST(test_complete_grid_city_artwork_catalog_maps_locally);
  RUN_TEST(test_unassigned_connection_states_have_explicit_ui_copy);
  RUN_TEST(test_tag_reader_and_movement_wire_labels_are_frozen);
  return UNITY_END();
}
