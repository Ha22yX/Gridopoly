#pragma once

#include <cstdint>

namespace tile_display_test::board {

inline constexpr int kLcdResetPin = 4;
inline constexpr int kLcdDataCommandPin = 5;
inline constexpr int kLcdMosiPin = 6;
inline constexpr int kLcdClockPin = 7;
inline constexpr int kLcdChipSelectPin = 15;
inline constexpr int kLcdBacklightPin = 16;
inline constexpr int kNoMisoPin = -1;

inline constexpr std::uint16_t kDisplayWidth = 240;
inline constexpr std::uint16_t kDisplayHeight = 320;
inline constexpr std::uint8_t kDisplayRotation = 0;
inline constexpr std::uint8_t kSpiMode = 0;
inline constexpr std::uint32_t kSpiFrequencyHz = 20'000'000;
inline constexpr std::uint32_t kSerialBaud = 115'200;

inline constexpr std::uint32_t kBacklightPwmFrequencyHz = 5'000;
inline constexpr std::uint8_t kBacklightPwmResolutionBits = 8;
inline constexpr std::uint8_t kBacklightTestDuty = 160;
inline constexpr std::uint8_t kBacklightRampMaximumDuty = 224;

}  // namespace tile_display_test::board
