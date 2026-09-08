# Gridopoly Tile Board Bring-Up Test

Combined single-board test for the first Gridopoly tile PCB.

## Covered hardware

- ST7789 240 x 320 display and PWM backlight.
- INA226 at I2C address `0x40` on SDA GPIO12 / SCL GPIO11.
- Ten WS2812B LEDs on GPIO21, physically documented as U12 through U21.

## WS2812 sequence

1. Only one LED is white at a time, from U12 through U21.
2. All LEDs show red, green, and blue at 25% channel brightness.
3. All LEDs show white briefly at 12% brightness.
4. All LEDs ramp white between 0% and 20% brightness.
5. All LEDs turn off and the sequence repeats.

The screen identifies the expected physical LED for the locate stage. Compare the
screen label with the actual illuminated LED and record the PCB positions.

## INA226 dashboard

The firmware scans address `0x40`, checks the TI manufacturer and die IDs, writes
configuration `0x0527`, and writes calibration `0x1400`. It then displays:

- `5V BUS` in volts, with a warning color outside 4.75 to 5.25 V.
- Current in mA using 100 uA/bit.
- Power in watts using 2.5 mW/bit.
- Peak measured current since boot.

The 10 mOhm shunt and calibration values match the current hardware baseline.

## Safety

The default animation is suitable for initial USB testing: RGB stages are limited
to 25%, white to 12%, and the all-LED white ramp to 20%. Do not convert this test to
full-brightness white while powered by an unverified computer USB port.

For calibration, compare the dashboard with a trusted meter or electronic load at
0.1 A, 0.5 A, and 1.0 A. The higher-current points require an appropriate external
power source, not a computer USB port.
