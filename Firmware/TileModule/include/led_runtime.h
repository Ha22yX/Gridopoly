#pragma once
#include "led_scene.h"

namespace gridopoly::tile {
struct LedRuntimeStatus {
  bool worker = false;
  ws2812::SendStatus output{};
  ws2812::Mode mode = ws2812::Mode::Scene;
  std::uint32_t remaining_ms = 0, frames = 0, late_frames = 0, max_gap_ms = 0;
  Rgb requested{};
  led_scene::FrameKind pattern = led_scene::FrameKind::Ordinary;
  Rgb pixels[board::kWs2812Count]{};
  std::uint8_t peak_index = 0, peak_channel = 0;
};
bool beginLedRuntime();
bool submitLedScene(const led_scene::Input &input);
bool submitLedCommand(const ws2812::Command &command, std::uint32_t issued_ms);
LedRuntimeStatus ledRuntimeStatus();
}  // namespace gridopoly::tile
