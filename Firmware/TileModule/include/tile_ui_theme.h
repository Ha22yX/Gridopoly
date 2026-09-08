#pragma once

#include <cstdint>

namespace gridopoly::tile::theme {

// Grid City visual tokens shared by convention with PlayerConsole.
// These are host-order RGB565 values; Adafruit_ST7789 performs wire byte order.
constexpr std::uint16_t kBackground = 0x0862;  // #090E10
constexpr std::uint16_t kPanel = 0x10C3;       // #11191B
constexpr std::uint16_t kSelected = 0x1185;    // #16302A
constexpr std::uint16_t kLine = 0x2186;        // #263234
constexpr std::uint16_t kText = 0xEF9E;        // #EDF3F1
constexpr std::uint16_t kMuted = 0x8491;       // #81908C
constexpr std::uint16_t kGreen = 0x56F6;       // #52DCB7
constexpr std::uint16_t kYellow = 0xF62A;      // #F2C453
constexpr std::uint16_t kRed = 0xEB8D;         // #EF7168
constexpr std::uint16_t kBlue = 0x5D3D;        // #58A7EB

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue) {
  return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) |
                                    ((green & 0xFCU) << 3U) |
                                    (blue >> 3U));
}

struct Rgb888 {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

// seatColorId 1..6 / P1..P6, matching the current PlayerConsole UI.
constexpr Rgb888 kPlayerColors[6] = {
    {88, 167, 235}, {239, 113, 104}, {82, 220, 183},
    {242, 196, 83}, {194, 138, 232}, {234, 138, 85},
};

constexpr std::int16_t kOuterMargin = 16;
constexpr std::int16_t kArtworkRadius = 8;
constexpr std::int16_t kCardRadius = 6;

}  // namespace gridopoly::tile::theme
