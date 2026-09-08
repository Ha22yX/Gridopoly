#include <unity.h>

#include <cstdint>
#include <cstring>

#include "hitag_s_protocol.h"

using namespace gridopoly::tile::hitag_s;

void setUp() {}
void tearDown() {}

void test_hitag_crc8_matches_standard_check_vector() {
  const std::uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX8(0xB4U, crc8HitagBits(data, 72U));
}

void test_ac_command_packs_length_prefix_and_crc_msb_first() {
  UidPrefix prefix;
  prefix.bit_count = 5U;
  const std::uint8_t bits[] = {1U, 0U, 1U, 0U, 1U};
  for (std::size_t bit = 0; bit < 5U; ++bit) {
    setUidBit(prefix.uid, bit, bits[bit]);
  }

  BitBuffer command;
  TEST_ASSERT_TRUE(buildAnticollisionCommand(prefix, command));
  TEST_ASSERT_EQUAL_UINT8(18U, command.bit_count);
  const std::uint8_t expected[] = {0x2DU, 0x4CU, 0x80U};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, command.bytes, 3U);
}

void test_collision_branch_extends_prefix_without_changing_prior_bits() {
  UidPrefix base;
  base.bit_count = 3U;
  setUidBit(base.uid, 0U, 1U);
  setUidBit(base.uid, 1U, 0U);
  setUidBit(base.uid, 2U, 1U);

  Uid response;
  setUidBit(response, 0U, 0U);
  setUidBit(response, 1U, 0U);

  UidPrefix extended;
  TEST_ASSERT_TRUE(extendPrefix(base, response, 2U, 1U, extended));
  TEST_ASSERT_EQUAL_UINT8(6U, extended.bit_count);
  const std::uint8_t expected[] = {1U, 0U, 1U, 0U, 0U, 1U};
  for (std::size_t bit = 0; bit < 6U; ++bit) {
    TEST_ASSERT_EQUAL_UINT8(expected[bit], uidBit(extended.uid, bit));
  }
}

void test_prefix_and_suffix_reconstruct_full_uid() {
  const Uid expected{{0xA5U, 0x5AU, 0xC3U, 0xF1U}};
  UidPrefix prefix;
  prefix.bit_count = 9U;
  for (std::size_t bit = 0; bit < prefix.bit_count; ++bit) {
    setUidBit(prefix.uid, bit, uidBit(expected, bit));
  }
  Uid suffix;
  for (std::size_t bit = prefix.bit_count; bit < 32U; ++bit) {
    setUidBit(suffix, bit - prefix.bit_count, uidBit(expected, bit));
  }

  const Uid combined = combinePrefixAndSuffix(
      prefix, suffix, static_cast<std::uint8_t>(32U - prefix.bit_count));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.wire_bytes, combined.wire_bytes, 4U);
}

void test_uid_set_is_unique_sorted_and_bounded() {
  UidSet set;
  const Uid middle{{0x80U, 0x00U, 0x00U, 0x70U}};
  const Uid first{{0x10U, 0x00U, 0x00U, 0x70U}};
  const Uid last{{0xF0U, 0x00U, 0x00U, 0x70U}};
  TEST_ASSERT_TRUE(addUniqueUid(set, middle));
  TEST_ASSERT_TRUE(addUniqueUid(set, first));
  TEST_ASSERT_TRUE(addUniqueUid(set, last));
  TEST_ASSERT_TRUE(addUniqueUid(set, middle));
  TEST_ASSERT_EQUAL_UINT8(3U, set.count);
  TEST_ASSERT_TRUE(equalUid(set.uids[0], first));
  TEST_ASSERT_TRUE(equalUid(set.uids[1], middle));
  TEST_ASSERT_TRUE(equalUid(set.uids[2], last));
  for (std::uint8_t index = set.count; index < kMaximumInventoryTags; ++index) {
    Uid extra{{index, 0x11U, 0x22U, 0x80U}};
    TEST_ASSERT_TRUE(addUniqueUid(set, extra));
  }
  TEST_ASSERT_EQUAL_UINT8(kMaximumInventoryTags, set.count);
  const Uid overflow{{0x42U, 0x33U, 0x44U, 0x90U}};
  TEST_ASSERT_FALSE(addUniqueUid(set, overflow));
  TEST_ASSERT_TRUE(set.overflow);
  TEST_ASSERT_TRUE(containsUid(set, middle));
  const Uid missing{{0x99U, 0x88U, 0x77U, 0x70U}};
  TEST_ASSERT_FALSE(containsUid(set, missing));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_hitag_crc8_matches_standard_check_vector);
  RUN_TEST(test_ac_command_packs_length_prefix_and_crc_msb_first);
  RUN_TEST(test_collision_branch_extends_prefix_without_changing_prior_bits);
  RUN_TEST(test_prefix_and_suffix_reconstruct_full_uid);
  RUN_TEST(test_uid_set_is_unique_sorted_and_bounded);
  return UNITY_END();
}
