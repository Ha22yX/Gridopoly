#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <unity.h>
#include "led_scene.h"
using namespace gridopoly::tile;
using led_scene::FrameKind;
void setUp() {}
void tearDown() {}
void equalColor(Rgb a, Rgb b) {
  TEST_ASSERT_EQUAL_UINT8(a.red, b.red);
  TEST_ASSERT_EQUAL_UINT8(a.green, b.green);
  TEST_ASSERT_EQUAL_UINT8(a.blue, b.blue);
}
template <std::size_t N> void equalFrame(const Rgb (&a)[N], const Rgb (&b)[N]) {
  for (unsigned i = 0; i < N; ++i) equalColor(a[i], b[i]);
}
led_scene::Input destination(std::uint32_t origin = 1000) {
  led_scene::Input scene;
  scene.assigned = true; scene.movement = TileMovementCue::Destination;
  scene.movement_started_ms = origin;
  return scene;
}
led_scene::Input pairing(TilePairingState state, std::uint32_t origin = 1000) {
  auto scene = destination(origin);
  scene.pairing = {true, state, 1, 2, 3, 30000, origin, 1, {}};
  scene.pairing_started_ms = origin;
  return scene;
}
void single_head_moves_through_all_ten_leds_with_fading_tail() {
  const auto input = destination();
  Rgb pixels[10]{};
  for (unsigned head = 0; head < 10; ++head) {
    TEST_ASSERT_EQUAL(FrameKind::Meteor, led_scene::render(input, 1000 + head * 200, pixels));
    for (unsigned i = 0; i < 10; ++i) {
      const unsigned distance = (head + 10 - i) % 10;
      const unsigned expected[] = {32,18,8,2,0,0,0,0,0,0};
      TEST_ASSERT_EQUAL_UINT8(expected[distance], pixels[i].green);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(pixels[i].green, pixels[i].red);
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(pixels[i].green, pixels[i].blue);
    }
  }
  led_scene::render(input, 1100, pixels);
  equalColor(pixels[0], {9,25,20});
  equalColor(pixels[1], {6,16,13}); // head smoothly shared with the next LED
  TEST_ASSERT_EQUAL_UINT8(1, pixels[7].green); // tail fades, no hard disappearance
}
void motion_is_continuous_at_pixel_and_period_boundaries() {
  Rgb previous[10]{}, pixels[10]{};
  const auto input = destination();
  led_scene::render(input, 1000, previous);
  for (unsigned elapsed = 1; elapsed <= 6000; ++elapsed) {
    led_scene::render(input, 1000 + elapsed, pixels);
    unsigned peak = 0, illuminated = 0;
    for (unsigned i = 0; i < 10; ++i) {
      TEST_ASSERT_INT_WITHIN(1, previous[i].red, pixels[i].red);
      TEST_ASSERT_INT_WITHIN(1, previous[i].green, pixels[i].green);
      TEST_ASSERT_INT_WITHIN(1, previous[i].blue, pixels[i].blue);
      peak = std::max(peak, unsigned(pixels[i].green));
      illuminated += pixels[i].green != 0;
    }
    TEST_ASSERT_GREATER_THAN_UINT(16, peak);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(32, peak);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(4, illuminated);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(5, illuminated);
    std::copy(std::begin(pixels), std::end(pixels), previous);
  }
  Rgb beginning[10]{};
  led_scene::render(input, 1000, beginning);
  equalFrame(beginning, pixels);
}
template <unsigned Count> void checkGenericRing() {
  Rgb pixels[Count]{};
  const auto input = destination();
  for (unsigned t = 0; t <= 4000; t += 7) {
    led_scene::render(input, 1000 + t, pixels);
    unsigned total = 0;
    for (auto p : pixels) {
      TEST_ASSERT_LESS_OR_EQUAL_UINT8(32, p.green);
      total += p.green;
    }
    TEST_ASSERT_GREATER_THAN_UINT(0, total);
  }
}
void count_one_two_and_other_rings_are_bounded_and_nonempty() {
  checkGenericRing<1>(); checkGenericRing<2>(); checkGenericRing<7>();
  checkGenericRing<10>(); checkGenericRing<16>(); checkGenericRing<256>();
}
void phase_is_elapsed_time_and_handles_wrap_and_delayed_samples() {
  auto normal = destination(), wrapped = destination(UINT32_MAX - 500U);
  led_scene::Animator animation;
  animation.update(normal);
  Rgb a[10]{}, b[10]{};
  for (std::uint32_t elapsed = 0; elapsed < 10000; elapsed += 17) {
    led_scene::render(normal, 1000 + elapsed, a);
    led_scene::render(wrapped, wrapped.movement_started_ms + elapsed, b);
    equalFrame(a, b);
    animation.render(1000 + elapsed, b); equalFrame(a, b);
  }
  animation.render(97000, a);
  led_scene::render(normal, 97000, b); equalFrame(a, b);
}
void producer_stalls_and_repeated_value_updates_keep_motion() {
  led_scene::Animator animation;
  auto input = destination(); animation.update(input);
  input.movement = TileMovementCue::None; // animator copied the producer value
  std::atomic<bool> stalled{false}, done{false};
  std::thread producer([&] {
    stalled.store(true);
    while (!done.load()) std::this_thread::yield();
  });
  while (!stalled.load()) std::this_thread::yield();
  Rgb first[10]{}, last[10]{}, expected[10]{};
  animation.render(1000, first); animation.render(1200, last);
  bool moved = std::memcmp(first, last, sizeof(first)) != 0;
  done.store(true); producer.join();
  TEST_ASSERT_TRUE(moved);
  for (unsigned now = 1000; now < 6000; now += 137) {
    animation.update(destination()); animation.render(now, last);
    led_scene::render(destination(), now, expected); equalFrame(last, expected);
  }
}
void pairing_has_priority_colors_and_expires_without_producer() {
  Rgb pixels[10]{}, expected[10]{};
  for (unsigned state = 1; state <= 10; ++state) {
    auto input = pairing(static_cast<TilePairingState>(state));
    input.pairing.lease_ms = 500;
    led_scene::Animator animation; animation.update(input);
    animation.render(1000, pixels); equalColor(pixels[0], led_scene::pairingHead(input.pairing.state));
    animation.render(1499, pixels);
    led_scene::render(input, 1499, expected); equalFrame(pixels, expected);
    animation.render(1500, pixels);
    led_scene::render(destination(), 1500, expected); equalFrame(pixels, expected);
    input.movement = TileMovementCue::None; input.ring_color = {255,0,0};
    animation.update(input);
    TEST_ASSERT_EQUAL(FrameKind::Ordinary, animation.render(1500, pixels));
    for (auto p : pixels) equalColor(p, {4,0,0});
  }
  auto input = pairing(TilePairingState::AwaitingTag); input.assigned = false;
  TEST_ASSERT_EQUAL(FrameKind::Meteor, led_scene::render(input, 1000, pixels));
  input.pairing.active = false;
  TEST_ASSERT_EQUAL(FrameKind::Ordinary, led_scene::render(input, 1000, pixels));
}
void pairing_heartbeat_revision_name_and_lease_do_not_reset_phase() {
  auto previous = pairing(TilePairingState::AwaitingTag).pairing, next = previous;
  ++next.revision; std::strcpy(next.player_name, "NEW NAME");
  next.received_ms = 2000;
  TEST_ASSERT_FALSE(led_scene::pairingContextChanged(next, previous, 2000));
  for (unsigned change = 0; change < 5; ++change) {
    next = previous;
    if (change == 0) ++next.session_id;
    if (change == 1) ++next.room_id;
    if (change == 2) ++next.player_id;
    if (change == 3) next.state = TilePairingState::SuccessRemove;
    if (change == 4) next.active = false;
    TEST_ASSERT_TRUE(led_scene::pairingContextChanged(next, previous, 2000));
  }
  next = previous; next.received_ms = 31000;
  TEST_ASSERT_TRUE(led_scene::pairingContextChanged(next, previous, 31000));
}
void all_previews_use_production_frames_and_expire_to_latest_scene() {
  const char *commands[] = {"LED DESTINATION 3", "LED DEPARTURE 3", "LED PAIRING 3",
                            "LED PAIRING-SUCCESS 3", "LED PAIRING-CLEAR 3"};
  for (unsigned i = 0; i < 5; ++i) {
    led_scene::Animator animation;
    led_scene::Input ordinary; ordinary.assigned = true; ordinary.ring_color = {255,0,0};
    animation.update(ordinary);
    TEST_ASSERT_TRUE(animation.command(ws2812::parseCommand(commands[i]), 1000));
    auto real = destination();
    if (i == 1) real.movement = TileMovementCue::Departure;
    if (i >= 2) real = pairing(i == 2 ? TilePairingState::AwaitingTag :
                             i == 3 ? TilePairingState::SuccessRemove : TilePairingState::ClearRequired);
    Rgb a[10]{}, b[10]{};
    for (unsigned now = 1000; now < 4000; now += 20) {
      TEST_ASSERT_EQUAL(FrameKind::Meteor, animation.render(now, a));
      led_scene::render(real, now, b); equalFrame(a, b);
    }
    ordinary.ring_color = {0,0,255}; animation.update(ordinary);
    TEST_ASSERT_EQUAL(FrameKind::Ordinary, animation.render(4000, a));
    for (auto p : a) equalColor(p, {0,0,4});
    TEST_ASSERT_EQUAL(ws2812::Mode::Scene, animation.mode(4000));
  }
}
void preview_queue_delay_wrap_auto_and_pure_colors_keep_limits() {
  led_scene::Animator animation;
  animation.update(destination()); Rgb pixels[10]{}, expected[10]{};
  for (const char *bad : {"LED PAIRING 0", "LED PAIRING 31", "LED PAIRING-CLEAR -1",
                         "LED PAIRING 1 extra", "LED DESTINATION 4294967297"})
    TEST_ASSERT_FALSE(animation.command(ws2812::parseCommand(bad), 1000));
  constexpr auto start = UINT32_MAX - 99U;
  animation.command(ws2812::parseCommand("LED PAIRING 1"), start);
  TEST_ASSERT_EQUAL_UINT32(1, animation.remaining(start + 999U));
  animation.render(start + 1000U, pixels);
  led_scene::render(destination(), start + 1000U, expected); equalFrame(pixels, expected);
  animation.render(start + 1U, pixels); // expired window cannot return after counter wrap
  led_scene::render(destination(), start + 1U, expected); equalFrame(pixels, expected);
  animation.command(ws2812::parseCommand("LED PAIRING 1"), 1000);
  animation.render(2500, pixels); led_scene::render(destination(), 2500, expected); equalFrame(pixels, expected);
  animation.command(ws2812::parseCommand("LED RED 30"), 3000);
  TEST_ASSERT_EQUAL(FrameKind::Ordinary, animation.render(3000, pixels));
  for (auto p : pixels) equalColor(p, {8,0,0});
  animation.command(ws2812::parseCommand("LED AUTO"), 3001);
  TEST_ASSERT_EQUAL(FrameKind::Meteor, animation.render(3001, pixels));
}
void final_send_ceiling_tracks_actual_selected_frame() {
  struct Transmitter {
    Rgb sent[10]{};
    bool send(const Rgb (&p)[10]) { std::copy(std::begin(p), std::end(p), sent); return true; }
  } transmitter;
  led_scene::Animator animation;
  auto input = pairing(TilePairingState::AwaitingTag); input.movement = TileMovementCue::None;
  input.pairing.lease_ms = 500; animation.update(input);
  Rgb pixels[10]{};
  for (auto now : {1000U, 1499U, 1500U}) {
    const auto kind = animation.render(now, pixels);
    const auto limit = now < 1500 ? 32 : 16;
    for (auto &p : pixels) p = {255,255,255}; // even corrupt source amplitudes get capped
    led_scene::sendPixels(transmitter, pixels, kind);
    for (auto p : transmitter.sent) equalColor(p, {std::uint8_t(limit),std::uint8_t(limit),std::uint8_t(limit)});
  }
  animation.command(ws2812::parseCommand("LED PAIRING 1"), 2000);
  TEST_ASSERT_EQUAL(FrameKind::Meteor, animation.render(2999, pixels));
  TEST_ASSERT_EQUAL(FrameKind::Ordinary, animation.render(3000, pixels));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(single_head_moves_through_all_ten_leds_with_fading_tail);
  RUN_TEST(motion_is_continuous_at_pixel_and_period_boundaries);
  RUN_TEST(count_one_two_and_other_rings_are_bounded_and_nonempty);
  RUN_TEST(phase_is_elapsed_time_and_handles_wrap_and_delayed_samples);
  RUN_TEST(producer_stalls_and_repeated_value_updates_keep_motion);
  RUN_TEST(pairing_has_priority_colors_and_expires_without_producer);
  RUN_TEST(pairing_heartbeat_revision_name_and_lease_do_not_reset_phase);
  RUN_TEST(all_previews_use_production_frames_and_expire_to_latest_scene);
  RUN_TEST(preview_queue_delay_wrap_auto_and_pure_colors_keep_limits);
  RUN_TEST(final_send_ceiling_tracks_actual_selected_frame);
  return UNITY_END();
}
