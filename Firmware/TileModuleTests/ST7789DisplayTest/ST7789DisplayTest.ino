#include <Arduino.h>
#include <SPI.h>

#include "board_pins.h"

namespace {

using namespace gridopoly::tile_display_test;

constexpr uint8_t kCmdSwReset = 0x01;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdNormalOn = 0x13;
constexpr uint8_t kCmdInvertOn = 0x21;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumnAddress = 0x2A;
constexpr uint8_t kCmdRowAddress = 0x2B;
constexpr uint8_t kCmdMemoryWrite = 0x2C;
constexpr uint8_t kCmdMemoryAccess = 0x36;
constexpr uint8_t kCmdPixelFormat = 0x3A;

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xF8U) << 8U) |
                               ((green & 0xFCU) << 3U) |
                               (blue >> 3U));
}

constexpr uint16_t kBlack = rgb565(0, 0, 0);
constexpr uint16_t kWhite = rgb565(255, 255, 255);
constexpr uint16_t kRed = rgb565(255, 0, 0);
constexpr uint16_t kGreen = rgb565(0, 255, 0);
constexpr uint16_t kBlue = rgb565(0, 0, 255);
constexpr uint16_t kCyan = rgb565(0, 255, 255);
constexpr uint16_t kMagenta = rgb565(255, 0, 255);
constexpr uint16_t kYellow = rgb565(255, 255, 0);

struct Stage {
  const char *name;
  const char *expectation;
  uint32_t durationMs;
};

constexpr Stage kStages[] = {
    {"SOLID RED", "The entire panel is red", 1500},
    {"SOLID GREEN", "The entire panel is green", 1500},
    {"SOLID BLUE", "The entire panel is blue", 1500},
    {"SOLID WHITE", "The entire panel is white", 1500},
    {"SOLID BLACK", "The entire panel is black", 1500},
    {"RGB CMY BARS", "Six vertical bars: red green blue cyan magenta yellow", 3500},
    {"GEOMETRY", "Unbroken border, center cross, four different corner marks", 4000},
    {"GRID AND GRAY", "Stable grid plus eight smooth grayscale blocks", 4000},
    {"MOTION", "Checker pattern changes without tearing or corrupted pixels", 4000},
    {"BACKLIGHT", "Brightness ramps smoothly, then the sequence restarts", 5000},
};

size_t gStageIndex = 0;
uint32_t gStageStartedMs = 0;
uint32_t gLastMotionFrameMs = 0;
uint32_t gFrameCounter = 0;
bool gStageNeedsRender = true;

void selectCommand() {
  digitalWrite(kDataCommandPin, LOW);
  digitalWrite(kChipSelectPin, LOW);
}

void selectData() {
  digitalWrite(kDataCommandPin, HIGH);
  digitalWrite(kChipSelectPin, LOW);
}

void deselect() {
  digitalWrite(kChipSelectPin, HIGH);
}

void writeCommand(uint8_t command) {
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectCommand();
  SPI.transfer(command);
  deselect();
  SPI.endTransaction();
}

void writeData(const uint8_t *data, size_t length) {
  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectData();
  SPI.writeBytes(data, length);
  deselect();
  SPI.endTransaction();
}

void writeCommandData(uint8_t command, const uint8_t *data, size_t length) {
  writeCommand(command);
  if (length != 0U) {
    writeData(data, length);
  }
}

void setAddressWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  const uint16_t xEnd = static_cast<uint16_t>(x + width - 1U);
  const uint16_t yEnd = static_cast<uint16_t>(y + height - 1U);
  const uint8_t columns[] = {
      static_cast<uint8_t>(x >> 8U), static_cast<uint8_t>(x),
      static_cast<uint8_t>(xEnd >> 8U), static_cast<uint8_t>(xEnd)};
  const uint8_t rows[] = {
      static_cast<uint8_t>(y >> 8U), static_cast<uint8_t>(y),
      static_cast<uint8_t>(yEnd >> 8U), static_cast<uint8_t>(yEnd)};
  writeCommandData(kCmdColumnAddress, columns, sizeof(columns));
  writeCommandData(kCmdRowAddress, rows, sizeof(rows));
  writeCommand(kCmdMemoryWrite);
}

void writeRepeatedColor(uint16_t color, uint32_t pixelCount) {
  constexpr size_t kChunkPixels = 128;
  uint8_t chunk[kChunkPixels * 2];
  const uint8_t high = static_cast<uint8_t>(color >> 8U);
  const uint8_t low = static_cast<uint8_t>(color);
  for (size_t index = 0; index < sizeof(chunk); index += 2U) {
    chunk[index] = high;
    chunk[index + 1U] = low;
  }

  SPI.beginTransaction(SPISettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectData();
  while (pixelCount != 0U) {
    const uint32_t pixels = min<uint32_t>(pixelCount, kChunkPixels);
    SPI.writeBytes(chunk, static_cast<size_t>(pixels) * 2U);
    pixelCount -= pixels;
  }
  deselect();
  SPI.endTransaction();
}

void fillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) {
  if (width <= 0 || height <= 0 || x >= kDisplayWidth || y >= kDisplayHeight) {
    return;
  }
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  width = min<int16_t>(width, static_cast<int16_t>(kDisplayWidth) - x);
  height = min<int16_t>(height, static_cast<int16_t>(kDisplayHeight) - y);
  if (width <= 0 || height <= 0) {
    return;
  }
  setAddressWindow(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                   static_cast<uint16_t>(width), static_cast<uint16_t>(height));
  writeRepeatedColor(color, static_cast<uint32_t>(width) * static_cast<uint32_t>(height));
}

void fillScreen(uint16_t color) {
  fillRect(0, 0, kDisplayWidth, kDisplayHeight, color);
}

void drawHorizontalLine(int16_t x, int16_t y, int16_t width, uint16_t color) {
  fillRect(x, y, width, 1, color);
}

void drawVerticalLine(int16_t x, int16_t y, int16_t height, uint16_t color) {
  fillRect(x, y, 1, height, color);
}

void drawFrame(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) {
  drawHorizontalLine(x, y, width, color);
  drawHorizontalLine(x, static_cast<int16_t>(y + height - 1), width, color);
  drawVerticalLine(x, y, height, color);
  drawVerticalLine(static_cast<int16_t>(x + width - 1), y, height, color);
}

void setBacklight(uint8_t duty) {
  analogWrite(kBacklightPin, duty);
}

void safeOutputs() {
  pinMode(kBacklightPin, OUTPUT);
  pinMode(kChipSelectPin, OUTPUT);
  pinMode(kClockPin, OUTPUT);
  pinMode(kMosiPin, OUTPUT);
  pinMode(kDataCommandPin, OUTPUT);
  pinMode(kResetPin, OUTPUT);
  digitalWrite(kBacklightPin, LOW);
  digitalWrite(kChipSelectPin, HIGH);
  digitalWrite(kClockPin, LOW);
  digitalWrite(kMosiPin, LOW);
  digitalWrite(kDataCommandPin, LOW);
  digitalWrite(kResetPin, HIGH);
}

void initializePanel() {
  digitalWrite(kResetPin, LOW);
  delay(20);
  digitalWrite(kResetPin, HIGH);
  delay(120);

  writeCommand(kCmdSwReset);
  delay(150);
  writeCommand(kCmdSleepOut);
  delay(120);

  const uint8_t pixelFormat = 0x55;  // RGB565
  writeCommandData(kCmdPixelFormat, &pixelFormat, 1);
  const uint8_t memoryAccess = 0x00;  // Portrait, RGB order
  writeCommandData(kCmdMemoryAccess, &memoryAccess, 1);

  const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  writeCommandData(0xB2, porch, sizeof(porch));
  const uint8_t gateControl = 0x35;
  writeCommandData(0xB7, &gateControl, 1);
  const uint8_t vcom = 0x19;
  writeCommandData(0xBB, &vcom, 1);
  const uint8_t lcmControl = 0x2C;
  writeCommandData(0xC0, &lcmControl, 1);
  const uint8_t vdvVrhEnable = 0x01;
  writeCommandData(0xC2, &vdvVrhEnable, 1);
  const uint8_t vrh = 0x12;
  writeCommandData(0xC3, &vrh, 1);
  const uint8_t vdv = 0x20;
  writeCommandData(0xC4, &vdv, 1);
  const uint8_t frameRate = 0x0F;
  writeCommandData(0xC6, &frameRate, 1);
  const uint8_t power[] = {0xA4, 0xA1};
  writeCommandData(0xD0, power, sizeof(power));

  const uint8_t positiveGamma[] = {
      0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
      0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
  writeCommandData(0xE0, positiveGamma, sizeof(positiveGamma));
  const uint8_t negativeGamma[] = {
      0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
      0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
  writeCommandData(0xE1, negativeGamma, sizeof(negativeGamma));

  writeCommand(kCmdInvertOn);
  writeCommand(kCmdNormalOn);
  delay(10);
  writeCommand(kCmdDisplayOn);
  delay(120);
}

void drawColorBars() {
  constexpr uint16_t colors[] = {kRed, kGreen, kBlue, kCyan, kMagenta, kYellow};
  constexpr int16_t barWidth = kDisplayWidth / 6;
  for (size_t index = 0; index < 6; ++index) {
    fillRect(static_cast<int16_t>(index * barWidth), 0, barWidth, kDisplayHeight, colors[index]);
  }
}

void drawGeometry() {
  fillScreen(kBlack);
  drawFrame(0, 0, kDisplayWidth, kDisplayHeight, kWhite);
  drawFrame(4, 4, kDisplayWidth - 8, kDisplayHeight - 8, kCyan);
  drawFrame(12, 12, kDisplayWidth - 24, kDisplayHeight - 24, kYellow);
  drawVerticalLine(kDisplayWidth / 2, 20, kDisplayHeight - 40, kWhite);
  drawHorizontalLine(20, kDisplayHeight / 2, kDisplayWidth - 40, kWhite);

  fillRect(1, 1, 24, 24, kRed);
  fillRect(kDisplayWidth - 25, 1, 24, 24, kGreen);
  fillRect(1, kDisplayHeight - 25, 24, 24, kBlue);
  fillRect(kDisplayWidth - 25, kDisplayHeight - 25, 24, 24, kMagenta);
}

void drawGridAndGray() {
  fillScreen(kBlack);
  for (int16_t x = 0; x < kDisplayWidth; x += 20) {
    drawVerticalLine(x, 0, 220, (x % 40 == 0) ? kCyan : rgb565(0, 70, 70));
  }
  for (int16_t y = 0; y <= 220; y += 20) {
    drawHorizontalLine(0, y, kDisplayWidth, (y % 40 == 0) ? kCyan : rgb565(0, 70, 70));
  }
  constexpr int16_t blockWidth = kDisplayWidth / 8;
  for (int16_t index = 0; index < 8; ++index) {
    const uint8_t gray = static_cast<uint8_t>(index * 255 / 7);
    fillRect(index * blockWidth, 230, blockWidth, 90, rgb565(gray, gray, gray));
  }
}

void drawChecker(uint32_t frame) {
  constexpr int16_t cell = 20;
  for (int16_t y = 0; y < kDisplayHeight; y += cell) {
    for (int16_t x = 0; x < kDisplayWidth; x += cell) {
      const bool bright = (((x / cell) + (y / cell) + frame) & 1) == 0;
      fillRect(x, y, cell, cell, bright ? kCyan : rgb565(0, 16, 32));
    }
  }
  drawFrame(0, 0, kDisplayWidth, kDisplayHeight, kWhite);
}

void renderStage(size_t index) {
  switch (index) {
    case 0: fillScreen(kRed); break;
    case 1: fillScreen(kGreen); break;
    case 2: fillScreen(kBlue); break;
    case 3: fillScreen(kWhite); break;
    case 4: fillScreen(kBlack); break;
    case 5: drawColorBars(); break;
    case 6: drawGeometry(); break;
    case 7: drawGridAndGray(); break;
    case 8: drawChecker(gFrameCounter++); break;
    case 9:
      fillScreen(rgb565(0, 40, 70));
      drawFrame(0, 0, kDisplayWidth, kDisplayHeight, kWhite);
      break;
    default: break;
  }
}

void logStage() {
  Serial.printf("[DISPLAY] Stage %u/%u: %s\r\n",
                static_cast<unsigned>(gStageIndex + 1U),
                static_cast<unsigned>(sizeof(kStages) / sizeof(kStages[0])),
                kStages[gStageIndex].name);
  Serial.printf("[DISPLAY] Expect: %s\r\n", kStages[gStageIndex].expectation);
}

}  // namespace

void setup() {
  safeOutputs();
  Serial.begin(kSerialBaud);
  SPI.begin(kClockPin, -1, kMosiPin, kChipSelectPin);

  Serial.println();
  Serial.println("GRIDOPOLY TILE ST7789 DISPLAY TEST");
  Serial.printf("Panel: %ux%u, SPI mode 0 at %lu Hz\r\n",
                kDisplayWidth, kDisplayHeight, static_cast<unsigned long>(kSpiFrequencyHz));
  Serial.printf("Pins: BL=%d CS=%d SCLK=%d MOSI=%d DC=%d RST=%d\r\n",
                kBacklightPin, kChipSelectPin, kClockPin, kMosiPin,
                kDataCommandPin, kResetPin);

  initializePanel();
  fillScreen(kBlack);
  setBacklight(180);
  gStageStartedMs = millis();
  gStageNeedsRender = true;
}

void loop() {
  const uint32_t now = millis();
  const Stage &stage = kStages[gStageIndex];

  if (gStageNeedsRender) {
    renderStage(gStageIndex);
    logStage();
    gStageNeedsRender = false;
    gLastMotionFrameMs = now;
  }

  if (gStageIndex == 8U && static_cast<uint32_t>(now - gLastMotionFrameMs) >= 350U) {
    drawChecker(gFrameCounter++);
    gLastMotionFrameMs = now;
  } else if (gStageIndex == 9U) {
    const uint32_t elapsed = now - gStageStartedMs;
    const uint32_t phase = elapsed % 2400U;
    const uint8_t duty = phase < 1200U
        ? static_cast<uint8_t>(30U + (phase * 190U / 1200U))
        : static_cast<uint8_t>(220U - ((phase - 1200U) * 190U / 1200U));
    setBacklight(duty);
  }

  if (static_cast<uint32_t>(now - gStageStartedMs) >= stage.durationMs) {
    if (gStageIndex == 9U) {
      setBacklight(180);
    }
    gStageIndex = (gStageIndex + 1U) % (sizeof(kStages) / sizeof(kStages[0]));
    gStageStartedMs = now;
    gStageNeedsRender = true;
  }

  delay(1);
}

