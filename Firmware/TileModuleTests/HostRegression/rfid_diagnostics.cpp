#include <cstdint>
#include <vector>
#include <unity.h>
#include "rfid_diagnostics.h"

using namespace gridopoly::tile;
rfid_diagnostics::Trace gRfidDiagnostics;
bool gHtrcTransmitterSafe = true;
std::uint32_t nowMs = 1000;
std::uint8_t pages[4]{}, settledPage = 0, currentSampling = 0;
bool offOk = true, samplingOk = true, pulseOk = true;
float fieldVoltage = 5, fieldCurrent = 200;
bool fieldValid = true;
int page2Reads = 0, powerReads = 0, offCalls = 0, pulseCalls = 0;
std::vector<unsigned> io;
std::uint32_t millis() { return nowMs; }
void delay(unsigned ms) { nowMs += ms; io.push_back(0x10000U + ms); }
void delayMicroseconds(unsigned us) { io.push_back(0x20000U + us); }
void htrcSetConfig(unsigned page, unsigned value) { io.push_back(0x30000U + page * 256 + value); }
std::uint8_t htrcGetConfig(unsigned page) {
  io.push_back(0x40000U + page);
  if (page == 2 && page2Reads++ > 0) return settledPage;
  return pages[page];
}
bool forceHtrcTransmitterOff() { ++offCalls; io.push_back(0x50000U); return offOk; }
std::uint8_t htrcReadPhase() { io.push_back(0x60000U); return 4; }
void htrcSetSampling(std::uint8_t value) { currentSampling = value; io.push_back(0x70000U + value); }
std::uint8_t htrcGetSampling() { io.push_back(0x80000U); return samplingOk ? currentSampling : currentSampling ^ 1U; }
bool htrcProgramWritePulseWidth(std::uint8_t value) { ++pulseCalls; io.push_back(0x90000U + value); return pulseOk; }
void samplePower();
#include "rfid_fixture.inc"
void samplePower() {
  ++powerReads;
  io.push_back(0xA0000U);
  gPower.valid = fieldValid;
  gPower.configured = true;
  gPower.bus_voltage_v = fieldVoltage;
  gPower.current_ma = fieldCurrent;
}
void setUp() {
  gRfidDiagnostics = {};
  gHtrcTransmitterSafe = true;
  nowMs = 1000;
  pages[0] = 0x0B; pages[1] = pages[2] = pages[3] = settledPage = 0;
  offOk = samplingOk = pulseOk = fieldValid = true;
  fieldVoltage = 5; fieldCurrent = 200;
  page2Reads = powerReads = offCalls = pulseCalls = 0;
  io.clear();
  gPower = {};
}
void tearDown() {}
PowerReading baseline() {
  PowerReading power;
  power.configured = power.valid = true;
  power.bus_voltage_v = 5;
  power.current_ma = 100;
  return power;
}
bool prepare(bool diagnostics = true) {
  if (diagnostics) { gRfidDiagnostics.enable(nowMs); gRfidDiagnostics.begin(nowMs); }
  std::uint8_t sampling = 0;
  return prepareHitagReader(baseline(), sampling);
}
void test_disabled_and_enabled_have_identical_hardware_calls() {
  TEST_ASSERT_TRUE(prepare(false));
  TEST_ASSERT_EQUAL_UINT(0, gRfidDiagnostics.size());
  const auto expected = io;
  setUp();
  TEST_ASSERT_TRUE(prepare());
  TEST_ASSERT_TRUE(expected == io);
  TEST_ASSERT_EQUAL_UINT(3, gRfidDiagnostics.size());
  TEST_ASSERT_EQUAL_INT(1, gRfidDiagnostics.at(1).safe);
}
void test_early_antfail_preserved_without_invented_power() {
  pages[2] = 0x10;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(1, offCalls);
  TEST_ASSERT_EQUAL_INT(0, powerReads);
  TEST_ASSERT_EQUAL_INT(0, pulseCalls);
  const auto &r = gRfidDiagnostics.at(0);
  TEST_ASSERT_EQUAL_INT(1, r.config);
  TEST_ASSERT_EQUAL_INT(0, r.antenna);
  TEST_ASSERT_EQUAL_INT(-1, r.power_valid);
  TEST_ASSERT_EQUAL_HEX8(0x10, r.pages[2]);
  TEST_ASSERT_EQUAL_INT(1, r.field_off);
}
void test_config_failure_and_field_off_failure_remain_failures() {
  pages[0] = 0; offOk = false;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(0).config);
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(0).field_off);
  TEST_ASSERT_FALSE(gHtrcTransmitterSafe);
}
void test_settled_antfail_prevents_request_setup() {
  settledPage = 0x10;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(0, pulseCalls);
  TEST_ASSERT_EQUAL_INT(1, powerReads);
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(1).antenna);
  TEST_ASSERT_EQUAL_HEX8(0x10, gRfidDiagnostics.at(1).pages[2]);
}
void test_invalid_power_and_voltage_current_delta_gates() {
  auto p = baseline();
  p.bus_voltage_v = 4.65F; p.current_ma = 350;
  TEST_ASSERT_TRUE(tagPowerSafe(p, 170));
  p.bus_voltage_v = 4.649F; TEST_ASSERT_FALSE(tagPowerSafe(p, 170));
  p.bus_voltage_v = 5; p.current_ma = 350.1F; TEST_ASSERT_FALSE(tagPowerSafe(p, 200));
  p.current_ma = 280.1F; TEST_ASSERT_FALSE(tagPowerSafe(p, 100));
  p.current_ma = 200; p.valid = false; TEST_ASSERT_FALSE(tagPowerSafe(p, 100));
  fieldValid = false;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(1, offCalls);
  TEST_ASSERT_EQUAL_INT(0, pulseCalls);
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(1).power_valid);
}
void test_sampling_and_pulse_failure_preserved() {
  samplingOk = false;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(1).sampling_ok);
  setUp(); pulseOk = false;
  TEST_ASSERT_FALSE(prepare());
  TEST_ASSERT_EQUAL_INT(0, gRfidDiagnostics.at(2).config);
}
void test_duration_rate_limit_wrap_and_capacity() {
  rfid_diagnostics::Trace trace;
  TEST_ASSERT_FALSE(trace.begin(0));
  trace.enable(0xFFFFFF00U);
  TEST_ASSERT_TRUE(trace.begin(0xFFFFFF00U));
  trace.finish();
  TEST_ASSERT_FALSE(trace.begin(0x000002E7U));
  TEST_ASSERT_TRUE(trace.begin(0x000002E8U));
  rfid_diagnostics::Record row;
  for (unsigned i = 0; i < 40; ++i) { row.field_ms = i; trace.add(row); }
  TEST_ASSERT_EQUAL_UINT(24, trace.size());
  TEST_ASSERT_EQUAL_UINT(16, trace.dropped());
  TEST_ASSERT_EQUAL_INT(39, trace.at(23).field_ms);
  TEST_ASSERT_FALSE(trace.begin(0xFFFFFF00U + 30000U));
  trace.enable(1000); trace.disable();
  TEST_ASSERT_FALSE(trace.begin(1001));
  TEST_ASSERT_TRUE(trace.begin(1001, true));
  trace.finish();
  TEST_ASSERT_FALSE(trace.enabled(1002));
}
void test_probe_ignores_only_antfail_and_preserves_all_other_capture_gates() {
  using namespace rfid_diagnostics;
  TEST_ASSERT_FALSE(captureAccepted(true, false, true, false, true, 30));
  TEST_ASSERT_TRUE(captureAccepted(true, false, true, false, true, 30, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_FALSE(captureAccepted(false, false, true, false, true, 30, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_FALSE(captureAccepted(true, false, false, false, true, 30, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_FALSE(captureAccepted(true, false, true, true, true, 30, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_FALSE(captureAccepted(true, false, true, false, false, 30, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_FALSE(captureAccepted(true, false, true, false, true, 351, AntennaPolicy::ProbeOnce));
  TEST_ASSERT_TRUE(captureAccepted(true, false, true, false, true, 350, AntennaPolicy::ProbeOnce));
}
void test_probe_prepare_passes_antfail_but_still_measures_field_power() {
  pages[2] = settledPage = 0x10;
  std::uint8_t sampling = 0;
  std::uint32_t duration = 0;
  TEST_ASSERT_TRUE(prepareHitagReader(baseline(), sampling,
      rfid_diagnostics::AntennaPolicy::ProbeOnce, &duration));
  TEST_ASSERT_EQUAL_INT(1, powerReads);
  TEST_ASSERT_EQUAL_INT(1, offCalls);
  fieldCurrent = 351; page2Reads = 0;
  TEST_ASSERT_FALSE(prepareHitagReader(baseline(), sampling,
      rfid_diagnostics::AntennaPolicy::ProbeOnce, &duration));
  fieldCurrent = 200; pages[0] = 0; page2Reads = 0;
  TEST_ASSERT_FALSE(prepareHitagReader(baseline(), sampling,
      rfid_diagnostics::AntennaPolicy::ProbeOnce, &duration));
}
void test_probe_budget_and_cooldown_wrap_and_rest() {
  rfid_diagnostics::ProbeBudget budget;
  TEST_ASSERT_FALSE(budget.next(0));
  TEST_ASSERT_TRUE(budget.claim(0xFFFFFF00U));
  TEST_ASSERT_FALSE(budget.claim(0xFFFFFF01U));
  for (unsigned i = 0; i < 12; ++i) TEST_ASSERT_TRUE(budget.next(0xFFFFFF00U + i));
  TEST_ASSERT_FALSE(budget.next(0xFFFFFF10U));
  budget.finish();
  TEST_ASSERT_FALSE(budget.claim(0xFFFFFF00U + 59999U));
  TEST_ASSERT_TRUE(budget.claim(0xFFFFFF00U + 60000U));
  TEST_ASSERT_FALSE(budget.next(0xFFFFFF00U + 65651U));
  TEST_ASSERT_EQUAL_UINT32(150, rfid_diagnostics::ProbeBudget::restMs(30));
  TEST_ASSERT_EQUAL_UINT32(400, rfid_diagnostics::ProbeBudget::restMs(100));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_and_enabled_have_identical_hardware_calls);
  RUN_TEST(test_early_antfail_preserved_without_invented_power);
  RUN_TEST(test_config_failure_and_field_off_failure_remain_failures);
  RUN_TEST(test_settled_antfail_prevents_request_setup);
  RUN_TEST(test_invalid_power_and_voltage_current_delta_gates);
  RUN_TEST(test_sampling_and_pulse_failure_preserved);
  RUN_TEST(test_duration_rate_limit_wrap_and_capacity);
  RUN_TEST(test_probe_ignores_only_antfail_and_preserves_all_other_capture_gates);
  RUN_TEST(test_probe_prepare_passes_antfail_but_still_measures_field_power);
  RUN_TEST(test_probe_budget_and_cooldown_wrap_and_rest);
  return UNITY_END();
}
