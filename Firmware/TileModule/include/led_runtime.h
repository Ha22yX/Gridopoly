#pragma once
#include "led_scene.h"

namespace gridopoly::tile {
struct LedRuntimeStatus {
  bool worker = false;
  ws2812::SendStatus output{};
  ws2812::Mode mode = ws2812::Mode::Scene;
  std::uint32_t remaining_ms = 0, frames = 0, late_frames = 0, max_gap_ms = 0;
  Rgb requested{};
};
bool beginLedRuntime();
bool submitLedScene(const led_scene::Input &input);
bool submitLedCommand(const ws2812::Command &command, std::uint32_t issued_ms);
LedRuntimeStatus ledRuntimeStatus();
}  // namespace gridopoly::tile
