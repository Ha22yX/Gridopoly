# HTRC110 digital-side test

This sketch validates the HTRC110 interface before an antenna coil is fitted.

## What it tests

- ESP32-S3 GPIO17 -> HTRC110 SCLK through the 74HCT125 level shifter.
- ESP32-S3 GPIO18 -> HTRC110 DIN through the 74HCT125 level shifter.
- HTRC110 DOUT -> ESP32-S3 GPIO8 through the SN74LVC1G17 level shifter.
- Configuration-page write/readback with the coil driver forced off.
- Sampling-time register write/readback using two test patterns.
- The configuration divider selection for the board's 4 MHz oscillator.
- INA226 5 V bus voltage and whole-board current while the test runs.

## Safety contract

- Leave the antenna connector open while using this sketch.
- The sketch writes config page 1 as `0x1` (`PD=0`, `TXDIS=1`) before LCD initialization.
- It reasserts `TXDIS=1` every second.
- It never sends `READ_TAG`, `WRITE_TAG`, or `WRITE_TAG_N`.
- `ANTFAIL` is not treated as a failure because its value is not meaningful with the
  transmitter disabled and no antenna connected.

## What it cannot prove without the coil or instruments

- It cannot read a tag or validate 125 kHz field strength.
- `4MHZ SELECT` confirms the divider configuration, not the oscillator frequency.
  Measure XTAL1/XTAL2 with a high-impedance oscilloscope probe for a direct 4 MHz check.

The sketch deliberately includes the display/INA226/WS2812 primitives from the already
validated `BoardBringUpTest`; LEDs are immediately latched off.
