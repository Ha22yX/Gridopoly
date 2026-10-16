#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unity.h>
#include "board_config.h"
#include "hitag_s_protocol.h"
#include "tag_presence_filter.h"
#define private public
#include "tile_network.h"
#undef private
#include "tile_connection_view.h"
#include "tile_ui_theme.h"
#include "ws2812_led.h"

using namespace gridopoly::tile;
using namespace gridopoly::tile::board;
using namespace gridopoly::tile::theme;
using SemaphoreHandle_t = void*;
constexpr bool pdTRUE = true;
constexpr unsigned pdMS_TO_TICKS(unsigned value) { return value; }
bool denyLock = false;
bool xSemaphoreTake(SemaphoreHandle_t, unsigned) { return !denyLock; }
void xSemaphoreGive(SemaphoreHandle_t) {}
struct SilentSerial {
  template <typename... Args> void printf(const char*, Args...) {}
  void print(const char*) {}
} Serial;
#define F(text) text
std::uint32_t testMillis = 1000;
std::uint32_t millis() { return testMillis; }
TileNetworkClient gNetwork;
enum class TagStatus : std::uint8_t;
TagStatus gTagStatus = static_cast<TagStatus>(0);
hitag_s::Uid gConfirmedTagUids[hitag_s::kMaximumInventoryTags]{};
char gTagUids[hitag_s::kMaximumInventoryTags][9]{};
std::uint8_t gConfirmedTagCount = 0;
bool gConfirmedTagOverflow = false;
std::uint8_t gTagMissingEvidence[hitag_s::kMaximumInventoryTags]{};
std::uint8_t gTagPreferredSampling = 0;
bool gTagPreferredSamplingValid = false;
bool gTagUiDirty = false;
bool gTagNetworkPublishPending = false;
bool gHtrcTransmitterSafe = true;
TileNetworkSnapshot gNetworkSnapshot{};
Rgb gPixels[kWs2812Count]{};
bool gLedReady = true;
ws2812::Maintenance gLedMaintenance;
std::uint32_t gMovementCueStartedMs = 0;
std::uint32_t gPairingCueStartedMs = 0;
TagScanEvidence gTagScanEvidence{};
std::uint32_t gScanPairingRoom = 0, gScanPairingSession = 0;
unsigned frames = 0;
std::uint32_t gDisplayExperimentDurationMs = 0;
std::uint32_t gDisplayClockHz = kDisplaySpiFrequencyHz;
bool gDiagnosticPage = false;
unsigned displayEvents = 0;
unsigned displayResetOrder = 0;
unsigned displayRenderOrder = 0;
void recoverDisplayController() {
  TEST_ASSERT_EQUAL_UINT32(8'000'000, gDisplayClockHz);
  displayResetOrder = ++displayEvents;
}
void renderPage() {
  TEST_ASSERT_FALSE(gDiagnosticPage);
  displayRenderOrder = ++displayEvents;
}
struct PixelTransmitter {
  Rgb sent[kWs2812Count]{};
  bool send_ok = true;
  ws2812::SendStatus last_status{};
  bool send(const Rgb (&pixels)[kWs2812Count]) {
    ++frames;
    std::copy(std::begin(pixels), std::end(pixels), std::begin(sent));
    return send_ok;
  }
  const ws2812::SendStatus &status() const { return last_status; }
} gLedTransmitter;
const TileState& currentTileState() { return gNetworkSnapshot.tile; }
#include "firmware_fixture.inc"

void setUp() {
  gNetwork = TileNetworkClient{};
  gNetwork.mutex_ = &denyLock;
  denyLock = false;
  testMillis = 1000;
  gLedMaintenance = {};
  gLedTransmitter = {};
  gLedReady = true;
  std::fill(std::begin(gPixels), std::end(gPixels), Rgb{});
  gTagScanEvidence = {};
  gScanPairingRoom = gScanPairingSession = 0;
  gTagStatus = TagStatus::Scanning;
  gConfirmedTagCount = 0;
  gConfirmedTagOverflow = false;
  gTagNetworkPublishPending = false;
  std::memset(gTagUids, 0, sizeof(gTagUids));
  std::memset(gTagMissingEvidence, 0, sizeof(gTagMissingEvidence));
  gNetworkSnapshot = {};
  gNetworkSnapshot.assigned = true;
  gMovementCueStartedMs = 1000;
  gPairingCueStartedMs = 1000;
  frames = 0;
}
void tearDown() {}

hitag_s::UidSet playerTags() {
  hitag_s::UidSet set{};
  hitag_s::Uid uid{{0xDF, 0x24, 0xFA, 0x8E}};
  hitag_s::addUniqueUid(set, uid);
  return set;
}

void test_failed_publish_retries_identical_local_inventory() {
  const auto tags = playerTags();
  denyLock = true;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  TEST_ASSERT_EQUAL_STRING("8EFA24DF", gTagUids[0]);
  TEST_ASSERT_EQUAL_UINT8(0, gNetwork.observed_tags_.count);
  denyLock = false;
  // Same scan results must not permanently suppress a failed network update.
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  publishPendingTagObservation();
  TEST_ASSERT_EQUAL_UINT8(1, gNetwork.observed_tags_.count);
  TEST_ASSERT_EQUAL_STRING("8EFA24DF", gNetwork.observed_tags_.uids[0]);
  TEST_ASSERT_EQUAL_UINT32(1, gNetwork.observed_tags_.revision);
  for (unsigned i = 0; i < 10; ++i) {
    publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
    publishPendingTagObservation();
  }
  TEST_ASSERT_EQUAL_UINT32(1, gNetwork.observed_tags_.revision);
}

void test_retry_publishes_latest_complete_state() {
  auto tags = playerTags();
  tags.overflow = true;
  denyLock = true;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  publishTagInventory(TagStatus::NoTag, nullptr, 0x2F, 0);
  publishTagInventory(TagStatus::NoTag, nullptr, 0x2F, 0);
  denyLock = false;
  publishPendingTagObservation();
  TEST_ASSERT_EQUAL(TileTagReaderState::Stable, gNetwork.observed_tags_.state);
  TEST_ASSERT_EQUAL_UINT8(0, gNetwork.observed_tags_.count);
  TEST_ASSERT_FALSE(gNetwork.observed_tags_.overflow);
  TEST_ASSERT_EQUAL_UINT32(1, gNetwork.observed_tags_.revision);
}

void test_full_inventory_and_overflow_remain_consistent() {
  auto tags = playerTags();
  tags.overflow = true;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  publishPendingTagObservation();
  TEST_ASSERT_EQUAL(TileTagReaderState::Stable, gNetwork.observed_tags_.state);
  TEST_ASSERT_TRUE(gNetwork.observed_tags_.overflow);
  TEST_ASSERT_EQUAL_UINT8(1, gNetwork.observed_tags_.count);
  TEST_ASSERT_EQUAL_STRING("8EFA24DF", gNetwork.observed_tags_.uids[0]);
}

void assertWholeRing() {
  for (const auto& pixel : gPixels) {
    TEST_ASSERT_EQUAL_UINT8(gPixels[0].red, pixel.red);
    TEST_ASSERT_EQUAL_UINT8(gPixels[0].green, pixel.green);
    TEST_ASSERT_EQUAL_UINT8(gPixels[0].blue, pixel.blue);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(kMaximumLedChannel, pixel.red);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(kMaximumLedChannel, pixel.green);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(kMaximumLedChannel, pixel.blue);
  }
}
void assertSentColor(Rgb expected) {
  for (unsigned i = 0; i < kWs2812Count; ++i) {
    TEST_ASSERT_EQUAL_UINT8(expected.red, gLedTransmitter.sent[i].red);
    TEST_ASSERT_EQUAL_UINT8(expected.green, gLedTransmitter.sent[i].green);
    TEST_ASSERT_EQUAL_UINT8(expected.blue, gLedTransmitter.sent[i].blue);
  }
  assertWholeRing();
}
void test_final_send_caps_every_pixel_without_compounding() {
  // Direct, unscaled pixels must not escape the common send boundary.
  for (unsigned i = 0; i < kWs2812Count; ++i)
    gPixels[i] = {static_cast<std::uint8_t>(i == 0 ? 0 : 255), 17, 8};
  showPixels();
  for (unsigned i = 0; i < kWs2812Count; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i == 0 ? 0 : 16, gLedTransmitter.sent[i].red);
    TEST_ASSERT_EQUAL_UINT8(16, gLedTransmitter.sent[i].green);
    TEST_ASSERT_EQUAL_UINT8(8, gLedTransmitter.sent[i].blue);
  }
  Rgb sent[kWs2812Count];
  std::copy(std::begin(gLedTransmitter.sent), std::end(gLedTransmitter.sent), sent);
  showPixels();
  TEST_ASSERT_EQUAL_MEMORY(sent, gLedTransmitter.sent, sizeof(sent));
  TEST_ASSERT_EQUAL_UINT(2, frames);
}
void test_every_connection_state_is_dim_and_keeps_breath_period() {
  gNetworkSnapshot.assigned = false;
  // Peaks are the V0.34 wire RGB divided by four, rounded down.
  const Rgb peaks[]{{3,5,8}, {3,5,8}, {8,6,2}, {2,7,6},
                    {2,7,6}, {2,7,6}, {8,3,3}};
  for (unsigned state = 0; state < 7; ++state) {
    gNetworkSnapshot.link = static_cast<TileNetworkLink>(state);
    renderLedScene(0);
    const auto low = gPixels[0];
    for (std::uint32_t now = 0; now < 1600; ++now) {
      renderLedScene(now);
      assertWholeRing();
      for (const auto &pixel : gLedTransmitter.sent) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(8, pixel.red);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(8, pixel.green);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(8, pixel.blue);
      }
    }
    renderLedScene(800);
    assertSentColor(peaks[state]);
    renderLedScene(1600);
    assertSentColor(low);
  }
}
void test_all_pairing_states_share_dimming_including_success() {
  auto &pair = gNetworkSnapshot.pairing;
  pair.active = true; pair.room_id = 1; pair.session_id = 2;
  pair.received_ms = 1000; pair.lease_ms = 5000;
  gNetworkSnapshot.movement.mode = TileMovementCue::Destination;
  for (unsigned state = 1; state <= 10; ++state) {
    pair.state = static_cast<TilePairingState>(state);
    const bool success = pair.state == TilePairingState::SuccessRemove ||
                         pair.state == TilePairingState::Complete;
    const bool waiting = pair.state == TilePairingState::WaitingReader ||
                         pair.state == TilePairingState::AwaitingTag;
    renderLedScene(1000);
    const auto low = gPixels[0];
    for (std::uint32_t elapsed = 0; elapsed < 1400; ++elapsed) {
      renderLedScene(1000 + elapsed);
      assertWholeRing();
      if (success) assertSentColor({2,13,5});
    }
    renderLedScene(1700);
    assertSentColor(success ? Rgb{2,13,5} : waiting ? Rgb{4,6,15} : Rgb{15,6,2});
    renderLedScene(2400);
    assertSentColor(low);
  }
}
void test_idle_owned_and_occupied_scenes_are_dim() {
  auto &tile = gNetworkSnapshot.tile;
  tile.accent = {255,128,64};
  tile.owner_player = 0; tile.occupied_players = 0;
  renderLedScene(0);
  assertSentColor({4,2,1});
  tile.occupied_players = 1;
  renderLedScene(0);
  assertSentColor({7,3,1});
  renderLedScene(600);
  assertSentColor({14,7,3});
  renderLedScene(1200);
  assertSentColor({7,3,1});
  for (std::uint8_t owner = 1; owner <= kMaximumPlayers; ++owner) {
    tile.owner_player = owner;
    for (std::uint32_t now = 0; now < 1200; ++now) {
      renderLedScene(now);
      assertWholeRing();
      const auto &pixel = gLedTransmitter.sent[0];
      TEST_ASSERT_TRUE(pixel.red != 0 || pixel.green != 0 || pixel.blue != 0);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(14, pixel.red);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(14, pixel.green);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(14, pixel.blue);
    }
  }
}
void test_destination_double_flash_and_departure_breath() {
  gNetworkSnapshot.movement.mode = TileMovementCue::Destination;
  for (std::uint32_t elapsed = 0; elapsed < 1800; ++elapsed) {
    renderLedScene(1000 + elapsed);
    assertWholeRing();
    const auto phase = elapsed % 900;
    if (phase < 180 || (phase >= 300 && phase < 480)) {
      TEST_ASSERT_EQUAL_UINT8(5, gPixels[0].red);
      TEST_ASSERT_EQUAL_UINT8(15, gPixels[0].green);
      TEST_ASSERT_EQUAL_UINT8(12, gPixels[0].blue);
      TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].red, gPixels[0].green);
    } else {
      TEST_ASSERT_EQUAL_UINT8(0, gPixels[0].green);
    }
  }
  gNetworkSnapshot.movement.mode = TileMovementCue::Departure;
  renderLedScene(1000);
  const auto low = gPixels[0].red;
  renderLedScene(1800);
  const auto high = gPixels[0].red;
  TEST_ASSERT_EQUAL_UINT8(14, high);
  TEST_ASSERT_EQUAL_UINT8(8, gPixels[0].green);
  TEST_ASSERT_EQUAL_UINT8(4, gPixels[0].blue);
  TEST_ASSERT_GREATER_THAN_UINT8(low, high);
  TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].green, gPixels[0].red);
  renderLedScene(2600);
  TEST_ASSERT_EQUAL_UINT8(low, gPixels[0].red);
  assertWholeRing();
}
void test_heartbeat_resync_preserves_animation_origin() {
  auto snapshot = gNetworkSnapshot;
  snapshot.movement = {TileMovementCue::Destination, 1, 100};
  consumeMovement(snapshot, 1234);
  TEST_ASSERT_EQUAL_UINT32(1234, gMovementCueStartedMs);
  snapshot.server_revision = 999;
  snapshot.assignment_revision = 888;
  snapshot.sequence = 10;
  consumeMovement(snapshot, 4567);
  TEST_ASSERT_EQUAL_UINT32(1234, gMovementCueStartedMs);
  ++snapshot.movement.revision;
  consumeMovement(snapshot, 5000);
  TEST_ASSERT_EQUAL_UINT32(5000, gMovementCueStartedMs);
  snapshot.movement.mode = TileMovementCue::None;
  snapshot.tile.accent = {255, 0, 0};
  consumeMovement(snapshot, 6000);
  renderLedScene(6000);
  TEST_ASSERT_EQUAL_UINT8(4, gPixels[0].red);
  TEST_ASSERT_EQUAL_UINT8(0, gPixels[0].green);
  assertWholeRing();
}

void test_transport_and_cue_updates_do_not_invalidate_tile_page() {
  gNetworkSnapshot.assigned = true;
  auto snapshot = gNetworkSnapshot;
  ++snapshot.assignment_revision;
  ++snapshot.server_revision;
  ++snapshot.sequence;
  snapshot.movement = {TileMovementCue::Destination, 1, 101};
  snapshot.rssi = -85;
  snapshot.link = TileNetworkLink::Fault;
  TEST_ASSERT_FALSE(pageChanged(snapshot));
  snapshot.tile.building_level = 3;
  TEST_ASSERT_TRUE(pageChanged(snapshot));
  snapshot = gNetworkSnapshot;
  snapshot.assigned = false;
  TEST_ASSERT_TRUE(pageChanged(snapshot));
  gNetworkSnapshot.assigned = false;
  snapshot = gNetworkSnapshot;
  snapshot.http_status = 503;
  TEST_ASSERT_TRUE(pageChanged(snapshot));
}

void test_safe_display_restores_8mhz_controller_before_normal_page() {
  gDisplayExperimentDurationMs = 300000;
  gDisplayClockHz = 40'000'000;
  gDiagnosticPage = true;
  displayEvents = displayResetOrder = displayRenderOrder = 0;
  restoreSafeDisplay();
  TEST_ASSERT_EQUAL_UINT32(0, gDisplayExperimentDurationMs);
  TEST_ASSERT_EQUAL_UINT32(8'000'000, gDisplayClockHz);
  TEST_ASSERT_EQUAL_UINT32(1, displayResetOrder);
  TEST_ASSERT_EQUAL_UINT32(2, displayRenderOrder);
}

void test_display_clock_override_is_bound_to_validated_device() {
  TEST_ASSERT_EQUAL_UINT32(8'000'000, displaySpiFrequencyForDevice(nullptr));
  TEST_ASSERT_EQUAL_UINT32(8'000'000, displaySpiFrequencyForDevice(""));
  TEST_ASSERT_EQUAL_UINT32(8'000'000, displaySpiFrequencyForDevice("68:ee:8f:54:11:a4"));
  const char *validated = GRIDOPOLY_TILE_DISPLAY_VALIDATED_DEVICE;
  if (validated[0] != '\0') {
    TEST_ASSERT_EQUAL_UINT32(kDisplaySpiFrequencyHz, displaySpiFrequencyForDevice(validated));
  }
}

void test_freshness_expires_and_incomplete_inventory_cannot_refresh() {
  const auto tags = playerTags();
  gScanPairingRoom = 20; gScanPairingSession = 7;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  publishPendingTagObservation();
  auto old = gNetwork.observed_tags_;
  TEST_ASSERT_TRUE(tagScanFresh(old.scan, 5999));
  TEST_ASSERT_FALSE(tagScanFresh(old.scan, 6000));
  TEST_ASSERT_EQUAL_UINT32(20, old.scan.pairing_room_id);
  TEST_ASSERT_EQUAL_UINT32(7, old.scan.pairing_session_id);
  testMillis = 3000;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  publishPendingTagObservation();
  TEST_ASSERT_EQUAL_UINT32(old.revision, gNetwork.observed_tags_.revision);
  TEST_ASSERT_GREATER_THAN_UINT32(old.scan.sequence, gNetwork.observed_tags_.scan.sequence);
  TEST_ASSERT_TRUE(tagScanFresh(gNetwork.observed_tags_.scan, 7000));
  publishTagInventory(TagStatus::Unstable, nullptr, 0x2F, 0);
  publishPendingTagObservation();
  TEST_ASSERT_FALSE(tagScanFresh(gNetwork.observed_tags_.scan, 3000));
  TEST_ASSERT_EQUAL_STRING("8EFA24DF", gNetwork.observed_tags_.uids[0]);
  // Debounced retained presence must not become a new confirmed inventory.
  publishTagInventory(TagStatus::NoTag, nullptr, 0x2F, 0);
  publishPendingTagObservation();
  TEST_ASSERT_FALSE(tagScanFresh(gNetwork.observed_tags_.scan, 3000));
}
void test_pairing_uses_scanned_session_not_current_cached_session() {
  const auto tags = playerTags();
  gScanPairingRoom = 20; gScanPairingSession = 7;
  denyLock = true;
  publishTagInventory(TagStatus::Present, &tags, 0x2F, 3);
  gNetworkSnapshot.pairing.room_id = 21;
  gNetworkSnapshot.pairing.session_id = 8;
  denyLock = false;
  publishPendingTagObservation();
  TEST_ASSERT_EQUAL_UINT32(20, gNetwork.observed_tags_.scan.pairing_room_id);
  TEST_ASSERT_EQUAL_UINT32(7, gNetwork.observed_tags_.scan.pairing_session_id);
}
void test_pairing_priority_clear_and_lease_rollover() {
  auto &pair = gNetworkSnapshot.pairing;
  pair.active = true; pair.room_id = 10; pair.session_id = 20;
  pair.state = TilePairingState::AwaitingTag;
  pair.received_ms = UINT32_MAX - 100; pair.lease_ms = 200;
  TEST_ASSERT_TRUE(pairingVisible(pair, 50));
  TEST_ASSERT_FALSE(pairingVisible(pair, 99));
  pair.received_ms = 1000; pair.lease_ms = 5000;
  gNetworkSnapshot.movement.mode = TileMovementCue::Destination;
  renderLedScene(1000);
  TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].green, gPixels[0].blue);
  assertWholeRing();
  pair.state = TilePairingState::AlreadyBound;
  renderLedScene(1000);
  TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].green, gPixels[0].red);
  pair.active = false;
  renderLedScene(1000);
  TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].blue, gPixels[0].green);
  TEST_ASSERT_EQUAL_STRING("PAIRED! TAKE IT BACK", pairingInstruction(TilePairingState::SuccessRemove));
  TilePairingState state;
  TEST_ASSERT_FALSE(parseTilePairingState("unknown", state));
  TEST_ASSERT_TRUE(parseTilePairingState("already_bound", state));
  auto next = gNetworkSnapshot; next.pairing.active = true;
  TEST_ASSERT_TRUE(pageChanged(next));
}

void test_led_maintenance_expires_into_current_game_scene() {
  gNetworkSnapshot.movement.mode = TileMovementCue::Destination;
  const auto before = gNetworkSnapshot;
  const char *commands[]{"LED RED 1", "LED GREEN 1", "LED BLUE 1", "LED OFF 1"};
  const Rgb colors[]{{8,0,0}, {0,8,0}, {0,0,8}, {0,0,0}};
  for (unsigned i = 0; i < 4; ++i) {
    gLedMaintenance.apply(ws2812::parseCommand(commands[i]), 1000);
    renderLedScene(1999);
    assertSentColor(colors[i]);  // Fixed physical 8/255, not quartered to two.
    renderLedScene(2000);  // Destination is illuminated at this phase.
    assertSentColor({5,15,12});
    TEST_ASSERT_EQUAL(ws2812::Mode::Scene, gLedMaintenance.mode(1001));
    TEST_ASSERT_EQUAL_MEMORY(&before, &gNetworkSnapshot, sizeof(before));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_failed_publish_retries_identical_local_inventory);
  RUN_TEST(test_retry_publishes_latest_complete_state);
  RUN_TEST(test_full_inventory_and_overflow_remain_consistent);
  RUN_TEST(test_final_send_caps_every_pixel_without_compounding);
  RUN_TEST(test_every_connection_state_is_dim_and_keeps_breath_period);
  RUN_TEST(test_all_pairing_states_share_dimming_including_success);
  RUN_TEST(test_idle_owned_and_occupied_scenes_are_dim);
  RUN_TEST(test_destination_double_flash_and_departure_breath);
  RUN_TEST(test_heartbeat_resync_preserves_animation_origin);
  RUN_TEST(test_transport_and_cue_updates_do_not_invalidate_tile_page);
  RUN_TEST(test_safe_display_restores_8mhz_controller_before_normal_page);
  RUN_TEST(test_display_clock_override_is_bound_to_validated_device);
  RUN_TEST(test_freshness_expires_and_incomplete_inventory_cannot_refresh);
  RUN_TEST(test_pairing_uses_scanned_session_not_current_cached_session);
  RUN_TEST(test_pairing_priority_clear_and_lease_rollover);
  RUN_TEST(test_led_maintenance_expires_into_current_game_scene);
  return UNITY_END();
}
