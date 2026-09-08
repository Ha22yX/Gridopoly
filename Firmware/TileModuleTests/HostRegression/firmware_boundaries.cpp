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
std::uint32_t millis() { return 1000; }
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
std::uint32_t gMovementCueStartedMs = 0;
unsigned frames = 0;
void showPixels() { ++frames; }
const TileState& currentTileState() { return gNetworkSnapshot.tile; }
#include "firmware_fixture.inc"

void setUp() {
  gNetwork = TileNetworkClient{};
  gNetwork.mutex_ = &denyLock;
  denyLock = false;
  gTagStatus = TagStatus::Scanning;
  gConfirmedTagCount = 0;
  gConfirmedTagOverflow = false;
  gTagNetworkPublishPending = false;
  std::memset(gTagUids, 0, sizeof(gTagUids));
  std::memset(gTagMissingEvidence, 0, sizeof(gTagMissingEvidence));
  gNetworkSnapshot = {};
  gNetworkSnapshot.assigned = true;
  gMovementCueStartedMs = 1000;
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
void test_destination_double_flash_and_departure_breath() {
  gNetworkSnapshot.movement.mode = TileMovementCue::Destination;
  for (std::uint32_t elapsed = 0; elapsed < 1800; ++elapsed) {
    renderLedScene(1000 + elapsed);
    assertWholeRing();
    const auto phase = elapsed % 900;
    if (phase < 180 || (phase >= 300 && phase < 480)) {
      TEST_ASSERT_GREATER_THAN_UINT8(50, gPixels[0].green);
      TEST_ASSERT_GREATER_THAN_UINT8(gPixels[0].red, gPixels[0].green);
    } else {
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(2, gPixels[0].green);
    }
  }
  gNetworkSnapshot.movement.mode = TileMovementCue::Departure;
  renderLedScene(1000);
  const auto low = gPixels[0].red;
  renderLedScene(1800);
  const auto high = gPixels[0].red;
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
  TEST_ASSERT_EQUAL_UINT8(18, gPixels[0].red);
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_failed_publish_retries_identical_local_inventory);
  RUN_TEST(test_retry_publishes_latest_complete_state);
  RUN_TEST(test_full_inventory_and_overflow_remain_consistent);
  RUN_TEST(test_destination_double_flash_and_departure_breath);
  RUN_TEST(test_heartbeat_resync_preserves_animation_origin);
  RUN_TEST(test_transport_and_cue_updates_do_not_invalidate_tile_page);
  return UNITY_END();
}
