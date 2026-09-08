# Gridopoly Tile ST7789 Display Test

Standalone bring-up firmware for the new Gridopoly tile PCB.

## Hardware

| Signal | ESP32-S3 GPIO | J4 pin |
| --- | ---: | ---: |
| GND | - | 1 |
| 3V3 | - | 2 |
| LCD backlight | 16 | 3 |
| LCD chip select | 15 | 4 |
| LCD clock | 7 | 5 |
| LCD MOSI | 6 | 6 |
| LCD data/command | 5 | 7 |
| LCD reset | 4 | 8 |

The panel is a write-only 240 x 320 ST7789. The test begins at 20 MHz SPI.

## Visual sequence

1. Solid red, green, blue, white, and black fields.
2. RGB/CMY vertical bars.
3. Outer borders, center crosshair, and unique corner colors.
4. Grid and grayscale blocks.
5. Animated checker pattern.
6. Backlight brightness ramp.

The sequence repeats automatically. Serial diagnostics use 115200 baud.

