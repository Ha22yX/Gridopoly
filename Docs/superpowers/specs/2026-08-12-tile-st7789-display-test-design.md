# Tile ST7789 Display Test Design

## Goal

Provide a standalone PlatformIO + Arduino firmware that lets a developer connect the
Gridopoly tile module's 2.0-inch ST7789 display and visually verify all eight J4
connections, panel orientation, visible bounds, color order, SPI signal integrity, and
backlight control before the full tile firmware exists.

## Scope

- Reserve `Firmware/TileModule/` exclusively for the production tile firmware.
- Put hardware bring-up programs under `Firmware/TileModuleTests/`; add this independent
  project at `Firmware/TileModuleTests/ST7789DisplayTest/`.
- Target the ESP32-S3-WROOM-1-N16R8 using PlatformIO's
  `esp32-s3-devkitc-1` board definition and the Arduino framework.
- Use `Adafruit GFX Library` and `Adafruit ST7735 and ST7789 Library` as pinned
  PlatformIO dependencies.
- Drive only the display and USB serial console. Do not initialize RFID, RS485,
  ORDER, WS2812, INA226, Wi-Fi, or game logic.
- Keep every board-level GPIO definition in one header and do not duplicate raw pin
  numbers in application logic.
- Do not include test-only sources from the production firmware project or production
  sources from the test project. Shared code may be extracted later only when a real
  production use case exists.

## Hardware contract

The firmware uses the current J4 connection map:

| Function | ESP32-S3 GPIO | J4 pin |
| --- | ---: | ---: |
| `LCD_BL_PWM` | 16 | 3 |
| `LCD_CS` | 15 | 4 |
| `LCD_SCLK` | 7 | 5 |
| `LCD_MOSI` | 6 | 6 |
| `LCD_DC` | 5 | 7 |
| `LCD_RST` | 4 | 8 |

J4 pin 1 is GND and pin 2 is `3V3_SYS`. The panel is 240 x 320, SPI mode 0,
write-only, and has no MISO connection. Initial validation runs at 20 MHz. The
firmware must not claim that panel presence or pixel contents were electronically
verified because the hardware provides no readback path.

## Approaches considered

1. **Adafruit GFX plus Adafruit ST7789 (selected):** proven ST7789 initialization,
   readable drawing primitives, and low bring-up risk.
2. **TFT_eSPI:** potentially faster, but its global configuration introduces more
   setup surface than this isolated hardware test needs.
3. **Direct SPI commands:** removes external dependencies, but duplicates controller
   initialization and makes panel-variant mistakes more likely.

## Architecture

`board_pins.h` owns the six GPIO assignments and display dimensions.
`display_test_plan.h/.cpp` contains a platform-independent ordered list of diagnostic
stages and stage timing. `main.cpp` owns Arduino setup/loop, the Adafruit display
adapter, drawing functions, serial reporting, and PWM backlight control.

The repository layout is deliberately split by purpose:

```text
Firmware/
  TileModule/                         # future production tile firmware only
  TileModuleTests/
    ST7789DisplayTest/                # standalone display bring-up firmware
      platformio.ini
      include/
      src/
      test/
```

At startup the program establishes safe outputs before starting SPI: backlight low,
CS high, clock low, and MOSI low. It then starts USB serial, initializes the ST7789 at
240 x 320 and 20 MHz, clears the panel while the backlight remains off, and fades the
backlight up. Failure to open a serial monitor must never block the display test.

The main loop advances through the diagnostic stages automatically and prints the
stage number, name, and expected observation to the serial console. Timing uses
elapsed-time comparisons and must not depend on long blocking delays.

## Visual test sequence

1. Show full-screen red, green, blue, white, and black fields to verify data transfer,
   RGB color order, stuck channels, and obvious pixel defects.
2. Show labeled RGB and CMY color bars to make swapped red/blue channels visible.
3. Show a one-pixel outer border, inset rectangles, a center crosshair, and distinct
   TL/TR/BL/BR corner markers to reveal clipping, controller offsets, mirroring, and
   rotation.
4. Show a regular grid and grayscale steps to expose dropped SPI data, unstable
   wiring, and rendering artifacts.
5. Show a diagnostic summary containing controller name, resolution, SPI frequency,
   rotation, GPIO map, and a changing frame counter.
6. Run a bounded backlight ramp, then leave the panel at a moderate brightness before
   restarting the sequence.

The initial orientation is portrait. Rotation remains a single board configuration
constant so it can be changed after observing the real mechanical installation.

## Backlight behavior

- Backlight is off until panel initialization and the first clear complete.
- PWM ramps smoothly from off to the configured test brightness.
- The brightness test exercises a useful visible range without leaving the panel at
  maximum brightness continuously.
- Restart and initialization paths always return the output to a safe off state first.

## Error handling and diagnostics

- USB serial runs at 115200 baud and reports firmware identity, pin mapping, SPI
  settings, and each stage transition.
- The application continues cycling even when no panel is connected, allowing the
  serial log and a logic analyzer to confirm firmware activity.
- Because the interface is write-only, a blank or corrupt panel is diagnosed through
  the displayed pattern, serial stage log, power checks, and signal probing rather
  than a fabricated software pass/fail result.
- PlatformIO must not hard-code a COM port; upload and monitor ports remain selectable
  on the developer machine.

## Verification

- A native PlatformIO test validates the stage order, unique stage identifiers,
  positive durations, wraparound, and human-readable expectations without Arduino or
  display-library dependencies.
- The native test is written and observed failing before the production test-plan
  implementation is added.
- The ESP32-S3 environment must compile successfully with warnings enabled.
- Static checks confirm the authoritative GPIO values, 240 x 320 dimensions, 20 MHz
  initial SPI frequency, and safe-start sequence.
- Physical acceptance requires correct red/green/blue fields, all four corner markers,
  an unbroken outer border, stable grid lines, readable diagnostic text, and a smooth
  backlight ramp on the connected module.

## Non-goals

- No LVGL, images, fonts beyond the Adafruit built-in font, touch input, networking,
  game protocol, or resource caching.
- No automatic panel-presence assertion or pixel readback.
- No optimization beyond stable 20 MHz bring-up operation.
- No integration into the future production tile application in this change.
