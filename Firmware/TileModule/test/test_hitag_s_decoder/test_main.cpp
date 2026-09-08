#include <unity.h>

#include <cstdint>
#include <vector>

#include "hitag_s_decoder.h"

using namespace gridopoly::tile::hitag_s;

void setUp() {}
void tearDown() {}

namespace {

void appendAcBit(std::vector<Pulse> &pulses, std::uint8_t bit,
                 std::uint16_t short_us, bool &level, int jitter = 0) {
  const std::size_t count = bit != 0U ? 4U : 2U;
  const std::uint16_t nominal = bit != 0U ? short_us : short_us * 2U;
  for (std::size_t index = 0; index < count; ++index) {
    const int adjusted = static_cast<int>(nominal) +
                         ((index & 1U) != 0U ? jitter : -jitter);
    pulses.push_back({static_cast<std::uint16_t>(adjusted), level});
    level = !level;
  }
}

std::vector<Pulse> makeResponse(const Uid &uid, int jitter = 0,
                                bool terminate_last_run = true) {
  std::vector<Pulse> pulses = {{300U, false}, {70U, true}, {420U, false}};
  bool level = true;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level, jitter);
  }
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    for (int bit = 7; bit >= 0; --bit) {
      appendAcBit(pulses,
                  static_cast<std::uint8_t>((uid.wire_bytes[byte] >> bit) & 1U),
                  128U, level, jitter);
    }
  }
  if (terminate_last_run) {
    pulses.push_back({500U, level});
  } else {
    pulses.back().duration_us =
        static_cast<std::uint16_t>(pulses.back().duration_us + 4000U);
  }
  return pulses;
}

}  // namespace

void test_advanced_response_decodes_uid_and_formats_most_significant_byte_first() {
  const Uid expected{{0x12, 0x34, 0x56, 0x7A}};
  const std::vector<Pulse> pulses = makeResponse(expected, 12);
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
  TEST_ASSERT_UINT_WITHIN(12, 128, result.quarter_us);
  TEST_ASSERT_TRUE(hasHitagSProductIdentifier(result.uid));
  TEST_ASSERT_TRUE(result.product_id_valid);
  char text[9]{};
  formatUid(result.uid, text);
  TEST_ASSERT_EQUAL_STRING("7A563412", text);
}

void test_decoder_is_independent_of_dout_polarity() {
  const Uid expected{{0xA5, 0x5A, 0xC3, 0xF1}};
  std::vector<Pulse> pulses = makeResponse(expected);
  for (Pulse &pulse : pulses) {
    pulse.level = !pulse.level;
  }
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
}

void test_short_opposite_level_glitch_is_merged() {
  const Uid expected{{0x00, 0x11, 0x22, 0x83}};
  std::vector<Pulse> pulses = makeResponse(expected);
  const Pulse original = pulses[20];
  pulses[20].duration_us = static_cast<std::uint16_t>(original.duration_us / 2U);
  pulses.insert(pulses.begin() + 21, Pulse{20U, !original.level});
  pulses.insert(pulses.begin() + 22,
                Pulse{static_cast<std::uint16_t>(original.duration_us -
                                                 original.duration_us / 2U),
                      original.level});
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
}

void test_last_zero_or_one_decodes_without_a_terminating_edge() {
  for (const Uid expected : {Uid{{0x12, 0x34, 0x56, 0x7A}},
                             Uid{{0x12, 0x34, 0x56, 0x7B}}}) {
    const std::vector<Pulse> pulses = makeResponse(expected, 0, false);
    const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
    TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                      static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
  }
}

void test_pid_candidate_is_preferred_after_conflicting_fallback_candidates() {
  const Uid fallback_a{{0x10, 0x20, 0x30, 0x41}};
  const Uid fallback_b{{0x50, 0x60, 0x70, 0x42}};
  const Uid expected{{0x12, 0x34, 0x56, 0x7A}};
  std::vector<Pulse> pulses = makeResponse(fallback_a);
  const std::vector<Pulse> second = makeResponse(fallback_b);
  const std::vector<Pulse> third = makeResponse(expected);
  pulses.insert(pulses.end(), second.begin(), second.end());
  pulses.insert(pulses.end(), third.begin(), third.end());
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
  TEST_ASSERT_TRUE(result.product_id_valid);
}

void test_collision_pattern_is_rejected() {
  std::vector<Pulse> pulses = {{300U, false}, {70U, true}, {420U, false}};
  bool level = true;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level);
  }
  pulses.push_back({384U, level});
  level = !level;
  pulses.push_back({128U, level});
  level = !level;
  for (std::size_t bit = 1; bit < 32U; ++bit) {
    appendAcBit(pulses, 0U, 128U, level);
  }
  pulses.push_back({500U, level});
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Collision),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(0U, result.collision_bit);
  TEST_ASSERT_EQUAL_UINT8(0U, result.decoded_bits);
}

void test_collision_reports_known_uid_prefix() {
  std::vector<Pulse> pulses;
  bool level = true;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level);
  }
  const std::uint8_t prefix[] = {1U, 0U, 1U, 1U, 0U};
  for (const std::uint8_t bit : prefix) {
    appendAcBit(pulses, bit, 128U, level);
  }
  pulses.push_back({384U, level});
  level = !level;
  pulses.push_back({128U, level});
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Collision),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(5U, result.collision_bit);
  for (std::size_t bit = 0; bit < 5U; ++bit) {
    TEST_ASSERT_EQUAL_UINT8(prefix[bit], uidBit(result.uid, bit));
  }
}
void test_earliest_sof_collision_wins_over_nested_later_candidate() {
  std::vector<Pulse> pulses;
  bool level = true;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level);
  }
  appendAcBit(pulses, 1U, 128U, level);
  pulses.push_back({384U, level});
  level = !level;
  pulses.push_back({128U, level});
  pulses.push_back({600U, level});
  level = !level;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level);
  }
  for (std::size_t bit = 0; bit < 5U; ++bit) {
    appendAcBit(pulses, static_cast<std::uint8_t>(bit & 1U), 128U, level);
  }
  pulses.push_back({384U, level});
  level = !level;
  pulses.push_back({128U, level});

  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Collision),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(1U, result.collision_bit);
  TEST_ASSERT_EQUAL_UINT(0U, result.start_pulse);
}


void test_partial_ac_response_decodes_requested_suffix_length() {
  const Uid expected{{0xA5, 0x5A, 0xC3, 0xF1}};
  std::vector<Pulse> pulses;
  bool level = true;
  for (std::size_t bit = 0; bit < 3U; ++bit) {
    appendAcBit(pulses, 1U, 128U, level);
  }
  for (std::size_t bit = 0; bit < 13U; ++bit) {
    appendAcBit(pulses, uidBit(expected, bit), 128U, level);
  }
  pulses.push_back({500U, level});
  const DecodeResult result =
      decodeAdvancedBits(pulses.data(), pulses.size(), 13U);
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(13U, result.decoded_bits);
  TEST_ASSERT_TRUE(equalUidBits(expected, result.uid, 13U));
}

void test_real_com12_trace_accepts_final_run_before_recovery_noise() {
  // Captured from HTRC110 DOUT on COM12 with the user's HITAG S256 tag.
  const std::uint16_t durations[] = {
      74, 386, 106, 374, 184, 80, 168, 94, 158, 100, 152, 108, 142, 122,
      130, 126, 128, 130, 126, 132, 242, 274, 236, 282, 102, 148, 110,
      148, 106, 152, 104, 148, 110, 144, 112, 148, 234, 274, 114, 142,
      114, 144, 234, 282, 232, 278, 106, 150, 106, 152, 230, 282, 230,
      286, 98, 154, 102, 156, 228, 286, 100, 152, 104, 148, 110, 146,
      108, 154, 100, 154, 104, 148, 110, 148, 106, 156, 98, 156, 102,
      148, 110, 150, 102, 158, 226, 284, 100, 154, 104, 152, 228, 288,
      98, 152, 106, 156, 224, 290, 224, 286, 228, 274, 114, 142, 114,
      142, 114, 142, 114, 142, 114, 142, 112, 148, 232, 1806, 3489};

  std::vector<Pulse> pulses;
  pulses.reserve(sizeof(durations) / sizeof(durations[0]));
  bool level = false;
  for (const std::uint16_t duration : durations) {
    pulses.push_back({duration, level});
    level = !level;
  }

  const Uid expected{{0x9D, 0x25, 0xFA, 0x8E}};
  const DecodeResult result = decodeAdvancedUid(pulses.data(), pulses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeStatus::Present),
                    static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, result.uid.wire_bytes, 4);
  TEST_ASSERT_TRUE(result.product_id_valid);
  char text[9]{};
  formatUid(result.uid, text);
  TEST_ASSERT_EQUAL_STRING("8EFA259D", text);
}

void test_noise_and_truncated_response_are_rejected() {
  const Pulse noise[] = {{20U, false}, {700U, true}, {91U, false},
                         {330U, true}, {55U, false}};

  TEST_ASSERT_EQUAL(

      static_cast<int>(DecodeStatus::NoResponse),
      static_cast<int>(decodeAdvancedUid(noise, 5U).status));

  const Uid uid{{0x12, 0x34, 0x56, 0x7A}};
  std::vector<Pulse> truncated = makeResponse(uid);
  truncated.resize(40U);
  TEST_ASSERT_EQUAL(
      static_cast<int>(DecodeStatus::NoResponse),
      static_cast<int>(decodeAdvancedUid(truncated.data(), truncated.size()).status));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_advanced_response_decodes_uid_and_formats_most_significant_byte_first);
  RUN_TEST(test_decoder_is_independent_of_dout_polarity);
  RUN_TEST(test_short_opposite_level_glitch_is_merged);
  RUN_TEST(test_last_zero_or_one_decodes_without_a_terminating_edge);
  RUN_TEST(test_pid_candidate_is_preferred_after_conflicting_fallback_candidates);
  RUN_TEST(test_collision_pattern_is_rejected);
  RUN_TEST(test_collision_reports_known_uid_prefix);
  RUN_TEST(test_partial_ac_response_decodes_requested_suffix_length);
  RUN_TEST(test_earliest_sof_collision_wins_over_nested_later_candidate);
  RUN_TEST(test_real_com12_trace_accepts_final_run_before_recovery_noise);
  RUN_TEST(test_noise_and_truncated_response_are_rejected);
  return UNITY_END();
}
