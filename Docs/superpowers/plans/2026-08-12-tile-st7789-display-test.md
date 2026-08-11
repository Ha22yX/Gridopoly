# Tile ST7789 Display Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone PlatformIO + Arduino ESP32-S3 program that visually verifies the Gridopoly tile module's eight-wire ST7789 display connection.

**Architecture:** Keep future production code under `Firmware/TileModule/` and all bring-up programs under `Firmware/TileModuleTests/`. The ST7789 project separates immutable board configuration, a native-testable stage scheduler, and the Arduino/Adafruit rendering adapter so test sequencing can be verified without hardware.

**Tech Stack:** PlatformIO Core 6.1.19, `platformio/espressif32@7.0.1`, Arduino, C++17, PlatformIO Unity native tests, `adafruit/Adafruit GFX Library@1.12.6`, `adafruit/Adafruit ST7735 and ST7789 Library@1.11.0`.

## Global Constraints

- Production tile firmware belongs only in `Firmware/TileModule/`.
- Hardware tests belong only in `Firmware/TileModuleTests/`.
- The test target is ESP32-S3-WROOM-1-N16R8 through PlatformIO board `esp32-s3-devkitc-1`.
- J4 uses GPIO16 BL, GPIO15 CS, GPIO7 SCLK, GPIO6 MOSI, GPIO5 DC, and GPIO4 RST; J4 pin 1 is GND and pin 2 is 3.3 V.
- The display is 240 x 320, write-only ST7789, SPI mode 0 at 20 MHz, with no MISO.
- Backlight and CS must enter safe states before display initialization.
- The firmware must never report electronic panel detection or pixel verification because no readback path exists.
- Upload and monitor ports must not be hard-coded.

---

## File Structure

- Create `Firmware/TileModule/README.md`: reserve the production-only directory and point hardware experiments elsewhere.
- Create `Firmware/TileModuleTests/README.md`: define the hardware-test directory contract.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/platformio.ini`: pinned native and ESP32-S3 environments.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/include/board_pins.h`: authoritative GPIO, display, timing, and backlight constants.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/include/display_test_plan.h`: platform-independent stage API.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/src/display_test_plan.cpp`: fixed stage table and wrap-safe scheduler helpers.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/src/main.cpp`: safe startup, ST7789 adapter, renderers, PWM, serial diagnostics, and non-blocking loop.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/test/test_display_test_plan/test_main.cpp`: Unity tests for configuration and stage sequencing.
- Create `Firmware/TileModuleTests/ST7789DisplayTest/README.md`: wiring, commands, visual acceptance, and fault isolation.

### Task 1: Directory Contract and Failing Native Test

**Files:**
- Create: `Firmware/TileModule/README.md`
- Create: `Firmware/TileModuleTests/README.md`
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/platformio.ini`
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/include/board_pins.h`
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/include/display_test_plan.h`
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/test/test_display_test_plan/test_main.cpp`

**Interfaces:**
- Produces: `tile_display_test::StageId`, `Stage`, `stageCount()`, `stageAt(size_t)`, `nextStageIndex(size_t)`, and `stageElapsed(uint32_t, uint32_t, uint32_t)` declarations.
- Produces: `tile_display_test::board` compile-time GPIO and display constants.

- [ ] **Step 1: Add the directory contracts**

State that production code belongs only under `TileModule`, tests only under `TileModuleTests`, and no test project is linked into production.

- [ ] **Step 2: Add pinned PlatformIO environments**

```ini
[platformio]
default_envs = tile_esp32s3

[env]
build_flags = -std=gnu++17 -Wall -Wextra -Wpedantic

[env:tile_esp32s3]
platform = espressif32@7.0.1
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_upload.flash_size = 16MB
build_flags =
    ${env.build_flags}
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    adafruit/Adafruit GFX Library@1.12.6
    adafruit/Adafruit ST7735 and ST7789 Library@1.11.0

[env:native]
platform = native
test_framework = unity
test_build_src = yes
build_src_filter = +<display_test_plan.cpp> -<main.cpp>
```

- [ ] **Step 3: Add board constants and the wished-for scheduler API**

Use `inline constexpr` values for GPIO16/15/7/6/5/4, width 240, height 320, SPI mode 0, SPI frequency 20,000,000 Hz, serial 115200, portrait rotation 0, and bounded PWM values.

```cpp
enum class StageId : uint8_t {
  SolidRed, SolidGreen, SolidBlue, SolidWhite, SolidBlack,
  ColorBars, Geometry, GridAndGray, Diagnostics, BacklightRamp
};

struct Stage {
  StageId id;
  const char *name;
  const char *expectation;
  uint32_t duration_ms;
};

size_t stageCount();
const Stage &stageAt(size_t index);
size_t nextStageIndex(size_t index);
bool stageElapsed(uint32_t now_ms, uint32_t started_ms, uint32_t duration_ms);
```

- [ ] **Step 4: Write native tests before implementation**

Test the exact GPIO/dimension/frequency constants, ten stages in the required order, nonempty names and expectations, unique IDs, positive durations, final-to-first wraparound, out-of-range `stageAt()` normalization, and unsigned-millisecond wraparound in `stageElapsed()`.

- [ ] **Step 5: Run the native test and verify RED**

Run: `pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native`

Expected: link failure for the declared but undefined scheduler functions. A missing toolchain or malformed test is not an acceptable RED result.

- [ ] **Step 6: Commit the verified failing test scaffold**

```powershell
git add -- Firmware/TileModule Firmware/TileModuleTests
git commit -m "test: specify tile display bring-up sequence"
```

### Task 2: Stage Scheduler

**Files:**
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/src/display_test_plan.cpp`
- Test: `Firmware/TileModuleTests/ST7789DisplayTest/test/test_display_test_plan/test_main.cpp`

**Interfaces:**
- Consumes: declarations from `display_test_plan.h`.
- Produces: a fixed ten-stage table and all scheduler functions used by `main.cpp`.

- [ ] **Step 1: Implement the minimal fixed stage table**

Create one `constexpr Stage[]` in the exact tested order. Solid-color stages last 1500 ms, pattern and diagnostics stages last 3500 ms, and the backlight ramp lasts 5000 ms. `stageAt()` normalizes with modulo; `nextStageIndex()` wraps; `stageElapsed()` uses unsigned subtraction.

- [ ] **Step 2: Run the native test and verify GREEN**

Run: `pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native`

Expected: all scheduler and board-configuration tests pass with zero failures.

- [ ] **Step 3: Commit the scheduler**

```powershell
git add -- Firmware/TileModuleTests/ST7789DisplayTest/src/display_test_plan.cpp
git commit -m "feat: add tile display test scheduler"
```

### Task 3: ESP32-S3 Display Adapter and Visual Patterns

**Files:**
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/src/main.cpp`
- Modify: `Firmware/TileModuleTests/ST7789DisplayTest/test/test_display_test_plan/test_main.cpp`

**Interfaces:**
- Consumes: board constants, `Stage`, `stageAt()`, `stageCount()`, `nextStageIndex()`, and `stageElapsed()`.
- Produces: Arduino `setup()` and `loop()` for the physical display test.

- [ ] **Step 1: Add static source-contract tests before Arduino implementation**

Extend the native test to load `src/main.cpp` through `GRIDOPOLY_DISPLAY_TEST_DIR` and assert that the source calls the safe-start function before `display.init`, sets SPI speed from `kSpiFrequencyHz`, uses `SPI_MODE0`, initializes 240 x 320, and does not wait indefinitely for `Serial`.

- [ ] **Step 2: Run the test and verify RED**

Run: `pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native`

Expected: failure because `src/main.cpp` does not yet exist or lacks the required startup contract.

- [ ] **Step 3: Implement safe startup and hardware initialization**

Create a dedicated `SPIClass`, pass it to `Adafruit_ST7789`, set BL low and CS high before SPI startup, call `SPIClass::begin(GPIO7, -1, GPIO6, GPIO15)`, call `setSPISpeed(20000000)` before `init(240, 320, SPI_MODE0)`, set rotation 0, clear black, then attach PWM and fade to moderate brightness. Start serial without `while (!Serial)`.

- [ ] **Step 4: Implement the visual renderers**

Implement focused functions for solid fields, labeled RGB/CMY bars, geometry/corners, grid/grayscale, diagnostics, and backlight ramp. The diagnostics page prints controller, 240 x 320, 20 MHz, rotation, six GPIO assignments, stage index, and frame counter.

- [ ] **Step 5: Implement the non-blocking stage loop**

On transition, render once and log stage name plus expectation. During diagnostics update only the frame counter region. During backlight ramp derive duty from elapsed stage time, then return to moderate duty. Use unsigned elapsed time and no multi-second `delay()`.

- [ ] **Step 6: Run native tests and verify GREEN**

Run: `pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native`

Expected: all tests pass with zero failures.

- [ ] **Step 7: Build the ESP32-S3 firmware**

Run: `pio run -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3`

Expected: dependency resolution succeeds and firmware compiles for ESP32-S3 with no project warnings or errors.

- [ ] **Step 8: Commit the display implementation**

```powershell
git add -- Firmware/TileModuleTests/ST7789DisplayTest/src/main.cpp Firmware/TileModuleTests/ST7789DisplayTest/test/test_display_test_plan/test_main.cpp
git commit -m "feat: add ST7789 tile display bring-up firmware"
```

### Task 4: Operator Documentation and Final Verification

**Files:**
- Create: `Firmware/TileModuleTests/ST7789DisplayTest/README.md`

**Interfaces:**
- Consumes: final wiring and build behavior.
- Produces: exact build, upload, monitor, visual-acceptance, and fault-isolation instructions.

- [ ] **Step 1: Document wiring and safety**

Include the full eight-pin J4 table, connector-mirroring warning, 3.3 V-only warning, safe power-up order, and the fact that SCL/SDA labels are SPI SCLK/MOSI rather than I2C.

- [ ] **Step 2: Document commands and expected observations**

```powershell
pio test -d Firmware/TileModuleTests/ST7789DisplayTest -e native
pio run -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3
pio run -d Firmware/TileModuleTests/ST7789DisplayTest -e tile_esp32s3 -t upload
pio device monitor -d Firmware/TileModuleTests/ST7789DisplayTest -b 115200
```

List the ten visual stages and fault hints for blank screen, white screen, wrong colors, clipping/mirroring, intermittent artifacts, and missing backlight.

- [ ] **Step 3: Run complete verification**

Run the native test, ESP32 build, `git diff --check`, a placeholder scan, and a search proving no test sources exist under `Firmware/TileModule/`.

Expected: tests pass, firmware builds, diff check is clean, no placeholders remain, and the production directory contains documentation only.

- [ ] **Step 4: Commit documentation**

```powershell
git add -- Firmware/TileModule/README.md Firmware/TileModuleTests/README.md Firmware/TileModuleTests/ST7789DisplayTest/README.md
git commit -m "docs: add tile display test instructions"
```
