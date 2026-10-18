#pragma once

#include <algorithm>
#include <cstdint>
#include "board_config.h"
#include "tile_connection_view.h"
#include "tile_network.h"
#include "ws2812_led.h"

namespace gridopoly::tile::led_scene {

inline constexpr std::uint32_t kFrameMs = 20;
inline constexpr std::uint32_t kMeteorPeriodMs = 2000;
inline constexpr std::uint8_t kMeteorMaximumChannel = 32;
enum class FrameKind : std::uint8_t { Ordinary, Meteor };
inline std::uint8_t channelLimit(FrameKind kind) {
  return kind == FrameKind::Meteor ? kMeteorMaximumChannel : board::kMaximumLedChannel;
}

// Value-only input: the LED owner never reads mutable network/display/RFID state.
struct Input {
  bool assigned = false, occupied = false;
  TileNetworkLink link = TileNetworkLink::Starting;
  Rgb ring_color{};
  TileMovementCue movement = TileMovementCue::None;
  std::uint32_t movement_started_ms = 0, pairing_started_ms = 0;
  TilePairingSignal pairing{};
};

inline bool pairingContextChanged(const TilePairingSignal &next,
                                  const TilePairingSignal &previous,
                                  std::uint32_t now) {
  // Revision/name/lease refreshes are not a new cue. A renewed lease after a
  // real expiry starts afresh even if the producer missed the expiry frame.
  return next.active != previous.active || next.state != previous.state ||
         next.room_id != previous.room_id || next.session_id != previous.session_id ||
         next.player_id != previous.player_id ||
         (pairingVisible(next, now) && !pairingVisible(previous, now));
}

inline Rgb scale(Rgb color, std::uint8_t level) {
  constexpr std::uint16_t divisor = 255U * board::kLedSceneScaleDivisor;
  return {static_cast<std::uint8_t>(color.red * level / divisor),
          static_cast<std::uint8_t>(color.green * level / divisor),
          static_cast<std::uint8_t>(color.blue * level / divisor)};
}

inline Rgb ordinaryColor(const Input &input, std::uint32_t now) {
  if (!input.assigned) {
    const auto phase = now % 1600U;
    const auto level = phase < 800U ? 12U + phase * 24U / 800U : 36U - (phase - 800U) * 24U / 800U;
    return scale(tileConnectionView(input.link).accent, static_cast<std::uint8_t>(level));
  }
  const auto phase = now % 1200U;
  const auto level = input.occupied ?
      (phase < 600U ? 28U + phase * 28U / 600U : 56U - (phase - 600U) * 28U / 600U) : 18U;
  return scale(input.ring_color, static_cast<std::uint8_t>(level));
}

inline Rgb pairingHead(TilePairingState state) {
  if (state == TilePairingState::SuccessRemove || state == TilePairingState::Complete)
    return {7, 32, 14};
  if (state == TilePairingState::AwaitingTag || state == TilePairingState::WaitingReader)
    return {9, 14, 32};
  return {32, 14, 4};
}

template <std::size_t Count>
void meteor(Rgb (&pixels)[Count], Rgb head, std::uint32_t elapsed) {
  static_assert(Count > 0 && Count <= 256, "Bounded, nonempty LED ring required");
  // Interpolate two adjacent whole spatial frames, including the leading edge.
  // The head crosses between LEDs rather than jumping. A quadratic tail
  // occupies 40% of the ring, reaches zero smoothly, and wraps at pixel zero.
  constexpr std::size_t tail = std::max<std::size_t>(1U, (Count * 2U + 4U) / 5U);
  const auto progress = (elapsed % kMeteorPeriodMs) * Count;
  const auto index = progress / kMeteorPeriodMs;
  const auto fraction = progress % kMeteorPeriodMs;
  const auto weight = [tail](std::size_t distance) -> std::uint32_t {
    if (distance >= tail) return 0;
    const auto remaining = tail - distance;
    return static_cast<std::uint32_t>(remaining * remaining * 4096U / (tail * tail));
  };
  for (std::size_t i = 0; i < Count; ++i) {
    const auto current = weight((index + Count - i) % Count);
    const auto next = weight((index + 1U + Count - i) % Count);
    const auto intensity = (current * (kMeteorPeriodMs - fraction) + next * fraction) / kMeteorPeriodMs;
    const auto channel = [intensity](std::uint8_t value) {
      return static_cast<std::uint8_t>((value * intensity + 2048U) / 4096U);
    };
    pixels[i] = {channel(head.red), channel(head.green), channel(head.blue)};
  }
}

template <std::size_t Count>
FrameKind render(const Input &input, std::uint32_t now, Rgb (&pixels)[Count]) {
  if (pairingVisible(input.pairing, now)) {
    meteor(pixels, pairingHead(input.pairing.state), now - input.pairing_started_ms);
    return FrameKind::Meteor;
  }
  if (input.assigned && input.movement != TileMovementCue::None) {
    meteor(pixels, input.movement == TileMovementCue::Destination ? Rgb{12,32,26} : Rgb{32,19,10},
           now - input.movement_started_ms);
    return FrameKind::Meteor;
  }
  const auto color = ordinaryColor(input, now);
  for (auto &pixel : pixels) pixel = color;
  return FrameKind::Ordinary;
}

template <typename Transmitter, std::size_t Count>
bool sendPixels(Transmitter &transmitter, Rgb (&pixels)[Count], FrameKind kind = FrameKind::Ordinary) {
  // The currently rendered frame determines the final ceiling. Expired cues
  // and maintenance pure colors cannot inherit the brighter meteor allowance.
  const auto limit = channelLimit(kind);
  for (auto &pixel : pixels) {
    pixel.red = std::min(pixel.red, limit);
    pixel.green = std::min(pixel.green, limit);
    pixel.blue = std::min(pixel.blue, limit);
  }
  return transmitter.send(pixels);
}

// Sole LED-task owner. No I/O, allocations, locks or borrowed pointers.
class Animator {
 public:
  void update(const Input &input) { input_ = input; }
  bool command(const ws2812::Command &command, std::uint32_t issued_ms) {
    return maintenance_.apply(command, issued_ms);
  }
  template <std::size_t Count>
  FrameKind render(std::uint32_t now, Rgb (&pixels)[Count]) {
    Rgb output{};
    if (maintenance_.color(now, output)) {
      for (auto &pixel : pixels) pixel = output;
      return FrameKind::Ordinary;
    }
    const auto mode = maintenance_.mode(now);
    if (ws2812::isCuePreview(mode)) {
      Input preview{};
      preview.assigned = true;
      preview.movement_started_ms = preview.pairing_started_ms = now - maintenance_.elapsed(now);
      if (mode == ws2812::Mode::Destination || mode == ws2812::Mode::Departure) {
        preview.movement = mode == ws2812::Mode::Destination ? TileMovementCue::Destination : TileMovementCue::Departure;
      } else {
        preview.pairing = {true, mode == ws2812::Mode::Pairing ? TilePairingState::AwaitingTag :
            mode == ws2812::Mode::PairingSuccess ? TilePairingState::SuccessRemove : TilePairingState::ClearRequired,
            1, 1, 1, 30000, now, 1, {}};
      }
      return led_scene::render(preview, now, pixels);
    }
    return led_scene::render(input_, now, pixels);
  }
  ws2812::Mode mode(std::uint32_t now) const { return maintenance_.mode(now); }
  std::uint32_t remaining(std::uint32_t now) const { return maintenance_.remaining(now); }
 private:
  Input input_{};
  ws2812::Maintenance maintenance_{};
};

}  // namespace gridopoly::tile::led_scene
