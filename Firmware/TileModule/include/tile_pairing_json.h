#pragma once
#include <ArduinoJson.h>
#include "tile_pairing.h"
#include "tile_model.h"

namespace gridopoly::tile {
inline bool parsePairingJson(JsonVariantConst value, std::uint32_t now,
                             TilePairingSignal &result) {
  TilePairingSignal signal{};
  if (value.isNull()) { result = signal; return true; }
  if (!value.is<JsonObjectConst>() || !value["active"].is<bool>()) return false;
  if (!value["active"].as<bool>()) { result = signal; return true; }
  if (!value["roomId"].is<std::uint32_t>() ||
      !value["sessionId"].is<std::uint32_t>() ||
      !value["revision"].is<std::uint32_t>() ||
      !value["leaseMs"].is<std::uint32_t>() ||
      !value["playerId"].is<std::uint8_t>() ||
      !value["playerName"].is<const char *>() ||
      !parseTilePairingState(value["state"] | "", signal.state)) return false;
  signal.active = true;
  signal.room_id = value["roomId"].as<std::uint32_t>();
  signal.session_id = value["sessionId"].as<std::uint32_t>();
  signal.revision = value["revision"].as<std::uint32_t>();
  signal.lease_ms = value["leaseMs"].as<std::uint32_t>();
  signal.player_id = value["playerId"].as<std::uint8_t>();
  signal.received_ms = now;
  if (signal.room_id == 0 || signal.session_id == 0 || signal.lease_ms == 0 ||
      signal.lease_ms > 60000 || signal.player_id > kMaximumPlayers) return false;
  std::strncpy(signal.player_name, value["playerName"].as<const char *>(),
               sizeof(signal.player_name) - 1);
  result = signal;
  return true;
}
}  // namespace gridopoly::tile
