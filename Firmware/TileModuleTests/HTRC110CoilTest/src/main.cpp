#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp32-hal-rmt.h>

#include "hitag_s_decoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Gridopoly TileModule production PCB pins.
constexpr int kLcdResetPin = 4;
constexpr int kLcdDataCommandPin = 5;
constexpr int kLcdMosiPin = 6;
constexpr int kLcdClockPin = 7;
constexpr int kHtrcDoutPin = 8;
constexpr int kOrderInPin = 9;
constexpr int kOrderOutPin = 10;
constexpr int kCurrentSclPin = 11;
constexpr int kCurrentSdaPin = 12;
constexpr int kRs485DirectionPin = 14;
constexpr int kLcdChipSelectPin = 15;
constexpr int kLcdBacklightPin = 16;
constexpr int kHtrcSclkPin = 17;
constexpr int kHtrcDinPin = 18;
constexpr int kWs2812DataPin = 21;

constexpr std::uint8_t kIna226Address = 0x40;
constexpr std::uint8_t kInaRegConfig = 0x00;
constexpr std::uint8_t kInaRegBusVoltage = 0x02;
constexpr std::uint8_t kInaRegPower = 0x03;
constexpr std::uint8_t kInaRegCurrent = 0x04;
constexpr std::uint8_t kInaRegCalibration = 0x05;
constexpr std::uint8_t kInaRegManufacturerId = 0xFE;
constexpr std::uint8_t kInaRegDieId = 0xFF;
constexpr std::uint16_t kInaConfig = 0x0527;
constexpr std::uint16_t kInaCalibration = 0x1400;

constexpr std::uint8_t kGetSamplingTime = 0x02;
constexpr std::uint8_t kGetConfigPage = 0x04;
constexpr std::uint8_t kReadPhase = 0x08;
constexpr std::uint8_t kSetConfigPage = 0x40;
constexpr std::uint8_t kSetSamplingTime = 0x80;

// Config page 0: gain 500, 6 kHz low-pass, 160 Hz high-pass for HITAG S.
constexpr std::uint8_t kReceiverPage0 = 0x0B;
// Config page 1: active chip, no hysteresis. Bit 0 is TXDIS.
constexpr std::uint8_t kFieldOnPage1 = 0x00;
constexpr std::uint8_t kFieldOffPage1 = 0x01;
// Config page 3: normal low-pass/smart comparator and 4 MHz clock divider.
constexpr std::uint8_t kClockPage3 = 0x00;
constexpr std::uint8_t kAntFailMask = 0x10;

constexpr std::uint32_t kHtrcHalfCycleUs = 24;
constexpr std::uint32_t kHtrcGuardUs = 12;
constexpr std::uint32_t kHtrcFastHalfCycleUs = 2;
constexpr std::uint32_t kHtrcFastGuardUs = 1;
constexpr std::uint32_t kSafeRewritePeriodMs = 500;
constexpr std::uint32_t kStatusPeriodMs = 2000;
constexpr std::uint32_t kFieldTestMaximumMs = 350;
constexpr std::uint32_t kTagCaptureMs = 24;
constexpr std::size_t kTagMaximumEdges = 1024;
constexpr std::uint8_t kHitagWritePulseCycles = 7;
constexpr std::uint32_t kHitagFieldResetMs = 6;
constexpr std::uint32_t kHitagFirstRequestUs = 3000;
constexpr std::uint32_t kHitagZeroIntervalUs = 160;
constexpr std::uint32_t kHitagOneIntervalUs = 224;
constexpr std::uint32_t kHitagEofSilenceUs = 304;
constexpr std::uint8_t kHitagUidRequestBits[] = {1U, 1U, 0U, 0U, 1U};

// INA226 measures the whole board, not antenna peak current. These limits are
// deliberately conservative trip points for a short diagnostic pulse.
constexpr float kMinimumBusVoltageV = 4.65F;
constexpr float kMaximumTotalCurrentMa = 350.0F;
constexpr float kMaximumCurrentIncreaseMa = 120.0F;

struct PowerReading {
  bool valid = false;
  float bus_voltage_v = 0.0F;
  float current_ma = 0.0F;
  float power_mw = 0.0F;
};

struct CoilResult {
  bool ran = false;
  bool passed = false;
  bool digital_ok = false;
  bool antenna_ok = false;
  bool sampling_ok = false;
  bool power_ok = false;
  bool aborted = false;
  std::uint8_t page0 = 0xFF;
  std::uint8_t page1 = 0xFF;
  std::uint8_t page2 = 0xFF;
  std::uint8_t page3 = 0xFF;
  std::uint8_t phase_min = 0xFF;
  std::uint8_t phase_max = 0;
  std::uint8_t phase_average = 0;
  std::uint8_t sampling = 0xFF;
  float baseline_current_ma = 0.0F;
  float field_current_ma = 0.0F;
  float current_increase_ma = 0.0F;
  float minimum_bus_voltage_v = 100.0F;
};

struct TagEdge {
  std::uint32_t elapsed_us = 0;
  std::uint8_t level = 0;
};

struct HitagAttempt {
  gridopoly::tile::hitag_s::DecodeResult decoded{};
  bool safety_ok = false;
  bool antenna_ok = false;
  bool field_off_ok = false;
  bool overflow = false;
  std::uint16_t edge_count = 0;
  std::size_t pulse_count = 0;
  std::size_t short_runs = 0;
  std::size_t long_runs = 0;
  std::size_t other_runs = 0;
  std::uint32_t first_edge_us = 0;
  std::uint32_t last_edge_us = 0;
  std::uint8_t sampling = 0;
  std::uint32_t field_duration_ms = 0;
  PowerReading field_power{};
};

bool gInaReady = false;
bool gDigitalLinkOk = false;
bool gFieldOn = false;
std::uint32_t gLastSafeRewriteMs = 0;
std::uint32_t gLastStatusMs = 0;
char gSerialLine[64]{};
std::size_t gSerialLength = 0;
CoilResult gLastResult;
DRAM_ATTR volatile TagEdge gTagEdges[kTagMaximumEdges]{};
DRAM_ATTR volatile std::uint16_t gTagEdgeCount = 0;
DRAM_ATTR volatile std::uint32_t gTagCaptureStartUs = 0;
DRAM_ATTR volatile bool gTagCaptureActive = false;
DRAM_ATTR volatile bool gTagCaptureOverflow = false;
std::uint16_t gLastPulseDurations[128]{};
std::size_t gLastPulseDurationCount = 0;

void setUnrelatedOutputsSafe() {
  digitalWrite(kLcdBacklightPin, LOW);
  pinMode(kLcdBacklightPin, OUTPUT);
  digitalWrite(kLcdChipSelectPin, HIGH);
  pinMode(kLcdChipSelectPin, OUTPUT);
  digitalWrite(kLcdDataCommandPin, LOW);
  pinMode(kLcdDataCommandPin, OUTPUT);
  digitalWrite(kLcdResetPin, LOW);
  pinMode(kLcdResetPin, OUTPUT);
  digitalWrite(kLcdMosiPin, LOW);
  pinMode(kLcdMosiPin, OUTPUT);
  digitalWrite(kLcdClockPin, LOW);
  pinMode(kLcdClockPin, OUTPUT);
  digitalWrite(kRs485DirectionPin, LOW);
  pinMode(kRs485DirectionPin, OUTPUT);
  digitalWrite(kOrderOutPin, LOW);
  pinMode(kOrderOutPin, OUTPUT);
  pinMode(kOrderInPin, INPUT);
  digitalWrite(kWs2812DataPin, LOW);
  pinMode(kWs2812DataPin, OUTPUT);
}

bool clearWs2812Latch() {
  rmt_data_t symbols[10U * 24U]{};
  for (rmt_data_t &symbol : symbols) {
    symbol.level0 = 1;
    symbol.duration0 = 3;  // 375 ns at 8 MHz: WS2812 zero high time.
    symbol.level1 = 0;
    symbol.duration1 = 7;
  }
  rmt_obj_t *rmt =
      rmtInit(kWs2812DataPin, RMT_TX_MODE, RMT_MEM_256);
  if (rmt == nullptr || rmtSetTick(rmt, 125.0F) <= 0.0F) {
    return false;
  }
  rmtWriteBlocking(rmt, symbols, 10U * 24U);
  delayMicroseconds(80);
  digitalWrite(kWs2812DataPin, LOW);
  return true;
}

void initializeHtrcPins() {
  digitalWrite(kHtrcSclkPin, LOW);
  pinMode(kHtrcSclkPin, OUTPUT);
  digitalWrite(kHtrcDinPin, LOW);
  pinMode(kHtrcDinPin, OUTPUT);
  pinMode(kHtrcDoutPin, INPUT);
}

void htrcDelay() {
  delayMicroseconds(kHtrcHalfCycleUs);
}

void htrcSerialReset() {
  // DIN low-to-high while SCLK is high resets the HTRC110 serial interface.
  digitalWrite(kHtrcSclkPin, LOW);
  digitalWrite(kHtrcDinPin, LOW);
  delayMicroseconds(kHtrcGuardUs);
  digitalWrite(kHtrcSclkPin, HIGH);
  htrcDelay();
  digitalWrite(kHtrcDinPin, HIGH);
  htrcDelay();
  digitalWrite(kHtrcSclkPin, LOW);
  htrcDelay();
}

void htrcWriteBits(std::uint8_t value, std::uint8_t bit_count) {
  for (int bit = bit_count - 1; bit >= 0; --bit) {
    digitalWrite(kHtrcDinPin, (value & (1U << bit)) != 0U ? HIGH : LOW);
    delayMicroseconds(kHtrcGuardUs);
    digitalWrite(kHtrcSclkPin, HIGH);
    htrcDelay();
    digitalWrite(kHtrcSclkPin, LOW);
    htrcDelay();
  }
}

std::uint8_t htrcReadBits(std::uint8_t bit_count) {
  std::uint8_t value = 0;
  digitalWrite(kHtrcDinPin, LOW);
  delayMicroseconds(kHtrcGuardUs);
  for (std::uint8_t bit = 0; bit < bit_count; ++bit) {
    digitalWrite(kHtrcSclkPin, HIGH);
    htrcDelay();
    value = static_cast<std::uint8_t>(
        (value << 1U) | (digitalRead(kHtrcDoutPin) == HIGH ? 1U : 0U));
    digitalWrite(kHtrcSclkPin, LOW);
    htrcDelay();
  }
  return value;
}

void htrcWriteCommand(std::uint8_t command) {
  htrcSerialReset();
  htrcWriteBits(command, 8);
  digitalWrite(kHtrcDinPin, LOW);
}

std::uint8_t htrcReadCommand(std::uint8_t command) {
  htrcSerialReset();
  htrcWriteBits(command, 8);
  return htrcReadBits(8);
}

void htrcSetConfig(std::uint8_t page, std::uint8_t data) {
  htrcWriteCommand(static_cast<std::uint8_t>(
      kSetConfigPage | ((page & 0x03U) << 4U) | (data & 0x0FU)));
}

std::uint8_t htrcGetConfig(std::uint8_t page) {
  return htrcReadCommand(
      static_cast<std::uint8_t>(kGetConfigPage | (page & 0x03U)));
}

void htrcSetSampling(std::uint8_t value) {
  htrcWriteCommand(
      static_cast<std::uint8_t>(kSetSamplingTime | (value & 0x3FU)));
}

std::uint8_t htrcGetSampling() {
  return htrcReadCommand(kGetSamplingTime);
}

std::uint8_t htrcReadPhase() {
  return static_cast<std::uint8_t>(htrcReadCommand(kReadPhase) & 0x3FU);
}

void htrcFastSerialReset() {
  digitalWrite(kHtrcSclkPin, LOW);
  digitalWrite(kHtrcDinPin, LOW);
  delayMicroseconds(kHtrcFastGuardUs);
  digitalWrite(kHtrcSclkPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kHtrcDinPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kHtrcSclkPin, LOW);
  delayMicroseconds(kHtrcFastHalfCycleUs);
}

void htrcFastWriteBits(std::uint8_t value, std::uint8_t bit_count) {
  for (int bit = bit_count - 1; bit >= 0; --bit) {
    digitalWrite(kHtrcDinPin, (value & (1U << bit)) != 0U ? HIGH : LOW);
    delayMicroseconds(kHtrcFastGuardUs);
    digitalWrite(kHtrcSclkPin, HIGH);
    delayMicroseconds(kHtrcFastHalfCycleUs);
    digitalWrite(kHtrcSclkPin, LOW);
    delayMicroseconds(kHtrcFastHalfCycleUs);
  }
}

void htrcFastSetConfig(std::uint8_t page, std::uint8_t data) {
  htrcFastSerialReset();
  htrcFastWriteBits(static_cast<std::uint8_t>(
                        kSetConfigPage | ((page & 0x03U) << 4U) |
                        (data & 0x0FU)),
                    8U);
  digitalWrite(kHtrcDinPin, LOW);
}

void htrcFastExitMode() {
  digitalWrite(kHtrcDinPin, LOW);
  digitalWrite(kHtrcSclkPin, HIGH);
  delayMicroseconds(kHtrcFastHalfCycleUs);
  digitalWrite(kHtrcSclkPin, LOW);
  delayMicroseconds(kHtrcFastHalfCycleUs);
}

void htrcEnterReadTag() {
  // READ_TAG is the three-bit command 111. The third rising SCLK edge enters
  // transparent mode immediately; SCLK must then stay LOW for the capture.
  htrcFastSerialReset();
  htrcFastWriteBits(0x07U, 3U);
  digitalWrite(kHtrcDinPin, LOW);
}

void htrcExitReadTag() {
  // One LOW-to-HIGH transition terminates transparent mode.
  htrcFastExitMode();
}

void waitUntilMicros(std::uint32_t target_us) {
  while (static_cast<std::int32_t>(micros() - target_us) < 0) {
  }
}

void htrcTriggerWritePulse() {
  digitalWrite(kHtrcDinPin, HIGH);
  delayMicroseconds(kHtrcFastGuardUs);
  digitalWrite(kHtrcDinPin, LOW);
}

bool htrcProgramWritePulseWidth(std::uint8_t carrier_cycles) {
  noInterrupts();
  htrcFastSerialReset();
  htrcFastWriteBits(static_cast<std::uint8_t>(0x10U | carrier_cycles), 8U);
  // WRITE_TAG_N enters transmit mode. DIN is brought low without a rising
  // edge, then SCLK exits the mode while TXDIS is still asserted.
  digitalWrite(kHtrcDinPin, LOW);
  delayMicroseconds(static_cast<std::uint32_t>(carrier_cycles) * 8U + 8U);
  htrcFastExitMode();
  interrupts();
  const std::uint8_t page1 = htrcGetConfig(1);
  return (page1 >> 4U) == carrier_cycles &&
         (page1 & 0x0FU) == kFieldOffPage1;
}

void htrcSendHitagUidRequest(std::uint32_t field_started_us) {
  // NXP fast settling sequence: freeze before the first write pulse.
  htrcSetConfig(2, 0x09U);
  const std::uint32_t first_boundary =
      field_started_us + kHitagFirstRequestUs;
  waitUntilMicros(first_boundary - 120U);

  noInterrupts();
  htrcFastSerialReset();
  htrcFastWriteBits(0x06U, 3U);  // WRITE_TAG, reusing programmed N=7.
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

  // Finish the NXP fast settling sequence well before the tag's response.
  delayMicroseconds(150);
  htrcFastSetConfig(2, 0x0BU);
  delayMicroseconds(200);
  htrcFastSetConfig(2, 0x00U);
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
      gpio_get_level(static_cast<gpio_num_t>(kHtrcDoutPin)) != 0 ? 1U : 0U);
  gTagEdgeCount = static_cast<std::uint16_t>(index + 1U);
}

bool forceFieldOff() {
  // A failed readback means the physical field state is unknown, not OFF.
  // Retry immediately and leave gFieldOn asserted unless OFF is proven.
  for (std::uint8_t attempt = 0; attempt < 3U; ++attempt) {
    htrcSetConfig(1, kFieldOffPage1);
    const std::uint8_t page1 = htrcGetConfig(1);
    if ((page1 & 0x0FU) == kFieldOffPage1) {
      gFieldOn = false;
      return true;
    }
    delay(1);
  }
  gFieldOn = true;
  return false;
}

bool verifyDigitalLink() {
  if (!forceFieldOff()) {
    return false;
  }
  const std::uint8_t original_sampling =
      static_cast<std::uint8_t>(htrcGetSampling() & 0x3FU);
  constexpr std::uint8_t kPattern = 0x2A;
  htrcSetSampling(kPattern);
  const std::uint8_t pattern = htrcGetSampling();
  htrcSetSampling(original_sampling);
  const std::uint8_t restored = htrcGetSampling();
  const std::uint8_t page3 = htrcGetConfig(3);
  return (pattern & 0xC0U) == 0U && (pattern & 0x3FU) == kPattern &&
         (restored & 0xC0U) == 0U &&
         (restored & 0x3FU) == original_sampling &&
         (page3 & 0xC0U) == 0U;
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

bool initializeIna226() {
  Wire.begin(kCurrentSdaPin, kCurrentSclPin, 400000U);
  delay(5);
  Wire.beginTransmission(kIna226Address);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  std::uint16_t manufacturer = 0;
  std::uint16_t die = 0;
  std::uint16_t calibration = 0;
  const bool ok = inaRead(kInaRegManufacturerId, manufacturer) &&
                  inaRead(kInaRegDieId, die) && manufacturer == 0x5449U &&
                  (die & 0xFFF0U) == 0x2260U &&
                  inaWrite(kInaRegConfig, kInaConfig) &&
                  inaWrite(kInaRegCalibration, kInaCalibration) &&
                  inaRead(kInaRegCalibration, calibration) &&
                  calibration == kInaCalibration;
  Serial.printf("[INA226] manufacturer=0x%04X die=0x%04X calibration=%s\r\n",
                manufacturer, die, ok ? "PASS" : "FAIL");
  delay(80);
  return ok;
}

PowerReading readPower() {
  PowerReading reading;
  if (!gInaReady) {
    return reading;
  }
  std::uint16_t bus = 0;
  std::uint16_t current = 0;
  std::uint16_t power = 0;
  if (!inaRead(kInaRegBusVoltage, bus) ||
      !inaRead(kInaRegCurrent, current) ||
      !inaRead(kInaRegPower, power)) {
    return reading;
  }
  reading.valid = true;
  reading.bus_voltage_v = static_cast<float>(bus) * 0.00125F;
  reading.current_ma =
      static_cast<float>(static_cast<std::int16_t>(current)) * 0.1F;
  reading.power_mw = static_cast<float>(power) * 2.5F;
  return reading;
}

PowerReading averagePower(std::uint8_t samples, std::uint32_t spacing_ms) {
  PowerReading average;
  std::uint8_t valid_count = 0;
  for (std::uint8_t index = 0; index < samples; ++index) {
    delay(spacing_ms);
    const PowerReading reading = readPower();
    if (!reading.valid) {
      continue;
    }
    average.bus_voltage_v += reading.bus_voltage_v;
    average.current_ma += reading.current_ma;
    average.power_mw += reading.power_mw;
    ++valid_count;
  }
  if (valid_count != 0U) {
    average.valid = true;
    average.bus_voltage_v /= valid_count;
    average.current_ma /= valid_count;
    average.power_mw /= valid_count;
  }
  return average;
}

bool powerReadingSafe(const PowerReading &reading, float baseline_current_ma) {
  if (!reading.valid || !std::isfinite(reading.bus_voltage_v) ||
      !std::isfinite(reading.current_ma)) {
    return false;
  }
  return reading.bus_voltage_v >= kMinimumBusVoltageV &&
         reading.current_ma <= kMaximumTotalCurrentMa &&
         reading.current_ma - baseline_current_ma <= kMaximumCurrentIncreaseMa;
}

void printResult(const CoilResult &result) {
  if (!result.ran) {
    Serial.println(F("[COIL] no controlled field test has run"));
    return;
  }
  Serial.printf(
      "[COIL] verdict=%s digital=%s antenna=%s sampling=%s power=%s aborted=%s\r\n",
      result.passed ? "PASS" : "FAIL", result.digital_ok ? "PASS" : "FAIL",
      result.antenna_ok ? "PASS" : "FAIL",
      result.sampling_ok ? "PASS" : "FAIL",
      result.power_ok ? "PASS" : "FAIL", result.aborted ? "YES" : "NO");
  Serial.printf(
      "[COIL] P0=0x%02X P1=0x%02X P2=0x%02X P3=0x%02X phase=%u..%u avg=%u sampling=0x%02X\r\n",
      result.page0, result.page1, result.page2, result.page3,
      result.phase_min, result.phase_max, result.phase_average, result.sampling);
  Serial.printf(
      "[COIL] baseline=%.1fmA field=%.1fmA delta=%.1fmA min_bus=%.3fV field_now=%s\r\n",
      result.baseline_current_ma, result.field_current_ma,
      result.current_increase_ma, result.minimum_bus_voltage_v,
      gFieldOn ? "ON" : "OFF");
}

void runCoilTest() {
  CoilResult result;
  result.ran = true;
  Serial.println(F("[COIL] controlled test starting; field is time-limited"));

  result.digital_ok = verifyDigitalLink();
  const PowerReading baseline = averagePower(5, 40);
  if (!result.digital_ok || !baseline.valid ||
      baseline.bus_voltage_v < kMinimumBusVoltageV ||
      baseline.current_ma > kMaximumTotalCurrentMa) {
    const bool off_ok = forceFieldOff();
    result.digital_ok = result.digital_ok && off_ok;
    result.aborted = true;
    result.minimum_bus_voltage_v = baseline.bus_voltage_v;
    result.baseline_current_ma = baseline.current_ma;
    gLastResult = result;
    Serial.println(F("[COIL] ABORT before field: digital link or baseline power invalid"));
    printResult(gLastResult);
    return;
  }
  result.baseline_current_ma = baseline.current_ma;
  result.minimum_bus_voltage_v = baseline.bus_voltage_v;

  htrcSetConfig(3, kClockPage3);
  htrcSetConfig(0, kReceiverPage0);
  htrcSetConfig(2, 0x00);
  htrcSetConfig(1, kFieldOnPage1);
  gFieldOn = true;
  const std::uint32_t field_started_ms = millis();

  result.page1 = htrcGetConfig(1);
  if ((result.page1 & 0x0FU) != kFieldOnPage1) {
    const bool off_ok = forceFieldOff();
    result.digital_ok = result.digital_ok && off_ok;
    result.aborted = true;
    gLastResult = result;
    Serial.println(F("[COIL] ABORT: TX enable did not read back"));
    printResult(gLastResult);
    return;
  }

  delay(5);
  result.page0 = htrcGetConfig(0);
  result.page2 = htrcGetConfig(2);
  result.page3 = htrcGetConfig(3);
  result.digital_ok = result.digital_ok &&
                      (result.page0 & 0x0FU) == kReceiverPage0 &&
                      (result.page1 & 0x0FU) == kFieldOnPage1;
  result.antenna_ok = ((result.page2 | result.page3) & kAntFailMask) == 0U &&
                      (result.page2 & 0x0FU) == 0x00U &&
                      (result.page3 & 0x0FU) == kClockPage3;
  if (!result.digital_ok || !result.antenna_ok) {
    const bool off_ok = forceFieldOff();
    result.digital_ok = result.digital_ok && off_ok;
    result.aborted = true;
    gLastResult = result;
    Serial.println(F("[COIL] ABORT: config readback or ANTFAIL"));
    printResult(gLastResult);
    return;
  }

  std::uint16_t phase_sum = 0;
  constexpr std::uint8_t kPhaseSamples = 16;
  for (std::uint8_t index = 0; index < kPhaseSamples; ++index) {
    const std::uint8_t phase = htrcReadPhase();
    result.phase_min = std::min(result.phase_min, phase);
    result.phase_max = std::max(result.phase_max, phase);
    phase_sum = static_cast<std::uint16_t>(phase_sum + phase);
  }
  result.phase_average = static_cast<std::uint8_t>(
      (phase_sum + kPhaseSamples / 2U) / kPhaseSamples);
  result.sampling = static_cast<std::uint8_t>(
      ((static_cast<std::uint16_t>(result.phase_average) * 2U) + 0x3FU) &
      0x3FU);
  htrcSetSampling(result.sampling);
  result.sampling_ok = (htrcGetSampling() & 0x3FU) == result.sampling;

  // Receiver settling sequence from NXP AN98080. No READ_TAG/WRITE_TAG is
  // issued by this coil-only diagnostic.
  htrcSetConfig(2, 0x0B);
  delay(4);
  htrcSetConfig(2, 0x08);
  delay(1);
  htrcSetConfig(2, 0x00);

  float field_current_sum = 0.0F;
  std::uint8_t field_samples = 0;
  result.power_ok = true;
  while (millis() - field_started_ms < kFieldTestMaximumMs) {
    delay(35);
    const std::uint8_t live_page2 = htrcGetConfig(2);
    if ((live_page2 & kAntFailMask) != 0U ||
        (live_page2 & 0x0FU) != 0x00U) {
      result.antenna_ok = false;
      result.aborted = true;
      break;
    }
    const PowerReading reading = readPower();
    if (!powerReadingSafe(reading, result.baseline_current_ma)) {
      result.power_ok = false;
      result.aborted = true;
      if (reading.valid) {
        result.minimum_bus_voltage_v =
            std::min(result.minimum_bus_voltage_v, reading.bus_voltage_v);
        result.field_current_ma =
            std::max(result.field_current_ma, reading.current_ma);
      }
      break;
    }
    result.minimum_bus_voltage_v =
        std::min(result.minimum_bus_voltage_v, reading.bus_voltage_v);
    field_current_sum += reading.current_ma;
    result.field_current_ma =
        std::max(result.field_current_ma, reading.current_ma);
    ++field_samples;
  }

  const bool field_off_ok = forceFieldOff();
  if (field_samples != 0U && result.field_current_ma == 0.0F) {
    result.field_current_ma = field_current_sum / field_samples;
  }
  result.current_increase_ma =
      result.field_current_ma - result.baseline_current_ma;
  result.digital_ok = result.digital_ok && field_off_ok;
  result.power_ok = result.power_ok && field_samples != 0U;
  result.passed = result.digital_ok && result.antenna_ok &&
                  result.sampling_ok && result.power_ok && !result.aborted;
  gLastResult = result;
  printResult(gLastResult);
  if (field_off_ok) {
    Serial.println(F("[COIL] field is OFF; this does not certify 125 kHz tuning"));
  } else {
    Serial.println(F("[COIL] FIELD OFF NOT VERIFIED; DISCONNECT POWER"));
  }
}

bool prepareHitagReader(const PowerReading &baseline, std::uint8_t &phase,
                        std::uint8_t &sampling) {
  htrcSetConfig(3, kClockPage3);
  htrcSetConfig(0, kReceiverPage0);
  htrcSetConfig(2, 0x00U);
  htrcSetConfig(1, kFieldOnPage1);
  gFieldOn = true;
  delayMicroseconds(200);

  const std::uint8_t page0 = htrcGetConfig(0);
  const std::uint8_t page1 = htrcGetConfig(1);
  const std::uint8_t page2 = htrcGetConfig(2);
  const std::uint8_t page3 = htrcGetConfig(3);
  const bool config_ok = (page0 & 0x0FU) == kReceiverPage0 &&
                         (page1 & 0x0FU) == kFieldOnPage1 &&
                         (page2 & 0x0FU) == 0x00U &&
                         (page3 & 0x0FU) == kClockPage3;
  const bool antenna_ok = ((page2 | page3) & kAntFailMask) == 0U;
  if (!config_ok || !antenna_ok) {
    (void)forceFieldOff();
    Serial.printf("[HITAG] ABORT config=%s ANTFAIL=%s\r\n",
                  config_ok ? "PASS" : "FAIL", antenna_ok ? "0" : "1");
    return false;
  }

  std::uint16_t phase_sum = 0;
  constexpr std::uint8_t kPhaseSamples = 8;
  for (std::uint8_t sample = 0; sample < kPhaseSamples; ++sample) {
    phase_sum = static_cast<std::uint16_t>(phase_sum + htrcReadPhase());
  }
  phase = static_cast<std::uint8_t>(
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
  const PowerReading field_power = readPower();
  const bool power_ok = powerReadingSafe(field_power, baseline.current_ma);
  const bool settled_ok = sampling_ok &&
                          (settled_page2 & kAntFailMask) == 0U &&
                          (settled_page2 & 0x0FU) == 0x00U;
  const bool off_ok = forceFieldOff();
  if (!settled_ok || !power_ok || !off_ok) {
    Serial.printf(
        "[HITAG] ABORT settle=%s power=%s field_off=%s VBUS=%.3fV current=%.1fmA\r\n",
        settled_ok ? "PASS" : "FAIL", power_ok ? "PASS" : "FAIL",
        off_ok ? "PASS" : "FAIL", field_power.bus_voltage_v,
        field_power.current_ma);
    return false;
  }

  const bool pulse_width_ok =
      htrcProgramWritePulseWidth(kHitagWritePulseCycles);
  delay(kHitagFieldResetMs);
  Serial.printf(
      "[HITAG] ready phase=%u sampling=0x%02X pulse=%uus receiver=P0x%X\r\n",
      phase, sampling, kHitagWritePulseCycles * 8U, kReceiverPage0);
  if (!pulse_width_ok) {
    Serial.println(F("[HITAG] ABORT WRITE_TAG pulse-width readback failed"));
  }
  return pulse_width_ok;
}

HitagAttempt captureHitagAttempt(const PowerReading &baseline,
                                 std::uint8_t sampling) {
  using gridopoly::tile::hitag_s::Pulse;
  using gridopoly::tile::hitag_s::decodeAdvancedUid;

  HitagAttempt attempt;
  attempt.sampling = sampling;
  htrcSetSampling(sampling);
  if ((htrcGetSampling() & 0x3FU) != sampling) {
    attempt.field_off_ok = forceFieldOff();
    return attempt;
  }

  htrcSetConfig(1, kFieldOnPage1);
  gFieldOn = true;
  const std::uint32_t field_started_us = micros();
  const std::uint32_t field_started_ms = millis();
  const std::uint8_t page1 = htrcGetConfig(1);
  if ((page1 & 0x0FU) != kFieldOnPage1) {
    attempt.field_off_ok = forceFieldOff();
    return attempt;
  }

  htrcSendHitagUidRequest(field_started_us);
  gTagCaptureActive = false;
  gTagEdgeCount = 0;
  gTagCaptureOverflow = false;
  attachInterrupt(digitalPinToInterrupt(kHtrcDoutPin), captureTagEdge, CHANGE);
  gTagCaptureStartUs = micros();
  gTagCaptureActive = true;
  htrcEnterReadTag();
  delay(kTagCaptureMs);
  const std::uint32_t capture_ended_us = micros() - gTagCaptureStartUs;
  gTagCaptureActive = false;
  detachInterrupt(digitalPinToInterrupt(kHtrcDoutPin));
  htrcExitReadTag();

  attempt.edge_count = gTagEdgeCount;
  attempt.overflow = gTagCaptureOverflow;
  attempt.field_power = readPower();
  const std::uint8_t final_page2 = htrcGetConfig(2);
  attempt.antenna_ok = (final_page2 & kAntFailMask) == 0U &&
                       (final_page2 & 0x0FU) == 0x00U;
  attempt.field_off_ok = forceFieldOff();
  attempt.field_duration_ms = millis() - field_started_ms;

  if (attempt.edge_count != 0U) {
    attempt.first_edge_us = gTagEdges[0].elapsed_us;
    attempt.last_edge_us =
        gTagEdges[attempt.edge_count - 1U].elapsed_us;
  }

  static Pulse pulses[kTagMaximumEdges]{};
  for (std::uint16_t index = 0;
       index + 1U < attempt.edge_count &&
       attempt.pulse_count < kTagMaximumEdges;
       ++index) {
    const std::uint32_t duration =
        gTagEdges[index + 1U].elapsed_us - gTagEdges[index].elapsed_us;
    if (duration == 0U || duration > 0xFFFFU) {
      continue;
    }
    pulses[attempt.pulse_count++] = {
        static_cast<std::uint16_t>(duration), gTagEdges[index].level != 0U};
  }
  // If the final AC2K run and idle share a level there is no closing edge.
  // Preserve that stable tail; the decoder accepts an overlong final run.
  if (attempt.edge_count != 0U && attempt.pulse_count < kTagMaximumEdges) {
    const std::uint32_t started =
        gTagEdges[attempt.edge_count - 1U].elapsed_us;
    const std::uint32_t duration = capture_ended_us - started;
    if (duration != 0U && duration <= 0xFFFFU) {
      pulses[attempt.pulse_count++] = {
          static_cast<std::uint16_t>(duration),
          gTagEdges[attempt.edge_count - 1U].level != 0U};
    }
  }

  for (std::size_t index = 0; index < attempt.pulse_count; ++index) {
    const std::uint16_t duration = pulses[index].duration_us;
    if (duration >= 80U && duration <= 180U) {
      ++attempt.short_runs;
    } else if (duration >= 180U && duration <= 360U) {
      ++attempt.long_runs;
    } else {
      ++attempt.other_runs;
    }
  }

  gLastPulseDurationCount =
      std::min<std::size_t>(attempt.pulse_count,
                            sizeof(gLastPulseDurations) /
                                sizeof(gLastPulseDurations[0]));
  for (std::size_t index = 0; index < gLastPulseDurationCount; ++index) {
    gLastPulseDurations[index] = pulses[index].duration_us;
  }
  attempt.safety_ok = !attempt.overflow && attempt.antenna_ok &&
                      attempt.field_off_ok &&
                      attempt.field_duration_ms <= kFieldTestMaximumMs &&
                      powerReadingSafe(attempt.field_power,
                                       baseline.current_ma);
  if (attempt.safety_ok) {
    attempt.decoded = decodeAdvancedUid(pulses, attempt.pulse_count);
  }
  return attempt;
}

void printHitagAttempt(std::uint8_t number, const HitagAttempt &attempt) {
  using gridopoly::tile::hitag_s::DecodeStatus;
  char uid[9] = "--------";
  if (attempt.decoded.status == DecodeStatus::Present) {
    gridopoly::tile::hitag_s::formatUid(attempt.decoded.uid, uid);
  }
  const char *state = attempt.decoded.status == DecodeStatus::Present
                          ? "PRESENT"
                          : attempt.decoded.status == DecodeStatus::Ambiguous
                                ? "AMBIGUOUS"
                                : "NO_TAG";
  Serial.printf(
      "[HITAG] attempt=%u sample=0x%02X state=%s uid=%s pid=%s edges=%u pulses=%u quarter=%uus runs=%u/%u/%u\r\n",
      number, attempt.sampling, state, uid,
      attempt.decoded.product_id_valid ? "VALID" : "UNVERIFIED",
      attempt.edge_count, static_cast<unsigned>(attempt.pulse_count),
      attempt.decoded.quarter_us, static_cast<unsigned>(attempt.short_runs),
      static_cast<unsigned>(attempt.long_runs),
      static_cast<unsigned>(attempt.other_runs));
  Serial.printf(
      "[HITAG] safety=%s power=%s ANTFAIL=%s overflow=%s field_off=%s field=%lums VBUS=%.3fV current=%.1fmA\r\n",
      attempt.safety_ok ? "PASS" : "FAIL",
      attempt.field_power.valid ? "PASS" : "FAIL",
      attempt.antenna_ok ? "0" : "1", attempt.overflow ? "YES" : "NO",
      attempt.field_off_ok ? "PASS" : "FAIL",
      static_cast<unsigned long>(attempt.field_duration_ms),
      attempt.field_power.bus_voltage_v, attempt.field_power.current_ma);
  Serial.printf("[HITAG] timing first=%luus last=%luus raw=",
                static_cast<unsigned long>(attempt.first_edge_us),
                static_cast<unsigned long>(attempt.last_edge_us));
  for (std::size_t index = 0; index < gLastPulseDurationCount; ++index) {
    Serial.printf("%u%s", gLastPulseDurations[index],
                  index + 1U == gLastPulseDurationCount ? "\r\n" : ",");
  }
}

void runTagScan() {
  using gridopoly::tile::hitag_s::DecodeStatus;
  using gridopoly::tile::hitag_s::Uid;
  using gridopoly::tile::hitag_s::equalUid;
  using gridopoly::tile::hitag_s::formatUid;

  Serial.println(F("[TAG] guarded HITAG S256 UID request starting"));
  const bool link_ok = verifyDigitalLink();
  const PowerReading baseline = averagePower(3, 40);
  if (!link_ok || !baseline.valid ||
      baseline.bus_voltage_v < kMinimumBusVoltageV ||
      baseline.current_ma > kMaximumTotalCurrentMa) {
    (void)forceFieldOff();
    Serial.println(F("[TAG] ABORT before field: digital link or baseline power invalid"));
    return;
  }

  std::uint8_t phase = 0;
  std::uint8_t sampling = 0;
  if (!prepareHitagReader(baseline, phase, sampling)) {
    return;
  }

  Uid candidate{};
  std::uint8_t present_count = 0;
  std::uint8_t selected_sampling = sampling;
  std::uint8_t attempt_number = 1;
  bool consistent = true;
  bool all_safe = true;
  bool ambiguous = false;
  bool pid_valid = true;
  bool detected = false;
  const std::uint8_t sampling_candidates[] = {
      sampling,
      static_cast<std::uint8_t>((sampling + 8U) & 0x3FU),
      static_cast<std::uint8_t>((sampling - 8U) & 0x3FU),
  };
  for (const std::uint8_t candidate_sampling : sampling_candidates) {
    delay(kHitagFieldResetMs);
    const HitagAttempt attempt =
        captureHitagAttempt(baseline, candidate_sampling);
    printHitagAttempt(attempt_number++, attempt);
    if (!attempt.safety_ok) {
      all_safe = false;
      break;
    }
    if (attempt.decoded.status == DecodeStatus::Ambiguous) {
      ambiguous = true;
    } else if (attempt.decoded.status == DecodeStatus::Present) {
      candidate = attempt.decoded.uid;
      selected_sampling = candidate_sampling;
      pid_valid = attempt.decoded.product_id_valid;
      present_count = 1U;
      ambiguous = false;
      detected = true;
      break;
    }
  }

  for (std::uint8_t confirmation = 0;
       detected && all_safe && confirmation < 2U; ++confirmation) {
    delay(kHitagFieldResetMs);
    const HitagAttempt attempt =
        captureHitagAttempt(baseline, selected_sampling);
    printHitagAttempt(attempt_number++, attempt);
    if (!attempt.safety_ok) {
      all_safe = false;
      break;
    }
    if (attempt.decoded.status == DecodeStatus::Ambiguous) {
      ambiguous = true;
    } else if (attempt.decoded.status == DecodeStatus::Present) {
      consistent = consistent && equalUid(candidate, attempt.decoded.uid);
      pid_valid = pid_valid && attempt.decoded.product_id_valid;
      ++present_count;
    }
  }

  if (!all_safe) {
    Serial.println(F("[TAG] state=ABORT safety interlock; field forced OFF"));
  } else if (present_count == 3U && consistent && !ambiguous) {
    char uid[9]{};
    formatUid(candidate, uid);
    Serial.printf(
        "[TAG] state=PRESENT type=HITAG_S256 uid=%s consistent=3/3 pid=%s sampling=0x%02X\r\n",
        uid, pid_valid ? "VALID" : "UNVERIFIED", selected_sampling);
  } else if (present_count != 0U || ambiguous) {
    Serial.printf("[TAG] state=UNSTABLE consistent=%u/3\r\n", present_count);
  } else {
    Serial.println(F("[TAG] state=NO_TAG consistent=0/3"));
  }
  Serial.println(F("[TAG] UID request only; no tag memory write command was sent"));
}

void printStatus() {
  const PowerReading power = readPower();
  const std::uint8_t page1 = htrcGetConfig(1);
  const bool tx_disabled = (page1 & 0x0FU) == kFieldOffPage1;
  Serial.printf(
      "[STATUS] digital=%s TXDIS=%s P1=0x%02X INA=%s VBUS=%.3fV current=%.1fmA\r\n",
      gDigitalLinkOk ? "PASS" : "FAIL", tx_disabled ? "1/OFF" : "0/ON",
      page1, power.valid ? "PASS" : "FAIL", power.bus_voltage_v,
      power.current_ma);
  printResult(gLastResult);
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  STATUS     read safe state and whole-board power"));
  Serial.println(F("  TEST       run one guarded <=350 ms field pulse"));
  Serial.println(F("  SCAN       request and confirm one HITAG S256 UID"));
  Serial.println(F("  FIELD OFF  immediately force TXDIS=1"));
  Serial.println(F("  HELP       print this list"));
  Serial.println(F("No command enables a continuous field or writes a tag."));
}

void uppercase(char *text) {
  while (*text != '\0') {
    *text = static_cast<char>(
        std::toupper(static_cast<unsigned char>(*text)));
    ++text;
  }
}

void processCommand(char *command) {
  while (*command == ' ') {
    ++command;
  }
  uppercase(command);
  if (std::strcmp(command, "STATUS") == 0) {
    printStatus();
  } else if (std::strcmp(command, "TEST") == 0) {
    runCoilTest();
  } else if (std::strcmp(command, "SCAN") == 0) {
    runTagScan();
  } else if (std::strcmp(command, "FIELD OFF") == 0) {
    const bool ok = forceFieldOff();
    Serial.printf("[COIL] forced OFF verify=%s\r\n", ok ? "PASS" : "FAIL");
  } else if (std::strcmp(command, "HELP") == 0 ||
             std::strcmp(command, "?") == 0) {
    printHelp();
  } else if (*command != '\0') {
    Serial.println(F("ERR: unknown command; type HELP"));
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r' || character == '\n') {
      if (gSerialLength != 0U) {
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
  setUnrelatedOutputsSafe();
  initializeHtrcPins();
  // NXP specifies at least 10 ms for the external oscillator to settle.
  delay(15);
  const bool initial_off_ok = forceFieldOff();
  const bool pixels_off_ok = clearWs2812Latch();

  Serial.begin(115200);
  Serial.setTxTimeoutMs(5);
  delay(20);
  Serial.println();
  Serial.println(F("GRIDOPOLY HTRC110 HITAG S256 UID TEST V0.3"));
  Serial.printf("[BOOT] field_off=%s pixels_off=%s; LCD/backlight/Wi-Fi disabled\r\n",
                initial_off_ok ? "PASS" : "FAIL",
                pixels_off_ok ? "PASS" : "FAIL");

  gDigitalLinkOk = initial_off_ok && verifyDigitalLink();
  gInaReady = initializeIna226();
  gLastSafeRewriteMs = millis();
  gLastStatusMs = millis();
  printHelp();
  printStatus();
}

void loop() {
  pollSerial();
  const std::uint32_t now = millis();
  if (now - gLastSafeRewriteMs >= kSafeRewritePeriodMs) {
    gLastSafeRewriteMs = now;
    if (!forceFieldOff()) {
      gDigitalLinkOk = false;
    }
  }
  if (now - gLastStatusMs >= kStatusPeriodMs) {
    gLastStatusMs = now;
    const PowerReading power = readPower();
    Serial.printf("[SAFE] TXDIS=%s digital=%s VBUS=%.3fV current=%.1fmA\r\n",
                  gFieldOn ? "UNKNOWN/FAIL" : "1/OFF",
                  gDigitalLinkOk ? "PASS" : "FAIL",
                  power.bus_voltage_v, power.current_ma);
  }
  delay(1);
}

