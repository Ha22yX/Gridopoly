#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <unity.h>
#include "hitag_s_protocol.h"
#include "rfid_diagnostics.h"
using namespace gridopoly::tile;
constexpr float kTagMinimumBusVoltageV = 4.65F;
constexpr float kTagMaximumTotalCurrentMa = 350.0F;
constexpr float kTagMaximumCurrentIncreaseMa = 180.0F;
constexpr std::uint32_t kTagMaximumFieldMs = 350;
rfid_diagnostics::Trace gRfidDiagnostics;
rfid_diagnostics::ProbeBudget gRfidProbeBudget;
bool gHtrcTransmitterSafe = true, digitalOk = true, prepareOk = true;
bool powerValid = true, captureOk = true, emitUid = false, mismatch = false;
unsigned captureCalls = 0, prepareCalls = 0, offCalls = 0;
std::uint32_t nowMs = 1000, fieldEnded = 0, captureDuration = 80;
std::vector<std::uint8_t> samples;
std::vector<std::uint32_t> rests;
std::string output;
std::uint32_t millis() { return nowMs; }
void delay(std::uint32_t ms) { nowMs += ms; }
bool forceHtrcTransmitterOff() { ++offCalls; return true; }
bool verifyHtrcDigitalLink() { return digitalOk; }
void samplePower();
struct RfidTraceScope {
  explicit RfidTraceScope(bool one_shot) { gRfidDiagnostics.begin(nowMs, one_shot); }
  ~RfidTraceScope() { gRfidDiagnostics.finish(); }
};
struct FakeSerial {
  void println(const char *text) { TEST_ASSERT_TRUE(gHtrcTransmitterSafe); output += text; }
  template <typename... Args> void printf(const char *format, Args... args) {
    TEST_ASSERT_TRUE(gHtrcTransmitterSafe);
    char line[512]; std::snprintf(line, sizeof line, format, args...); output += line;
  }
} Serial;
#define F(text) text
#include "probe_fixture.inc"
void samplePower() {
  gPower.configured = true; gPower.valid = powerValid;
  gPower.bus_voltage_v = 5; gPower.current_ma = 120;
}
bool prepareHitagReader(const PowerReading&, std::uint8_t &sampling,
                       rfid_diagnostics::AntennaPolicy policy, std::uint32_t *field_ms) {
  TEST_ASSERT_TRUE(policy == rfid_diagnostics::AntennaPolicy::ProbeOnce);
  ++prepareCalls;
  sampling = 7; *field_ms = 20; nowMs += 20; fieldEnded = nowMs;
  return prepareOk;
}
TagAttempt captureHitagAttempt(const PowerReading&, std::uint8_t sampling,
                              rfid_diagnostics::AntennaPolicy policy) {
  TEST_ASSERT_TRUE(policy == rfid_diagnostics::AntennaPolicy::ProbeOnce);
  rests.push_back(nowMs - fieldEnded);
  samples.push_back(sampling);
  ++captureCalls;
  nowMs += captureDuration;
  fieldEnded = nowMs;
  TagAttempt attempt;
  attempt.safety_ok = captureOk;
  attempt.field_off_ok = true;
  attempt.field_duration_ms = captureDuration;
  if (emitUid) {
    attempt.decoded.status = hitag_s::DecodeStatus::Present;
    attempt.decoded.uid = {{0x12,0x34,0x56,0x78}};
    if (mismatch && captureCalls == 2) attempt.decoded.uid.wire_bytes[0] = 0x13;
  }
  return attempt;
}
void setUp() {
  gRfidDiagnostics = {}; gRfidProbeBudget = {}; gPower = {};
  gHtrcTransmitterSafe = digitalOk = prepareOk = powerValid = captureOk = true;
  emitUid = mismatch = false;
  captureCalls = prepareCalls = offCalls = 0;
  nowMs = 1000; fieldEnded = 0; captureDuration = 80;
  samples.clear(); rests.clear(); output.clear();
}
void tearDown() {}
void test_probe_confirms_three_then_returns_to_inactive_and_cooldown() {
  emitUid = true;
  runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(3, captureCalls);
  TEST_ASSERT_TRUE(output.find("reason=UID_CONFIRMED") != std::string::npos);
  TEST_ASSERT_TRUE(output.find("uid=78563412") != std::string::npos);
  TEST_ASSERT_TRUE(output.find("published=NO normal_mode=STRICT") != std::string::npos);
  TEST_ASSERT_FALSE(gRfidDiagnostics.enabled(nowMs));
  TEST_ASSERT_FALSE(gRfidDiagnostics.capturing());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(150, nowMs - fieldEnded);
  runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(3, captureCalls);
  TEST_ASSERT_TRUE(output.find("rejected=COOLDOWN") != std::string::npos);
}
void test_probe_no_uid_covers_finite_sampling_and_stops() {
  runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(12, captureCalls);
  TEST_ASSERT_EQUAL_UINT8(7, samples[0]);
  TEST_ASSERT_EQUAL_UINT8(15, samples[1]);
  TEST_ASSERT_EQUAL_UINT8(63, samples[2]);
  TEST_ASSERT_TRUE(output.find("reason=ATTEMPT_LIMIT") != std::string::npos);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(7400, nowMs - 1000);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(150, rests[0]);
  for (unsigned i = 1; i < rests.size(); ++i) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(320, rests[i]);
}
void test_every_non_antenna_failure_exits_and_cleans_up() {
  digitalOk = false;
  runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(0, prepareCalls); TEST_ASSERT_EQUAL_UINT(0, captureCalls);
  TEST_ASSERT_EQUAL_UINT(2, offCalls);
  setUp(); powerValid = false; runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(0, prepareCalls); TEST_ASSERT_EQUAL_UINT(2, offCalls);
  setUp(); prepareOk = false; runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(0, captureCalls); TEST_ASSERT_EQUAL_UINT(2, offCalls);
  setUp(); captureOk = false; runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(1, captureCalls); TEST_ASSERT_EQUAL_UINT(2, offCalls);
  TEST_ASSERT_TRUE(output.find("reason=CAPTURE_UNSAFE") != std::string::npos);
}
void test_mismatched_uid_is_not_confirmed() {
  emitUid = mismatch = true;
  runRfidProbeOnce();
  TEST_ASSERT_EQUAL_UINT(2, captureCalls);
  TEST_ASSERT_TRUE(output.find("reason=UID_MISMATCH") != std::string::npos);
  TEST_ASSERT_TRUE(output.find("confirmed=0") != std::string::npos);
}
void test_slow_fields_get_fourfold_rest_and_hit_time_budget() {
  captureDuration = 300;
  runRfidProbeOnce();
  TEST_ASSERT_LESS_THAN_UINT(12, captureCalls);
  for (unsigned i = 1; i < rests.size(); ++i) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1200, rests[i]);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1200, nowMs - fieldEnded);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(7400, nowMs - 1000);
  TEST_ASSERT_TRUE(output.find("reason=TIME_BUDGET") != std::string::npos);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_probe_confirms_three_then_returns_to_inactive_and_cooldown);
  RUN_TEST(test_probe_no_uid_covers_finite_sampling_and_stops);
  RUN_TEST(test_every_non_antenna_failure_exits_and_cleans_up);
  RUN_TEST(test_mismatched_uid_is_not_confirmed);
  RUN_TEST(test_slow_fields_get_fourfold_rest_and_hit_time_budget);
  return UNITY_END();
}
