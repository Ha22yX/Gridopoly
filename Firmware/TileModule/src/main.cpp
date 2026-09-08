#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp32-hal-rmt.h>
#include <esp_system.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_config.h"
#include "hitag_s_decoder.h"
#include "hitag_s_protocol.h"
#include "tag_presence_filter.h"
#include "tile_artwork.h"
#include "tile_connection_view.h"
#include "tile_model.h"
#include "tile_network.h"
#include "tile_ui_theme.h"

namespace {

using namespace gridopoly::tile;
using namespace gridopoly::tile::board;
using namespace gridopoly::tile::theme;

constexpr std::uint8_t kInaRegConfig = 0x00;
constexpr std::uint8_t kInaRegShuntVoltage = 0x01;
constexpr std::uint8_t kInaRegBusVoltage = 0x02;
constexpr std::uint8_t kInaRegPower = 0x03;
constexpr std::uint8_t kInaRegCurrent = 0x04;
constexpr std::uint8_t kInaRegCalibration = 0x05;
constexpr std::uint8_t kInaRegManufacturerId = 0xFE;
constexpr std::uint8_t kInaRegDieId = 0xFF;
constexpr std::uint16_t kInaConfig = 0x0527;
constexpr std::uint16_t kInaCalibration = 0x1400;

constexpr std::uint32_t kRmtFrequencyHz = 8'000'000;
constexpr std::uint32_t kPowerSamplePeriodMs = 200;
constexpr std::uint32_t kUiRefreshPeriodMs = 500;
constexpr std::uint32_t kLedFramePeriodMs = 50;
constexpr std::uint32_t kTileRenderConfirmDelayMs = 1000;
constexpr std::uint8_t kHtrcGetConfigPage = 0x04;
constexpr std::uint8_t kHtrcSetConfigPage = 0x40;
constexpr std::uint8_t kHtrcFieldOffPage1 = 0x01;
constexpr std::uint32_t kHtrcHalfCycleUs = 24;
constexpr std::uint32_t kHtrcGuardUs = 12;
constexpr std::uint32_t kHtrcSafeRewritePeriodMs = 1000;
constexpr std::uint8_t kHtrcGetSamplingTime = 0x02;
constexpr std::uint8_t kHtrcReadPhaseCommand = 0x08;
constexpr std::uint8_t kHtrcSetSamplingTime = 0x80;
constexpr std::uint8_t kHtrcReceiverPage0 = 0x0B;
constexpr std::uint8_t kHtrcFieldOnPage1 = 0x00;
constexpr std::uint8_t kHtrcClockPage3 = 0x00;
constexpr std::uint8_t kHtrcAntFailMask = 0x10;
constexpr std::uint32_t kHtrcFastHalfCycleUs = 2;
constexpr std::uint32_t kHtrcFastGuardUs = 1;
constexpr std::uint32_t kTagSearchPeriodMs = 400;
constexpr std::uint32_t kTagPresencePollPeriodMs = 250;
constexpr std::uint32_t kTagMultiPollPeriodMs = 500;
constexpr std::uint32_t kTagCaptureMs = 24;
constexpr std::uint32_t kTagMaximumFieldMs = 350;
constexpr std::uint32_t kTagInventoryWorkBudgetMs = 300;
constexpr std::uint32_t kTagInventoryCommandGuardUs = 1600;
constexpr std::size_t kTagInventoryPrefixCapacity = 32;
constexpr std::size_t kTagMaximumEdges = 1024;
constexpr std::uint8_t kHitagWritePulseCycles = 7;
constexpr std::uint32_t kHitagFieldResetMs = 6;
constexpr std::uint32_t kHitagFirstRequestUs = 3000;
constexpr std::uint32_t kHitagZeroIntervalUs = 160;
constexpr std::uint32_t kHitagOneIntervalUs = 224;
constexpr std::uint32_t kHitagEofSilenceUs = 304;
constexpr std::uint8_t kHitagUidRequestBits[] = {1U, 1U, 0U, 0U, 1U};
constexpr float kTagMinimumBusVoltageV = 4.65F;
constexpr float kTagMaximumTotalCurrentMa = 350.0F;
constexpr float kTagMaximumCurrentIncreaseMa = 180.0F;

enum class TagStatus : std::uint8_t {
  Scanning,
  NoTag,
  Present,
  Unstable,
  Fault,
};

struct PowerReading {
  bool present = false;
  bool configured = false;
  bool valid = false;
  float bus_voltage_v = 0.0F;
  float current_ma = 0.0F;
  float power_mw = 0.0F;
  float peak_current_ma = 0.0F;
  std::uint16_t manufacturer_id = 0;
  std::uint16_t die_id = 0;
};

struct TagEdge {
  std::uint32_t elapsed_us = 0;
  std::uint8_t level = 0;
};

struct TagAttempt {
  hitag_s::DecodeResult decoded{};
  bool safety_ok = false;
  bool field_off_ok = false;
  bool overflow = false;
  std::uint16_t edge_count = 0;
  std::uint16_t pulse_count = 0;
  std::uint32_t first_edge_us = 0;
};

struct TagResponseCapture {
  hitag_s::DecodeResult decoded{};
  bool overflow = false;
  std::uint16_t edge_count = 0;
  std::uint16_t pulse_count = 0;
  std::uint32_t first_edge_us = 0;
};

struct TagInventoryAttempt {
  hitag_s::UidSet tags{};
  bool safety_ok = false;
  bool field_off_ok = false;
  bool overflow = false;
  bool incomplete = false;
  std::uint8_t commands = 0;
  bool antenna_ok = false;
  std::uint32_t field_duration_ms = 0;
  float field_voltage_v = 0.0F;
  float field_current_ma = 0.0F;
  float baseline_current_ma = 0.0F;
};

SPIClass gDisplaySpi(FSPI);
Adafruit_ST7789 gDisplay(&gDisplaySpi, kLcdChipSelectPin,
                         kLcdDataCommandPin, kLcdResetPin);
GFXcanvas16 gFooterCanvas(kDisplayWidth, 34);
TileNetworkClient gNetwork;
TileNetworkSnapshot gNetworkSnapshot{};
PowerReading gPower;
Rgb gPixels[kWs2812Count]{};
rmt_data_t gLedSymbols[kWs2812Count * 24]{};
alignas(4) std::uint16_t gArtworkRow[160]{};
bool gDisplayReady = false;
rmt_obj_t *gLedRmt = nullptr;
bool gLedReady = false;
bool gDiagnosticPage = false;
bool gTileRenderConfirmationPending = false;
std::uint8_t gBacklightDuty = kBacklightDuty;
std::uint32_t gLastPowerSampleMs = 0;
std::uint32_t gLastUiRefreshMs = 0;
std::uint32_t gLastLedFrameMs = 0;
std::uint32_t gMovementCueStartedMs = 0;
std::uint32_t gTileFirstRenderMs = 0;
std::uint32_t gLastHtrcSafeRewriteMs = 0;
bool gHtrcTransmitterSafe = false;
std::uint32_t gLastTagScanMs = 0;
TagStatus gTagStatus = TagStatus::Scanning;
hitag_s::Uid gConfirmedTagUids[hitag_s::kMaximumInventoryTags]{};
char gTagUids[hitag_s::kMaximumInventoryTags][9]{};
std::uint8_t gConfirmedTagCount = 0;
bool gConfirmedTagOverflow = false;
std::uint8_t gTagMissingEvidence[hitag_s::kMaximumInventoryTags]{};
std::uint8_t gTagPreferredSampling = 0;
bool gTagPreferredSamplingValid = false;
bool gTagUiDirty = false;
bool gTagNetworkPublishPending = false;
bool gRfidDebug = false;
char gSerialLine[64]{};
std::size_t gSerialLength = 0;
DRAM_ATTR volatile TagEdge gTagEdges[kTagMaximumEdges]{};
DRAM_ATTR volatile std::uint16_t gTagEdgeCount = 0;
DRAM_ATTR volatile std::uint32_t gTagCaptureStartUs = 0;
DRAM_ATTR volatile bool gTagCaptureActive = false;
DRAM_ATTR volatile bool gTagCaptureOverflow = false;

std::uint16_t displayColor(Rgb color) {
  return rgb565(color.red, color.green, color.blue);
}

Rgb playerColor(std::uint8_t player) {
  if (player < 1 || player > kMaximumPlayers) {
    return {};
  }
  const Rgb888 color = kPlayerColors[player - 1U];
  return {color.red, color.green, color.blue};
}

const TileState &currentTileState() {
  return gNetworkSnapshot.tile;
}

Rgb scaleColor(Rgb color, std::uint8_t maximum_channel) {
  return {
      static_cast<std::uint8_t>(static_cast<std::uint16_t>(color.red) * maximum_channel / 255U),
      static_cast<std::uint8_t>(static_cast<std::uint16_t>(color.green) * maximum_channel / 255U),
      static_cast<std::uint8_t>(static_cast<std::uint16_t>(color.blue) * maximum_channel / 255U),
  };
}

void setSafeOutputs() {
  digitalWrite(kLcdBacklightPin, LOW);
  pinMode(kLcdBacklightPin, OUTPUT);
  digitalWrite(kLcdChipSelectPin, HIGH);
  pinMode(kLcdChipSelectPin, OUTPUT);
  digitalWrite(kLcdDataCommandPin, LOW);
  pinMode(kLcdDataCommandPin, OUTPUT);
  digitalWrite(kLcdResetPin, HIGH);
  pinMode(kLcdResetPin, OUTPUT);

  digitalWrite(kRs485DirectionPin, LOW);
  pinMode(kRs485DirectionPin, OUTPUT);
  digitalWrite(kOrderOutPin, LOW);
  pinMode(kOrderOutPin, OUTPUT);
  pinMode(kOrderInPin, INPUT);
  digitalWrite(kRfidClockPin, LOW);
  pinMode(kRfidClockPin, OUTPUT);
  digitalWrite(kRfidDataInPin, LOW);
  pinMode(kRfidDataInPin, OUTPUT);
  pinMode(kRfidDataOutPin, INPUT);
  digitalWrite(kWs2812DataPin, LOW);
  pinMode(kWs2812DataPin, OUTPUT);
}

void htrcDelay() {
  delayMicroseconds(kHtrcHalfCycleUs);
}

void htrcSerialReset() {
  // HTRC110 serial-interface reset: DIN low-to-high while SCLK is high.
  digitalWrite(kRfidClockPin, LOW);
  digitalWrite(kRfidDataInPin, LOW);
  delayMicroseconds(kHtrcGuardUs);
  digitalWrite(kRfidClockPin, HIGH);
  htrcDelay();
  digitalWrite(kRfidDataInPin, HIGH);
  htrcDelay();
  digitalWrite(kRfidClockPin, LOW);
  htrcDelay();
}

void htrcWriteBits(std::uint8_t value, std::uint8_t bit_count) {
  for (int bit = bit_count - 1; bit >= 0; --bit) {
    digitalWrite(kRfidDataInPin,
                 (value & (1U << bit)) != 0U ? HIGH : LOW);
    delayMicroseconds(kHtrcGuardUs);
    digitalWrite(kRfidClockPin, HIGH);
    htrcDelay();
    digitalWrite(kRfidClockPin, LOW);
    htrcDelay();
  }
}

std::uint8_t htrcReadBits(std::uint8_t bit_count) {
  std::uint8_t value = 0;
  digitalWrite(kRfidDataInPin, LOW);
  delayMicroseconds(kHtrcGuardUs);
  for (std::uint8_t bit = 0; bit < bit_count; ++bit) {
    digitalWrite(kRfidClockPin, HIGH);
    htrcDelay();
    value = static_cast<std::uint8_t>(
        (value << 1U) |
        (digitalRead(kRfidDataOutPin) == HIGH ? 1U : 0U));
    digitalWrite(kRfidClockPin, LOW);
    htrcDelay();
  }
  return value;
}

void htrcWriteCommand(std::uint8_t command) {
  htrcSerialReset();
  htrcWriteBits(command, 8);
  digitalWrite(kRfidDataInPin, LOW);
}

std::uint8_t htrcReadCommand(std::uint8_t command) {
  htrcSerialReset();
  htrcWriteBits(command, 8);
  return htrcReadBits(8);
}

bool forceHtrcTransmitterOff() {
  const std::uint8_t set_page1 = static_cast<std::uint8_t>(
      kHtrcSetConfigPage | (1U << 4U) | kHtrcFieldOffPage1);
  for (std::uint8_t attempt = 0; attempt < 3U; ++attempt) {
    htrcWriteCommand(set_page1);
    const std::uint8_t page1 = htrcReadCommand(
        static_cast<std::uint8_t>(kHtrcGetConfigPage | 1U));
    if ((page1 & 0x0FU) == kHtrcFieldOffPage1) {
      return true;
    }
    delay(1);
  }
  return false;
}

void htrcSetConfig(std::uint8_t page, std::uint8_t data) {
  htrcWriteCommand(static_cast<std::uint8_t>(
      kHtrcSetConfigPage | ((page & 0x03U) << 4U) | (data & 0x0FU)));
}

std::uint8_t htrcGetConfig(std::uint8_t page) {
  return htrcReadCommand(
      static_cast<std::uint8_t>(kHtrcGetConfigPage | (page & 0x03U)));
}

void htrcSetSampling(std::uint8_t value) {
  htrcWriteCommand(
      static_cast<std::uint8_t>(kHtrcSetSamplingTime | (value & 0x3FU)));
}

std::uint8_t htrcGetSampling() {
  return htrcReadCommand(kHtrcGetSamplingTime);
}

std::uint8_t htrcReadPhase() {
  return static_cast<std::uint8_t>(
      htrcReadCommand(kHtrcReadPhaseCommand) & 0x3FU);
}

void htrcFastSerialReset() {
  digitalWrite(kRfidClockPin, LOW);
  digitalWrite(kRfidDataInPin, LOW);
  delayMicroseconds(kHtrcFastGuardUs);
  digitalWrite(kRfidClockPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kRfidDataInPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kRfidClockPin, LOW);
  delayMicroseconds(kHtrcFastHalfCycleUs);
}

void htrcFastWriteBits(std::uint8_t value, std::uint8_t bit_count) {
  for (int bit = bit_count - 1; bit >= 0; --bit) {
    digitalWrite(kRfidDataInPin,
                 (value & (1U << bit)) != 0U ? HIGH : LOW);
    delayMicroseconds(kHtrcFastGuardUs);
    digitalWrite(kRfidClockPin, HIGH);
    delayMicroseconds(kHtrcFastHalfCycleUs);
    digitalWrite(kRfidClockPin, LOW);
    delayMicroseconds(kHtrcFastHalfCycleUs);
  }
}

void htrcFastSetConfig(std::uint8_t page, std::uint8_t data) {
  htrcFastSerialReset();
  htrcFastWriteBits(static_cast<std::uint8_t>(
                        kHtrcSetConfigPage | ((page & 0x03U) << 4U) |
                        (data & 0x0FU)),
                    8U);
  digitalWrite(kRfidDataInPin, LOW);
}

void htrcFastExitMode() {
  digitalWrite(kRfidDataInPin, LOW);
  digitalWrite(kRfidClockPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kRfidClockPin, LOW);
  delayMicroseconds(kHtrcFastHalfCycleUs);
}

void htrcEnterReadTag() {
  // READ_TAG (111) enters transparent mode on the third rising edge.
  htrcFastSerialReset();
  htrcFastWriteBits(0x07U, 3U);
  digitalWrite(kRfidDataInPin, LOW);
}

void waitUntilMicros(std::uint32_t target_us) {
  while (static_cast<std::int32_t>(micros() - target_us) < 0) {
  }
}

void htrcTriggerWritePulse() {
  digitalWrite(kRfidDataInPin, HIGH);
  delayMicroseconds(kHtrcFastGuardUs);
  digitalWrite(kRfidDataInPin, LOW);
}

bool htrcProgramWritePulseWidth(std::uint8_t carrier_cycles) {
  noInterrupts();
  htrcFastSerialReset();
  htrcFastWriteBits(static_cast<std::uint8_t>(0x10U | carrier_cycles), 8U);
  digitalWrite(kRfidDataInPin, LOW);
  delayMicroseconds(static_cast<std::uint32_t>(carrier_cycles) * 8U + 8U);
  htrcFastExitMode();
  interrupts();
  const std::uint8_t page1 = htrcGetConfig(1);
  return (page1 >> 4U) == carrier_cycles &&
         (page1 & 0x0FU) == kHtrcFieldOffPage1;
}

bool htrcSendHitagFrame(const std::uint8_t *bytes, std::uint8_t bit_count,
                        std::uint32_t first_boundary) {
  if (bytes == nullptr || bit_count == 0U ||
      bit_count > hitag_s::kMaximumReaderFrameBits) {
    return false;
  }
  htrcSetConfig(2, 0x09U);
  waitUntilMicros(first_boundary - 120U);

  noInterrupts();
  htrcFastSerialReset();
  htrcFastWriteBits(0x06U, 3U);
  waitUntilMicros(first_boundary);
  htrcTriggerWritePulse();
  std::uint32_t boundary = first_boundary;
  for (std::size_t index = 0; index < bit_count; ++index) {
    const std::uint8_t bit = hitag_s::bitAt(bytes, index);
    boundary += bit != 0U ? kHitagOneIntervalUs : kHitagZeroIntervalUs;
    waitUntilMicros(boundary);
    htrcTriggerWritePulse();
  }
  waitUntilMicros(boundary + kHitagEofSilenceUs);
  htrcFastExitMode();
  interrupts();

  // NXP fast-settling sequence before the tag response window.
  delayMicroseconds(150);
  htrcFastSetConfig(2, 0x0BU);
  delayMicroseconds(200);
  htrcFastSetConfig(2, 0x00U);
  return true;
}

bool htrcSendHitagUidRequest(std::uint32_t field_started_us) {
  // Preserve the V0.23 single-tag waveform byte-for-byte. The packed generic
  // sender above is reserved for variable-length AC SEQUENCE frames.
  htrcSetConfig(2, 0x09U);
  const std::uint32_t first_boundary =
      field_started_us + kHitagFirstRequestUs;
  waitUntilMicros(first_boundary - 120U);

  noInterrupts();
  htrcFastSerialReset();
  htrcFastWriteBits(0x06U, 3U);
  waitUntilMicros(first_boundary);
  htrcTriggerWritePulse();
  std::uint32_t boundary = first_boundary;
  for (const std::uint8_t bit : kHitagUidRequestBits) {
    boundary += bit != 0U ? kHitagOneIntervalUs : kHitagZeroIntervalUs;
    waitUntilMicros(boundary);
    htrcTriggerWritePulse();
  }
  waitUntilMicros(boundary + kHitagEofSilenceUs);
  htrcFastExitMode();
  interrupts();

  delayMicroseconds(150);
  htrcFastSetConfig(2, 0x0BU);
  delayMicroseconds(200);
  htrcFastSetConfig(2, 0x00U);
  return true;
}

void ARDUINO_ISR_ATTR captureTagEdge() {
  if (!gTagCaptureActive) {
    return;
  }
  const std::uint16_t index = gTagEdgeCount;
  if (index >= kTagMaximumEdges) {
    gTagCaptureOverflow = true;
    return;
  }
  gTagEdges[index].elapsed_us = micros() - gTagCaptureStartUs;
  gTagEdges[index].level = static_cast<std::uint8_t>(
      gpio_get_level(static_cast<gpio_num_t>(kRfidDataOutPin)) != 0 ? 1U : 0U);
  gTagEdgeCount = static_cast<std::uint16_t>(index + 1U);
}

bool verifyHtrcDigitalLink() {
  if (!forceHtrcTransmitterOff()) {
    return false;
  }
  const std::uint8_t original =
      static_cast<std::uint8_t>(htrcGetSampling() & 0x3FU);
  constexpr std::uint8_t kPattern = 0x2AU;
  htrcSetSampling(kPattern);
  const std::uint8_t pattern = htrcGetSampling();
  htrcSetSampling(original);
  const std::uint8_t restored = htrcGetSampling();
  return (pattern & 0x3FU) == kPattern &&
         (restored & 0x3FU) == original;
}

void initializeDisplay() {
  gDisplaySpi.begin(kLcdClockPin, kNoMisoPin, kLcdMosiPin, kLcdChipSelectPin);
  gDisplay.init(kDisplayWidth, kDisplayHeight, SPI_MODE0);
  // ST7789::init() resets the Adafruit bus setting to its 32 MHz default.
  // Apply the board-safe rate after init so Wi-Fi-active artwork writes really
  // run at 8 MHz instead of silently returning to 32 MHz.
  gDisplay.setSPISpeed(kDisplaySpiFrequencyHz);
  gDisplay.setRotation(0);
  gDisplay.setTextWrap(false);
  gDisplay.fillScreen(ST77XX_BLACK);
  analogWrite(kLcdBacklightPin, gBacklightDuty);
  gDisplayReady = true;
}

void recoverDisplayController() {
  Serial.println(F("[DISPLAY] reinitializing ST7789 controller"));
  analogWrite(kLcdBacklightPin, 0);
  gDisplayReady = false;
  gDisplaySpi.end();
  delay(20);
  initializeDisplay();
}

bool inaWrite(std::uint8_t reg, std::uint16_t value) {
  Wire.beginTransmission(kIna226Address);
  Wire.write(reg);
  Wire.write(static_cast<std::uint8_t>(value >> 8U));
  Wire.write(static_cast<std::uint8_t>(value));
  return Wire.endTransmission() == 0;
}

bool inaRead(std::uint8_t reg, std::uint16_t &value) {
  Wire.beginTransmission(kIna226Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(kIna226Address, static_cast<std::uint8_t>(2)) != 2) {
    return false;
  }
  value = static_cast<std::uint16_t>(Wire.read()) << 8U;
  value |= static_cast<std::uint16_t>(Wire.read());
  return true;
}

void initializePowerMonitor() {
  Wire.begin(kCurrentSdaPin, kCurrentSclPin, 400000U);
  delay(5);
  Wire.beginTransmission(kIna226Address);
  gPower.present = Wire.endTransmission() == 0;
  if (!gPower.present) {
    Serial.println(F("[INA226] not found at 0x40"));
    return;
  }
  std::uint16_t calibration = 0;
  const bool ids_ok = inaRead(kInaRegManufacturerId, gPower.manufacturer_id) &&
                      inaRead(kInaRegDieId, gPower.die_id);
  gPower.configured = ids_ok && gPower.manufacturer_id == 0x5449U &&
                      (gPower.die_id & 0xFFF0U) == 0x2260U &&
                      inaWrite(kInaRegConfig, kInaConfig) &&
                      inaWrite(kInaRegCalibration, kInaCalibration) &&
                      inaRead(kInaRegCalibration, calibration) &&
                      calibration == kInaCalibration;
  Serial.printf("[INA226] manufacturer=0x%04X die=0x%04X calibration=%s\r\n",
                gPower.manufacturer_id, gPower.die_id,
                gPower.configured ? "OK" : "FAIL");
}

void samplePower() {
  if (!gPower.configured) {
    gPower.valid = false;
    return;
  }
  std::uint16_t bus = 0;
  std::uint16_t shunt = 0;
  std::uint16_t current = 0;
  std::uint16_t power = 0;
  if (!inaRead(kInaRegBusVoltage, bus) || !inaRead(kInaRegShuntVoltage, shunt) ||
      !inaRead(kInaRegCurrent, current) || !inaRead(kInaRegPower, power)) {
    gPower.valid = false;
    return;
  }
  gPower.bus_voltage_v = static_cast<float>(bus) * 0.00125F;
  gPower.current_ma = static_cast<float>(static_cast<std::int16_t>(current)) * 0.1F;
  gPower.power_mw = static_cast<float>(power) * 2.5F;
  gPower.peak_current_ma = std::max(gPower.peak_current_ma, gPower.current_ma);
  gPower.valid = true;
  (void)shunt;
}

bool tagPowerSafe(const PowerReading &reading, float baseline_current_ma) {
  return reading.valid && reading.bus_voltage_v >= kTagMinimumBusVoltageV &&
         reading.current_ma <= kTagMaximumTotalCurrentMa &&
         reading.current_ma - baseline_current_ma <=
             kTagMaximumCurrentIncreaseMa;
}

bool prepareHitagReader(const PowerReading &baseline, std::uint8_t &sampling) {
  htrcSetConfig(3, kHtrcClockPage3);
  htrcSetConfig(0, kHtrcReceiverPage0);
  htrcSetConfig(2, 0x00U);
  htrcSetConfig(1, kHtrcFieldOnPage1);
  gHtrcTransmitterSafe = false;
  delayMicroseconds(200);

  const std::uint8_t page0 = htrcGetConfig(0);
  const std::uint8_t page1 = htrcGetConfig(1);
  const std::uint8_t page2 = htrcGetConfig(2);
  const std::uint8_t page3 = htrcGetConfig(3);
  const bool config_ok = (page0 & 0x0FU) == kHtrcReceiverPage0 &&
                         (page1 & 0x0FU) == kHtrcFieldOnPage1 &&
                         (page2 & 0x0FU) == 0x00U &&
                         (page3 & 0x0FU) == kHtrcClockPage3;
  const bool antenna_ok = ((page2 | page3) & kHtrcAntFailMask) == 0U;
  if (!config_ok || !antenna_ok) {
    gHtrcTransmitterSafe = forceHtrcTransmitterOff();
    return false;
  }

  std::uint16_t phase_sum = 0;
  constexpr std::uint8_t kPhaseSamples = 8;
  for (std::uint8_t index = 0; index < kPhaseSamples; ++index) {
    phase_sum = static_cast<std::uint16_t>(phase_sum + htrcReadPhase());
  }
  const std::uint8_t phase = static_cast<std::uint8_t>(
      (phase_sum + kPhaseSamples / 2U) / kPhaseSamples);
  sampling = static_cast<std::uint8_t>(
      ((static_cast<std::uint16_t>(phase) * 2U) + 0x3FU) & 0x3FU);
  htrcSetSampling(sampling);
  const bool sampling_ok = (htrcGetSampling() & 0x3FU) == sampling;

  htrcSetConfig(2, 0x0BU);
  delay(4);
  htrcSetConfig(2, 0x08U);
  delay(1);
  htrcSetConfig(2, 0x00U);
  const std::uint8_t settled_page2 = htrcGetConfig(2);
  samplePower();
  const PowerReading field_power = gPower;
  const bool settled_ok = sampling_ok &&
                          (settled_page2 & kHtrcAntFailMask) == 0U &&
                          (settled_page2 & 0x0FU) == 0x00U;
  gHtrcTransmitterSafe = forceHtrcTransmitterOff();
  if (!settled_ok || !tagPowerSafe(field_power, baseline.current_ma) ||
      !gHtrcTransmitterSafe) {
    return false;
  }

  const bool pulse_width_ok =
      htrcProgramWritePulseWidth(kHitagWritePulseCycles);
  delay(kHitagFieldResetMs);
  return pulse_width_ok;
}

TagResponseCapture captureHitagResponse(std::uint8_t expected_bits) {
  TagResponseCapture response;
  if (expected_bits == 0U || expected_bits > 32U) {
    return response;
  }
  gTagCaptureActive = false;
  gTagEdgeCount = 0;
  gTagCaptureOverflow = false;
  attachInterrupt(digitalPinToInterrupt(kRfidDataOutPin), captureTagEdge,
                  CHANGE);
  gTagCaptureStartUs = micros();
  gTagCaptureActive = true;
  htrcEnterReadTag();
  const std::uint32_t capture_ms = expected_bits == 32U
                                       ? kTagCaptureMs
                                       : 6U + (expected_bits + 1U) / 2U;
  delay(capture_ms);
  const std::uint32_t capture_ended_us = micros() - gTagCaptureStartUs;
  gTagCaptureActive = false;
  detachInterrupt(digitalPinToInterrupt(kRfidDataOutPin));
  htrcFastExitMode();

  response.overflow = gTagCaptureOverflow;
  static hitag_s::Pulse pulses[kTagMaximumEdges]{};
  std::size_t pulse_count = 0;
  const std::uint16_t edge_count = gTagEdgeCount;
  response.edge_count = edge_count;
  response.first_edge_us = edge_count == 0U ? 0U : gTagEdges[0].elapsed_us;
  for (std::uint16_t index = 0;
       index + 1U < edge_count && pulse_count < kTagMaximumEdges; ++index) {
    const std::uint32_t duration =
        gTagEdges[index + 1U].elapsed_us - gTagEdges[index].elapsed_us;
    if (duration != 0U && duration <= 0xFFFFU) {
      pulses[pulse_count++] = {
          static_cast<std::uint16_t>(duration),
          gTagEdges[index].level != 0U};
    }
  }
  if (edge_count != 0U && pulse_count < kTagMaximumEdges) {
    const std::uint32_t duration =
        capture_ended_us - gTagEdges[edge_count - 1U].elapsed_us;
    if (duration != 0U && duration <= 0xFFFFU) {
      pulses[pulse_count++] = {
          static_cast<std::uint16_t>(duration),
          gTagEdges[edge_count - 1U].level != 0U};
    }
  }
  if (!response.overflow) {
    response.pulse_count = static_cast<std::uint16_t>(pulse_count);
    response.decoded =
        hitag_s::decodeAdvancedBits(pulses, pulse_count, expected_bits);
  }
  return response;
}

TagAttempt captureHitagAttempt(const PowerReading &baseline,
                               std::uint8_t sampling) {
  TagAttempt attempt;
  htrcSetSampling(sampling);
  if ((htrcGetSampling() & 0x3FU) != sampling) {
    attempt.field_off_ok = forceHtrcTransmitterOff();
    gHtrcTransmitterSafe = attempt.field_off_ok;
    return attempt;
  }

  htrcSetConfig(1, kHtrcFieldOnPage1);
  gHtrcTransmitterSafe = false;
  const std::uint32_t field_started_us = micros();
  const std::uint32_t field_started_ms = millis();
  if ((htrcGetConfig(1) & 0x0FU) != kHtrcFieldOnPage1) {
    attempt.field_off_ok = forceHtrcTransmitterOff();
    gHtrcTransmitterSafe = attempt.field_off_ok;
    return attempt;
  }

  htrcSendHitagUidRequest(field_started_us);
  gTagCaptureActive = false;
  gTagEdgeCount = 0;
  gTagCaptureOverflow = false;
  attachInterrupt(digitalPinToInterrupt(kRfidDataOutPin), captureTagEdge,
                  CHANGE);
  gTagCaptureStartUs = micros();
  gTagCaptureActive = true;
  htrcEnterReadTag();
  delay(kTagCaptureMs);
  const std::uint32_t capture_ended_us = micros() - gTagCaptureStartUs;
  gTagCaptureActive = false;
  detachInterrupt(digitalPinToInterrupt(kRfidDataOutPin));
  htrcFastExitMode();

  attempt.overflow = gTagCaptureOverflow;
  samplePower();
  const PowerReading field_power = gPower;
  const std::uint8_t final_page2 = htrcGetConfig(2);
  const bool antenna_ok = (final_page2 & kHtrcAntFailMask) == 0U &&
                          (final_page2 & 0x0FU) == 0x00U;
  attempt.field_off_ok = forceHtrcTransmitterOff();
  gHtrcTransmitterSafe = attempt.field_off_ok;
  const std::uint32_t field_duration_ms = millis() - field_started_ms;

  static hitag_s::Pulse pulses[kTagMaximumEdges]{};
  std::size_t pulse_count = 0;
  const std::uint16_t edge_count = gTagEdgeCount;
  attempt.edge_count = edge_count;
  attempt.first_edge_us = edge_count == 0U ? 0U : gTagEdges[0].elapsed_us;
  for (std::uint16_t index = 0;
       index + 1U < edge_count && pulse_count < kTagMaximumEdges; ++index) {
    const std::uint32_t duration =
        gTagEdges[index + 1U].elapsed_us - gTagEdges[index].elapsed_us;
    if (duration != 0U && duration <= 0xFFFFU) {
      pulses[pulse_count++] = {
          static_cast<std::uint16_t>(duration),
          gTagEdges[index].level != 0U};
    }
  }
  // Preserve the final AC2K run when it shares a level with receiver idle.
  if (edge_count != 0U && pulse_count < kTagMaximumEdges) {
    const std::uint32_t duration =
        capture_ended_us - gTagEdges[edge_count - 1U].elapsed_us;
    if (duration != 0U && duration <= 0xFFFFU) {
      pulses[pulse_count++] = {
          static_cast<std::uint16_t>(duration),
          gTagEdges[edge_count - 1U].level != 0U};
    }
  }

  attempt.safety_ok = !attempt.overflow && antenna_ok &&
                      attempt.field_off_ok &&
                      field_duration_ms <= kTagMaximumFieldMs &&
                      tagPowerSafe(field_power, baseline.current_ma);
  if (attempt.safety_ok) {
    attempt.pulse_count = static_cast<std::uint16_t>(pulse_count);
    attempt.decoded = hitag_s::decodeAdvancedUid(pulses, pulse_count);
  }
  return attempt;
}

TagInventoryAttempt enumerateHitagTags(const PowerReading &baseline,
                                       std::uint8_t sampling) {
  TagInventoryAttempt attempt;
  htrcSetSampling(sampling);
  if ((htrcGetSampling() & 0x3FU) != sampling) {
    attempt.field_off_ok = forceHtrcTransmitterOff();
    gHtrcTransmitterSafe = attempt.field_off_ok;
    return attempt;
  }

  htrcSetConfig(1, kHtrcFieldOnPage1);
  gHtrcTransmitterSafe = false;
  const std::uint32_t field_started_us = micros();
  const std::uint32_t field_started_ms = millis();
  if ((htrcGetConfig(1) & 0x0FU) != kHtrcFieldOnPage1) {
    attempt.field_off_ok = forceHtrcTransmitterOff();
    gHtrcTransmitterSafe = attempt.field_off_ok;
    return attempt;
  }

  static hitag_s::UidPrefix prefixes[kTagInventoryPrefixCapacity]{};
  std::size_t prefix_count = 0;
  const auto add_full_uid = [&](const hitag_s::Uid &uid) {
    if (hitag_s::hasHitagSProductIdentifier(uid)) {
      (void)hitag_s::addUniqueUid(attempt.tags, uid);
    } else {
      attempt.incomplete = true;
    }
  };
  const auto branch_collision = [&](const hitag_s::UidPrefix &base,
                                    const hitag_s::DecodeResult &collision) {
    for (int branch = 1; branch >= 0; --branch) {
      hitag_s::UidPrefix extended;
      if (!hitag_s::extendPrefix(base, collision.uid,
                                 collision.collision_bit,
                                 static_cast<std::uint8_t>(branch),
                                 extended)) {
        attempt.incomplete = true;
        continue;
      }
      if (extended.bit_count == 32U) {
        add_full_uid(extended.uid);
      } else if (prefix_count < kTagInventoryPrefixCapacity) {
        prefixes[prefix_count++] = extended;
      } else {
        attempt.incomplete = true;
      }
    }
  };

  if (!htrcSendHitagUidRequest(field_started_us)) {
    attempt.incomplete = true;
  } else {
    ++attempt.commands;
    const TagResponseCapture root = captureHitagResponse(32U);
    if (gRfidDebug) {
      Serial.printf("[RFID-RAW] kind=ROOT sample=0x%02X edges=%u pulses=%u first=%luus decode=%u start=%u bits=%u collision=%u quarter=%uus\r\n",
                    sampling, static_cast<unsigned>(root.edge_count),
                    static_cast<unsigned>(root.pulse_count),
                    static_cast<unsigned long>(root.first_edge_us),
                    static_cast<unsigned>(root.decoded.status),
                    static_cast<unsigned>(root.decoded.start_pulse),
                    static_cast<unsigned>(root.decoded.decoded_bits),
                    static_cast<unsigned>(root.decoded.collision_bit),
                    static_cast<unsigned>(root.decoded.quarter_us));
    }
    attempt.overflow = root.overflow;
    const hitag_s::UidPrefix empty_prefix{};
    if (root.decoded.status == hitag_s::DecodeStatus::Present) {
      add_full_uid(root.decoded.uid);
    } else if (root.decoded.status == hitag_s::DecodeStatus::Collision) {
      branch_collision(empty_prefix, root.decoded);
    } else if (root.decoded.status == hitag_s::DecodeStatus::Ambiguous) {
      attempt.incomplete = true;
    }
  }

  while (prefix_count != 0U && !attempt.overflow &&
         !attempt.tags.overflow) {
    if (static_cast<std::uint32_t>(millis() - field_started_ms) >=
        kTagInventoryWorkBudgetMs) {
      attempt.incomplete = true;
      break;
    }
    const hitag_s::UidPrefix prefix = prefixes[--prefix_count];
    hitag_s::BitBuffer command;
    if (!hitag_s::buildAnticollisionCommand(prefix, command)) {
      attempt.incomplete = true;
      break;
    }
    const std::uint32_t boundary = micros() + kTagInventoryCommandGuardUs;
    if (!htrcSendHitagFrame(command.bytes, command.bit_count, boundary)) {
      attempt.incomplete = true;
      break;
    }
    ++attempt.commands;
    const std::uint8_t remaining_bits =
        static_cast<std::uint8_t>(32U - prefix.bit_count);
    const TagResponseCapture response =
        captureHitagResponse(remaining_bits);
    if (gRfidDebug) {
      Serial.printf("[RFID-RAW] kind=AC prefix=%u remain=%u sample=0x%02X edges=%u pulses=%u first=%luus decode=%u start=%u bits=%u collision=%u quarter=%uus\r\n",
                    static_cast<unsigned>(prefix.bit_count),
                    static_cast<unsigned>(remaining_bits), sampling,
                    static_cast<unsigned>(response.edge_count),
                    static_cast<unsigned>(response.pulse_count),
                    static_cast<unsigned long>(response.first_edge_us),
                    static_cast<unsigned>(response.decoded.status),
                    static_cast<unsigned>(response.decoded.start_pulse),
                    static_cast<unsigned>(response.decoded.decoded_bits),
                    static_cast<unsigned>(response.decoded.collision_bit),
                    static_cast<unsigned>(response.decoded.quarter_us));
    }
    attempt.overflow = attempt.overflow || response.overflow;
    if (response.decoded.status == hitag_s::DecodeStatus::Present) {
      add_full_uid(hitag_s::combinePrefixAndSuffix(
          prefix, response.decoded.uid, remaining_bits));
    } else if (response.decoded.status == hitag_s::DecodeStatus::Collision) {
      branch_collision(prefix, response.decoded);
    } else if (response.decoded.status == hitag_s::DecodeStatus::Ambiguous) {
      attempt.incomplete = true;
    }
  }
  if (prefix_count != 0U) {
    attempt.incomplete = true;
  }

  samplePower();
  const PowerReading field_power = gPower;
  const std::uint8_t final_page2 = htrcGetConfig(2);
  const bool antenna_ok = (final_page2 & kHtrcAntFailMask) == 0U &&
                          (final_page2 & 0x0FU) == 0x00U;
  attempt.antenna_ok = antenna_ok;
  attempt.field_voltage_v = field_power.bus_voltage_v;
  attempt.field_current_ma = field_power.current_ma;
  attempt.baseline_current_ma = baseline.current_ma;
  attempt.field_off_ok = forceHtrcTransmitterOff();
  gHtrcTransmitterSafe = attempt.field_off_ok;
  const std::uint32_t field_duration_ms = millis() - field_started_ms;
  attempt.field_duration_ms = field_duration_ms;
  attempt.safety_ok = !attempt.overflow && antenna_ok &&
                      attempt.field_off_ok &&
                      field_duration_ms <= kTagMaximumFieldMs &&
                      tagPowerSafe(field_power, baseline.current_ma);
  return attempt;
}

TileTagReaderState networkTagReaderState(TagStatus status) {
  switch (status) {
    case TagStatus::Present:
    case TagStatus::NoTag:
      return TileTagReaderState::Stable;
    case TagStatus::Fault:
      return TileTagReaderState::Fault;
    case TagStatus::Scanning:
    case TagStatus::Unstable:
      return TileTagReaderState::Scanning;
  }
  return TileTagReaderState::Fault;
}

void publishPendingTagObservation() {
  if (gTagNetworkPublishPending &&
      gNetwork.updateTagObservation(networkTagReaderState(gTagStatus),
                                    gTagUids, gConfirmedTagCount,
                                    gConfirmedTagOverflow)) {
    gTagNetworkPublishPending = false;
  }
}

void publishTagInventory(TagStatus status, const hitag_s::UidSet *tags,
                         std::uint8_t sampling,
                         std::uint8_t confirmations) {
  // An incomplete collision tree is only an internal sample result. Keep the
  // last stable PRESENT/NO TAG state so the public UI never sticks on
  // UNSTABLE while the next clean scan is already pending.
  if (status == TagStatus::Unstable &&
      (gTagStatus == TagStatus::Present ||
       gTagStatus == TagStatus::NoTag)) {
    return;
  }

  hitag_s::UidSet filtered;
  const hitag_s::UidSet *effective_tags = tags;
  TagStatus effective_status = status;
  if (gTagStatus == TagStatus::Present && gConfirmedTagCount != 0U) {
    const bool reports_tags =
        status == TagStatus::Present && tags != nullptr && tags->count != 0U;
    bool loses_confirmed_uid = false;
    bool removal_confirmed = false;
    for (std::size_t index = 0; index < gConfirmedTagCount; ++index) {
      const bool observed =
          reports_tags && hitag_s::containsUid(*tags, gConfirmedTagUids[index]);
      loses_confirmed_uid = loses_confirmed_uid || !observed;
      gTagMissingEvidence[index] = tag_presence::updateMissingEvidence(
          gTagMissingEvidence[index], observed);
      removal_confirmed =
          removal_confirmed ||
          (!observed && tag_presence::removalConfirmed(
                            gTagMissingEvidence[index]));
    }
    if (loses_confirmed_uid && !removal_confirmed) {
      return;
    }
    if (loses_confirmed_uid) {
      // Remove only UIDs whose individual evidence crossed the threshold.
      // Another UID missed only in this scan remains in the stable set.
      for (std::size_t index = 0; index < gConfirmedTagCount; ++index) {
        if (!tag_presence::removalConfirmed(gTagMissingEvidence[index])) {
          (void)hitag_s::addUniqueUid(filtered, gConfirmedTagUids[index]);
        }
      }
      if (reports_tags) {
        for (std::size_t index = 0; index < tags->count; ++index) {
          (void)hitag_s::addUniqueUid(filtered, tags->uids[index]);
        }
        filtered.overflow = filtered.overflow || tags->overflow;
      }
      effective_status =
          filtered.count == 0U ? TagStatus::NoTag : TagStatus::Present;
      effective_tags = filtered.count == 0U ? nullptr : &filtered;
    }
  }

  std::uint8_t next_count = 0;
  bool next_overflow = false;
  if (effective_status == TagStatus::Present && effective_tags != nullptr &&
      effective_tags->count != 0U) {
    next_count = effective_tags->count;
    next_overflow = effective_tags->overflow;
  }
  bool changed = effective_status != gTagStatus ||
                 next_count != gConfirmedTagCount ||
                 next_overflow != gConfirmedTagOverflow;
  if (!changed) {
    for (std::size_t index = 0; index < next_count; ++index) {
      if (!hitag_s::equalUid(gConfirmedTagUids[index],
                             effective_tags->uids[index])) {
        changed = true;
        break;
      }
    }
  }
  if (!changed) {
    // The per-UID evidence was already advanced or recovered above. Do not
    // erase it just because the externally published set stayed the same.
    if (next_count != 0U) {
      gTagPreferredSampling = sampling;
      gTagPreferredSamplingValid = true;
    }
    return;
  }

  std::memset(gTagUids, 0, sizeof(gTagUids));
  std::memset(gTagMissingEvidence, 0, sizeof(gTagMissingEvidence));
  if (next_count != 0U) {
    gTagPreferredSampling = sampling;
    gTagPreferredSamplingValid = true;
    for (std::size_t index = 0; index < next_count; ++index) {
      gConfirmedTagUids[index] = effective_tags->uids[index];
      hitag_s::formatUid(effective_tags->uids[index], gTagUids[index]);
    }
  }
  gConfirmedTagCount = next_count;
  gConfirmedTagOverflow = next_overflow;
  gTagStatus = effective_status;
  if (changed) {
    // Local display state may advance even when the network mutex is busy.
    // Retry the latest complete inventory until it is accepted; an identical
    // later scan must not suppress a failed publication permanently.
    gTagNetworkPublishPending = true;
    publishPendingTagObservation();

    gTagUiDirty = true;
    const char *state = effective_status == TagStatus::Present
                            ? "PRESENT"
                            : effective_status == TagStatus::NoTag
                                  ? "NO_TAG"
                                  : effective_status == TagStatus::Unstable
                                        ? "UNSTABLE"
                                        : effective_status == TagStatus::Fault
                                              ? "FAULT"
                                              : "SCANNING";
    Serial.printf("[TAG] t=%lums state=%s type=HITAG_S256 count=%u%s uids=",
                  static_cast<unsigned long>(millis()), state,
                  static_cast<unsigned>(gConfirmedTagCount),
                  gConfirmedTagOverflow ? "+" : "");
    if (gConfirmedTagCount == 0U) {
      Serial.print(F("--------"));
    } else {
      for (std::size_t index = 0; index < gConfirmedTagCount; ++index) {
        Serial.printf("%s%s", index == 0U ? "" : ",", gTagUids[index]);
      }
    }
    Serial.printf(" consistent=%u sampling=0x%02X field_off=%s\r\n",
                  confirmations, sampling,
                  gHtrcTransmitterSafe ? "PASS" : "FAIL");
  }
}

void publishTagStatus(TagStatus status, const hitag_s::Uid *uid,
                      std::uint8_t sampling, std::uint8_t confirmations) {
  hitag_s::UidSet tags;
  if (status == TagStatus::Present && uid != nullptr) {
    (void)hitag_s::addUniqueUid(tags, *uid);
  }
  publishTagInventory(status, &tags, sampling, confirmations);
}

void scanHitagTag() {
  samplePower();
  const PowerReading baseline = gPower;
  if (!verifyHtrcDigitalLink() || !baseline.valid ||
      baseline.bus_voltage_v < kTagMinimumBusVoltageV ||
      baseline.current_ma > kTagMaximumTotalCurrentMa) {
    gHtrcTransmitterSafe = forceHtrcTransmitterOff();
    publishTagStatus(TagStatus::Fault, nullptr, 0U, 0U);
    return;
  }

  std::uint8_t sampling = 0;
  if (!prepareHitagReader(baseline, sampling)) {
    publishTagStatus(TagStatus::Fault, nullptr, sampling, 0U);
    return;
  }

  const auto inventory_at_sampling = [&](std::uint8_t value) {
    hitag_s::UidSet combined;
    bool saw_incomplete = false;
    std::uint16_t command_count = 0;
    constexpr std::uint8_t kInventoryRounds = 3U;
    for (std::uint8_t round = 0; round < kInventoryRounds; ++round) {
      delay(kHitagFieldResetMs);
      const TagInventoryAttempt inventory =
          enumerateHitagTags(baseline, value);
      command_count = static_cast<std::uint16_t>(command_count +
                                                  inventory.commands);
      samplePower();
      if (!inventory.safety_ok || !gHtrcTransmitterSafe) {
        Serial.printf(
            "[TAG-INV] commands=%u count=%u safe=NO sampling=0x%02X duration=%lums voltage=%.3fV current=%.1fmA baseline=%.1fmA antenna=%s field_off=%s overflow=%s\r\n",
            static_cast<unsigned>(inventory.commands),
            static_cast<unsigned>(inventory.tags.count), value,
            static_cast<unsigned long>(inventory.field_duration_ms),
            inventory.field_voltage_v, inventory.field_current_ma,
            inventory.baseline_current_ma,
            inventory.antenna_ok ? "PASS" : "FAIL",
            inventory.field_off_ok ? "PASS" : "FAIL",
            inventory.overflow ? "YES" : "NO");
        publishTagStatus(TagStatus::Fault, nullptr, value, 0U);
        return true;
      }
      saw_incomplete = saw_incomplete || inventory.incomplete;
      combined.overflow = combined.overflow || inventory.tags.overflow;
      for (std::size_t index = 0; index < inventory.tags.count; ++index) {
        (void)hitag_s::addUniqueUid(combined, inventory.tags.uids[index]);
      }
      // Once the current known multi-tag cardinality has been recovered there
      // is no benefit in keeping the field active for another round.
      if (combined.count >= 2U &&
          combined.count >= gConfirmedTagCount && !inventory.incomplete) {
        break;
      }
    }
    if (combined.count != 0U) {
      publishTagInventory(TagStatus::Present, &combined, value, 1U);
      return true;
    }
    if (saw_incomplete) {
      Serial.printf(
          "[TAG-INV] commands=%u count=%u%s complete=NO sampling=0x%02X\r\n",
          static_cast<unsigned>(command_count), 0U,
          combined.overflow ? "+" : "", value);
      publishTagStatus(TagStatus::Unstable, nullptr, value, 0U);
      return true;
    }
    // A collision-shaped weak frame can be caused by a missed DOUT edge. If
    // both AC branches are empty, keep trying the remaining sampling phases
    // instead of allowing that false collision to abort normal single-tag
    // confirmation.
    return false;
  };

  if (gTagStatus == TagStatus::Present && gConfirmedTagCount > 1U &&
      gTagPreferredSamplingValid) {
    if (!inventory_at_sampling(gTagPreferredSampling)) {
      samplePower();
      publishTagStatus(TagStatus::NoTag, nullptr,
                       gTagPreferredSampling, 0U);
    }
    return;
  }

  // Once a UID is confirmed, poll the proven phase first and immediately try
  // the current phase window if it misses. Only a complete multi-phase miss
  // counts toward removal, preventing false NO TAG flashes from phase drift.
  if (gTagStatus == TagStatus::Present && gTagPreferredSamplingValid) {
    std::uint8_t presence_samples[8]{};
    std::size_t presence_sample_count = 0;
    const auto append_presence_sample = [&](std::uint8_t value) {
      for (std::size_t index = 0; index < presence_sample_count; ++index) {
        if (presence_samples[index] == value) {
          return;
        }
      }
      // Confirm twice at the same analogue phase; cross-phase matches can be
      // two different false SOF alignments.
      presence_samples[presence_sample_count++] = value;
      presence_samples[presence_sample_count++] = value;
    };
    append_presence_sample(gTagPreferredSampling);
    append_presence_sample(sampling);
    append_presence_sample(
        static_cast<std::uint8_t>((sampling + 8U) & 0x3FU));
    append_presence_sample(
        static_cast<std::uint8_t>((sampling - 8U) & 0x3FU));

    bool saw_ambiguous = false;
    bool saw_different_uid = false;
    hitag_s::DecodeResult presence_collision{};
    std::uint8_t presence_collision_count = 0;
    std::uint8_t presence_collision_sampling = sampling;
    const auto record_presence_collision = [&](const TagAttempt &value,
                                               std::uint8_t sample) {
      const bool matches =
          presence_collision_count != 0U &&
          presence_collision.collision_bit == value.decoded.collision_bit &&
          hitag_s::equalUidBits(presence_collision.uid, value.decoded.uid,
                                value.decoded.collision_bit);
      presence_collision = value.decoded;
      presence_collision_sampling = sample;
      presence_collision_count =
          static_cast<std::uint8_t>(matches ? presence_collision_count + 1U
                                            : 1U);
      return presence_collision_count >= 2U;
    };
    for (std::size_t index = 0; index < presence_sample_count; ++index) {
      const std::uint8_t presence_sampling = presence_samples[index];
      delay(kHitagFieldResetMs);
      const TagAttempt presence =
          captureHitagAttempt(baseline, presence_sampling);
      if (gRfidDebug) {
        Serial.printf("[RFID-RAW] kind=PRESENCE sample=0x%02X edges=%u pulses=%u first=%luus decode=%u start=%u bits=%u collision=%u quarter=%uus\r\n",
                      presence_sampling,
                      static_cast<unsigned>(presence.edge_count),
                      static_cast<unsigned>(presence.pulse_count),
                      static_cast<unsigned long>(presence.first_edge_us),
                      static_cast<unsigned>(presence.decoded.status),
                      static_cast<unsigned>(presence.decoded.start_pulse),
                      static_cast<unsigned>(presence.decoded.decoded_bits),
                      static_cast<unsigned>(presence.decoded.collision_bit),
                      static_cast<unsigned>(presence.decoded.quarter_us));
      }
      if (!presence.safety_ok || !gHtrcTransmitterSafe) {
        samplePower();
        publishTagStatus(TagStatus::Fault, nullptr, presence_sampling, 0U);
        return;
      }
      if (presence.decoded.status == hitag_s::DecodeStatus::Collision) {
        if (record_presence_collision(presence, presence_sampling)) {
          if (inventory_at_sampling(presence_collision_sampling)) {
            return;
          }
          presence_collision_count = 0U;
        }
        continue;
      }
      if (presence.decoded.status == hitag_s::DecodeStatus::Present) {
        if (hitag_s::equalUid(gConfirmedTagUids[0], presence.decoded.uid)) {
          samplePower();
          publishTagStatus(TagStatus::Present, &gConfirmedTagUids[0],
                           presence_sampling, 1U);
          return;
        }
        saw_different_uid = true;
        break;
      }
      saw_ambiguous = saw_ambiguous ||
                      presence.decoded.status ==
                          hitag_s::DecodeStatus::Ambiguous;
    }
    samplePower();
    if (!saw_different_uid) {
      publishTagStatus(saw_ambiguous ? TagStatus::Unstable
                                     : TagStatus::NoTag,
                       nullptr, gTagPreferredSampling, 0U);
      return;
    }
    // A different complete UID falls through to full confirmation below.
  }

  hitag_s::Uid candidate{};
  std::uint8_t present_count = 0;
  std::uint8_t attempt_count = 0;
  std::uint8_t selected_sampling = sampling;
  bool consistent = true;
  bool all_safe = true;
  bool ambiguous = false;
  hitag_s::DecodeResult collision{};
  std::uint8_t collision_count = 0;
  std::uint8_t collision_sampling = sampling;
  const auto record_collision = [&](const TagAttempt &value,
                                    std::uint8_t sample) {
    const bool matches =
        collision_count != 0U &&
        collision.collision_bit == value.decoded.collision_bit &&
        hitag_s::equalUidBits(collision.uid, value.decoded.uid,
                              value.decoded.collision_bit);
    collision = value.decoded;
    collision_sampling = sample;
    collision_count = static_cast<std::uint8_t>(
        matches ? collision_count + 1U : 1U);
    return collision_count >= 2U;
  };
  std::uint8_t sampling_candidates[8]{};
  std::size_t sampling_candidate_count = 0;
  const auto append_sampling = [&](std::uint8_t value) {
    for (std::size_t index = 0; index < sampling_candidate_count; ++index) {
      if (sampling_candidates[index] == value) {
        return;
      }
    }
    sampling_candidates[sampling_candidate_count++] = value;
    sampling_candidates[sampling_candidate_count++] = value;
  };
  if (gTagPreferredSamplingValid) {
    append_sampling(gTagPreferredSampling);
  }
  append_sampling(sampling);
  append_sampling(static_cast<std::uint8_t>((sampling + 8U) & 0x3FU));
  append_sampling(static_cast<std::uint8_t>((sampling - 8U) & 0x3FU));

  for (std::size_t index = 0;
       index < sampling_candidate_count && attempt_count < 8U; ++index) {
    const std::uint8_t candidate_sampling = sampling_candidates[index];
    delay(kHitagFieldResetMs);
    const TagAttempt attempt =
        captureHitagAttempt(baseline, candidate_sampling);
    if (gRfidDebug) {
      Serial.printf("[RFID-RAW] kind=SEARCH sample=0x%02X edges=%u pulses=%u first=%luus decode=%u start=%u bits=%u collision=%u quarter=%uus\r\n",
                    candidate_sampling,
                    static_cast<unsigned>(attempt.edge_count),
                    static_cast<unsigned>(attempt.pulse_count),
                    static_cast<unsigned long>(attempt.first_edge_us),
                    static_cast<unsigned>(attempt.decoded.status),
                    static_cast<unsigned>(attempt.decoded.start_pulse),
                    static_cast<unsigned>(attempt.decoded.decoded_bits),
                    static_cast<unsigned>(attempt.decoded.collision_bit),
                    static_cast<unsigned>(attempt.decoded.quarter_us));
    }
    ++attempt_count;
    if (!attempt.safety_ok) {
      all_safe = false;
      break;
    }
    if (attempt.decoded.status == hitag_s::DecodeStatus::Collision) {
      if (record_collision(attempt, candidate_sampling)) {
        if (inventory_at_sampling(collision_sampling)) {
          return;
        }
        collision_count = 0U;
      }
      continue;
    }
    if (attempt.decoded.status == hitag_s::DecodeStatus::Ambiguous) {
      ambiguous = true;
    } else if (attempt.decoded.status == hitag_s::DecodeStatus::Present) {
      candidate = attempt.decoded.uid;
      selected_sampling = candidate_sampling;
      present_count = 1U;
      ambiguous = false;
      break;
    }
  }

  // Three matching responses within five total attempts: one weak frame no
  // longer delays detection until the next polling cycle.
  while (present_count != 0U && present_count < 3U && all_safe &&
         attempt_count < 5U) {
    delay(kHitagFieldResetMs);
    const TagAttempt attempt =
        captureHitagAttempt(baseline, selected_sampling);
    if (gRfidDebug) {
      Serial.printf("[RFID-RAW] kind=CONFIRM sample=0x%02X edges=%u pulses=%u first=%luus decode=%u start=%u bits=%u collision=%u quarter=%uus\r\n",
                    selected_sampling,
                    static_cast<unsigned>(attempt.edge_count),
                    static_cast<unsigned>(attempt.pulse_count),
                    static_cast<unsigned long>(attempt.first_edge_us),
                    static_cast<unsigned>(attempt.decoded.status),
                    static_cast<unsigned>(attempt.decoded.start_pulse),
                    static_cast<unsigned>(attempt.decoded.decoded_bits),
                    static_cast<unsigned>(attempt.decoded.collision_bit),
                    static_cast<unsigned>(attempt.decoded.quarter_us));
    }
    ++attempt_count;
    if (!attempt.safety_ok) {
      all_safe = false;
      break;
    }
    if (attempt.decoded.status == hitag_s::DecodeStatus::Collision) {
      if (record_collision(attempt, selected_sampling)) {
        if (inventory_at_sampling(collision_sampling)) {
          return;
        }
        collision_count = 0U;
      }
      continue;
    }
    if (attempt.decoded.status == hitag_s::DecodeStatus::Ambiguous) {
      ambiguous = true;
    } else if (attempt.decoded.status == hitag_s::DecodeStatus::Present) {
      consistent = consistent && hitag_s::equalUid(candidate, attempt.decoded.uid);
      ++present_count;
    }
  }

  samplePower();
  if (!all_safe || !gHtrcTransmitterSafe) {
    publishTagStatus(TagStatus::Fault, nullptr, selected_sampling,
                     present_count);
  } else if (present_count == 3U && consistent && !ambiguous) {
    publishTagStatus(TagStatus::Present, &candidate, selected_sampling,
                     present_count);
  } else if (present_count != 0U || ambiguous) {
    publishTagStatus(TagStatus::Unstable, nullptr, selected_sampling,
                     present_count);
  } else {
    publishTagStatus(TagStatus::NoTag, nullptr, selected_sampling, 0U);
  }
}

void encodeLedByte(std::uint8_t value, std::size_t &symbol_index) {
  for (int bit = 7; bit >= 0; --bit) {
    const bool one = (value & (1U << bit)) != 0U;
    rmt_data_t &symbol = gLedSymbols[symbol_index++];
    symbol.level0 = 1;
    symbol.duration0 = one ? 6 : 3;
    symbol.level1 = 0;
    symbol.duration1 = one ? 4 : 7;
  }
}

void showPixels() {
  if (!gLedReady) {
    return;
  }
  std::size_t symbol_index = 0;
  for (const Rgb &pixel : gPixels) {
    encodeLedByte(pixel.green, symbol_index);
    encodeLedByte(pixel.red, symbol_index);
    encodeLedByte(pixel.blue, symbol_index);
  }
  rmtWriteBlocking(gLedRmt, gLedSymbols, symbol_index);
  delayMicroseconds(80);
}

void initializeLeds() {
  gLedRmt = rmtInit(kWs2812DataPin, RMT_TX_MODE, RMT_MEM_256);
  gLedReady = gLedRmt != nullptr && rmtSetTick(gLedRmt, 125.0F) > 0.0F;
  if (gLedReady) {
    std::memset(gPixels, 0, sizeof(gPixels));
    showPixels();
  }
  Serial.printf("[WS2812] count=%u RMT=%s\r\n",
                static_cast<unsigned>(kWs2812Count), gLedReady ? "OK" : "FAIL");
}

void renderLedScene(std::uint32_t now_ms) {
  if (!gLedReady) {
    return;
  }
  if (!gNetworkSnapshot.assigned) {
    const TileConnectionView view = tileConnectionView(gNetworkSnapshot.link);
    const std::uint32_t phase = now_ms % 1600U;
    const std::uint8_t level = static_cast<std::uint8_t>(
        phase < 800U ? 12U + phase * 24U / 800U
                     : 36U - (phase - 800U) * 24U / 800U);
    std::fill(std::begin(gPixels), std::end(gPixels),
              scaleColor(view.accent, level));
    showPixels();
    return;
  }

  const std::uint32_t cue_elapsed = now_ms - gMovementCueStartedMs;
  if (gNetworkSnapshot.movement.mode == TileMovementCue::Destination) {
    // Two crisp green flashes per 900 ms make the destination unmistakable
    // without driving all ten LEDs at full channel brightness.
    constexpr Rgb kDestinationGreen{82, 220, 183};
    const std::uint32_t phase = cue_elapsed % 900U;
    const bool illuminated = phase < 180U ||
                             (phase >= 300U && phase < 480U);
    const std::uint8_t level = illuminated ? 72U : 2U;
    std::fill(std::begin(gPixels), std::end(gPixels),
              scaleColor(kDestinationGreen, level));
    showPixels();
    return;
  }
  if (gNetworkSnapshot.movement.mode == TileMovementCue::Departure) {
    // The origin remains visible but calmer than the destination: one slow
    // orange breath indicates "move from here".
    constexpr Rgb kDepartureOrange{239, 140, 74};
    const std::uint32_t phase = cue_elapsed % 1600U;
    const std::uint8_t level = static_cast<std::uint8_t>(
        phase < 800U ? 16U + phase * 48U / 800U
                     : 64U - (phase - 800U) * 48U / 800U);
    std::fill(std::begin(gPixels), std::end(gPixels),
              scaleColor(kDepartureOrange, level));
    showPixels();
    return;
  }

  const TileState &state = currentTileState();
  const Rgb ring_color =
      hasOwner(state) ? playerColor(state.owner_player) : state.accent;
  const std::uint32_t phase = now_ms % 1200U;
  const bool occupied = state.occupied_players != 0U;
  const std::uint8_t level = occupied
      ? static_cast<std::uint8_t>(
            phase < 600U ? 28U + phase * 28U / 600U
                         : 56U - (phase - 600U) * 28U / 600U)
      : 18U;
  std::fill(std::begin(gPixels), std::end(gPixels),
            scaleColor(ring_color, level));
  showPixels();
}
void centeredText(const char *text, std::int16_t y, std::uint8_t size,
                  std::uint16_t foreground, std::uint16_t background) {
  gDisplay.setTextSize(size);
  gDisplay.setTextColor(foreground, background);
  std::int16_t x1 = 0;
  std::int16_t y1 = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  gDisplay.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  const std::int16_t x = static_cast<std::int16_t>(
      std::max<std::int32_t>(0, (gDisplay.width() - width) / 2));
  gDisplay.setCursor(x, y);
  gDisplay.print(text);
}

void textAt(std::int16_t x, std::int16_t y, const char *text,
            std::uint8_t size, std::uint16_t foreground,
            std::uint16_t background = kPanel) {
  gDisplay.setTextSize(size);
  gDisplay.setTextColor(foreground, background);
  gDisplay.setCursor(x, y);
  gDisplay.print(text);
}

void footerTextAt(std::int16_t x, std::int16_t y, const char *text,
                  std::uint16_t foreground) {
  gFooterCanvas.setTextSize(1);
  gFooterCanvas.setTextColor(foreground, kBackground);
  gFooterCanvas.setCursor(x, y);
  gFooterCanvas.print(text);
}

void drawPanel(std::int16_t x, std::int16_t y, std::int16_t width,
               std::int16_t height, std::uint16_t border = kLine) {
  gDisplay.fillRoundRect(x, y, width, height, kCardRadius, kPanel);
  gDisplay.drawRoundRect(x, y, width, height, kCardRadius, border);
}
void formatMoney(char *buffer, std::size_t capacity, std::uint16_t amount) {
  std::snprintf(buffer, capacity, "$%u", static_cast<unsigned>(amount));
}

void drawArtwork(const TileState &state, std::uint16_t accent) {
  constexpr std::int16_t kImageX = 40;
  constexpr std::int16_t kImageY = 48;
  constexpr std::int16_t kImageSize = 160;
  constexpr std::int16_t kFrameX = 36;
  constexpr std::int16_t kFrameY = 44;
  constexpr std::int16_t kFrameSize = 168;
  gDisplay.fillRoundRect(kFrameX, kFrameY, kFrameSize, kFrameSize,
                         kArtworkRadius, kPanel);
  gDisplay.drawRoundRect(kFrameX, kFrameY, kFrameSize, kFrameSize,
                         kArtworkRadius, accent);
  gDisplay.drawRoundRect(kFrameX + 1, kFrameY + 1, kFrameSize - 2,
                         kFrameSize - 2, kArtworkRadius - 1, accent);

  const TileArtwork *artwork = tileArtwork(state.artwork);
  if (artwork != nullptr && artwork->pixels != nullptr &&
      artwork->width == kImageSize && artwork->height == kImageSize) {
    // Do not pass the full PROGMEM asset directly to ESP32 SPI. Once Wi-Fi is
    // active, a long 51.2 KB flash-backed transfer has proven unreliable on
    // the first board. Copy one aligned row into RAM and transmit it before
    // loading the next row. The LCD address window still remains continuous.
    gDisplay.startWrite();
    gDisplay.setAddrWindow(kImageX, kImageY, artwork->width, artwork->height);
    for (std::uint16_t row = 0; row < artwork->height; ++row) {
      const std::uint16_t *source =
          artwork->pixels + static_cast<std::uint32_t>(row) * artwork->width;
      std::memcpy(gArtworkRow, source,
                  static_cast<std::size_t>(artwork->width) *
                      sizeof(std::uint16_t));
      gDisplay.writePixels(gArtworkRow, artwork->width, true, false);
    }
    gDisplay.endWrite();
    return;
  }

  gDisplay.fillRect(kImageX, kImageY, kImageSize, kImageSize, kSelected);
  centeredText(state.tile_id, 109, 3, accent, kSelected);
  centeredText("ARTWORK PENDING", 142, 1, kMuted, kSelected);
}
void drawBuildingBadges(const TileState &state, std::int16_t x,
                        std::int16_t y, std::uint16_t accent) {
  if (state.kind != TileKind::Property || state.building_level == 0) {
    return;
  }
  for (std::uint8_t index = 0; index < 5; ++index) {
    const bool active = index < state.building_level;
    gDisplay.fillRect(static_cast<std::int16_t>(x + index * 11), y, 8,
                      index == 4 ? 9 : 7, active ? accent : kLine);
  }
}

void drawAssetStateCard(const TileState &state, std::uint16_t accent) {
  constexpr std::int16_t kCardX = 16;
  constexpr std::int16_t kCardY = 216;
  constexpr std::int16_t kCardWidth = 208;
  constexpr std::int16_t kCardHeight = 60;
  char amount[16];

  if (isOwnable(state.kind) && !hasOwner(state)) {
    drawPanel(kCardX, kCardY, kCardWidth, kCardHeight);
    textAt(28, 226, "PURCHASE PRICE", 1, kMuted);
    formatMoney(amount, sizeof(amount), state.purchase_price);
    textAt(28, 244, amount, 3, kYellow);
    return;
  }

  if (isOwnable(state.kind)) {
    const std::uint16_t owner_color =
        displayColor(playerColor(state.owner_player));
    drawPanel(kCardX, kCardY, kCardWidth, kCardHeight,
              state.mortgaged ? kRed : owner_color);
    gDisplay.fillCircle(29, 252, 4, state.mortgaged ? kRed : owner_color);
    textAt(28, 226, state.mortgaged ? "MORTGAGED" : "OWNED BY", 1,
           state.mortgaged ? kRed : kMuted);
    char fallback[20];
    const char *owner = state.owner_display_name;
    if (owner[0] == '\0') {
      std::snprintf(fallback, sizeof(fallback), "PLAYER %u", state.owner_player);
      owner = fallback;
    }
    textAt(40, 244, owner, 2, state.mortgaged ? kRed : owner_color);
    drawBuildingBadges(state, 157, 263, accent);
    return;
  }

  drawPanel(kCardX, kCardY, kCardWidth, kCardHeight);
  textAt(28, 226, tileKindLabel(state.kind), 1, kMuted);
  textAt(28, 244, state.status, 2,
         state.activity == TileActivity::PayFee ? kRed : accent);
  textAt(28, 263, state.detail, 1, kMuted);
}
void drawPowerFooter() {
  if (gFooterCanvas.getBuffer() == nullptr) {
    return;
  }
  gFooterCanvas.setTextWrap(false);
  gFooterCanvas.fillScreen(kBackground);
  gFooterCanvas.drawFastHLine(kOuterMargin, 0, 208, kLine);
  char tag[32];
  std::uint16_t tag_color = kYellow;
  if (gTagStatus == TagStatus::Present && gConfirmedTagCount != 0U) {
    const std::uint8_t display_index = static_cast<std::uint8_t>(
        (millis() / 1500U) % gConfirmedTagCount);
    if (gConfirmedTagCount == 1U) {
      std::snprintf(tag, sizeof(tag), "TAG  %s", gTagUids[0]);
    } else if (gConfirmedTagOverflow) {
      std::snprintf(tag, sizeof(tag), "TAGS %u+  %u/%u %s",
                    static_cast<unsigned>(gConfirmedTagCount),
                    static_cast<unsigned>(display_index + 1U),
                    static_cast<unsigned>(gConfirmedTagCount),
                    gTagUids[display_index]);
    } else {
      std::snprintf(tag, sizeof(tag), "TAGS %u  %u/%u %s",
                    static_cast<unsigned>(gConfirmedTagCount),
                    static_cast<unsigned>(display_index + 1U),
                    static_cast<unsigned>(gConfirmedTagCount),
                    gTagUids[display_index]);
    }
    tag_color = kGreen;
  } else if (gTagStatus == TagStatus::NoTag) {
    std::snprintf(tag, sizeof(tag), "TAG  NO TAG");
    tag_color = kMuted;
  } else if (gTagStatus == TagStatus::Unstable) {
    std::snprintf(tag, sizeof(tag), "TAG  UNSTABLE");
  } else if (gTagStatus == TagStatus::Fault) {
    std::snprintf(tag, sizeof(tag), "TAG  RFID ERROR");
    tag_color = kRed;
  } else {
    std::snprintf(tag, sizeof(tag), "TAG  SCANNING");
  }
  footerTextAt(kOuterMargin, 4, tag, tag_color);
  char left[32];
  if (gPower.valid) {
    std::snprintf(left, sizeof(left), "%.2FV  %.0FmA  %.2FW", gPower.bus_voltage_v,
                  gPower.current_ma, gPower.power_mw / 1000.0F);
  } else {
    std::snprintf(left, sizeof(left), "POWER N/A");
  }
  footerTextAt(kOuterMargin, 19, left, gPower.valid ? kMuted : kRed);
  const char *network = tileNetworkLabel(gNetworkSnapshot.link);
  std::uint16_t network_color = kYellow;
  if (gNetworkSnapshot.link == TileNetworkLink::OnlineAuto) {
    network_color = kGreen;
  } else if (gNetworkSnapshot.link == TileNetworkLink::OnlineManual) {
    network_color = kBlue;
  } else if (gNetworkSnapshot.link == TileNetworkLink::Fault) {
    network_color = kRed;
  }
  gDisplay.setTextSize(1);
  std::int16_t x1 = 0;
  std::int16_t y1 = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  gFooterCanvas.getTextBounds(network, 0, 19, &x1, &y1, &width, &height);
  footerTextAt(static_cast<std::int16_t>(224 - width), 19, network,
               network_color);
  gDisplay.drawRGBBitmap(0, 286, gFooterCanvas.getBuffer(), kDisplayWidth,
                         34);
}
void drawConnectionProgress() {
  if (!gDisplayReady || gNetworkSnapshot.assigned) {
    return;
  }
  const TileConnectionView view = tileConnectionView(gNetworkSnapshot.link);
  const std::uint16_t accent = displayColor(view.accent);
  gDisplay.fillRect(0, 106, kDisplayWidth, 14, kBackground);
  char progress[5] = "   ";
  const std::uint8_t dot_count =
      static_cast<std::uint8_t>((millis() / 500U) % 4U);
  for (std::uint8_t index = 0; index < dot_count; ++index) {
    progress[index] = '.';
  }
  centeredText(progress, 110, 1, accent, kBackground);
}
void drawConnectionPage() {
  if (!gDisplayReady) {
    return;
  }
  const TileConnectionView view = tileConnectionView(gNetworkSnapshot.link);
  const std::uint16_t accent = displayColor(view.accent);
  gDisplay.fillScreen(kBackground);
  gDisplay.fillRect(0, 0, kDisplayWidth, 6, accent);
  textAt(kOuterMargin, 12, "GRIDOPOLY TILE MODULE", 1, kMuted, kBackground);

  centeredText(view.eyebrow, 48, 1, accent, kBackground);
  centeredText(view.title, 68, 2, kText, kBackground);
  centeredText(view.detail, 96, 1, kMuted, kBackground);

  drawConnectionProgress();

  drawPanel(16, 128, 208, 118, accent);
  textAt(28, 140, "CONNECTION", 1, kMuted);
  textAt(28, 157, tileNetworkLabel(gNetworkSnapshot.link), 2, accent);
  textAt(28, 184, "MODULE ID", 1, kMuted);
  textAt(28, 201,
         gNetworkSnapshot.module_id[0] == '\0'
             ? "IDENTIFYING..."
             : gNetworkSnapshot.module_id,
         1, kText);

  char transport[36];
  if (gNetworkSnapshot.http_status != 0) {
    std::snprintf(transport, sizeof(transport), "HTTP %d  RSSI %d DBM",
                  gNetworkSnapshot.http_status,
                  static_cast<int>(gNetworkSnapshot.rssi));
  } else {
    std::snprintf(transport, sizeof(transport), "HTTP PENDING");
  }
  textAt(28, 225, transport, 1, kMuted);
  centeredText("ASSIGNMENT COMES FROM SERVER", 266, 1, kMuted, kBackground);
  drawPowerFooter();
}
void drawTilePage() {
  if (!gDisplayReady) {
    return;
  }
  const TileState &state = currentTileState();
  const std::uint16_t accent = displayColor(state.accent);
  gDisplay.fillScreen(kBackground);

  // District color remains an accent regardless of ownership.
  gDisplay.fillRect(0, 0, kDisplayWidth, 6, accent);
  char eyebrow[40];
  std::snprintf(eyebrow, sizeof(eyebrow), "%s  TILE %02u", state.subtitle,
                state.map_index);
  textAt(kOuterMargin, 10, eyebrow, 1, accent, kBackground);

  const char *kind = tileKindLabel(state.kind);
  gDisplay.setTextSize(1);
  std::int16_t x1 = 0;
  std::int16_t y1 = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  gDisplay.getTextBounds(kind, 0, 10, &x1, &y1, &width, &height);
  textAt(static_cast<std::int16_t>(224 - width), 10, kind, 1, kMuted,
         kBackground);

  centeredText(state.display_name, 25, 2, kText, kBackground);
  drawArtwork(state, accent);
  drawAssetStateCard(state, accent);
  drawPowerFooter();
}
void drawDiagnosticPage() {
  gDisplay.fillScreen(kBackground);
  gDisplay.fillRect(0, 0, kDisplayWidth, 39, kPanel);
  centeredText("TILE DIAGNOSTICS", 12, 2, kBlue, kPanel);
  drawPanel(12, 50, 216, 115, kBlue);
  textAt(22, 62, "DISPLAY", 1, kMuted);
  textAt(150, 62, gDisplayReady ? "OK" : "FAIL", 1,
         gDisplayReady ? kGreen : kRed);
  textAt(22, 84, "INA226", 1, kMuted);
  textAt(150, 84, gPower.configured ? "OK" : "FAIL", 1,
         gPower.configured ? kGreen : kRed);
  textAt(22, 106, "WS2812 RMT", 1, kMuted);
  textAt(150, 106, gLedReady ? "OK" : "FAIL", 1, gLedReady ? kGreen : kRed);
  textAt(22, 128, "RS485 DIR", 1, kMuted);
  textAt(150, 128, "SAFE LOW", 1, kGreen);
  textAt(22, 150, "ORDER OUT", 1, kMuted);
  textAt(150, 150, "SAFE LOW", 1, kGreen);

  drawPanel(12, 178, 216, 94, kLine);
  char value[32];
  std::snprintf(value, sizeof(value), "BUS     %.3F V", gPower.bus_voltage_v);
  textAt(22, 191, value, 2, kText);
  std::snprintf(value, sizeof(value), "CURRENT %.1F MA", gPower.current_ma);
  textAt(22, 216, value, 2, kText);
  std::snprintf(value, sizeof(value), "PEAK    %.1F MA", gPower.peak_current_ma);
  textAt(22, 241, value, 2, kYellow);
  centeredText("TYPE DIAG TO RETURN", 294, 1, kMuted, kBackground);
}

void renderPage() {
  if (gDiagnosticPage) {
    drawDiagnosticPage();
    Serial.println(F("[DISPLAY] page=DIAGNOSTIC"));
  } else if (gNetworkSnapshot.assigned) {
    drawTilePage();
    Serial.printf("[DISPLAY] page=TILE id=%s revision=%llu\r\n",
                  gNetworkSnapshot.tile.tile_id,
                  static_cast<unsigned long long>(
                      gNetworkSnapshot.assignment_revision));
  } else {
    drawConnectionPage();
    Serial.printf("[DISPLAY] page=CONNECTION state=%s\r\n",
                  tileNetworkLabel(gNetworkSnapshot.link));
  }
}

void recoverAndRenderPage(std::uint32_t now_ms) {
  recoverDisplayController();
  renderPage();
  if (gNetworkSnapshot.assigned && !gDiagnosticPage) {
    gTileFirstRenderMs = now_ms;
    gTileRenderConfirmationPending = true;
  }
}

void printStatus() {
  if (gNetworkSnapshot.assigned) {
    const TileState &state = currentTileState();
    Serial.printf("[TILE] source=%s id=%s map=%u artwork=%s kind=%s activity=%s owner=%u owner_name=%s buildings=%u\r\n",
                  gNetworkSnapshot.source, state.tile_id,
                  static_cast<unsigned>(state.map_index), artworkKey(state.artwork),
                  tileKindLabel(state.kind), tileActivityLabel(state.activity),
                  state.owner_player, state.owner_display_name,
                  state.building_level);
  } else {
    Serial.println(F("[TILE] assignment=NONE waiting_for_server=YES"));
  }
  Serial.printf("[NET] state=%s module=%s device=%s assigned=%s server_revision=%llu assignment_revision=%llu http=%d rssi=%d\r\n",
                tileNetworkLabel(gNetworkSnapshot.link),
                gNetworkSnapshot.module_id, gNetworkSnapshot.device_id,
                gNetworkSnapshot.assigned ? "YES" : "NO",
                static_cast<unsigned long long>(gNetworkSnapshot.server_revision),
                static_cast<unsigned long long>(gNetworkSnapshot.assignment_revision),
                gNetworkSnapshot.http_status,
                static_cast<int>(gNetworkSnapshot.rssi));
  if (gPower.valid) {
    Serial.printf("[POWER] %.3fV %.1fmA %.3fW peak=%.1fmA\r\n",
                  gPower.bus_voltage_v, gPower.current_ma,
                  gPower.power_mw / 1000.0F, gPower.peak_current_ma);
  }
  Serial.printf("[HTRC110] TXDIS=%s\r\n",
                gHtrcTransmitterSafe ? "1/OFF" : "UNKNOWN/FAIL");
  Serial.printf("[TAG-STATUS] state=%u count=%u%s uids=",
                static_cast<unsigned>(gTagStatus),
                static_cast<unsigned>(gConfirmedTagCount),
                gConfirmedTagOverflow ? "+" : "");
  if (gConfirmedTagCount == 0U) {
    Serial.print(F("--------"));
  } else {
    for (std::size_t index = 0; index < gConfirmedTagCount; ++index) {
      Serial.printf("%s%s", index == 0U ? "" : ",", gTagUids[index]);
    }
  }
  Serial.println();
  Serial.printf("[MOVE] cue=%s player=%u revision=%llu\r\n",
                tileMovementCueLabel(gNetworkSnapshot.movement.mode),
                static_cast<unsigned>(gNetworkSnapshot.movement.player_id),
                static_cast<unsigned long long>(
                    gNetworkSnapshot.movement.revision));
}
void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  DIAG              toggle hardware diagnostics page"));
  Serial.println(F("  REDRAW            reset ST7789 and redraw current page"));
  Serial.println(F("  RESTART           restart ESP32-S3 for boot timing test"));
  Serial.println(F("  BRIGHTNESS 0..255 set LCD backlight PWM"));
  Serial.println(F("  STATUS            print current state and power"));
  Serial.println(F("  RFID DEBUG ON|OFF toggle raw RFID capture diagnostics"));
  Serial.println(F("  HELP              print this list"));
}

void uppercase(char *text) {
  while (*text != '\0') {
    *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
    ++text;
  }
}

void processCommand(char *command) {
  while (*command == ' ') {
    ++command;
  }
  uppercase(command);
  if (std::strcmp(command, "DIAG") == 0) {
    gDiagnosticPage = !gDiagnosticPage;
    renderPage();
  } else if (std::strcmp(command, "REDRAW") == 0) {
    recoverAndRenderPage(millis());
  } else if (std::strcmp(command, "RESTART") == 0) {
    Serial.println(F("[BOOT] software restart requested"));
    Serial.flush();
    delay(50);
    ESP.restart();
  } else if (std::strncmp(command, "BRIGHTNESS ", 11) == 0) {
    const unsigned long value = std::strtoul(command + 11, nullptr, 10);
    if (value <= 255) {
      gBacklightDuty = static_cast<std::uint8_t>(value);
      analogWrite(kLcdBacklightPin, gBacklightDuty);
      Serial.printf("[DISPLAY] backlight=%u/255\r\n", gBacklightDuty);
    } else {
      Serial.println(F("ERR: brightness must be 0..255"));
    }
  } else if (std::strcmp(command, "STATUS") == 0) {
    printStatus();
  } else if (std::strcmp(command, "RFID DEBUG ON") == 0) {
    gRfidDebug = true;
    Serial.println(F("[RFID] raw diagnostics=ON"));
  } else if (std::strcmp(command, "RFID DEBUG OFF") == 0) {
    gRfidDebug = false;
    Serial.println(F("[RFID] raw diagnostics=OFF"));
  } else if (std::strcmp(command, "HELP") == 0 || std::strcmp(command, "?") == 0) {
    printHelp();
  } else if (*command != '\0') {
    Serial.println(F("ERR: unknown command; type HELP"));
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r' || character == '\n') {
      if (gSerialLength != 0) {
        gSerialLine[gSerialLength] = '\0';
        processCommand(gSerialLine);
        gSerialLength = 0;
      }
    } else if (gSerialLength + 1U < sizeof(gSerialLine)) {
      gSerialLine[gSerialLength++] = character;
    }
  }
}

}  // namespace

void setup() {
  setSafeOutputs();
  // The HTRC110 powers up with TXDIS=0. Wait for its 4 MHz oscillator, then
  // explicitly disable and verify the coil driver before slower peripherals.
  delay(15);
  gHtrcTransmitterSafe = forceHtrcTransmitterOff();
  gLastHtrcSafeRewriteMs = millis();
  Serial.begin(kSerialBaud);
  // Never use zero here: ESP32-S3 HWCDC underflows its retry counter when
  // the USB host is present but no monitor drains a full TX ring. Diagnostic
  // output must be dropped after a few milliseconds, never block networking.
  Serial.setTxTimeoutMs(5);
  Serial.printf("[BOOT] reset_reason=%d\r\n",
                static_cast<int>(esp_reset_reason()));
  Serial.printf("[HTRC110] TXDIS=1 verify=%s\r\n",
                gHtrcTransmitterSafe ? "PASS" : "FAIL");
  initializeDisplay();
  initializePowerMonitor();
  initializeLeds();
  samplePower();
  const std::uint32_t now = millis();
  gLastTagScanMs = now - kTagSearchPeriodMs;
  gNetwork.begin();
  (void)gNetwork.consume(gNetworkSnapshot);
  renderPage();
  renderLedScene(now);

  Serial.println();
  Serial.println(F("GRIDOPOLY TILE MODULE V0.28 - TAG REPORT RETRY"));
  Serial.println(F("RS485 and ORDER remain disabled; server assignment uses Wi-Fi/HTTP."));
  printHelp();
  printStatus();
}

void loop() {
  const std::uint32_t now = millis();
  pollSerial();
  publishPendingTagObservation();
  if (static_cast<std::uint32_t>(now - gLastHtrcSafeRewriteMs) >=
      kHtrcSafeRewritePeriodMs) {
    gLastHtrcSafeRewriteMs = now;
    const bool was_safe = gHtrcTransmitterSafe;
    gHtrcTransmitterSafe = forceHtrcTransmitterOff();
    if (!gHtrcTransmitterSafe && was_safe) {
      Serial.println(F("[HTRC110] ERROR: TXDIS readback failed"));
    }
  }
  const std::uint32_t tag_scan_period = gTagStatus == TagStatus::Present
                                            ? (gConfirmedTagCount > 1U
                                                   ? kTagMultiPollPeriodMs
                                                   : kTagPresencePollPeriodMs)
                                            : kTagSearchPeriodMs;
  if (static_cast<std::uint32_t>(now - gLastTagScanMs) >= tag_scan_period) {
    gLastTagScanMs = now;
    scanHitagTag();
    if (gTagUiDirty && gDisplayReady && !gDiagnosticPage) {
      drawPowerFooter();
      gTagUiDirty = false;
    }
  }


  TileNetworkSnapshot next_network;
  if (gNetwork.consume(next_network)) {
    const bool assignment_changed =
        next_network.assigned &&
        (!gNetworkSnapshot.assigned ||
         next_network.assignment_revision !=
             gNetworkSnapshot.assignment_revision ||
         std::strcmp(next_network.tile.tile_id,
                     gNetworkSnapshot.tile.tile_id) != 0);
    const bool movement_changed =
        next_network.movement.mode != gNetworkSnapshot.movement.mode ||
        next_network.movement.player_id !=
            gNetworkSnapshot.movement.player_id ||
        next_network.movement.revision !=
            gNetworkSnapshot.movement.revision;
    gNetworkSnapshot = next_network;
    if (movement_changed) {
      gMovementCueStartedMs = now;
    }
    gDiagnosticPage = false;
    renderPage();
    if (assignment_changed) {
      // Keep the already-working SPI/LCD session. Reinitializing the write-only
      // ST7789 here can leave the physical panel on the previous connection
      // frame even though the renderer has advanced to the tile state.
      gTileFirstRenderMs = now;
      gTileRenderConfirmationPending = true;
    }
    renderLedScene(now);
    printStatus();
  }

  if (static_cast<std::uint32_t>(now - gLastPowerSampleMs) >= kPowerSamplePeriodMs) {
    gLastPowerSampleMs = now;
    samplePower();
  }
  if (static_cast<std::uint32_t>(now - gLastUiRefreshMs) >= kUiRefreshPeriodMs) {
    gLastUiRefreshMs = now;
    if (gDiagnosticPage) {
      drawDiagnosticPage();
    } else if (!gNetworkSnapshot.assigned) {
      // The connection page is fully redrawn only when its semantic network
      // state changes. At 8 MHz a full-screen refresh every 500 ms produced a
      // clearly visible top-to-bottom wipe. Animate only the tiny progress
      // region and update the fixed footer here.
      drawConnectionProgress();
      drawPowerFooter();
    } else if (gTileRenderConfirmationPending &&
               static_cast<std::uint32_t>(now - gTileFirstRenderMs) >=
                   kTileRenderConfirmDelayMs) {
      drawTilePage();
      gTileRenderConfirmationPending = false;
      Serial.printf("[DISPLAY] page=TILE confirmed id=%s revision=%llu\r\n",
                    gNetworkSnapshot.tile.tile_id,
                    static_cast<unsigned long long>(
                        gNetworkSnapshot.assignment_revision));
    } else {
      drawPowerFooter();
    }
  }
  if (static_cast<std::uint32_t>(now - gLastLedFrameMs) >= kLedFramePeriodMs) {
    gLastLedFrameMs = now;
    renderLedScene(now);
  }
  delay(1);
}
