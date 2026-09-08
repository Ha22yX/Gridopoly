#include <Arduino.h>

// Reuse the display, INA226, and WS2812 primitives already proven on this PCB.
// Rename the original Arduino entry points so this sketch can provide its own.
#define setup boardBringUpUnusedSetup
#define loop boardBringUpUnusedLoop
#include "../BoardBringUpTest/BoardBringUpTest.ino"
#undef loop
#undef setup

namespace {

using namespace gridopoly::tile_bring_up;

constexpr int kHtrcSclkPin = 17;
constexpr int kHtrcDinPin = 18;
constexpr int kHtrcDoutPin = 8;

constexpr uint8_t kGetSamplingTime = 0x02;
constexpr uint8_t kGetConfigPage = 0x04;
constexpr uint8_t kSetConfigPage = 0x40;
constexpr uint8_t kSetSamplingTime = 0x80;

// Config page 1: PD_MODE=0, PD=0, HYSTERESIS=0, TXDIS=1.
constexpr uint8_t kSafePage1 = 0x01;
constexpr uint8_t kSafePage1Hysteresis = 0x03;
constexpr uint8_t kSamplingPatternA = 0x15;
constexpr uint8_t kSamplingPatternB = 0x2A;
constexpr uint32_t kHtrcHalfCycleUs = 24;
constexpr uint32_t kHtrcGuardUs = 12;
constexpr uint32_t kSafetyRewritePeriodMs = 1000;
constexpr uint32_t kHealthReadPeriodMs = 500;
constexpr uint32_t kRfidDashboardPeriodMs = 250;
constexpr uint32_t kPowerSamplePeriodMs = 200;

struct HtrcStatus {
  bool initialized = false;
  bool serialPass = false;
  bool configRoundTripPass = false;
  bool samplingRoundTripPass = false;
  bool txDisabled = false;
  bool clockDivider4Mhz = false;
  uint8_t config[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t samplingTime = 0xFF;
  uint32_t readsOk = 0;
  uint32_t errors = 0;
  uint32_t safetyWrites = 0;
};

HtrcStatus gHtrc;
uint32_t gLastSafetyRewriteMs = 0;
uint32_t gLastHealthReadMs = 0;
uint32_t gLastPowerSampleMs = 0;
uint32_t gLastRfidDashboardMs = 0;

void htrcDelay() {
  delayMicroseconds(kHtrcHalfCycleUs);
}

void htrcSerialInitialize() {
  // Interface reset: DIN low-to-high while SCLK is high.
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

void htrcWriteBits(uint8_t value, uint8_t bitCount) {
  for (int bit = bitCount - 1; bit >= 0; --bit) {
    digitalWrite(kHtrcDinPin, (value & (1U << bit)) != 0U ? HIGH : LOW);
    delayMicroseconds(kHtrcGuardUs);
    digitalWrite(kHtrcSclkPin, HIGH);
    htrcDelay();
    digitalWrite(kHtrcSclkPin, LOW);
    htrcDelay();
  }
}

uint8_t htrcReadBits(uint8_t bitCount) {
  uint8_t value = 0;
  digitalWrite(kHtrcDinPin, LOW);
  delayMicroseconds(kHtrcGuardUs);
  for (uint8_t bit = 0; bit < bitCount; ++bit) {
    digitalWrite(kHtrcSclkPin, HIGH);
    htrcDelay();
    value = static_cast<uint8_t>((value << 1U) |
                                 (digitalRead(kHtrcDoutPin) == HIGH ? 1U : 0U));
    digitalWrite(kHtrcSclkPin, LOW);
    htrcDelay();
  }
  return value;
}

void htrcWriteCommand(uint8_t command) {
  htrcSerialInitialize();
  htrcWriteBits(command, 8);
  digitalWrite(kHtrcDinPin, LOW);
}

uint8_t htrcReadCommand(uint8_t command) {
  htrcSerialInitialize();
  htrcWriteBits(command, 8);
  return htrcReadBits(8);
}

void htrcSetConfigPage(uint8_t page, uint8_t data) {
  htrcWriteCommand(static_cast<uint8_t>(kSetConfigPage |
                                        ((page & 0x03U) << 4U) |
                                        (data & 0x0FU)));
}

uint8_t htrcGetConfigPage(uint8_t page) {
  return htrcReadCommand(static_cast<uint8_t>(kGetConfigPage | (page & 0x03U)));
}

void htrcSetSamplingTime(uint8_t value) {
  htrcWriteCommand(static_cast<uint8_t>(kSetSamplingTime | (value & 0x3FU)));
}

uint8_t htrcGetSamplingTime() {
  return htrcReadCommand(kGetSamplingTime);
}

void forceTransmitterOff() {
  htrcSetConfigPage(1, kSafePage1);
  ++gHtrc.safetyWrites;
  const uint8_t page1 = htrcGetConfigPage(1);
  gHtrc.config[1] = page1;
  gHtrc.txDisabled = (page1 & 0x0FU) == kSafePage1;
  if (!gHtrc.txDisabled) {
    ++gHtrc.errors;
  }
}

bool readAllConfigPages() {
  bool plausible = true;
  for (uint8_t page = 0; page < 4; ++page) {
    gHtrc.config[page] = htrcGetConfigPage(page);
  }
  // Reserved response bits 7 and 6 are zero on status pages 2 and 3.
  plausible &= (gHtrc.config[2] & 0xC0U) == 0U;
  plausible &= (gHtrc.config[3] & 0xC0U) == 0U;
  plausible &= (gHtrc.config[1] & 0x0FU) == kSafePage1;
  gHtrc.clockDivider4Mhz = (gHtrc.config[3] & 0x03U) == 0U;
  if (plausible) {
    ++gHtrc.readsOk;
  } else {
    ++gHtrc.errors;
  }
  return plausible;
}

bool runConfigRoundTrip() {
  htrcSetConfigPage(1, kSafePage1Hysteresis);
  const uint8_t high = htrcGetConfigPage(1);
  htrcSetConfigPage(1, kSafePage1);
  const uint8_t low = htrcGetConfigPage(1);
  gHtrc.config[1] = low;
  gHtrc.txDisabled = (low & 0x0FU) == kSafePage1;
  return (high & 0x0FU) == kSafePage1Hysteresis &&
         (low & 0x0FU) == kSafePage1;
}

bool runSamplingRoundTrip() {
  const uint8_t original = static_cast<uint8_t>(htrcGetSamplingTime() & 0x3FU);
  htrcSetSamplingTime(kSamplingPatternA);
  const uint8_t readA = htrcGetSamplingTime();
  htrcSetSamplingTime(kSamplingPatternB);
  const uint8_t readB = htrcGetSamplingTime();
  htrcSetSamplingTime(original);
  gHtrc.samplingTime = htrcGetSamplingTime();
  return (readA & 0xC0U) == 0U && (readA & 0x3FU) == kSamplingPatternA &&
         (readB & 0xC0U) == 0U && (readB & 0x3FU) == kSamplingPatternB &&
         (gHtrc.samplingTime & 0x3FU) == original;
}

void initializeHtrc110() {
  pinMode(kHtrcSclkPin, OUTPUT);
  pinMode(kHtrcDinPin, OUTPUT);
  pinMode(kHtrcDoutPin, INPUT);
  digitalWrite(kHtrcSclkPin, LOW);
  digitalWrite(kHtrcDinPin, LOW);

  // NXP specifies 10 ms oscillator settling after power-up.
  delay(15);
  forceTransmitterOff();
  gHtrc.configRoundTripPass = runConfigRoundTrip();
  gHtrc.samplingRoundTripPass = runSamplingRoundTrip();
  const bool pagesPlausible = readAllConfigPages();
  gHtrc.serialPass = gHtrc.txDisabled && gHtrc.configRoundTripPass &&
                     gHtrc.samplingRoundTripPass && pagesPlausible;
  gHtrc.initialized = true;

  Serial.printf("[HTRC110] pins SCLK=%d DIN=%d DOUT=%d\r\n",
                kHtrcSclkPin, kHtrcDinPin, kHtrcDoutPin);
  Serial.printf("[HTRC110] config P0=%02X P1=%02X P2=%02X P3=%02X\r\n",
                gHtrc.config[0], gHtrc.config[1],
                gHtrc.config[2], gHtrc.config[3]);
  Serial.printf("[HTRC110] TXDIS=%s config_roundtrip=%s sampling_roundtrip=%s sampling=%02X\r\n",
                gHtrc.txDisabled ? "ON" : "FAIL",
                gHtrc.configRoundTripPass ? "PASS" : "FAIL",
                gHtrc.samplingRoundTripPass ? "PASS" : "FAIL",
                gHtrc.samplingTime);
  Serial.printf("[HTRC110] digital_link=%s clock_divider=%s antenna=NOT_CONNECTED\r\n",
                gHtrc.serialPass ? "PASS" : "FAIL",
                gHtrc.clockDivider4Mhz ? "4MHZ_SELECTED" : "CHECK");
}

void drawRfidStaticDashboard() {
  fillRect(0, 0, kDisplayWidth, kDisplayHeight, kBackground);
  fillRect(0, 0, kDisplayWidth, 42, kPanel);
  drawCenteredText(5, "HTRC110 DIGITAL", kWhite, 2);
  drawCenteredText(26, "NO ANTENNA TEST", kYellow, 1);
  drawHorizontalLine(0, 41, kDisplayWidth, kCyan);

  drawText(12, 53, "SERIAL LINK", kMuted, 1);
  drawFrame(126, 47, 102, 24, kPanelBright);
  drawText(12, 82, "TX DRIVER", kMuted, 1);
  drawText(12, 108, "CONFIG P0-P3", kMuted, 1);
  drawText(12, 150, "REGISTER TEST", kMuted, 1);
  drawText(12, 176, "CLOCK DIVIDER", kMuted, 1);
  drawText(12, 202, "ANTENNA", kMuted, 1);

  drawHorizontalLine(12, 75, 216, kPanelBright);
  drawHorizontalLine(12, 101, 216, kPanelBright);
  drawHorizontalLine(12, 143, 216, kPanelBright);
  drawHorizontalLine(12, 169, 216, kPanelBright);
  drawHorizontalLine(12, 195, 216, kPanelBright);
  drawHorizontalLine(12, 221, 216, kPanelBright);

  fillRect(0, 228, kDisplayWidth, 92, kPanel);
  drawHorizontalLine(0, 228, kDisplayWidth, kCyan);
  drawText(12, 238, "INA226 POWER", kMuted, 1);
  drawText(12, 266, "5V BUS", kMuted, 1);
  drawText(12, 292, "CURRENT", kMuted, 1);
}

void drawRfidDashboard() {
  char text[32];
  const uint16_t statusColor = gHtrc.serialPass ? kGreen : kRed;

  fillRect(127, 48, 100, 22, kPanel);
  drawFrame(127, 48, 100, 22, statusColor);
  drawCenteredIn(127, 100, 55, gHtrc.serialPass ? "PASS" : "FAIL", statusColor, 2);

  fillRect(102, 79, 126, 20, kBackground);
  drawText(130, 83, gHtrc.txDisabled ? "OFF SAFE" : "UNKNOWN", gHtrc.txDisabled ? kGreen : kRed, 1);

  fillRect(12, 119, 216, 20, kBackground);
  snprintf(text, sizeof(text), "%02X  %02X  %02X  %02X",
           gHtrc.config[0], gHtrc.config[1], gHtrc.config[2], gHtrc.config[3]);
  drawCenteredText(122, text, kWhite, 1);

  fillRect(110, 147, 118, 20, kBackground);
  drawText(121, 151,
           (gHtrc.configRoundTripPass && gHtrc.samplingRoundTripPass) ? "PASS" : "FAIL",
           (gHtrc.configRoundTripPass && gHtrc.samplingRoundTripPass) ? kGreen : kRed, 1);

  fillRect(110, 173, 118, 20, kBackground);
  drawText(121, 177, gHtrc.clockDivider4Mhz ? "4MHZ SELECT" : "CHECK CFG", gHtrc.clockDivider4Mhz ? kGreen : kYellow, 1);

  fillRect(90, 199, 138, 20, kBackground);
  drawText(103, 203, "NOT CONNECTED", kYellow, 1);

  fillRect(82, 258, 146, 24, kPanel);
  fillRect(82, 284, 146, 24, kPanel);
  if (gIna.valid) {
    snprintf(text, sizeof(text), "%.3F V", gIna.busVoltageV);
    drawText(112, 264, text,
             (gIna.busVoltageV >= 4.75F && gIna.busVoltageV <= 5.25F) ? kWhite : kYellow, 2);
    snprintf(text, sizeof(text), "%.1F MA", gIna.currentMa);
    drawText(106, 290, text, kWhite, 2);
  } else {
    drawText(148, 264, "N/A", kRed, 2);
    drawText(148, 290, "N/A", kRed, 2);
  }
}

void refreshHtrcHealth() {
  // Reassert the safe state before every health read.
  forceTransmitterOff();
  const bool pagesPlausible = readAllConfigPages();
  gHtrc.serialPass = gHtrc.txDisabled && gHtrc.configRoundTripPass &&
                     gHtrc.samplingRoundTripPass && pagesPlausible;
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  Serial.setTxTimeoutMs(0);

  // Safety first: disable the RFID transmitter before initializing slow peripherals.
  initializeHtrc110();
  initializeDisplay();
  initializeIna226();
  initializeWs2812();
  clearPixels();
  showPixels();
  sampleIna226();
  drawRfidStaticDashboard();
  drawRfidDashboard();

  const uint32_t now = millis();
  gLastSafetyRewriteMs = now;
  gLastHealthReadMs = now;
  gLastPowerSampleMs = now;
  gLastRfidDashboardMs = now;

  Serial.println();
  Serial.println("GRIDOPOLY HTRC110 DIGITAL-SIDE TEST");
  Serial.println("Antenna coil must remain disconnected.");
  Serial.println("No READ_TAG, WRITE_TAG, or field generation commands are used.");
}

void loop() {
  const uint32_t now = millis();

  if (now - gLastSafetyRewriteMs >= kSafetyRewritePeriodMs) {
    gLastSafetyRewriteMs = now;
    forceTransmitterOff();
  }
  if (now - gLastHealthReadMs >= kHealthReadPeriodMs) {
    gLastHealthReadMs = now;
    refreshHtrcHealth();
    if ((gHtrc.readsOk % 10U) == 0U || !gHtrc.serialPass) {
      Serial.printf("[HTRC110] link=%s TXDIS=%s P0=%02X P1=%02X P2=%02X P3=%02X reads=%lu errors=%lu\r\n",
                    gHtrc.serialPass ? "PASS" : "FAIL",
                    gHtrc.txDisabled ? "1" : "0",
                    gHtrc.config[0], gHtrc.config[1],
                    gHtrc.config[2], gHtrc.config[3],
                    static_cast<unsigned long>(gHtrc.readsOk),
                    static_cast<unsigned long>(gHtrc.errors));
    }
  }
  if (now - gLastPowerSampleMs >= kPowerSamplePeriodMs) {
    gLastPowerSampleMs = now;
    sampleIna226();
  }
  if (now - gLastRfidDashboardMs >= kDashboardPeriodMs) {
    gLastRfidDashboardMs = now;
    drawRfidDashboard();
  }
  delay(1);
}
