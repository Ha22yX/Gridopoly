# HTRC110 guarded coil test

This is an isolated PlatformIO + Arduino diagnostic for the Gridopoly TileModule
HTRC110 antenna connector. It is not linked into the production firmware.

## Safety contract

- The field is forced off (`Config Page 1 TXDIS=1`) before Serial, I2C, LCD,
  LEDs, or Wi-Fi initialization.
- The LCD backlight, RS485 transmitter, and ORDER output stay off. A real all-
  black WS2812 frame clears colors that may have remained latched across upload.
- There is no continuous-field command and no tag-write command.
- `TEST` and `SCAN` use guarded short field windows, each limited to 350 ms.
- A digital-interface error, HTRC110 `ANTFAIL`, invalid INA226 sample, bus below
  4.65 V, whole-board current above 350 mA, or increase above 120 mA ends the
  pulse immediately.
- The firmware reasserts `TXDIS=1` every 500 ms while idle.

The INA226 measures total board input current. It cannot measure or certify the
HTRC110 antenna peak current. A passing test also does not certify resonance at
125 kHz; final tuning still requires the measured coil L/DCR/Q, a current
measurement, and a suitable high-voltage/low-capacitance oscilloscope probe.

## Wiring

The external coil must be connected only across J3:

- J3 pin 1: `ANT_COIL_A`
- J3 pin 2: `ANT_TAP_HV`

Neither coil lead connects to ground, 5 V, or an ESP32 GPIO. Do not touch or
probe `ANT_TAP_HV` with an ordinary low-voltage probe while the field is active.

## Commands

```text
STATUS
TEST
SCAN
FIELD OFF
HELP
```

`TEST` reports:

- HTRC110 digital-interface status
- `ANTFAIL`
- 16 phase samples and derived sampling-time register
- whole-board current with the field off/on and their delta
- minimum 5 V bus voltage during the pulse

`SCAN` sends the HITAG S Advanced UID REQUEST (`11001`), enters transparent
`READ_TAG`, decodes the AC2K response, sweeps three sampling phases, and requires
three matching responses before publishing the 32-bit UID. Every attempt checks
`ANTFAIL`, INA226 limits, capture overflow, field duration, and verified field-off
readback. The command reads the immutable UID only; it does not issue a tag-memory
write command.

## Build and upload

```powershell
pio run -d Firmware/TileModuleTests/HTRC110CoilTest
pio run -d Firmware/TileModuleTests/HTRC110CoilTest -t upload --upload-port COM12
pio device monitor --port COM12 --baud 115200 --dtr 0 --rts 0
```

