#include <unity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "board_pins.h"
#include "display_startup.h"
#include "display_test_plan.h"

void setUp() {}
void tearDown() {}

namespace {

using tile_display_test::DisplayStartupPort;
using tile_display_test::StageId;

enum class StartupCall {
  SafeOutputs,
  SerialBegin,
  SpiBegin,
  SpiSpeed,
  PanelInit,
  Rotation,
  ClearBlack,
  BacklightAttach,
  BacklightFade,
};

struct RecordedCall {
  StartupCall call;
  std::array<std::int64_t, 4> arguments;
};

class RecordingStartupPort final : public DisplayStartupPort {
 public:
  void setSafeOutputs() override { record(StartupCall::SafeOutputs); }

  void beginSerial(std::uint32_t baud) override {
    record(StartupCall::SerialBegin, baud);
  }

  void beginSpi(int clock_pin, int miso_pin, int mosi_pin,
                int chip_select_pin) override {
    record(StartupCall::SpiBegin, clock_pin, miso_pin, mosi_pin,
           chip_select_pin);
  }

  void setDisplaySpiSpeed(std::uint32_t frequency_hz) override {
    record(StartupCall::SpiSpeed, frequency_hz);
  }

  void initializePanel(std::uint16_t width, std::uint16_t height,
                       std::uint8_t spi_mode) override {
    record(StartupCall::PanelInit, width, height, spi_mode);
  }

  void setRotation(std::uint8_t rotation) override {
    record(StartupCall::Rotation, rotation);
  }

  void clearBlack() override { record(StartupCall::ClearBlack); }

  void attachBacklightPwm(int pin, std::uint32_t frequency_hz,
                          std::uint8_t resolution_bits) override {
    record(StartupCall::BacklightAttach, pin, frequency_hz, resolution_bits);
  }

  void fadeBacklightTo(std::uint8_t duty) override {
    record(StartupCall::BacklightFade, duty);
  }

  const std::vector<RecordedCall> &calls() const { return calls_; }

 private:
  void record(StartupCall call, std::int64_t first = 0,
              std::int64_t second = 0, std::int64_t third = 0,
              std::int64_t fourth = 0) {
    calls_.push_back({call, {first, second, third, fourth}});
  }

  std::vector<RecordedCall> calls_;
};

void test_board_contract_matches_j4() {
  using namespace tile_display_test::board;

  TEST_ASSERT_EQUAL_INT(16, kLcdBacklightPin);
  TEST_ASSERT_EQUAL_INT(15, kLcdChipSelectPin);
  TEST_ASSERT_EQUAL_INT(7, kLcdClockPin);
  TEST_ASSERT_EQUAL_INT(6, kLcdMosiPin);
  TEST_ASSERT_EQUAL_INT(5, kLcdDataCommandPin);
  TEST_ASSERT_EQUAL_INT(4, kLcdResetPin);
  TEST_ASSERT_EQUAL_INT(-1, kNoMisoPin);
  TEST_ASSERT_EQUAL_UINT16(240, kDisplayWidth);
  TEST_ASSERT_EQUAL_UINT16(320, kDisplayHeight);
  TEST_ASSERT_EQUAL_UINT8(0, kSpiMode);
  TEST_ASSERT_EQUAL_UINT32(20'000'000, kSpiFrequencyHz);
}

void test_stage_sequence_covers_every_visual_diagnostic_once() {
  constexpr std::array<StageId, 10> kExpectedIds = {
      StageId::SolidRed,    StageId::SolidGreen, StageId::SolidBlue,
      StageId::SolidWhite,  StageId::SolidBlack, StageId::ColorBars,
      StageId::Geometry,    StageId::GridAndGray,
      StageId::Diagnostics, StageId::BacklightRamp,
  };

  TEST_ASSERT_EQUAL_size_t(kExpectedIds.size(),
                           tile_display_test::stageCount());

  std::array<bool, kExpectedIds.size()> seen{};
  for (std::size_t index = 0; index < kExpectedIds.size(); ++index) {
    const auto &stage = tile_display_test::stageAt(index);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(kExpectedIds[index]),
                            static_cast<std::uint8_t>(stage.id));
    TEST_ASSERT_NOT_NULL(stage.name);
    TEST_ASSERT_NOT_EQUAL(0, stage.name[0]);
    TEST_ASSERT_NOT_NULL(stage.expectation);
    TEST_ASSERT_NOT_EQUAL(0, stage.expectation[0]);
    TEST_ASSERT_GREATER_THAN_UINT32(0, stage.duration_ms);

    const auto id = static_cast<std::size_t>(stage.id);
    TEST_ASSERT_LESS_THAN_size_t(seen.size(), id);
    TEST_ASSERT_FALSE(seen[id]);
    seen[id] = true;
  }
}

void test_stage_navigation_wraps_and_normalizes_indices() {
  const auto count = tile_display_test::stageCount();
  TEST_ASSERT_EQUAL_size_t(0, tile_display_test::nextStageIndex(count - 1));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(tile_display_test::stageAt(0).id),
      static_cast<std::uint8_t>(tile_display_test::stageAt(count).id));
}

void test_elapsed_time_handles_millis_wraparound() {
  constexpr std::uint32_t kStarted =
      std::numeric_limits<std::uint32_t>::max() - 15U;

  TEST_ASSERT_FALSE(tile_display_test::stageElapsed(31U, kStarted, 48U));
  TEST_ASSERT_TRUE(tile_display_test::stageElapsed(32U, kStarted, 48U));
}

void test_startup_sequence_applies_safe_hardware_contract() {
  using namespace tile_display_test::board;

  RecordingStartupPort port;
  tile_display_test::initializeDisplay(port);

  const auto &calls = port.calls();
  constexpr std::array<StartupCall, 9> kExpectedCalls = {
      StartupCall::SafeOutputs,     StartupCall::SerialBegin,
      StartupCall::SpiBegin,        StartupCall::SpiSpeed,
      StartupCall::PanelInit,       StartupCall::Rotation,
      StartupCall::ClearBlack,      StartupCall::BacklightAttach,
      StartupCall::BacklightFade,
  };
  TEST_ASSERT_EQUAL_size_t(kExpectedCalls.size(), calls.size());
  for (std::size_t index = 0; index < kExpectedCalls.size(); ++index) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kExpectedCalls[index]),
                          static_cast<int>(calls[index].call));
  }

  TEST_ASSERT_EQUAL_INT64(kSerialBaud, calls[1].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kLcdClockPin, calls[2].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kNoMisoPin, calls[2].arguments[1]);
  TEST_ASSERT_EQUAL_INT64(kLcdMosiPin, calls[2].arguments[2]);
  TEST_ASSERT_EQUAL_INT64(kLcdChipSelectPin, calls[2].arguments[3]);
  TEST_ASSERT_EQUAL_INT64(kSpiFrequencyHz, calls[3].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kDisplayWidth, calls[4].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kDisplayHeight, calls[4].arguments[1]);
  TEST_ASSERT_EQUAL_INT64(kSpiMode, calls[4].arguments[2]);
  TEST_ASSERT_EQUAL_INT64(kDisplayRotation, calls[5].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kLcdBacklightPin, calls[7].arguments[0]);
  TEST_ASSERT_EQUAL_INT64(kBacklightPwmFrequencyHz, calls[7].arguments[1]);
  TEST_ASSERT_EQUAL_INT64(kBacklightPwmResolutionBits,
                          calls[7].arguments[2]);
  TEST_ASSERT_EQUAL_INT64(kBacklightTestDuty, calls[8].arguments[0]);
}

}  // namespace

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_board_contract_matches_j4);
  RUN_TEST(test_stage_sequence_covers_every_visual_diagnostic_once);
  RUN_TEST(test_stage_navigation_wraps_and_normalizes_indices);
  RUN_TEST(test_elapsed_time_handles_millis_wraparound);
  RUN_TEST(test_startup_sequence_applies_safe_hardware_contract);
  return UNITY_END();
}
