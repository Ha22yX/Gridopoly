#include <Arduino.h>
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "board_pins.h"
#include "display_startup.h"
#include "display_test_plan.h"

namespace {

using tile_display_test::Stage;
using tile_display_test::StageId;
using namespace tile_display_test::board;

SPIClass display_spi(FSPI);
Adafruit_ST7789 display(&display_spi, kLcdChipSelectPin,
                        kLcdDataCommandPin, kLcdResetPin);

bool backlight_pwm_ready = false;
std::size_t active_stage_index = 0;
std::uint32_t stage_started_ms = 0;
std::uint32_t last_diagnostic_update_ms = 0;
std::uint32_t diagnostic_frame_count = 0;

void writeBacklightDuty(std::uint8_t duty) {
  if (backlight_pwm_ready) {
    ledcWrite(kBacklightPwmChannel, duty);
  } else {
    digitalWrite(kLcdBacklightPin, duty == 0U ? LOW : HIGH);
  }
}

class ArduinoDisplayStartupPort final
    : public tile_display_test::DisplayStartupPort {
 public:
  void setSafeOutputs() override {
    digitalWrite(kLcdBacklightPin, LOW);
    pinMode(kLcdBacklightPin, OUTPUT);

    digitalWrite(kLcdChipSelectPin, HIGH);
    pinMode(kLcdChipSelectPin, OUTPUT);

    digitalWrite(kLcdClockPin, LOW);
    pinMode(kLcdClockPin, OUTPUT);
    digitalWrite(kLcdMosiPin, LOW);
    pinMode(kLcdMosiPin, OUTPUT);

    digitalWrite(kLcdDataCommandPin, LOW);
    pinMode(kLcdDataCommandPin, OUTPUT);
    digitalWrite(kLcdResetPin, HIGH);
    pinMode(kLcdResetPin, OUTPUT);
  }

  void beginSerial(std::uint32_t baud) override { Serial.begin(baud); }

  void beginSpi(int clock_pin, int miso_pin, int mosi_pin,
                int chip_select_pin) override {
    display_spi.begin(clock_pin, miso_pin, mosi_pin, chip_select_pin);
  }

  void setDisplaySpiSpeed(std::uint32_t frequency_hz) override {
    display.setSPISpeed(frequency_hz);
  }

  void initializePanel(std::uint16_t width, std::uint16_t height,
                       std::uint8_t spi_mode) override {
    display.init(width, height, spi_mode);
  }

  void setRotation(std::uint8_t rotation) override {
    display.setRotation(rotation);
  }

  void clearBlack() override { display.fillScreen(ST77XX_BLACK); }

  void attachBacklightPwm(int pin, std::uint32_t frequency_hz,
                          std::uint8_t resolution_bits) override {
    const auto configured_frequency =
        ledcSetup(kBacklightPwmChannel, frequency_hz, resolution_bits);
    backlight_pwm_ready = configured_frequency != 0U;
    if (backlight_pwm_ready) {
      ledcAttachPin(pin, kBacklightPwmChannel);
    }
    if (!backlight_pwm_ready) {
      Serial.println(F("WARN: backlight PWM attach failed; using digital output"));
    }
  }

  void fadeBacklightTo(std::uint8_t duty) override {
    constexpr std::uint8_t kFadeStep = 4;
    for (std::uint16_t value = 0; value < duty; value += kFadeStep) {
      writeBacklightDuty(static_cast<std::uint8_t>(value));
      delay(8);
    }
    writeBacklightDuty(duty);
  }
};

ArduinoDisplayStartupPort startup_port;

void setDefaultText(std::uint16_t foreground = ST77XX_WHITE,
                    std::uint16_t background = ST77XX_BLACK) {
  display.setTextWrap(false);
  display.setTextSize(2);
  display.setTextColor(foreground, background);
}

void drawCenteredText(const char *text, std::int16_t y, std::uint8_t size,
                      std::uint16_t foreground,
                      std::uint16_t background) {
  display.setTextSize(size);
  display.setTextColor(foreground, background);
  std::int16_t x1 = 0;
  std::int16_t y1 = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  display.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  const auto x = static_cast<std::int16_t>(
      std::max<std::int32_t>(0, (display.width() - width) / 2));
  display.setCursor(x, y);
  display.print(text);
}

void renderSolid(std::uint16_t color) { display.fillScreen(color); }

void renderColorBars() {
  constexpr std::uint16_t kColors[] = {
      ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE,
      ST77XX_CYAN, ST77XX_MAGENTA, ST77XX_YELLOW,
  };
  constexpr const char *kLabels[] = {"R", "G", "B", "C", "M", "Y"};
  constexpr std::size_t kBarCount = sizeof(kColors) / sizeof(kColors[0]);
  const auto bar_width =
      static_cast<std::int16_t>(display.width() / kBarCount);

  display.fillScreen(ST77XX_BLACK);
  for (std::size_t index = 0; index < kBarCount; ++index) {
    const auto x = static_cast<std::int16_t>(index * bar_width);
    const auto width = static_cast<std::int16_t>(
        index + 1U == kBarCount ? display.width() - x : bar_width);
    display.fillRect(x, 0, width, display.height(), kColors[index]);
    display.setTextSize(3);
    display.setTextColor(index == 1U || index == 3U || index == 5U
                             ? ST77XX_BLACK
                             : ST77XX_WHITE);
    display.setCursor(x + 11, display.height() / 2 - 12);
    display.print(kLabels[index]);
  }
}

void drawCornerMarker(std::int16_t x, std::int16_t y, std::uint16_t color,
                      const char *label) {
  display.fillRect(x, y, 42, 28, color);
  display.setTextSize(1);
  display.setTextColor(ST77XX_BLACK, color);
  display.setCursor(x + 11, y + 10);
  display.print(label);
}

void renderGeometry() {
  display.fillScreen(ST77XX_BLACK);
  display.drawRect(0, 0, display.width(), display.height(), ST77XX_WHITE);
  display.drawRect(4, 4, display.width() - 8, display.height() - 8,
                   ST77XX_CYAN);
  display.drawFastHLine(0, display.height() / 2, display.width(),
                        ST77XX_YELLOW);
  display.drawFastVLine(display.width() / 2, 0, display.height(),
                        ST77XX_YELLOW);
  display.drawCircle(display.width() / 2, display.height() / 2, 32,
                     ST77XX_MAGENTA);

  drawCornerMarker(8, 8, ST77XX_RED, "TL");
  drawCornerMarker(display.width() - 50, 8, ST77XX_GREEN, "TR");
  drawCornerMarker(8, display.height() - 36, ST77XX_BLUE, "BL");
  drawCornerMarker(display.width() - 50, display.height() - 36,
                   ST77XX_YELLOW, "BR");
  drawCenteredText("TOP", 42, 2, ST77XX_WHITE, ST77XX_BLACK);
}

void renderGridAndGray() {
  display.fillScreen(ST77XX_BLACK);
  constexpr std::int16_t kGridBottom = 220;
  constexpr std::int16_t kGridSpacing = 20;
  for (std::int16_t x = 0; x < display.width(); x += kGridSpacing) {
    display.drawFastVLine(x, 0, kGridBottom, ST77XX_CYAN);
  }
  for (std::int16_t y = 0; y <= kGridBottom; y += kGridSpacing) {
    display.drawFastHLine(0, y, display.width(), ST77XX_CYAN);
  }

  constexpr std::uint8_t kGraySteps = 8;
  const auto step_width =
      static_cast<std::int16_t>(display.width() / kGraySteps);
  for (std::uint8_t index = 0; index < kGraySteps; ++index) {
    const auto level = static_cast<std::uint8_t>(index * 255U /
                                                 (kGraySteps - 1U));
    const auto color = display.color565(level, level, level);
    const auto x = static_cast<std::int16_t>(index * step_width);
    display.fillRect(x, kGridBottom + 1, step_width,
                     display.height() - kGridBottom - 1, color);
  }
}

void renderDiagnostics(std::uint32_t frame_count) {
  display.fillScreen(ST77XX_BLACK);
  setDefaultText(ST77XX_CYAN);
  display.setCursor(12, 12);
  display.println(F("GRIDOPOLY TILE"));
  display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  display.setTextSize(1);
  display.setCursor(12, 48);
  display.println(F("ST7789  240 x 320"));
  display.println(F("SPI mode 0 @ 20 MHz"));
  display.println(F("Write-only (no MISO)"));
  display.println();
  display.println(F("J4 / ESP32-S3"));
  display.println(F("3 BL   -> GPIO16"));
  display.println(F("4 CS   -> GPIO15"));
  display.println(F("5 SCLK -> GPIO7"));
  display.println(F("6 MOSI -> GPIO6"));
  display.println(F("7 DC   -> GPIO5"));
  display.println(F("8 RST  -> GPIO4"));
  display.println();
  display.println(F("Rotation: portrait / 0"));
  display.setTextSize(2);
  display.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  display.setCursor(12, 252);
  display.print(F("FRAME "));
  display.print(frame_count);
}

void updateDiagnosticCounter(std::uint32_t frame_count) {
  display.fillRect(84, 250, 146, 24, ST77XX_BLACK);
  display.setTextSize(2);
  display.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  display.setCursor(84, 252);
  display.print(frame_count);
}

void renderBacklightRamp() {
  display.fillScreen(ST77XX_BLACK);
  drawCenteredText("BACKLIGHT", 112, 3, ST77XX_YELLOW, ST77XX_BLACK);
  drawCenteredText("SMOOTH RAMP", 158, 2, ST77XX_WHITE, ST77XX_BLACK);
  drawCenteredText("No reset / flicker", 198, 1, ST77XX_CYAN,
                   ST77XX_BLACK);
}

void renderStage(const Stage &stage) {
  switch (stage.id) {
    case StageId::SolidRed:
      renderSolid(ST77XX_RED);
      break;
    case StageId::SolidGreen:
      renderSolid(ST77XX_GREEN);
      break;
    case StageId::SolidBlue:
      renderSolid(ST77XX_BLUE);
      break;
    case StageId::SolidWhite:
      renderSolid(ST77XX_WHITE);
      break;
    case StageId::SolidBlack:
      renderSolid(ST77XX_BLACK);
      break;
    case StageId::ColorBars:
      renderColorBars();
      break;
    case StageId::Geometry:
      renderGeometry();
      break;
    case StageId::GridAndGray:
      renderGridAndGray();
      break;
    case StageId::Diagnostics:
      renderDiagnostics(diagnostic_frame_count);
      break;
    case StageId::BacklightRamp:
      renderBacklightRamp();
      break;
  }
}

void logStage(std::size_t index, const Stage &stage) {
  Serial.printf("\n[STAGE %u/%u] %s\n", static_cast<unsigned>(index + 1U),
                static_cast<unsigned>(tile_display_test::stageCount()),
                stage.name);
  Serial.printf("Expected: %s\n", stage.expectation);
}

void activateStage(std::size_t index, std::uint32_t now_ms) {
  active_stage_index = index;
  stage_started_ms = now_ms;
  last_diagnostic_update_ms = now_ms;

  const auto &stage = tile_display_test::stageAt(index);
  if (stage.id != StageId::BacklightRamp) {
    writeBacklightDuty(kBacklightTestDuty);
  }
  renderStage(stage);
  logStage(index, stage);
}

void updateBacklightRamp(std::uint32_t elapsed_ms,
                         std::uint32_t duration_ms) {
  const auto half_duration = duration_ms / 2U;
  const auto position = elapsed_ms < half_duration
                            ? elapsed_ms
                            : duration_ms - std::min(elapsed_ms, duration_ms);
  const auto range = static_cast<std::uint32_t>(
      kBacklightRampMaximumDuty - kBacklightTestDuty);
  const auto duty = static_cast<std::uint8_t>(
      kBacklightTestDuty + (range * position) / half_duration);
  writeBacklightDuty(duty);
}

void printBootDiagnostics() {
  Serial.println();
  Serial.println(F("Gridopoly ST7789 display connection test"));
  Serial.println(F("Panel: 240x320, write-only SPI mode 0 @ 20 MHz"));
  Serial.println(F("J4: BL16 CS15 SCLK7 MOSI6 DC5 RST4"));
  Serial.println(F("The panel has no MISO; visual inspection is required."));
}

}  // namespace

void setup() {
  tile_display_test::initializeDisplay(startup_port);
  printBootDiagnostics();
  activateStage(0, millis());
}

void loop() {
  const auto now_ms = millis();
  const auto &stage = tile_display_test::stageAt(active_stage_index);
  const auto elapsed_ms =
      static_cast<std::uint32_t>(now_ms - stage_started_ms);

  if (stage.id == StageId::BacklightRamp) {
    updateBacklightRamp(elapsed_ms, stage.duration_ms);
  } else if (stage.id == StageId::Diagnostics &&
             static_cast<std::uint32_t>(now_ms -
                                        last_diagnostic_update_ms) >= 250U) {
    last_diagnostic_update_ms = now_ms;
    ++diagnostic_frame_count;
    updateDiagnosticCounter(diagnostic_frame_count);
  }

  if (tile_display_test::stageElapsed(now_ms, stage_started_ms,
                                      stage.duration_ms)) {
    activateStage(tile_display_test::nextStageIndex(active_stage_index),
                  now_ms);
  }

  yield();
}
