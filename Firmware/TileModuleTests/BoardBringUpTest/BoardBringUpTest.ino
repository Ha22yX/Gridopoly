#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "esp32-hal-rmt.h"

#include "board_pins.h"

namespace {

using namespace gridopoly::tile_bring_up;

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

constexpr uint8_t kInaRegConfig = 0x00;
constexpr uint8_t kInaRegShuntVoltage = 0x01;
constexpr uint8_t kInaRegBusVoltage = 0x02;
constexpr uint8_t kInaRegPower = 0x03;
constexpr uint8_t kInaRegCurrent = 0x04;
constexpr uint8_t kInaRegCalibration = 0x05;
constexpr uint8_t kInaRegManufacturerId = 0xFE;
constexpr uint8_t kInaRegDieId = 0xFF;
constexpr uint16_t kInaConfig = 0x0527;       // 16 averages, 1.1 ms bus/shunt, continuous.
constexpr uint16_t kInaCalibration = 0x1400;  // 10 mOhm shunt, 100 uA/bit.

constexpr uint32_t kRmtFrequencyHz = 8000000;
constexpr uint8_t kLedTestBrightness = 64;  // 25% maximum for RGB tests.
constexpr uint8_t kWhiteTestBrightness = 31;
constexpr uint8_t kRampMaximum = 51;        // 20% maximum with all ten LEDs white.
constexpr uint32_t kInaSamplePeriodMs = 200;
constexpr uint32_t kDashboardPeriodMs = 250;

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xF8U) << 8U) |
                               ((green & 0xFCU) << 3U) |
                               (blue >> 3U));
}

constexpr uint16_t kBackground = rgb565(6, 16, 23);
constexpr uint16_t kPanel = rgb565(9, 31, 42);
constexpr uint16_t kPanelBright = rgb565(12, 45, 57);
constexpr uint16_t kWhite = rgb565(240, 247, 250);
constexpr uint16_t kMuted = rgb565(118, 153, 166);
constexpr uint16_t kCyan = rgb565(49, 225, 214);
constexpr uint16_t kGreen = rgb565(74, 222, 128);
constexpr uint16_t kYellow = rgb565(250, 204, 21);
constexpr uint16_t kRed = rgb565(248, 113, 113);

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct InaReading {
  bool valid = false;
  float busVoltageV = 0.0F;
  float shuntVoltageMv = 0.0F;
  float currentMa = 0.0F;
  float powerMw = 0.0F;
  float peakCurrentMa = 0.0F;
  float minimumBusV = 100.0F;
  float maximumBusV = 0.0F;
  uint32_t sampleCount = 0;
};

enum class LedStage : uint8_t {
  Locate,
  Red,
  Green,
  Blue,
  White,
  BrightnessRamp,
  AllOff,
};

Rgb gPixels[kWs2812Count]{};
rmt_data_t gLedSymbols[kWs2812Count * 24]{};
InaReading gIna;
bool gInaFound = false;
bool gInaIdsMatch = false;
bool gInaCalibrationVerified = false;
bool gLedDriverReady = false;
uint16_t gInaManufacturerId = 0;
uint16_t gInaDieId = 0;

LedStage gLedStage = LedStage::Locate;
uint8_t gLocateIndex = 0;
uint32_t gLedStageStartedMs = 0;
uint32_t gLastLedFrameMs = 0;
uint32_t gLastInaSampleMs = 0;
uint32_t gLastDashboardMs = 0;
bool gStaticDashboardDrawn = false;

void selectCommand() {
  digitalWrite(kLcdDataCommandPin, LOW);
  digitalWrite(kLcdChipSelectPin, LOW);
}

void selectData() {
  digitalWrite(kLcdDataCommandPin, HIGH);
  digitalWrite(kLcdChipSelectPin, LOW);
}

void deselectDisplay() {
  digitalWrite(kLcdChipSelectPin, HIGH);
}

void writeCommand(uint8_t command) {
  SPI.beginTransaction(SPISettings(kDisplaySpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectCommand();
  SPI.transfer(command);
  deselectDisplay();
  SPI.endTransaction();
}

void writeData(const uint8_t *data, size_t length) {
  SPI.beginTransaction(SPISettings(kDisplaySpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectData();
  SPI.writeBytes(data, length);
  deselectDisplay();
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

  SPI.beginTransaction(SPISettings(kDisplaySpiFrequencyHz, MSBFIRST, SPI_MODE0));
  selectData();
  while (pixelCount != 0U) {
    const uint32_t pixels = min<uint32_t>(pixelCount, kChunkPixels);
    SPI.writeBytes(chunk, static_cast<size_t>(pixels) * 2U);
    pixelCount -= pixels;
  }
  deselectDisplay();
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

void drawHorizontalLine(int16_t x, int16_t y, int16_t width, uint16_t color) {
  fillRect(x, y, width, 1, color);
}

void drawFrame(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) {
  fillRect(x, y, width, 1, color);
  fillRect(x, static_cast<int16_t>(y + height - 1), width, 1, color);
  fillRect(x, y, 1, height, color);
  fillRect(static_cast<int16_t>(x + width - 1), y, 1, height, color);
}

int glyphIndex(char character) {
  if (character == ' ') {
    return 0;
  }
  if (character >= '0' && character <= '9') {
    return 1 + character - '0';
  }
  if (character >= 'A' && character <= 'Z') {
    return 11 + character - 'A';
  }
  return -1;
}

const uint8_t *glyphFor(char character) {
  static constexpr uint8_t kFont[][5] = {
      {0x00, 0x00, 0x00, 0x00, 0x00},
      {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
      {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
      {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
      {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
      {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
      {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
      {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
      {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
      {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
      {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
      {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
      {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
      {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
      {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
      {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
      {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
      {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
      {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
  };
  static constexpr uint8_t kPeriod[] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static constexpr uint8_t kColon[] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static constexpr uint8_t kDash[] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static constexpr uint8_t kSlash[] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static constexpr uint8_t kPercent[] = {0x23, 0x13, 0x08, 0x64, 0x62};
  static constexpr uint8_t kPlus[] = {0x08, 0x08, 0x3E, 0x08, 0x08};

  const int index = glyphIndex(character);
  if (index >= 0) {
    return kFont[index];
  }
  switch (character) {
    case '.': return kPeriod;
    case ':': return kColon;
    case '-': return kDash;
    case '/': return kSlash;
    case '%': return kPercent;
    case '+': return kPlus;
    default: return kFont[0];
  }
}

void drawCharacter(int16_t x, int16_t y, char character, uint16_t color, uint8_t scale) {
  const uint8_t *glyph = glyphFor(character);
  for (uint8_t column = 0; column < 5; ++column) {
    for (uint8_t row = 0; row < 7; ++row) {
      if ((glyph[column] & (1U << row)) != 0U) {
        fillRect(static_cast<int16_t>(x + column * scale),
                 static_cast<int16_t>(y + row * scale), scale, scale, color);
      }
    }
  }
}

void drawText(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t scale = 1) {
  while (*text != '\0') {
    drawCharacter(x, y, static_cast<char>(toupper(static_cast<unsigned char>(*text))), color, scale);
    x = static_cast<int16_t>(x + 6 * scale);
    ++text;
  }
}

int16_t textWidth(const char *text, uint8_t scale) {
  return static_cast<int16_t>(strlen(text) * 6U * scale);
}

void drawCenteredText(int16_t y, const char *text, uint16_t color, uint8_t scale = 1) {
  drawText(static_cast<int16_t>((kDisplayWidth - textWidth(text, scale)) / 2), y,
           text, color, scale);
}

void drawCenteredIn(int16_t x, int16_t width, int16_t y, const char *text,
                    uint16_t color, uint8_t scale = 1) {
  drawText(static_cast<int16_t>(x + (width - textWidth(text, scale)) / 2), y,
           text, color, scale);
}

void initializeDisplay() {
  pinMode(kLcdBacklightPin, OUTPUT);
  pinMode(kLcdChipSelectPin, OUTPUT);
  pinMode(kLcdDataCommandPin, OUTPUT);
  pinMode(kLcdResetPin, OUTPUT);
  digitalWrite(kLcdBacklightPin, LOW);
  digitalWrite(kLcdChipSelectPin, HIGH);
  digitalWrite(kLcdDataCommandPin, LOW);
  digitalWrite(kLcdResetPin, HIGH);

  SPI.begin(kLcdClockPin, -1, kLcdMosiPin, kLcdChipSelectPin);
  digitalWrite(kLcdResetPin, LOW);
  delay(20);
  digitalWrite(kLcdResetPin, HIGH);
  delay(120);
  writeCommand(kCmdSwReset);
  delay(150);
  writeCommand(kCmdSleepOut);
  delay(120);

  const uint8_t pixelFormat = 0x55;
  const uint8_t memoryAccess = 0x00;
  writeCommandData(kCmdPixelFormat, &pixelFormat, 1);
  writeCommandData(kCmdMemoryAccess, &memoryAccess, 1);
  const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  writeCommandData(0xB2, porch, sizeof(porch));
  const uint8_t gateControl = 0x35;
  const uint8_t vcom = 0x19;
  const uint8_t lcmControl = 0x2C;
  const uint8_t vdvVrhEnable = 0x01;
  const uint8_t vrh = 0x12;
  const uint8_t vdv = 0x20;
  const uint8_t frameRate = 0x0F;
  const uint8_t power[] = {0xA4, 0xA1};
  writeCommandData(0xB7, &gateControl, 1);
  writeCommandData(0xBB, &vcom, 1);
  writeCommandData(0xC0, &lcmControl, 1);
  writeCommandData(0xC2, &vdvVrhEnable, 1);
  writeCommandData(0xC3, &vrh, 1);
  writeCommandData(0xC4, &vdv, 1);
  writeCommandData(0xC6, &frameRate, 1);
  writeCommandData(0xD0, power, sizeof(power));
  writeCommand(kCmdInvertOn);
  writeCommand(kCmdNormalOn);
  delay(10);
  writeCommand(kCmdDisplayOn);
  delay(120);
  analogWrite(kLcdBacklightPin, 150);
}

bool i2cProbe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool inaWriteRegister(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(kIna226Address);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value >> 8U));
  Wire.write(static_cast<uint8_t>(value));
  return Wire.endTransmission() == 0;
}

bool inaReadRegister(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(kIna226Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(kIna226Address, static_cast<uint8_t>(2)) != 2) {
    return false;
  }
  value = static_cast<uint16_t>(Wire.read()) << 8U;
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

void initializeIna226() {
  Wire.begin(kCurrentSdaPin, kCurrentSclPin, 400000U);
  delay(5);
  gInaFound = i2cProbe(kIna226Address);
  if (!gInaFound) {
    Serial.println("[INA226] ERROR: address 0x40 not found");
    return;
  }

  uint16_t calibrationReadback = 0;
  const bool idsRead = inaReadRegister(kInaRegManufacturerId, gInaManufacturerId) &&
                       inaReadRegister(kInaRegDieId, gInaDieId);
  gInaIdsMatch = idsRead && gInaManufacturerId == 0x5449U &&
                 (gInaDieId & 0xFFF0U) == 0x2260U;
  const bool configured = inaWriteRegister(kInaRegConfig, kInaConfig) &&
                          inaWriteRegister(kInaRegCalibration, kInaCalibration) &&
                          inaReadRegister(kInaRegCalibration, calibrationReadback);
  gInaCalibrationVerified = configured && calibrationReadback == kInaCalibration;
  delay(25);

  Serial.printf("[INA226] found=1 manufacturer=0x%04X die=0x%04X ids=%s calibration=0x%04X %s\r\n",
                gInaManufacturerId, gInaDieId, gInaIdsMatch ? "OK" : "CHECK",
                calibrationReadback, gInaCalibrationVerified ? "OK" : "FAIL");
}

void sampleIna226() {
  if (!gInaFound || !gInaCalibrationVerified) {
    gIna.valid = false;
    return;
  }
  uint16_t busRaw = 0;
  uint16_t shuntRaw = 0;
  uint16_t currentRaw = 0;
  uint16_t powerRaw = 0;
  if (!inaReadRegister(kInaRegBusVoltage, busRaw) ||
      !inaReadRegister(kInaRegShuntVoltage, shuntRaw) ||
      !inaReadRegister(kInaRegCurrent, currentRaw) ||
      !inaReadRegister(kInaRegPower, powerRaw)) {
    gIna.valid = false;
    return;
  }

  gIna.busVoltageV = static_cast<float>(busRaw) * 0.00125F;
  gIna.shuntVoltageMv = static_cast<float>(static_cast<int16_t>(shuntRaw)) * 0.0025F;
  gIna.currentMa = static_cast<float>(static_cast<int16_t>(currentRaw)) * 0.1F;
  gIna.powerMw = static_cast<float>(powerRaw) * 2.5F;
  gIna.peakCurrentMa = max(gIna.peakCurrentMa, gIna.currentMa);
  gIna.minimumBusV = min(gIna.minimumBusV, gIna.busVoltageV);
  gIna.maximumBusV = max(gIna.maximumBusV, gIna.busVoltageV);
  ++gIna.sampleCount;
  gIna.valid = true;
}

void clearPixels() {
  memset(gPixels, 0, sizeof(gPixels));
}

void fillPixels(Rgb color) {
  for (Rgb &pixel : gPixels) {
    pixel = color;
  }
}

void encodeLedByte(uint8_t value, size_t &symbolIndex) {
  for (int bit = 7; bit >= 0; --bit) {
    const bool one = (value & (1U << bit)) != 0U;
    rmt_data_t &symbol = gLedSymbols[symbolIndex++];
    symbol.level0 = 1;
    symbol.duration0 = one ? 6 : 3;  // 750/375 ns at 8 MHz.
    symbol.level1 = 0;
    symbol.duration1 = one ? 4 : 7;  // Total bit time 1.25 us.
  }
}

bool showPixels() {
  if (!gLedDriverReady) {
    return false;
  }
  size_t symbolIndex = 0;
  for (const Rgb &pixel : gPixels) {
    encodeLedByte(pixel.green, symbolIndex);  // WS2812B uses GRB wire order.
    encodeLedByte(pixel.red, symbolIndex);
    encodeLedByte(pixel.blue, symbolIndex);
  }
  const bool sent = rmtWrite(kWs2812DataPin, gLedSymbols, symbolIndex, 10U);
  delayMicroseconds(80);
  return sent;
}

void initializeWs2812() {
  pinMode(kWs2812DataPin, OUTPUT);
  digitalWrite(kWs2812DataPin, LOW);
  gLedDriverReady = rmtInit(kWs2812DataPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_4,
                            kRmtFrequencyHz);
  if (gLedDriverReady) {
    rmtSetEOT(kWs2812DataPin, LOW);
    clearPixels();
    showPixels();
  }
  Serial.printf("[WS2812] pin=%d count=%u RMT=%s\r\n", kWs2812DataPin,
                static_cast<unsigned>(kWs2812Count), gLedDriverReady ? "OK" : "FAIL");
}

uint32_t ledStageDurationMs(LedStage stage) {
  switch (stage) {
    case LedStage::Locate: return 700U * kWs2812Count;
    case LedStage::Red:
    case LedStage::Green:
    case LedStage::Blue: return 1500U;
    case LedStage::White: return 1200U;
    case LedStage::BrightnessRamp: return 5000U;
    case LedStage::AllOff: return 900U;
  }
  return 1000U;
}

const char *ledStageName() {
  switch (gLedStage) {
    case LedStage::Locate: return "LOCATE";
    case LedStage::Red: return "RED 25%";
    case LedStage::Green: return "GREEN 25%";
    case LedStage::Blue: return "BLUE 25%";
    case LedStage::White: return "WHITE 12%";
    case LedStage::BrightnessRamp: return "WHITE RAMP";
    case LedStage::AllOff: return "ALL OFF";
  }
  return "UNKNOWN";
}

void applyLedStage(uint32_t now, bool force) {
  if (!gLedDriverReady) {
    return;
  }
  const uint32_t elapsed = now - gLedStageStartedMs;
  switch (gLedStage) {
    case LedStage::Locate: {
      const uint8_t index = static_cast<uint8_t>(
          min<uint32_t>(elapsed / 700U, static_cast<uint32_t>(kWs2812Count - 1U)));
      if (force || index != gLocateIndex) {
        gLocateIndex = index;
        clearPixels();
        gPixels[index] = {38, 38, 38};
        showPixels();
        Serial.printf("[WS2812] locate %u/%u: U%u only\r\n",
                      index + 1U, static_cast<unsigned>(kWs2812Count), 12U + index);
      }
      break;
    }
    case LedStage::Red:
      if (force) { fillPixels({kLedTestBrightness, 0, 0}); showPixels(); }
      break;
    case LedStage::Green:
      if (force) { fillPixels({0, kLedTestBrightness, 0}); showPixels(); }
      break;
    case LedStage::Blue:
      if (force) { fillPixels({0, 0, kLedTestBrightness}); showPixels(); }
      break;
    case LedStage::White:
      if (force) {
        fillPixels({kWhiteTestBrightness, kWhiteTestBrightness, kWhiteTestBrightness});
        showPixels();
      }
      break;
    case LedStage::BrightnessRamp:
      if (force || now - gLastLedFrameMs >= 40U) {
        const uint32_t phase = elapsed % 4000U;
        const uint8_t brightness = phase < 2000U
            ? static_cast<uint8_t>(phase * kRampMaximum / 2000U)
            : static_cast<uint8_t>((4000U - phase) * kRampMaximum / 2000U);
        fillPixels({brightness, brightness, brightness});
        showPixels();
        gLastLedFrameMs = now;
      }
      break;
    case LedStage::AllOff:
      if (force) { clearPixels(); showPixels(); }
      break;
  }
}

void advanceLedStage(uint32_t now) {
  if (now - gLedStageStartedMs < ledStageDurationMs(gLedStage)) {
    return;
  }
  switch (gLedStage) {
    case LedStage::Locate: gLedStage = LedStage::Red; break;
    case LedStage::Red: gLedStage = LedStage::Green; break;
    case LedStage::Green: gLedStage = LedStage::Blue; break;
    case LedStage::Blue: gLedStage = LedStage::White; break;
    case LedStage::White: gLedStage = LedStage::BrightnessRamp; break;
    case LedStage::BrightnessRamp: gLedStage = LedStage::AllOff; break;
    case LedStage::AllOff:
      gLedStage = LedStage::Locate;
      gLocateIndex = 0xFFU;
      break;
  }
  gLedStageStartedMs = now;
  applyLedStage(now, true);
  Serial.printf("[WS2812] stage: %s\r\n", ledStageName());
}

void drawStaticDashboard() {
  fillRect(0, 0, kDisplayWidth, kDisplayHeight, kBackground);
  fillRect(0, 0, kDisplayWidth, 39, kPanel);
  drawCenteredText(6, "GRIDOPOLY TILE", kWhite, 2);
  drawCenteredText(25, "HARDWARE DASHBOARD", kCyan, 1);
  drawHorizontalLine(0, 38, kDisplayWidth, kCyan);

  drawText(12, 48, "INA226", kWhite, 2);
  drawFrame(121, 44, 107, 25, kPanelBright);

  drawText(12, 77, "5V BUS", kMuted, 1);
  drawText(12, 114, "CURRENT", kMuted, 1);
  drawText(12, 151, "POWER", kMuted, 1);
  drawText(12, 188, "PEAK CURRENT", kMuted, 1);
  drawHorizontalLine(12, 108, 216, kPanelBright);
  drawHorizontalLine(12, 145, 216, kPanelBright);
  drawHorizontalLine(12, 182, 216, kPanelBright);
  drawHorizontalLine(12, 219, 216, kPanelBright);

  fillRect(0, 226, kDisplayWidth, 94, kPanel);
  drawHorizontalLine(0, 226, kDisplayWidth, kCyan);
  drawText(12, 235, "WS2812 TEST", kMuted, 1);
  drawFrame(12, 292, 216, 13, kPanelBright);
  drawCenteredText(309, "USB SAFE BRIGHTNESS", kMuted, 1);
  gStaticDashboardDrawn = true;
}

void drawInaStatus() {
  fillRect(122, 45, 105, 23, kPanel);
  const bool ready = gInaFound && gInaCalibrationVerified;
  const uint16_t color = ready ? kGreen : kRed;
  drawFrame(122, 45, 105, 23, color);
  drawCenteredIn(122, 105, 53, ready ? "OK 0X40" : "NOT FOUND", color, 1);
}

void drawMetricValue(int16_t y, const char *value, uint16_t color = kWhite) {
  fillRect(88, y - 2, 140, 20, kBackground);
  drawText(96, y, value, color, 2);
}

void drawDashboard(uint32_t now) {
  if (!gStaticDashboardDrawn) {
    drawStaticDashboard();
  }
  drawInaStatus();

  char value[24];
  if (gIna.valid) {
    snprintf(value, sizeof(value), "%5.3F V", gIna.busVoltageV);
    drawMetricValue(88, value,
                    (gIna.busVoltageV >= 4.75F && gIna.busVoltageV <= 5.25F) ? kWhite : kYellow);
    snprintf(value, sizeof(value), "%6.1F MA", gIna.currentMa);
    drawMetricValue(125, value);
    snprintf(value, sizeof(value), "%5.3F W", gIna.powerMw / 1000.0F);
    drawMetricValue(162, value);
    snprintf(value, sizeof(value), "%6.1F MA", gIna.peakCurrentMa);
    drawMetricValue(199, value, kCyan);
  } else {
    drawMetricValue(88, "N/A", kRed);
    drawMetricValue(125, "N/A", kRed);
    drawMetricValue(162, "N/A", kRed);
    drawMetricValue(199, "N/A", kRed);
  }

  fillRect(12, 248, 216, 39, kPanel);
  if (!gLedDriverReady) {
    drawCenteredText(251, "RMT DRIVER FAIL", kRed, 2);
  } else if (gLedStage == LedStage::Locate) {
    snprintf(value, sizeof(value), "U%u ONLY", 12U + gLocateIndex);
    drawCenteredText(251, value, kYellow, 2);
    snprintf(value, sizeof(value), "LED %u / %u", gLocateIndex + 1U,
             static_cast<unsigned>(kWs2812Count));
    drawCenteredText(273, value, kWhite, 1);
  } else {
    drawCenteredText(251, ledStageName(), kYellow, 2);
    drawCenteredText(273, "VERIFY COLOR AND CURRENT", kWhite, 1);
  }

  const uint32_t duration = ledStageDurationMs(gLedStage);
  const uint32_t elapsed = min<uint32_t>(now - gLedStageStartedMs, duration);
  fillRect(14, 294, 212, 9, kPanelBright);
  fillRect(14, 294, static_cast<int16_t>(212U * elapsed / max<uint32_t>(duration, 1U)),
           9, kCyan);
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  Serial.setTxTimeoutMs(0);
  initializeDisplay();
  drawStaticDashboard();
  initializeIna226();
  initializeWs2812();

  const uint32_t now = millis();
  gLedStageStartedMs = now;
  gLastLedFrameMs = now;
  gLocateIndex = 0xFFU;
  applyLedStage(now, true);
  sampleIna226();
  drawDashboard(now);

  Serial.println();
  Serial.println("GRIDOPOLY TILE BOARD BRING-UP TEST");
  Serial.println("WS2812: U12-U21 locate, RGBW, brightness ramp");
  Serial.println("INA226: 5V_IN voltage/current/power dashboard");
}

void loop() {
  const uint32_t now = millis();
  applyLedStage(now, false);
  advanceLedStage(now);

  if (now - gLastInaSampleMs >= kInaSamplePeriodMs) {
    gLastInaSampleMs = now;
    sampleIna226();
  }
  if (now - gLastDashboardMs >= kDashboardPeriodMs) {
    gLastDashboardMs = now;
    drawDashboard(now);
    if (gIna.valid && (gIna.sampleCount % 10U) == 0U) {
      Serial.printf("[POWER] VBUS=%.3fV current=%.1fmA power=%.3fW peak=%.1fmA shunt=%.3fmV\r\n",
                    gIna.busVoltageV, gIna.currentMa, gIna.powerMw / 1000.0F,
                    gIna.peakCurrentMa, gIna.shuntVoltageMv);
    }
  }
  delay(1);
}
