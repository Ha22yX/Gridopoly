#pragma once

#include <Arduino.h>

namespace gridopoly::tile_display_test {

inline constexpr int kBacklightPin = 16;
inline constexpr int kChipSelectPin = 15;
inline constexpr int kClockPin = 7;
inline constexpr int kMosiPin = 6;
inline constexpr int kDataCommandPin = 5;
inline constexpr int kResetPin = 4;

inline constexpr uint16_t kDisplayWidth = 240;
inline constexpr uint16_t kDisplayHeight = 320;
inline constexpr uint32_t kSpiFrequencyHz = 20000000;
inline constexpr uint32_t kSerialBaud = 115200;

}  // namespace gridopoly::tile_display_test

