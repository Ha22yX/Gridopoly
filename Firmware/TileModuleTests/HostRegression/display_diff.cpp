#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <unity.h>

#include "display_diff.h"

using namespace gridopoly::tile;
namespace {
constexpr std::uint16_t kWidth = 240;
constexpr std::uint16_t kHeight = 320;
constexpr std::size_t kPixels = kWidth * kHeight;
using Frame = std::array<std::uint16_t, kPixels>;
Frame next{}, previous{}, panel{};
struct Rect { std::uint16_t x, y, width, height; };
struct Sink {
  std::vector<Rect> rects;
  int fail_after = -1;
  bool write(std::uint16_t x, std::uint16_t y, std::uint16_t width,
             std::uint16_t height, const std::uint16_t *frame,
             std::uint16_t stride) {
    if (fail_after == static_cast<int>(rects.size())) return false;
    TEST_ASSERT_TRUE(width > 0 && height > 0);
    TEST_ASSERT_TRUE(x + width <= kWidth && y + height <= kHeight);
    // The shadow must still describe the physical panel before submission.
    TEST_ASSERT_EQUAL_MEMORY(panel.data(), previous.data(), sizeof(panel));
    for (std::uint16_t row = y; row < y + height; ++row) {
      std::copy_n(frame + row * stride + x, width, panel.data() + row * stride + x);
    }
    rects.push_back({x, y, width, height});
    return true;
  }
};
DisplayTransfer present(Sink &sink, bool force = false) {
  return presentDisplayFrame(next.data(), previous.data(), kWidth, kHeight,
                             force, sink);
}
void assertConverged() {
  TEST_ASSERT_EQUAL_MEMORY(next.data(), panel.data(), sizeof(next));
  TEST_ASSERT_EQUAL_MEMORY(next.data(), previous.data(), sizeof(next));
}
void first_frame_and_reset_ignore_matching_shadow() {
  Sink sink;
  const auto first = present(sink, true);
  TEST_ASSERT_EQUAL_UINT32(kPixels, first.pixels);
  TEST_ASSERT_EQUAL_UINT32(1, first.rectangles);
  panel.fill(0xFFFF);  // Controller reset lost GRAM, host shadow still matches.
  previous = panel;  // Fake sink checks pre-submission ordering independently.
  const auto reset = present(sink, true);
  TEST_ASSERT_EQUAL_UINT32(kPixels, reset.pixels);
  assertConverged();
}
void identical_frame_sends_nothing() {
  Sink sink;
  const auto result = present(sink);
  TEST_ASSERT_TRUE(result.complete);
  TEST_ASSERT_EQUAL_UINT32(0, result.pixels);
  TEST_ASSERT_EQUAL_UINT32(0, result.rectangles);
}
void adjacent_spans_merge_and_do_not_erase_surroundings() {
  previous.fill(0x1234); next = previous; panel = previous;
  for (int y = 290; y < 298; ++y)
    std::fill_n(next.data() + y * kWidth + 16, 100, 0xABCD);
  Sink sink;
  const auto result = present(sink);
  TEST_ASSERT_EQUAL_UINT32(800, result.pixels);
  TEST_ASSERT_EQUAL_UINT32(1, result.rectangles);
  TEST_ASSERT_EQUAL_UINT16(290, sink.rects[0].y);
  TEST_ASSERT_EQUAL_UINT16(8, sink.rects[0].height);
  assertConverged();
}
void edges_gaps_and_disjoint_rows_are_not_lost() {
  next[0] = 1;
  next[kWidth - 1] = 2;
  next[2 * kWidth + 120] = 3;
  next[kPixels - 1] = 4;
  Sink sink;
  const auto result = present(sink);
  TEST_ASSERT_EQUAL_UINT32(242, result.pixels);
  TEST_ASSERT_EQUAL_UINT32(3, result.rectangles);
  assertConverged();
}
void failed_rectangle_keeps_shadow_retryable() {
  next[3] = 1; next[2 * kWidth + 8] = 2;
  Sink sink;
  sink.fail_after = 1;
  const auto failed = present(sink);
  TEST_ASSERT_FALSE(failed.complete);
  TEST_ASSERT_EQUAL_UINT16(1, previous[3]);
  TEST_ASSERT_EQUAL_UINT16(0, previous[2 * kWidth + 8]);
  sink.fail_after = -1;
  const auto retried = present(sink);
  TEST_ASSERT_TRUE(retried.complete);
  TEST_ASSERT_EQUAL_UINT32(1, retried.pixels);
  assertConverged();
}
void random_updates_reconstruct_each_frame_exactly() {
  std::uint32_t random = 0x12345678;
  for (int frame = 0; frame < 60; ++frame) {
    for (int change = 0; change < 70; ++change) {
      random = random * 1664525U + 1013904223U;
      const std::size_t offset = random % kPixels;
      random = random * 1664525U + 1013904223U;
      next[offset] = static_cast<std::uint16_t>(random >> 16U);
    }
    Sink sink;
    TEST_ASSERT_TRUE(present(sink).complete);
    assertConverged();
  }
}
}  // namespace
void setUp() { next.fill(0); previous.fill(0); panel.fill(0); }
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(first_frame_and_reset_ignore_matching_shadow);
  RUN_TEST(identical_frame_sends_nothing);
  RUN_TEST(adjacent_spans_merge_and_do_not_erase_surroundings);
  RUN_TEST(edges_gaps_and_disjoint_rows_are_not_lost);
  RUN_TEST(failed_rectangle_keeps_shadow_retryable);
  RUN_TEST(random_updates_reconstruct_each_frame_exactly);
  return UNITY_END();
}
