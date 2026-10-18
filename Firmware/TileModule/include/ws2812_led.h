#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "tile_model.h"

namespace gridopoly::tile::ws2812 {

// Worldsemi WS2812B-B-V6, manufacturer datasheet p4 (C52941391).
// 16 MHz = 62.5 ns: 0 = 312.5/937.5 ns; 1 = 625/625 ns.
// All four pulse widths and the 1250 ns bit period meet that exact revision.
inline constexpr std::uint32_t kClockHz = 16'000'000;
inline constexpr unsigned kZeroHigh = 5, kZeroLow = 15;
inline constexpr unsigned kOneHigh = 10, kOneLow = 10;
inline constexpr unsigned kResetUs = 350;  // V6 requires >280 us low.
inline constexpr unsigned kSendTimeoutMs = 20;
inline constexpr std::uint8_t kDiagnosticLevel = 8;

template <typename Symbol>
bool encode(const Rgb *pixels, std::size_t count, Symbol *symbols,
            std::size_t capacity) {
  if (pixels == nullptr || symbols == nullptr || count == 0 ||
      count > capacity / 24U) {
    return false;
  }
  std::size_t index = 0;
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    const std::uint8_t grb[]{pixels[pixel].green, pixels[pixel].red,
                             pixels[pixel].blue};
    for (std::uint8_t value : grb) {
      for (int bit = 7; bit >= 0; --bit) {
        const bool one = (value & (1U << bit)) != 0;
        auto &symbol = symbols[index++];
        symbol.level0 = 1;
        symbol.duration0 = one ? kOneHigh : kZeroHigh;
        symbol.level1 = 0;
        symbol.duration1 = one ? kOneLow : kZeroLow;
      }
    }
  }
  return true;
}

enum class Mode : std::uint8_t { Scene, Off, Red, Green, Blue, Destination, Departure,
                               Pairing, PairingSuccess, PairingClear };
inline bool isCuePreview(Mode mode) {
  return mode == Mode::Destination || mode == Mode::Departure || mode == Mode::Pairing ||
         mode == Mode::PairingSuccess || mode == Mode::PairingClear;
}
inline const char *modeName(Mode mode) {
  switch (mode) {
    case Mode::Off: return "off";
    case Mode::Red: return "red";
    case Mode::Green: return "green";
    case Mode::Blue: return "blue";
    case Mode::Destination: return "destination-preview";
    case Mode::Departure: return "departure-preview";
    case Mode::Pairing: return "pairing-preview";
    case Mode::PairingSuccess: return "pairing-success-preview";
    case Mode::PairingClear: return "pairing-clear-preview";
    default: return "scene";
  }
}
struct Command {
  bool valid = false;
  bool status_only = false;
  Mode mode = Mode::Scene;
  std::uint32_t duration_ms = 0;
};

// Entire command must match. No signs, overflow, trailing garbage or white mode.
inline Command parseCommand(const char *text) {
  if (text == nullptr) return {};
  if (std::strcmp(text, "LED STATUS") == 0) return {true, true, Mode::Scene, 0};
  if (std::strcmp(text, "LED AUTO") == 0) return {true, false, Mode::Scene, 0};
  const char *prefixes[]{"LED OFF ", "LED RED ", "LED GREEN ", "LED BLUE ",
                         "LED DESTINATION ", "LED DEPARTURE ", "LED PAIRING ",
                         "LED PAIRING-SUCCESS ", "LED PAIRING-CLEAR "};
  const Mode modes[]{Mode::Off, Mode::Red, Mode::Green, Mode::Blue, Mode::Destination, Mode::Departure,
                     Mode::Pairing, Mode::PairingSuccess, Mode::PairingClear};
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    const auto length = std::strlen(prefixes[i]);
    if (std::strncmp(text, prefixes[i], length) != 0) continue;
    const char *number = text + length;
    if (*number == '\0') return {};
    unsigned seconds = 0;
    for (; *number != '\0'; ++number) {
      if (*number < '0' || *number > '9' || seconds > 30U) return {};
      seconds = seconds * 10U + static_cast<unsigned>(*number - '0');
    }
    if (seconds < 1U || seconds > 30U) return {};
    return {true, false, modes[i], seconds * 1000U};
  }
  return {};
}

class Maintenance {
 public:
  bool apply(const Command &command, std::uint32_t now) {
    if (!command.valid || command.status_only) return false;
    mode_ = command.mode;
    started_ = now;
    duration_ = command.duration_ms;
    return true;
  }
  std::uint32_t remaining(std::uint32_t now) const {
    const auto elapsed = static_cast<std::uint32_t>(now - started_);
    return mode_ != Mode::Scene && elapsed < duration_ ? duration_ - elapsed : 0;
  }
  Mode mode(std::uint32_t now) const { return remaining(now) ? mode_ : Mode::Scene; }
  std::uint32_t elapsed(std::uint32_t now) const { return now - started_; }
  void expire(std::uint32_t now) {
    if (mode_ != Mode::Scene && remaining(now) == 0) {
      mode_ = Mode::Scene;
      started_ = duration_ = 0;
    }
  }
  bool color(std::uint32_t now, Rgb &out) {
    expire(now);  // Permanently retire the window, including a later millis wrap.
    const Mode current = mode(now);
    if (current == Mode::Scene || isCuePreview(current)) return false;
    out = {0, 0, 0};
    if (current == Mode::Red) out.red = kDiagnosticLevel;
    if (current == Mode::Green) out.green = kDiagnosticLevel;
    if (current == Mode::Blue) out.blue = kDiagnosticLevel;
    return true;
  }
 private:
  Mode mode_ = Mode::Scene;
  std::uint32_t started_ = 0, duration_ = 0;
};

struct SendStatus {
  bool ready = false;
  std::uint32_t clock_hz = 0, attempted = 0, completed = 0, failed = 0;
  int driver_error = 0, stop_error = 0;
  const char *stage = "not_initialized";
};

// The ESP32 backend returns real ESP-IDF errors. Completion only describes the
// peripheral, never proves the physical DIN waveform or light output.
template <typename Driver, std::size_t Count>
class Transmitter {
 public:
  bool begin() {
    status_ = {};
    const int error = driver_.begin(status_.clock_hz);
    if (error != 0) return fail("initialize", error);
    if (status_.clock_hz != kClockHz) return fail("clock_mismatch", 0);
    driver_.holdLow(kResetUs);
    status_.ready = true;
    status_.stage = "ready";
    return true;
  }
  bool send(const Rgb (&pixels)[Count]) {
    if (!status_.ready) return false;
    ++status_.attempted;
    if (!encode(pixels, Count, symbols_, Count * 24U)) return fail("encode", 0);
    int error = driver_.write(symbols_, Count * 24U);
    if (error != 0) return fail("write", error);
    error = driver_.wait(kSendTimeoutMs);
    if (error != 0) return fail("wait", error);
    driver_.holdLow(kResetUs);
    ++status_.completed;
    status_.stage = "completed";
    return true;
  }
  const SendStatus &status() const { return status_; }
 private:
  bool fail(const char *stage, int error) {
    status_.ready = false;  // No automatic retry over an unfinished frame.
    status_.stage = stage;
    status_.driver_error = error;
    ++status_.failed;
    status_.stop_error = driver_.stop();
    return false;
  }
  Driver driver_{};
  typename Driver::Symbol symbols_[Count * 24U]{};
  SendStatus status_{};
};

}  // namespace gridopoly::tile::ws2812
