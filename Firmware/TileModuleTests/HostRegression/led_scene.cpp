#include <algorithm>
#include <atomic>
#include <thread>
#include <unity.h>
#include "led_scene.h"
using namespace gridopoly::tile;

void setUp() {}
void tearDown() {}
void equalColor(Rgb a, Rgb b) {
  TEST_ASSERT_EQUAL_UINT8(a.red, b.red);
  TEST_ASSERT_EQUAL_UINT8(a.green, b.green);
  TEST_ASSERT_EQUAL_UINT8(a.blue, b.blue);
}
led_scene::Input destination(std::uint32_t origin = 1000) {
  led_scene::Input scene;
  scene.assigned = true;
  scene.movement = TileMovementCue::Destination;
  scene.movement_started_ms = origin;
  return scene;
}
void waveform_is_symmetric_continuous_and_slow_at_turnarounds() {
  auto scene = destination();
  unsigned previous = 0;
  for (unsigned t = 0; t <= 1200; ++t) {
    auto up = led_scene::color(scene, 1000 + t);
    auto down = led_scene::color(scene, 3400 - t);
    equalColor(up, down);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(previous, up.green);
    previous = up.green;
  }
  // Flat derivative at extrema survives the intentionally low RGB resolution.
  equalColor(led_scene::color(scene, 1000), led_scene::color(scene, 1080));
  equalColor(led_scene::color(scene, 2200), led_scene::color(scene, 2120));
  equalColor(led_scene::color(scene, 3399), led_scene::color(scene, 3400));
}
void phase_is_based_on_elapsed_time_and_handles_wrap() {
  auto normal = destination(1000), wrapped = destination(UINT32_MAX - 500U);
  for (std::uint32_t elapsed = 0; elapsed < 10000; elapsed += 17)
    equalColor(led_scene::color(normal, 1000 + elapsed),
               led_scene::color(wrapped, wrapped.movement_started_ms + elapsed));
  led_scene::Animator animation;
  animation.update(normal);
  (void)animation.sample(1100);
  equalColor(animation.sample(2100), led_scene::color(normal, 2100));
  // Delayed wakeups skip stale frames; they do not restart or replay a burst.
  equalColor(animation.sample(9700), led_scene::color(normal, 9700));
}
void producer_stalls_do_not_stop_the_owned_animation() {
  led_scene::Animator animation;
  animation.update(destination());
  std::atomic<bool> producer_has_stalled{false}, render_done{false};
  // Simulates a producer occupied by synchronous RF/LCD/network work. The
  // renderer has a value snapshot; it does not call or wait for the producer.
  std::thread producer([&] {
    producer_has_stalled.store(true);
    while (!render_done.load()) std::this_thread::yield();
  });
  while (!producer_has_stalled.load()) std::this_thread::yield();
  unsigned transitions = 0;
  bool smooth = true;
  auto last = animation.sample(1000);
  for (unsigned now = 1020; now <= 3400; now += led_scene::kFrameMs) {
    auto next = animation.sample(now);
    if (next.green != last.green) ++transitions;
    smooth = smooth && (next.green <= last.green + 1U) && (last.green <= next.green + 1U);
    last = next;
  }
  render_done.store(true);
  producer.join();
  TEST_ASSERT_TRUE(smooth);
  TEST_ASSERT_EQUAL_UINT(24, transitions);
}
void identical_scene_updates_do_not_restart_or_mutate_copied_state() {
  auto input = destination();
  led_scene::Animator animation;
  animation.update(input);
  input.movement = TileMovementCue::None;
  equalColor(animation.sample(2200), {5,15,12});
  for (unsigned now = 1000; now <= 6000; now += 137) {
    animation.update(destination());
    equalColor(animation.sample(now), led_scene::color(destination(), now));
  }
}
void pairing_lease_expires_without_a_producer_update() {
  auto input = destination();
  input.pairing = {true, TilePairingState::AwaitingTag, 1, 2, 3, 500, 1000, 1, {}};
  input.pairing_started_ms = 1000;
  led_scene::Animator animation;
  animation.update(input);
  TEST_ASSERT_GREATER_THAN_UINT8(animation.sample(1499).green, animation.sample(1499).blue);
  equalColor(animation.sample(1500), led_scene::color(destination(), 1500));
}
void local_preview_uses_real_waveform_and_restores_latest_scene() {
  led_scene::Animator animation;
  led_scene::Input original;
  original.assigned = true; original.ring_color = {255,0,0};
  animation.update(original);
  TEST_ASSERT_TRUE(animation.command(ws2812::parseCommand("LED DESTINATION 3"), 1000));
  for (unsigned now = 1000; now < 4000; now += 20)
    equalColor(animation.sample(now), led_scene::color(destination(), now));
  original.ring_color = {0,0,255};
  animation.update(original);
  equalColor(animation.sample(4000), {0,0,4});
  TEST_ASSERT_EQUAL(ws2812::Mode::Scene, animation.mode(4000));
  TEST_ASSERT_TRUE(animation.command(ws2812::parseCommand("LED DEPARTURE 30"), 5000));
  equalColor(animation.sample(5800), {14,8,4});
  animation.command(ws2812::parseCommand("LED AUTO"), 5801);
  equalColor(animation.sample(5801), {0,0,4});
}
void preview_bounds_queue_delay_and_wrap_do_not_extend_maintenance() {
  led_scene::Animator animation;
  auto input = destination();
  animation.update(input);
  for (const char *bad : {"LED DESTINATION 0", "LED DESTINATION 31", "LED DEPARTURE -1",
                          "LED DESTINATION 1 extra", "LED DESTINATION 4294967297"})
    TEST_ASSERT_FALSE(animation.command(ws2812::parseCommand(bad), 1000));
  constexpr auto start = UINT32_MAX - 99U;
  TEST_ASSERT_TRUE(animation.command(ws2812::parseCommand("LED DESTINATION 1"), start));
  TEST_ASSERT_EQUAL_UINT32(1, animation.remaining(start + 999U));
  equalColor(animation.sample(start + 1000U), led_scene::color(input, start + 1000U));
  equalColor(animation.sample(start + 1U), led_scene::color(input, start + 1U));
  animation.command(ws2812::parseCommand("LED DESTINATION 1"), 1000);
  // Command delayed in the queue arrives after its original deadline.
  equalColor(animation.sample(2500), led_scene::color(input, 2500));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(waveform_is_symmetric_continuous_and_slow_at_turnarounds);
  RUN_TEST(phase_is_based_on_elapsed_time_and_handles_wrap);
  RUN_TEST(producer_stalls_do_not_stop_the_owned_animation);
  RUN_TEST(identical_scene_updates_do_not_restart_or_mutate_copied_state);
  RUN_TEST(pairing_lease_expires_without_a_producer_update);
  RUN_TEST(local_preview_uses_real_waveform_and_restores_latest_scene);
  RUN_TEST(preview_bounds_queue_delay_and_wrap_do_not_extend_maintenance);
  return UNITY_END();
}
