#pragma once

#include <Arduino.h>

namespace gridopoly::tile_bring_up {

inline constexpr int kLcdResetPin = 4;
inline constexpr int kLcdDataCommandPin = 5;
inline constexpr int kLcdMosiPin = 6;
inline constexpr int kLcdClockPin = 7;
inline constexpr int kLcdChipSelectPin = 15;
inline constexpr int kLcdBacklightPin = 16;

inline constexpr int kCurrentSclPin = 11;
inline constexpr int kCurrentSdaPin = 12;
inline constexpr uint8_t kIna226Address = 0x40;

inline constexpr int kWs2812DataPin = 21;
inline constexpr size_t kWs2812Count = 10;

inline constexpr uint16_t kDisplayWidth = 240;
inline constexpr uint16_t kDisplayHeight = 320;
inline constexpr uint32_t kDisplaySpiFrequencyHz = 20000000;
inline constexpr uint32_t kSerialBaud = 115200;

}  // namespace gridopoly::tile_bring_up
