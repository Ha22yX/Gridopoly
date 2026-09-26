#include <unity.h>
#include "module_profile.h"
#include "tile_pairing_json.h"
using namespace gridopoly::tile;
void setUp() {}
void tearDown() {}
void cue_contract_and_legacy_clear() {
  JsonDocument doc;
  const char *json = R"({"active":true,"roomId":5,"sessionId":7,"revision":9,"state":"success_remove","playerId":2,"playerName":"Alice","leaseMs":5000})";
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  TilePairingSignal signal;
  TEST_ASSERT_TRUE(parsePairingJson(doc.as<JsonVariantConst>(), 100, signal));
  TEST_ASSERT_TRUE(pairingVisible(signal, 5099));
  TEST_ASSERT_FALSE(pairingVisible(signal, 5100));
  TEST_ASSERT_EQUAL_STRING("Alice", signal.player_name);
  TEST_ASSERT_EQUAL(TilePairingState::SuccessRemove, signal.state);
  doc["active"] = false;
  TEST_ASSERT_TRUE(parsePairingJson(doc.as<JsonVariantConst>(), 200, signal));
  TEST_ASSERT_FALSE(signal.active);
  TEST_ASSERT_EQUAL_UINT32(0, signal.session_id);
  signal.active = true;
  TEST_ASSERT_TRUE(parsePairingJson(JsonVariantConst{}, 300, signal));
  TEST_ASSERT_FALSE(signal.active);
}
void malformed_cues_are_atomic_and_roles_explicit() {
  const char *base = R"({"active":true,"roomId":5,"sessionId":7,"revision":9,"state":"awaiting_tag","playerId":2,"playerName":"Alice","leaseMs":5000})";
  for (const char *field : {"roomId", "sessionId", "revision", "leaseMs", "playerId"}) {
    for (const char *bad : {"-1", "4294967296", "1.5", "\"2\"", "true"}) {
      JsonDocument doc, value;
      deserializeJson(doc, base); deserializeJson(value, bad);
      doc[field] = value.as<JsonVariantConst>();
      TilePairingSignal signal; signal.session_id = 888;
      TEST_ASSERT_FALSE(parsePairingJson(doc.as<JsonVariantConst>(), 0, signal));
      TEST_ASSERT_EQUAL_UINT32(888, signal.session_id);
    }
  }
  JsonDocument doc; deserializeJson(doc, base); doc["state"] = "future_state";
  TilePairingSignal signal;
  TEST_ASSERT_FALSE(parsePairingJson(doc.as<JsonVariantConst>(), 0, signal));
  TEST_ASSERT_EQUAL_STRING("unknown", moduleRoleLabel(ModuleRole::Unknown));
  TEST_ASSERT_EQUAL_STRING("long", moduleRoleLabel(ModuleRole::Long));
  TEST_ASSERT_EQUAL_STRING("corner", moduleRoleLabel(ModuleRole::Corner));
  TEST_ASSERT_EQUAL(ModuleRole::Unknown, kModuleRole);
}
int main() {
  UNITY_BEGIN(); RUN_TEST(cue_contract_and_legacy_clear);
  RUN_TEST(malformed_cues_are_atomic_and_roles_explicit); return UNITY_END();
}
