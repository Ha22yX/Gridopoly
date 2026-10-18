#pragma once

#include <algorithm>
#include <cstdint>
#include "board_config.h"
#include "tile_connection_view.h"
#include "tile_network.h"
#include "ws2812_led.h"

namespace gridopoly::tile::led_scene {

inline constexpr std::uint32_t kFrameMs = 20;
inline constexpr std::uint32_t kDestinationPeriodMs = 2400;

// Value-only input: the LED owner never reads mutable network/display/RFID state.
struct Input {
  bool assigned = false, occupied = false;
  TileNetworkLink link = TileNetworkLink::Starting;
  Rgb ring_color{};
  TileMovementCue movement = TileMovementCue::None;
  std::uint32_t movement_started_ms = 0, pairing_started_ms = 0;
  TilePairingSignal pairing{};
};

inline Rgb scale(Rgb color, std::uint8_t level) {
  constexpr std::uint16_t divisor = 255U * board::kLedSceneScaleDivisor;
  return {static_cast<std::uint8_t>(color.red * level / divisor),
          static_cast<std::uint8_t>(color.green * level / divisor),
          static_cast<std::uint8_t>(color.blue * level / divisor)};
}

inline std::uint16_t destinationFraction(std::uint32_t elapsed) {
  // Symmetric smoothstep: zero slope at both ends, no off interval. Keeping
  // the intermediate value in Q10 avoids extra quantization before RGB scaling.
  const auto phase = elapsed % kDestinationPeriodMs;
  const auto half = kDestinationPeriodMs / 2U;
  const auto distance = phase <= half ? phase : kDestinationPeriodMs - phase;
  const auto x = distance * 1024U / half;
  const auto eased = x * x * (3072U - 2U * x) / (1024U * 1024U);
  return static_cast<std::uint16_t>(eased);
}

inline Rgb destinationColor(std::uint32_t elapsed) {
  // Interpolate final wire amplitudes, not an already rounded 8-bit level.
  // Endpoints retain V0.35's quarter scaling and stay visibly green at the low
  // end: {1,3,2} .. {5,15,12}. No temporal dithering or off/on flashes.
  const auto low = scale({82, 220, 183}, 16), high = scale({82, 220, 183}, 72);
  const auto fraction = destinationFraction(elapsed);
  const auto channel = [fraction](std::uint8_t a, std::uint8_t b) {
    return static_cast<std::uint8_t>(a + ((b - a) * fraction + 512U) / 1024U);
  };
  return {channel(low.red, high.red), channel(low.green, high.green), channel(low.blue, high.blue)};
}

inline Rgb color(const Input &input, std::uint32_t now) {
  if (pairingVisible(input.pairing, now)) {
    const auto state = input.pairing.state;
    const bool success = state == TilePairingState::SuccessRemove || state == TilePairingState::Complete;
    const bool waiting = state == TilePairingState::AwaitingTag || state == TilePairingState::WaitingReader;
    const Rgb tint = success ? Rgb{50, 230, 100} :
                     waiting ? Rgb{70, 110, 255} : Rgb{255, 110, 35};
    const auto phase = (now - input.pairing_started_ms) % 1400U;
    const auto level = success ? 60U :
        (phase < 700U ? 20U + phase * 40U / 700U : 60U - (phase - 700U) * 40U / 700U);
    return scale(tint, static_cast<std::uint8_t>(level));
  }
  if (!input.assigned) {
    const auto phase = now % 1600U;
    const auto level = phase < 800U ? 12U + phase * 24U / 800U : 36U - (phase - 800U) * 24U / 800U;
    return scale(tileConnectionView(input.link).accent, static_cast<std::uint8_t>(level));
  }
  const auto elapsed = static_cast<std::uint32_t>(now - input.movement_started_ms);
  if (input.movement == TileMovementCue::Destination) {
    return destinationColor(elapsed);
  }
  if (input.movement == TileMovementCue::Departure) {
    const auto phase = elapsed % 1600U;
    const auto level = phase < 800U ? 16U + phase * 48U / 800U : 64U - (phase - 800U) * 48U / 800U;
    return scale({239, 140, 74}, static_cast<std::uint8_t>(level));
  }
  const auto phase = now % 1200U;
  const auto level = input.occupied ?
      (phase < 600U ? 28U + phase * 28U / 600U : 56U - (phase - 600U) * 28U / 600U) : 18U;
  return scale(input.ring_color, static_cast<std::uint8_t>(level));
}

template <typename Transmitter, std::size_t Count>
bool sendPixels(Transmitter &transmitter, Rgb (&pixels)[Count]) {
  // Last boundary, shared by every scene and maintenance command. Idempotent.
  for (auto &pixel : pixels) {
    pixel.red = std::min(pixel.red, board::kMaximumLedChannel);
    pixel.green = std::min(pixel.green, board::kMaximumLedChannel);
    pixel.blue = std::min(pixel.blue, board::kMaximumLedChannel);
  }
  return transmitter.send(pixels);
}

// Owned solely by the LED task; producer stalls do not freeze an animation or
// prolong a maintenance window. No I/O, allocations, locks or borrowed pointers.
class Animator {
 public:
  void update(const Input &input) { input_ = input; }
  bool command(const ws2812::Command &command, std::uint32_t issued_ms) {
    return maintenance_.apply(command, issued_ms);
  }
  Rgb sample(std::uint32_t now) {
    Rgb output{};
    if (maintenance_.color(now, output)) return output;
    const auto mode = maintenance_.mode(now);
    if (mode == ws2812::Mode::Destination || mode == ws2812::Mode::Departure) {
      Input preview{};
      preview.assigned = true;
      preview.movement = mode == ws2812::Mode::Destination ?
          TileMovementCue::Destination : TileMovementCue::Departure;
      preview.movement_started_ms = now - maintenance_.elapsed(now);
      return color(preview, now);
    }
    return color(input_, now);
  }
  ws2812::Mode mode(std::uint32_t now) const { return maintenance_.mode(now); }
  std::uint32_t remaining(std::uint32_t now) const { return maintenance_.remaining(now); }
 private:
  Input input_{};
  ws2812::Maintenance maintenance_{};
};

}  // namespace gridopoly::tile::led_scene
