#include <array>
#include <vector>
#include <string>
#include <unity.h>
#include "ws2812_led.h"
#include "serial_command_line.h"

using namespace gridopoly::tile;
using namespace gridopoly::tile::ws2812;
struct Symbol { unsigned level0 = 0, duration0 = 0, level1 = 0, duration1 = 0; };
struct FakeDriver {
  using Symbol = ::Symbol;
  static inline int begin_error = 0, write_error = 0, wait_error = 0, stop_error = 0;
  static inline std::uint32_t clock = kClockHz;
  static inline unsigned writes = 0, waits = 0, stops = 0, wait_ms = 0;
  static inline std::vector<unsigned> low_intervals;
  static inline std::vector<Symbol> sent;
  int begin(std::uint32_t &hz) { hz = clock; return begin_error; }
  int write(const Symbol *data, std::size_t size) {
    ++writes; sent.assign(data, data + size); return write_error;
  }
  int wait(unsigned timeout) { ++waits; wait_ms = timeout; return wait_error; }
  int stop() { ++stops; return stop_error; }
  void holdLow(unsigned us) { low_intervals.push_back(us); }
};
void setUp() {
  FakeDriver::begin_error = FakeDriver::write_error = FakeDriver::wait_error = 0;
  FakeDriver::stop_error = 0;
  FakeDriver::clock = kClockHz;
  FakeDriver::writes = FakeDriver::waits = FakeDriver::stops = FakeDriver::wait_ms = 0;
  FakeDriver::low_intervals.clear(); FakeDriver::sent.clear();
}
void tearDown() {}

void test_every_byte_satisfies_exact_v6_timing() {
  Symbol symbols[24];
  const double tick_ns = 1.0e9 / kClockHz;
  for (unsigned value = 0; value < 256; ++value) {
    Rgb pixels[]{ {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value),
                  static_cast<std::uint8_t>(value)} };
    TEST_ASSERT_TRUE(encode(pixels, 1, symbols, 24));
    for (const auto &s : symbols) {
      const double high = tick_ns * s.duration0, low = tick_ns * s.duration1;
      const bool one = s.duration0 == kOneHigh;
      TEST_ASSERT_EQUAL_UINT(1, s.level0);
      TEST_ASSERT_EQUAL_UINT(0, s.level1);
      TEST_ASSERT_TRUE(high >= (one ? 580 : 220) && high <= (one ? 1000 : 380));
      TEST_ASSERT_TRUE(low >= 580 && low <= 1000);
      TEST_ASSERT_TRUE(high + low >= 1250);
    }
  }
  TEST_ASSERT_GREATER_THAN(280, kResetUs);
}
unsigned decode(const Symbol *symbols) {
  unsigned value = 0;
  for (unsigned bit = 0; bit < 8; ++bit)
    value = (value << 1U) | (symbols[bit].duration0 == kOneHigh ? 1U : 0U);
  return value;
}
void test_grb_msb_order_across_all_ten_pixels() {
  Rgb pixels[10]{}; Symbol symbols[240]{};
  for (unsigned i = 0; i < 10; ++i)
    pixels[i] = {static_cast<std::uint8_t>(1U << (i % 8)),
                 static_cast<std::uint8_t>(0x80U >> (i % 8)),
                 static_cast<std::uint8_t>(0xA5U ^ i)};
  TEST_ASSERT_TRUE(encode(pixels, 10, symbols, 240));
  for (unsigned i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_UINT8(pixels[i].green, decode(symbols + i * 24));
    TEST_ASSERT_EQUAL_UINT8(pixels[i].red, decode(symbols + i * 24 + 8));
    TEST_ASSERT_EQUAL_UINT8(pixels[i].blue, decode(symbols + i * 24 + 16));
  }
}
void test_encode_rejects_capacity_before_writing() {
  Rgb pixels[10]{}; Symbol symbols[240]{};
  symbols[0].duration0 = 1234;
  TEST_ASSERT_FALSE(encode(pixels, 10, symbols, 239));
  TEST_ASSERT_EQUAL_UINT(1234, symbols[0].duration0);
  TEST_ASSERT_FALSE(encode(pixels, 0, symbols, 240));
  TEST_ASSERT_FALSE(encode<Symbol>(nullptr, 10, symbols, 240));
  TEST_ASSERT_FALSE(encode<Symbol>(pixels, 10, nullptr, 240));
}
void test_valid_overrides_are_dim_single_channel_or_black() {
  const char *commands[]{"LED OFF 1", "LED RED 30", "LED GREEN 2", "LED BLUE 3"};
  const Rgb expected[]{ {0,0,0}, {8,0,0}, {0,8,0}, {0,0,8} };
  for (unsigned i = 0; i < 4; ++i) {
    Maintenance state; Rgb color{};
    TEST_ASSERT_TRUE(state.apply(parseCommand(commands[i]), 100));
    TEST_ASSERT_TRUE(state.color(100, color));
    TEST_ASSERT_EQUAL_UINT8(expected[i].red, color.red);
    TEST_ASSERT_EQUAL_UINT8(expected[i].green, color.green);
    TEST_ASSERT_EQUAL_UINT8(expected[i].blue, color.blue);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(30'000, state.remaining(100));
  }
}
void test_invalid_or_status_commands_cannot_change_override() {
  Maintenance state;
  state.apply(parseCommand("LED GREEN 5"), 100);
  const char *invalid[]{"LED", "LED OFF", "LED WHITE 1", "LED RED 0", "LED RED 31",
      "LED RED -1", "LED RED +1", "LED RED 1x", "LED RED 1 2", "LED RED 1 ",
      "LED BLUE 4294967297", "LED AUTO 1", "LED STATUS extra", "LED OFF  2"};
  for (const char *text : invalid) {
    const auto command = parseCommand(text);
    TEST_ASSERT_FALSE(command.valid);
    TEST_ASSERT_FALSE(state.apply(command, 200));
  }
  TEST_ASSERT_FALSE(state.apply(parseCommand("LED STATUS"), 200));
  TEST_ASSERT_EQUAL(Mode::Green, state.mode(200));
  TEST_ASSERT_EQUAL_UINT32(4900, state.remaining(200));
}
void test_expiry_and_auto_restore_scene_even_across_clock_wrap() {
  Maintenance state; Rgb color{};
  constexpr std::uint32_t start = 0xFFFFFFF0U;
  state.apply(parseCommand("LED OFF 1"), start);
  TEST_ASSERT_TRUE(state.color(start + 999U, color));
  TEST_ASSERT_FALSE(state.color(start + 1000U, color));
  TEST_ASSERT_EQUAL(Mode::Scene, state.mode(start + 1000U));
  // The same counter value one entire uint32 period later must not resurrect it.
  TEST_ASSERT_FALSE(state.color(start + 1U, color));
  state.apply(parseCommand("LED BLUE 30"), 200);
  TEST_ASSERT_TRUE(state.apply(parseCommand("LED AUTO"), 201));
  TEST_ASSERT_FALSE(state.color(201, color));
}
void test_overlong_serial_command_discards_even_valid_truncated_prefix() {
  SerialCommandLine<64> line;
  const std::string prefix = "LED RED " + std::string(54, '0') + "1";
  TEST_ASSERT_EQUAL_UINT(63, prefix.size());
  TEST_ASSERT_TRUE(parseCommand(prefix.c_str()).valid);
  for (char c : prefix + "garbage LED OFF 1")
    TEST_ASSERT_EQUAL(SerialLineResult::None, line.feed(c));
  TEST_ASSERT_EQUAL(SerialLineResult::TooLong, line.feed('\r'));
  TEST_ASSERT_EQUAL(SerialLineResult::None, line.feed('\n'));
  for (char c : std::string("LED STATUS")) line.feed(c);
  TEST_ASSERT_EQUAL(SerialLineResult::Line, line.feed('\n'));
  TEST_ASSERT_EQUAL_STRING("LED STATUS", line.data());
  for (char c : prefix) line.feed(c);
  TEST_ASSERT_EQUAL(SerialLineResult::Line, line.feed('\n'));
  TEST_ASSERT_EQUAL_STRING(prefix.c_str(), line.data());
}
void test_transmitter_checks_clock_and_driver_init() {
  Transmitter<FakeDriver, 10> tx;
  FakeDriver::clock = 8'000'000;
  TEST_ASSERT_FALSE(tx.begin());
  TEST_ASSERT_EQUAL_STRING("clock_mismatch", tx.status().stage);
  TEST_ASSERT_EQUAL_UINT(0, FakeDriver::writes);
  TEST_ASSERT_EQUAL_UINT(1, FakeDriver::stops);
  FakeDriver::begin_error = 257;
  TEST_ASSERT_FALSE(tx.begin());
  TEST_ASSERT_EQUAL_STRING("initialize", tx.status().stage);
  TEST_ASSERT_EQUAL_INT(257, tx.status().driver_error);
}
void test_completion_includes_initial_and_post_frame_low_interval() {
  Transmitter<FakeDriver, 10> tx; Rgb pixels[10]{};
  TEST_ASSERT_TRUE(tx.begin());
  TEST_ASSERT_TRUE(tx.send(pixels));
  TEST_ASSERT_EQUAL_UINT(240, FakeDriver::sent.size());
  TEST_ASSERT_EQUAL_UINT(20, FakeDriver::wait_ms);
  TEST_ASSERT_EQUAL_UINT(2, FakeDriver::low_intervals.size());
  for (unsigned us : FakeDriver::low_intervals) TEST_ASSERT_GREATER_THAN(280, us);
  TEST_ASSERT_EQUAL_UINT32(1, tx.status().completed);
  TEST_ASSERT_EQUAL_UINT32(0, tx.status().failed);
}
void test_write_error_stops_and_disables_retries() {
  Transmitter<FakeDriver, 10> tx; Rgb pixels[10]{};
  TEST_ASSERT_TRUE(tx.begin());
  FakeDriver::write_error = 259;
  TEST_ASSERT_FALSE(tx.send(pixels));
  TEST_ASSERT_FALSE(tx.send(pixels));
  TEST_ASSERT_EQUAL_UINT(1, FakeDriver::writes);
  TEST_ASSERT_EQUAL_UINT(0, FakeDriver::waits);
  TEST_ASSERT_EQUAL_UINT32(0, tx.status().completed);
  TEST_ASSERT_EQUAL_UINT32(1, tx.status().failed);
  TEST_ASSERT_EQUAL_INT(259, tx.status().driver_error);
  TEST_ASSERT_EQUAL_STRING("write", tx.status().stage);
}
void test_completion_timeout_retains_error_and_stop_error() {
  Transmitter<FakeDriver, 10> tx; Rgb pixels[10]{};
  TEST_ASSERT_TRUE(tx.begin());
  FakeDriver::wait_error = 263; FakeDriver::stop_error = 258;
  TEST_ASSERT_FALSE(tx.send(pixels));
  TEST_ASSERT_EQUAL_UINT32(0, tx.status().completed);
  TEST_ASSERT_EQUAL_INT(263, tx.status().driver_error);
  TEST_ASSERT_EQUAL_INT(258, tx.status().stop_error);
  TEST_ASSERT_EQUAL_STRING("wait", tx.status().stage);
  TEST_ASSERT_FALSE(tx.status().ready);
  TEST_ASSERT_EQUAL_UINT(1, FakeDriver::low_intervals.size());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_byte_satisfies_exact_v6_timing);
  RUN_TEST(test_grb_msb_order_across_all_ten_pixels);
  RUN_TEST(test_encode_rejects_capacity_before_writing);
  RUN_TEST(test_valid_overrides_are_dim_single_channel_or_black);
  RUN_TEST(test_invalid_or_status_commands_cannot_change_override);
  RUN_TEST(test_expiry_and_auto_restore_scene_even_across_clock_wrap);
  RUN_TEST(test_overlong_serial_command_discards_even_valid_truncated_prefix);
  RUN_TEST(test_transmitter_checks_clock_and_driver_init);
  RUN_TEST(test_completion_includes_initial_and_post_frame_low_interval);
  RUN_TEST(test_write_error_stops_and_disables_retries);
  RUN_TEST(test_completion_timeout_retains_error_and_stop_error);
  return UNITY_END();
}
