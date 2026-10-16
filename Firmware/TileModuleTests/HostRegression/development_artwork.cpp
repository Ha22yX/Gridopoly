#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include <unity.h>
#include "development_artwork_cache.h"
#include "development_artwork_transfer.h"
#include "development_artwork_probe.h"

using namespace gridopoly::tile::development;
unsigned allocations = 0, releases = 0;
bool fail_allocation = false;
std::uint8_t *allocate(std::size_t bytes) {
  if (fail_allocation) return nullptr;
  ++allocations; return static_cast<std::uint8_t *>(std::malloc(bytes));
}
void release(void *pointer) { ++releases; std::free(pointer); }
void setUp() { allocations = releases = 0; fail_allocation = false; }
void tearDown() { TEST_ASSERT_EQUAL_UINT(allocations, releases); }
Key key(unsigned level = 1, const char *name = "a1-rivet-row") {
  return Key::from(name, static_cast<std::uint8_t>(level), true);
}
std::vector<std::uint8_t> fixture(Key id) {
  const std::string path = std::string(DEVELOPMENT_FIXTURE_DIR) + "/" + id.property->key +
                           "-l" + std::to_string(id.level) + ".rgb565";
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
struct Transport {
  std::vector<std::uint8_t> body;
  HttpReply reply{200, static_cast<int>(kImageBytes), 0, true};
  std::string path;
  std::size_t position = 0, chunk = 317;
  std::uint32_t tick = 0, step = 1;
  unsigned opened = 0, closed = 0, reads = 0;
  bool stall = false, keep_connected = false;
  explicit Transport(Key id) : body(fixture(id)) {}
  HttpReply open(const char *value) { path = value; ++opened; return reply; }
  int read(std::uint8_t *target, std::size_t capacity) {
    ++reads;
    const auto count = stall ? 0U : std::min({capacity, chunk, body.size() - position});
    std::memcpy(target, body.data() + position, count); position += count;
    return static_cast<int>(count);
  }
  bool connected() const { return keep_connected || position < body.size(); }
  std::uint32_t now() const { return tick; }
  void yield() { tick += step; }
  void close() { ++closed; }
};
Request requestFor(Cache &cache, Key selected, std::uint32_t start = 1000) {
  cache.online(true, start, 0);
  cache.select(selected, start, 0);
  Request request;
  TEST_ASSERT_TRUE(cache.next(start + 250, 0, request));
  return request;
}
void publish(Cache &cache, Key selected, std::uint32_t start) {
  auto request = requestFor(cache, selected, start);
  Transport transport(selected);
  TEST_ASSERT_EQUAL_UINT(kImageBytes, transport.body.size());
  const auto result = download(request, transport, [] { return false; });
  TEST_ASSERT_EQUAL(TransferState::Complete, result.state);
  TEST_ASSERT_TRUE(cache.finish(request, result, start + 500, 0));
}

void level_zero_nonproperty_unknown_never_allocate_or_request() {
  Cache cache(allocate, release); Request request;
  cache.online(true, 0, 0);
  const Key invalid[]{key(0), key(6), key(1, "corner-central-launch"),
                      key(1, "../../a1-rivet-row"), Key::from("a1-rivet-row", 1, false), Key{}};
  for (auto id : invalid) {
    TEST_ASSERT_FALSE(id.valid()); cache.select(id, 100, 0);
    TEST_ASSERT_FALSE(cache.next(10000, 0, request));
  }
  TEST_ASSERT_EQUAL_UINT(0, allocations);
  TEST_ASSERT_EQUAL_UINT32(0, cache.requests());
}
void current_level_only_atomic_publish_exact_le_pixels_and_cache_hit() {
  Cache cache(allocate, release); Request request;
  cache.online(true, 1000, 0); cache.select(key(3), 1000, 0);
  for (unsigned now = 1001; now < 1250; ++now) {
    cache.select(key(3), now, 0);
    TEST_ASSERT_FALSE(cache.next(now, 0, request));
  }
  TEST_ASSERT_TRUE(cache.next(1250, 0, request));
  Request second; TEST_ASSERT_FALSE(cache.next(9999, 0, second));
  TEST_ASSERT_NULL(cache.acquire(key(3)).pixels());
  Transport transport(key(3));
  auto result = download(request, transport, [] { return false; });
  TEST_ASSERT_EQUAL_STRING("/assets/development/v1/tile/a1-rivet-row-l3.rgb565", transport.path.c_str());
  TEST_ASSERT_EQUAL_UINT(1, transport.closed);
  TEST_ASSERT_NULL(cache.acquire(key(3)).pixels());
  TEST_ASSERT_TRUE(cache.finish(request, result, 1500, 0));
  auto lease = cache.acquire(key(3));
  TEST_ASSERT_EQUAL_MEMORY(transport.body.data(), lease.pixels(), kImageBytes);
  TEST_ASSERT_EQUAL_UINT16(transport.body[0] | (transport.body[1] << 8), lease.pixels()[0]);
  TEST_ASSERT_NULL(cache.acquire(key(2)).pixels());
  TEST_ASSERT_FALSE(cache.next(3000, 0, second));
  TEST_ASSERT_FALSE(cache.finish(request, result, 3000, 0));
  TEST_ASSERT_EQUAL_UINT32(1, cache.requests());
}
void transfer_failures_never_publish_and_always_close() {
  for (unsigned scenario = 0; scenario < 10; ++scenario) {
    Cache cache(allocate, release); auto request = requestFor(cache, key());
    Transport transport(key());
    switch (scenario) {
      case 0: transport.reply.content_length = -1; break;
      case 1: transport.reply.content_length = static_cast<int>(kImageBytes) - 1; break;
      case 2: transport.reply.content_length = static_cast<int>(kImageBytes) + 1; break;
      case 3: transport.body.resize(1234); break;
      case 4: transport.body[5000] ^= 1; break;
      case 5: transport.stall = transport.keep_connected = true; break;
      case 6: transport.chunk = 20; transport.step = 10; break;
      case 7: transport.reply.identity_encoding = false; break;
      case 8: transport.reply.status = 404; break;
      case 9: transport.reply.status = 503; transport.reply.retry_after_seconds = 5; break;
    }
    auto result = download(request, transport, [] { return false; });
    TEST_ASSERT_NOT_EQUAL(TransferState::Complete, result.state);
    TEST_ASSERT_FALSE(cache.finish(request, result, 2000, 0));
    TEST_ASSERT_NULL(cache.acquire(key()).pixels());
    TEST_ASSERT_EQUAL_UINT(0, cache.readyCount());
    TEST_ASSERT_EQUAL_UINT(1, transport.closed);
    TEST_ASSERT_FALSE(cache.busy());
  }
}
void selection_generation_rejects_reassign_downgrade_revoke_and_aba() {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    Cache cache(allocate, release); auto request = requestFor(cache, key(5));
    Transport transport(key(5));
    const auto result = download(request, transport, [] { return false; });
    const Key next = scenario == 0 ? key(1, "a2-copper-lane") : scenario == 1 ? key(2) : Key{};
    cache.select(next, 1500, 0);
    if (scenario == 4) cache.select(key(5), 1600, 0);
    TEST_ASSERT_FALSE(cache.finish(request, result, 1700, 0));
    TEST_ASSERT_NULL(cache.acquire(key(5)).pixels());
    TEST_ASSERT_NULL(cache.acquire(next).pixels());
    TEST_ASSERT_EQUAL_UINT32(1, cache.discarded());
  }
}
void offline_and_503_retry_after_survive_assignment_and_l0_changes() {
  Cache cache(allocate, release); Request request;
  cache.select(key(), 0, 0);
  TEST_ASSERT_FALSE(cache.next(5000, 0, request));
  cache.online(true, 5000, 0);
  TEST_ASSERT_FALSE(cache.next(5249, 0, request));
  TEST_ASSERT_TRUE(cache.next(5250, 0, request));
  Transport transport(key()); transport.reply.status = 503; transport.reply.retry_after_seconds = 5;
  auto result = download(request, transport, [] { return false; });
  cache.select({}, 5300, 0);
  TEST_ASSERT_FALSE(cache.finish(request, result, 5400, 0));
  cache.select(key(2), 5500, 0);
  cache.online(false, 6000, 0); cache.online(true, 6500, 0);
  for (unsigned now = 6500; now < 10400; now += 100) {
    cache.select(key(2), now, 0); TEST_ASSERT_FALSE(cache.next(now, 0, request));
  }
  TEST_ASSERT_TRUE(cache.next(10400, 0, request));
  transport.reply.status = 429; transport.reply.retry_after_seconds = 0;
  result = download(request, transport, [] { return false; });
  TEST_ASSERT_FALSE(cache.finish(request, result, 10500, 0));
  TEST_ASSERT_FALSE(cache.next(12499, 0, request));
  TEST_ASSERT_TRUE(cache.next(12500, 0, request));
  TEST_ASSERT_EQUAL_UINT8(2, cache.failures());
}
void five_ready_plus_staging_respects_pinned_draw_lease_and_lru() {
  Cache cache(allocate, release);
  for (unsigned level = 1; level <= 5; ++level) publish(cache, key(level), level * 1000);
  TEST_ASSERT_EQUAL_UINT(5, cache.readyCount());
  auto pinned = cache.acquire(key(1));
  const auto expected = fixture(key(1));
  auto request = requestFor(cache, key(1, "a2-copper-lane"), 7000);
  TEST_ASSERT_EQUAL_UINT(6 * kImageBytes, cache.allocatedBytes());
  Transport transport(request.key);
  const auto result = download(request, transport, [] { return false; });
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), pinned.pixels(), kImageBytes);
  TEST_ASSERT_TRUE(cache.finish(request, result, 7500, 0));
  TEST_ASSERT_EQUAL_UINT(5, cache.readyCount());
  TEST_ASSERT_NULL(cache.acquire(key(2)).pixels());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), pinned.pixels(), kImageBytes);
  publish(cache, key(1, "b1-lantern-avenue"), 8000);
  TEST_ASSERT_EQUAL_UINT(6, allocations);
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), pinned.pixels(), kImageBytes);
}
void all_pinned_entries_cannot_be_evicted_by_new_result() {
  Cache cache(allocate, release);
  for (unsigned level = 1; level <= 5; ++level) publish(cache, key(level), level * 1000);
  std::vector<Cache::Lease> leases;
  for (unsigned level = 1; level <= 5; ++level) leases.push_back(cache.acquire(key(level)));
  auto request = requestFor(cache, key(1, "a2-copper-lane"), 7000);
  Transport transport(request.key); auto result = download(request, transport, [] { return false; });
  TEST_ASSERT_FALSE(cache.finish(request, result, 7500, 0));
  TEST_ASSERT_EQUAL_UINT(5, cache.readyCount());
  TEST_ASSERT_NULL(cache.acquire(request.key).pixels());
  for (unsigned level = 1; level <= 5; ++level)
    TEST_ASSERT_EQUAL_MEMORY(fixture(key(level)).data(), leases[level - 1].pixels(), kImageBytes);
}
void allocation_failure_backs_off_without_internal_ram_or_job() {
  Cache cache(allocate, release); Request request;
  cache.online(true, 0, 0); cache.select(key(), 0, 0);
  fail_allocation = true;
  TEST_ASSERT_FALSE(cache.next(250, 0, request));
  TEST_ASSERT_FALSE(cache.busy()); TEST_ASSERT_EQUAL_UINT(0, cache.allocatedBytes());
  fail_allocation = false;
  TEST_ASSERT_FALSE(cache.next(1249, 0, request));
  TEST_ASSERT_TRUE(cache.next(1250, 0, request));
  TEST_ASSERT_EQUAL_UINT(1, allocations);
}
void cancelled_stream_and_true_worker_interleave_preserve_display_memory() {
  Cache cache(allocate, release); publish(cache, key(), 1000);
  auto visible = cache.acquire(key()); const auto expected = fixture(key());
  auto request = requestFor(cache, key(2), 2000);
  Transport transport(key(2));
  std::atomic<bool> worker_started{false}, proceed{false};
  std::atomic<std::uint32_t> generation{request.generation};
  TransferResult result;
  std::thread worker([&] {
    result = download(request, transport, [&] {
      if (transport.position >= 1024 && !worker_started.exchange(true))
        while (!proceed.load()) std::this_thread::yield();
      return generation.load() != request.generation;
    });
  });
  while (!worker_started.load()) std::this_thread::yield();
  cache.select(key(1, "a2-copper-lane"), 2300, 0);
  generation.store(cache.generation());
  const bool unchanged = std::memcmp(expected.data(), visible.pixels(), kImageBytes) == 0;
  proceed.store(true); worker.join();
  TEST_ASSERT_TRUE(unchanged);
  TEST_ASSERT_EQUAL(TransferState::Cancelled, result.state);
  TEST_ASSERT_FALSE(cache.finish(request, result, 2400, 0));
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), visible.pixels(), kImageBytes);
  TEST_ASSERT_NULL(cache.acquire(key(2)).pixels());
  TEST_ASSERT_EQUAL_UINT(1, transport.closed);
}
void rollover_and_reboot_leave_bounded_retry_and_empty_cache() {
  {
    Cache cache(allocate, release); Request request;
    const std::uint32_t start = 0xFFFFFF80U;
    cache.online(true, start, 0); cache.select(key(), start, 0);
    TEST_ASSERT_FALSE(cache.next(start + 249U, 0, request));
    TEST_ASSERT_TRUE(cache.next(start + 250U, 0, request));
    TransferResult failure;
    cache.finish(request, failure, start + 300U, 0);
    TEST_ASSERT_FALSE(cache.next(start + 1299U, 0, request));
    TEST_ASSERT_TRUE(cache.next(start + 1300U, 0, request));
  }
  Cache rebooted(allocate, release);
  TEST_ASSERT_EQUAL_UINT(0, rebooted.readyCount());
  TEST_ASSERT_EQUAL_UINT(0, rebooted.allocatedBytes());
  TEST_ASSERT_NULL(rebooted.acquire(key()).pixels());
}
void probe_uses_fixed_keys_levels_and_expires_or_stops_without_mutating_game() {
  Probe probe;
  TEST_ASSERT_FALSE(probe.start("corner-central-launch", 1, 0));
  TEST_ASSERT_FALSE(probe.start("a1-rivet-row", 256, 0));
  TEST_ASSERT_FALSE(probe.start("a1-rivet-row", 0, 0));
  const std::uint32_t start = 0xFFFFFF00U;
  TEST_ASSERT_TRUE(probe.start("a1-rivet-row", 5, start));
  TEST_ASSERT_FALSE(probe.expire(start + 29999U));
  TEST_ASSERT_TRUE(probe.expire(start + 30000U));
  TEST_ASSERT_FALSE(probe.active());
  TEST_ASSERT_TRUE(probe.start("a2-copper-lane", 1, 0));
  TEST_ASSERT_TRUE(probe.stop()); TEST_ASSERT_FALSE(probe.stop());
  const auto valid = parseCommand("ART PROBE A1-RIVET-ROW 3");
  TEST_ASSERT_EQUAL(CommandKind::Probe, valid.kind);
  TEST_ASSERT_EQUAL_STRING("a1-rivet-row", valid.key);
  const char *bad[]{"ART PROBE a1-rivet-row 0", "ART PROBE a1-rivet-row 6",
      "ART PROBE ../a1-rivet-row 1", "ART PROBE a1-rivet-row 2 junk", "ART PROBE a1-rivet-row -1"};
  for (auto text : bad) TEST_ASSERT_EQUAL(CommandKind::Invalid, parseCommand(text).kind);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(level_zero_nonproperty_unknown_never_allocate_or_request);
  RUN_TEST(current_level_only_atomic_publish_exact_le_pixels_and_cache_hit);
  RUN_TEST(transfer_failures_never_publish_and_always_close);
  RUN_TEST(selection_generation_rejects_reassign_downgrade_revoke_and_aba);
  RUN_TEST(offline_and_503_retry_after_survive_assignment_and_l0_changes);
  RUN_TEST(five_ready_plus_staging_respects_pinned_draw_lease_and_lru);
  RUN_TEST(all_pinned_entries_cannot_be_evicted_by_new_result);
  RUN_TEST(allocation_failure_backs_off_without_internal_ram_or_job);
  RUN_TEST(cancelled_stream_and_true_worker_interleave_preserve_display_memory);
  RUN_TEST(rollover_and_reboot_leave_bounded_retry_and_empty_cache);
  RUN_TEST(probe_uses_fixed_keys_levels_and_expires_or_stops_without_mutating_game);
  return UNITY_END();
}
