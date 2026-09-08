#include <unity.h>

#include "tag_presence_filter.h"

using namespace gridopoly::tile::tag_presence;

void setUp() {}
void tearDown() {}

void test_one_miss_does_not_remove_a_confirmed_tag() {
  const std::uint8_t evidence = updateMissingEvidence(0U, false);
  TEST_ASSERT_EQUAL_UINT8(2U, evidence);
  TEST_ASSERT_FALSE(removalConfirmed(evidence));
}

void test_two_consecutive_misses_remove_without_a_time_grace() {
  std::uint8_t evidence = updateMissingEvidence(0U, false);
  evidence = updateMissingEvidence(evidence, false);
  TEST_ASSERT_EQUAL_UINT8(kMissingEvidenceThreshold, evidence);
  TEST_ASSERT_TRUE(removalConfirmed(evidence));
}

void test_one_fringe_read_does_not_restart_removal_confirmation() {
  std::uint8_t evidence = updateMissingEvidence(0U, false);
  evidence = updateMissingEvidence(evidence, true);
  TEST_ASSERT_EQUAL_UINT8(1U, evidence);
  evidence = updateMissingEvidence(evidence, false);
  TEST_ASSERT_TRUE(removalConfirmed(evidence));
}

void test_stable_presence_gradually_clears_old_missing_evidence() {
  std::uint8_t evidence = updateMissingEvidence(0U, false);
  evidence = updateMissingEvidence(evidence, true);
  TEST_ASSERT_EQUAL_UINT8(1U, evidence);
  evidence = updateMissingEvidence(evidence, true);
  TEST_ASSERT_EQUAL_UINT8(0U, evidence);
  TEST_ASSERT_FALSE(removalConfirmed(evidence));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_one_miss_does_not_remove_a_confirmed_tag);
  RUN_TEST(test_two_consecutive_misses_remove_without_a_time_grace);
  RUN_TEST(test_one_fringe_read_does_not_restart_removal_confirmation);
  RUN_TEST(test_stable_presence_gradually_clears_old_missing_evidence);
  return UNITY_END();
}
