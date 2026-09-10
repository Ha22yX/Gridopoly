#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if __has_include("../config/display.local.h")
#include "../config/display.local.h"
#endif
#ifndef GRIDOPOLY_TILE_DISPLAY_SPI_HZ
#define GRIDOPOLY_TILE_DISPLAY_SPI_HZ 8000000
#endif
#ifndef GRIDOPOLY_TILE_DISPLAY_VALIDATED_DEVICE
#define GRIDOPOLY_TILE_DISPLAY_VALIDATED_DEVICE ""
#endif

namespace gridopoly::tile::board {

inline constexpr int kLcdResetPin = 4;
inline constexpr int kLcdDataCommandPin = 5;
inline constexpr int kLcdMosiPin = 6;
inline constexpr int kLcdClockPin = 7;
inline constexpr int kLcdChipSelectPin = 15;
inline constexpr int kLcdBacklightPin = 16;
inline constexpr int kNoMisoPin = -1;

inline constexpr int kRfidDataOutPin = 8;
inline constexpr int kOrderInPin = 9;
inline constexpr int kOrderOutPin = 10;
inline constexpr int kCurrentSclPin = 11;
inline constexpr int kCurrentSdaPin = 12;
inline constexpr int kRs485DirectionPin = 14;
inline constexpr int kRfidClockPin = 17;
inline constexpr int kRfidDataInPin = 18;
inline constexpr int kWs2812DataPin = 21;
inline constexpr int kRs485TxPin = 43;
inline constexpr int kRs485RxPin = 44;

inline constexpr std::uint8_t kIna226Address = 0x40;
inline constexpr std::size_t kWs2812Count = 10;
inline constexpr std::uint16_t kDisplayWidth = 240;
inline constexpr std::uint16_t kDisplayHeight = 320;
// The first board is not reliable at 20 MHz once the Wi-Fi radio is active.
// Keep that fallback independent of an explicitly validated local override.
// V0.30's internal-RAM transfer at 40 MHz passed visual checks on one module;
// it is outside the generic ST7789V timing guarantee, not a fleet default.
inline constexpr std::uint32_t kDisplaySafeSpiFrequencyHz = 8'000'000;
inline constexpr std::uint32_t kDisplaySpiFrequencyHz = GRIDOPOLY_TILE_DISPLAY_SPI_HZ;
static_assert(kDisplaySpiFrequencyHz == 8'000'000 ||
              kDisplaySpiFrequencyHz == 40'000'000,
              "Only the safe rate or the locally validated rate is supported");
inline std::uint32_t displaySpiFrequencyForDevice(const char *device_id) {
  return device_id != nullptr && device_id[0] != '\0' &&
                 GRIDOPOLY_TILE_DISPLAY_VALIDATED_DEVICE[0] != '\0' &&
                 std::strcmp(device_id, GRIDOPOLY_TILE_DISPLAY_VALIDATED_DEVICE) == 0
             ? kDisplaySpiFrequencyHz : kDisplaySafeSpiFrequencyHz;
}
inline constexpr std::uint32_t kSerialBaud = 115'200;
inline constexpr std::uint8_t kBacklightDuty = 150;
inline constexpr std::uint8_t kMaximumLedChannel = 64;

}  // namespace gridopoly::tile::board
