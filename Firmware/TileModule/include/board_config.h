#pragma once

#include <cstddef>
#include <cstdint>

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
// 8 MHz keeps margin on the ribbon/connector while remaining fast enough for
// a complete 240x320 page transition.
inline constexpr std::uint32_t kDisplaySpiFrequencyHz = 8'000'000;
inline constexpr std::uint32_t kSerialBaud = 115'200;
inline constexpr std::uint8_t kBacklightDuty = 150;
inline constexpr std::uint8_t kMaximumLedChannel = 64;

}  // namespace gridopoly::tile::board
