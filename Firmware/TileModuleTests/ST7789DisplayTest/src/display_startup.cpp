#include "display_startup.h"

#include "board_pins.h"

namespace tile_display_test {

void initializeDisplay(DisplayStartupPort &port) {
  using namespace board;

  port.setSafeOutputs();
  port.beginSerial(kSerialBaud);
  port.beginSpi(kLcdClockPin, kNoMisoPin, kLcdMosiPin,
                kLcdChipSelectPin);
  port.setDisplaySpiSpeed(kSpiFrequencyHz);
  port.initializePanel(kDisplayWidth, kDisplayHeight, kSpiMode);
  port.setRotation(kDisplayRotation);
  port.clearBlack();
  port.attachBacklightPwm(kLcdBacklightPin, kBacklightPwmFrequencyHz,
                          kBacklightPwmResolutionBits);
  port.fadeBacklightTo(kBacklightTestDuty);
}

}  // namespace tile_display_test
