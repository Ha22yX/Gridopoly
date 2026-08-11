#pragma once

#include <cstdint>

namespace tile_display_test {

class DisplayStartupPort {
 public:
  virtual ~DisplayStartupPort() = default;

  virtual void setSafeOutputs() = 0;
  virtual void beginSerial(std::uint32_t baud) = 0;
  virtual void beginSpi(int clock_pin, int miso_pin, int mosi_pin,
                        int chip_select_pin) = 0;
  virtual void setDisplaySpiSpeed(std::uint32_t frequency_hz) = 0;
  virtual void initializePanel(std::uint16_t width, std::uint16_t height,
                               std::uint8_t spi_mode) = 0;
  virtual void setRotation(std::uint8_t rotation) = 0;
  virtual void clearBlack() = 0;
  virtual void attachBacklightPwm(int pin, std::uint32_t frequency_hz,
                                  std::uint8_t resolution_bits) = 0;
  virtual void fadeBacklightTo(std::uint8_t duty) = 0;
};

void initializeDisplay(DisplayStartupPort &port);

}  // namespace tile_display_test
