#include <unity.h>
#include "order_link.h"
using namespace gridopoly::tile::order;

namespace {
constexpr std::uint64_t kA = 0x0123456789ABCDEFULL;
constexpr std::uint64_t kB = 0xAABBCCDDEEFF1234ULL;
std::uint32_t feedFrame(Receiver &rx, const Frame &frame, std::uint32_t now) {
  rx.tick(now, true);
  now += 100'000; rx.tick(now, false);
  now += 90'000; rx.tick(now, true);
  now += 45'000; rx.tick(now, false);
  for (unsigned bit = 0; bit < 96; ++bit) {
    now += (frame[bit / 8] & (0x80U >> (bit % 8))) ? 30'000 : 15'000;
    rx.tick(now, true);
    if (bit != 95) { now += 15'000; rx.tick(now, false); }
  }
  return now;
}
void crc_reference_and_frame_corruption() {
  const auto *text = reinterpret_cast<const std::uint8_t *>("123456789");
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(text, 9));
  const Frame original = encode({kA, 65535});
  Beacon result;
  TEST_ASSERT_TRUE(decode(original, result));
  TEST_ASSERT_EQUAL_HEX64(kA, result.boot_id);
  TEST_ASSERT_EQUAL_UINT16(65535, result.sequence);
  for (unsigned bit = 0; bit < 96; ++bit) {
    Frame damaged = original; damaged[bit / 8] ^= 1U << (bit % 8);
    TEST_ASSERT_FALSE(decode(damaged, result));
  }
  TEST_ASSERT_FALSE(decode(encode({0, 1}), result));
}
void independent_links_report_only_immediate_upstream() {
  Link a, b, c;
  a.begin(kA, 0, true); b.begin(kB, 0, true); c.begin(0x33, 0, true);
  for (std::uint32_t now = 1000; now <= 12'000'000; now += 1000) {
    const bool a_low = a.tick(now, true);
    const bool b_low = b.tick(now, !a_low);
    c.tick(now, !b_low);
  }
  TEST_ASSERT_FALSE(a.receiver().valid());
  TEST_ASSERT_TRUE(b.receiver().valid());
  TEST_ASSERT_TRUE(c.receiver().valid());
  TEST_ASSERT_EQUAL_HEX64(kA, b.receiver().upstream().boot_id);
  TEST_ASSERT_EQUAL_HEX64(kB, c.receiver().upstream().boot_id);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, b.receiver().accepted());
}
void duplicate_old_frames_do_not_refresh_or_resurrect() {
  Receiver rx;
  auto now = feedFrame(rx, encode({kA, 7}), 0);
  TEST_ASSERT_TRUE(rx.valid());
  now = feedFrame(rx, encode({kA, 7}), now + 100000);
  TEST_ASSERT_EQUAL_UINT32(1, rx.accepted());
  TEST_ASSERT_GREATER_THAN_UINT32(1000, rx.ageMs(now));
  now += kFreshUs; rx.tick(now, true);
  TEST_ASSERT_FALSE(rx.valid());
  now = feedFrame(rx, encode({kA, 6}), now + 100000);
  TEST_ASSERT_FALSE(rx.valid());
  now = feedFrame(rx, encode({kA, 7}), now + 100000);
  TEST_ASSERT_FALSE(rx.valid());
  feedFrame(rx, encode({kA, 8}), now + 100000);
  TEST_ASSERT_TRUE(rx.valid());
}
void sequence_wrap_and_reboot_nonce() {
  Receiver rx;
  auto now = feedFrame(rx, encode({kA, 65535}), 0);
  now = feedFrame(rx, encode({kA, 0}), now + 100000);
  TEST_ASSERT_EQUAL_UINT32(2, rx.accepted());
  TEST_ASSERT_EQUAL_UINT16(0, rx.upstream().sequence);
  now = feedFrame(rx, encode({kA, 32768}), now + 100000);
  TEST_ASSERT_EQUAL_UINT32(2, rx.accepted());
  feedFrame(rx, encode({kB, 0}), now + 100000);
  TEST_ASSERT_EQUAL_HEX64(kB, rx.upstream().boot_id);
}
void unplug_and_stuck_low_fail_closed_then_recover() {
  Receiver rx;
  auto now = feedFrame(rx, encode({kA, 1}), 0);
  rx.tick(now + kFreshUs - 1, true); TEST_ASSERT_TRUE(rx.valid());
  now += kFreshUs; rx.tick(now, true); TEST_ASSERT_FALSE(rx.valid());
  now = feedFrame(rx, encode({kA, 2}), now + 100000);
  rx.tick(now + 1000, false);
  now += kStuckLowUs + 2000; rx.tick(now, false);
  TEST_ASSERT_TRUE(rx.stuckLow()); TEST_ASSERT_FALSE(rx.valid());
  now += 1000; rx.tick(now, true);
  TEST_ASSERT_FALSE(rx.stuckLow()); TEST_ASSERT_FALSE(rx.valid());
  feedFrame(rx, encode({kA, 3}), now + 100000);
  TEST_ASSERT_TRUE(rx.valid());
}
void malformed_pulse_and_crc_do_not_make_an_edge() {
  Receiver rx;
  Frame corrupt = encode({kA, 1}); corrupt[2] ^= 0x08;
  auto now = feedFrame(rx, corrupt, 0);
  TEST_ASSERT_FALSE(rx.valid());
  now += 100000; rx.tick(now, false);
  now += 90000; rx.tick(now, true);
  now += 45000; rx.tick(now, false);
  now += 22000; rx.tick(now, true);  // Gap between zero and one ranges.
  TEST_ASSERT_FALSE(rx.receiving()); TEST_ASSERT_FALSE(rx.valid());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, rx.rejected());
}
void late_scheduler_releases_output_and_requires_new_frame() {
  Link link;
  link.begin(kA, 0, true);
  bool pull = false;
  for (std::uint32_t now = 1000; now <= 100000; now += 1000) pull = link.tick(now, true);
  TEST_ASSERT_TRUE(pull);  // Sync pulse actively pulls the downstream wire low.
  TEST_ASSERT_FALSE(link.tick(108000, true));
  TEST_ASSERT_EQUAL_UINT32(1, link.timingDrops());
  for (std::uint32_t now = 109000; now < 6108000; now += 1000)
    TEST_ASSERT_FALSE(link.tick(now, true));
  TEST_ASSERT_TRUE(link.tick(6108000, true));
}
void timestamps_wrap_without_false_loss_or_extra_pulses() {
  Link a, b;
  constexpr std::uint32_t start = UINT32_MAX - 50000U;
  a.begin(kA, start, true); b.begin(kB, start, true);
  for (std::uint32_t elapsed = 1000; elapsed <= 6000000; elapsed += 1000) {
    const auto now = static_cast<std::uint32_t>(start + elapsed);
    b.tick(now, !a.tick(now, true));
  }
  TEST_ASSERT_TRUE(b.receiver().valid());
  TEST_ASSERT_EQUAL_UINT32(0, a.timingDrops());
  TEST_ASSERT_EQUAL_UINT32(0, b.timingDrops());
}
}  // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(crc_reference_and_frame_corruption);
  RUN_TEST(independent_links_report_only_immediate_upstream);
  RUN_TEST(duplicate_old_frames_do_not_refresh_or_resurrect);
  RUN_TEST(sequence_wrap_and_reboot_nonce);
  RUN_TEST(unplug_and_stuck_low_fail_closed_then_recover);
  RUN_TEST(malformed_pulse_and_crc_do_not_make_an_edge);
  RUN_TEST(late_scheduler_releases_output_and_requires_new_frame);
  RUN_TEST(timestamps_wrap_without_false_loss_or_extra_pulses);
  return UNITY_END();
}
