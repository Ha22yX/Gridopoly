#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <lvgl.h>
#include <new>

#include "app_config.h"
#include "app_state.h"
#include "demo_transport.h"
#include "hardware_input.h"
#include "logic_tests.h"
#include "lvgl_v8_port.h"
#include "rom_console_stream.h"
#include "ui_renderer.h"

#ifndef BOARD_HAS_PSRAM
#error "Gridopoly Player Console requires PSRAM"
#endif

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 1
#error "Use ESP32-S3 Hardware CDC/JTAG USB mode"
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

#if GRIDOPOLY_SELF_TEST == 1
bool runPureLogicTests(Stream &out);
bool runLvglComponentTests(Stream &out);
#endif

namespace {

AppState app;
DemoTransport transport;
bool ready = false;
bool lvglReady = false;

#if GRIDOPOLY_SELF_TEST == 1
constexpr size_t kTestCapacity = 8192;
constexpr uint32_t kCarouselPerfTransitionMs = 220;
constexpr uint32_t kCarouselPerfMinFrames = 6;
constexpr uint32_t kCarouselPerfMinFps = 24;
constexpr uint32_t kCarouselPerfMaxGapMs = 42;
constexpr uint32_t kCarouselPerfObserveMs = 320;
constexpr uint32_t kSelfTestCheckpointMagic = 0x47525034;
char testBuffer[kTestCapacity] = {};
RomConsoleStream testOutput(testBuffer, sizeof(testBuffer));

struct SelfTestCheckpoint {
    uint32_t magic;
    uint32_t inverseMagic;
    uint32_t componentPassed;
    uint32_t inverseComponentPassed;
};

RTC_NOINIT_ATTR SelfTestCheckpoint selfTestCheckpoint;

struct CarouselPerfResult {
    uint32_t frames = 0;
    uint32_t fps = 0;
    uint32_t maxGapMs = 0;
    uint32_t coverageMs = 0;
    bool passed = false;
};

struct CarouselPerfProbe {
    volatile bool armed = false;
    volatile uint32_t startedMs = 0;
    volatile uint32_t previousFrameMs = 0;
    volatile uint32_t lastFrameMs = 0;
    volatile uint32_t frames = 0;
    volatile uint32_t maxGapMs = 0;
    void (*previousMonitor)(lv_disp_drv_t *, uint32_t, uint32_t) = nullptr;
};

CarouselPerfProbe carouselPerfProbe{};

constexpr bool carouselPerfMeetsGate(uint32_t frames, uint32_t fps,
                                     uint32_t maxGapMs, uint32_t coverageMs)
{
    return frames >= kCarouselPerfMinFrames && fps >= kCarouselPerfMinFps &&
           maxGapMs <= kCarouselPerfMaxGapMs && coverageMs >= kCarouselPerfTransitionMs;
}

static_assert(carouselPerfMeetsGate(6, 24, 42, 220), "carousel performance boundary must pass");
static_assert(!carouselPerfMeetsGate(5, 24, 42, 220), "carousel frame count must span the transition");
static_assert(!carouselPerfMeetsGate(6, 23, 42, 220), "carousel FPS floor must be enforced");
static_assert(!carouselPerfMeetsGate(6, 24, 43, 220), "carousel frame gap ceiling must be enforced");
static_assert(!carouselPerfMeetsGate(6, 24, 42, 219), "carousel measurement must span 220ms");

void carouselPerfMonitor(lv_disp_drv_t *driver, uint32_t renderMs, uint32_t pixelCount)
{
    if (carouselPerfProbe.armed && pixelCount > 0) {
        const uint32_t nowMs = millis();
        const uint32_t gapMs = nowMs - carouselPerfProbe.previousFrameMs;
        carouselPerfProbe.previousFrameMs = nowMs;
        carouselPerfProbe.lastFrameMs = nowMs;
        if (gapMs > carouselPerfProbe.maxGapMs) carouselPerfProbe.maxGapMs = gapMs;
        ++carouselPerfProbe.frames;
    }
    if (carouselPerfProbe.previousMonitor != nullptr) {
        carouselPerfProbe.previousMonitor(driver, renderMs, pixelCount);
    }
}

CarouselPerfResult runCarouselPerfFixture(AppState &state)
{
    CarouselPerfResult result{};
    if (!lvgl_port_lock(1000)) return result;
    lv_disp_t *display = lv_disp_get_default();
    if (display == nullptr || display->driver == nullptr ||
        display->driver->monitor_cb == carouselPerfMonitor) {
        lvgl_port_unlock();
        return result;
    }

    carouselPerfProbe.previousMonitor = display->driver->monitor_cb;
    carouselPerfProbe.frames = 0;
    carouselPerfProbe.maxGapMs = 0;
    carouselPerfProbe.startedMs = millis();
    carouselPerfProbe.previousFrameMs = carouselPerfProbe.startedMs;
    carouselPerfProbe.lastFrameMs = carouselPerfProbe.startedMs;
    carouselPerfProbe.armed = true;
    display->driver->monitor_cb = carouselPerfMonitor;
    appHandleInput(
        state,
        InputEvent{InputKind::Rotate, 1, carouselPerfProbe.startedMs},
        carouselPerfProbe.startedMs
    );
    uiRendererRender(state, carouselPerfProbe.startedMs);
    lvgl_port_unlock();

    delay(kCarouselPerfObserveMs);

    if (!lvgl_port_lock(1000)) return result;
    carouselPerfProbe.armed = false;
    display->driver->monitor_cb = carouselPerfProbe.previousMonitor;
    result.frames = carouselPerfProbe.frames;
    result.maxGapMs = carouselPerfProbe.maxGapMs;
    result.coverageMs = carouselPerfProbe.lastFrameMs - carouselPerfProbe.startedMs;
    lvgl_port_unlock();

    if (result.coverageMs > 0) {
        result.fps = result.frames * 1000U / result.coverageMs;
    }
    result.passed = carouselPerfMeetsGate(
        result.frames, result.fps, result.maxGapMs, result.coverageMs
    );
    return result;
}
#endif

void fault(const char *code)
{
    ready = false;
    Serial.printf("Gridopoly fault: %s\n", code);
    if (!lvglReady || !lvgl_port_lock(-1)) return;
    uiRendererShowFault(code);
    lvgl_port_unlock();
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(200);

#if GRIDOPOLY_SELF_TEST == 1
    const bool resumedComponentPhase =
        esp_reset_reason() == ESP_RST_SW &&
        selfTestCheckpoint.magic == kSelfTestCheckpointMagic &&
        selfTestCheckpoint.inverseMagic == ~kSelfTestCheckpointMagic &&
        selfTestCheckpoint.componentPassed <= 1 &&
        selfTestCheckpoint.inverseComponentPassed == ~selfTestCheckpoint.componentPassed;
    if (!resumedComponentPhase) {
        // The uploader releases native USB after this boot has already started.
        delay(3000);
        testOutput.reset();
        const bool cleanStart = !lv_is_initialized();
        const bool componentTestsPassed = runLvglComponentTests(testOutput);
        const bool cleanObjects = lv_disp_get_next(nullptr) == nullptr &&
                                  lv_indev_get_next(nullptr) == nullptr;
        const bool componentPhasePassed = cleanStart && componentTestsPassed && cleanObjects;
        testOutput.printf(
            "[%s] fake-display component phase releases every display and input\n",
            cleanObjects ? "PASS" : "FAIL"
        );
        selfTestCheckpoint.componentPassed = componentPhasePassed ? 1U : 0U;
        selfTestCheckpoint.inverseComponentPassed = ~selfTestCheckpoint.componentPassed;
        selfTestCheckpoint.inverseMagic = ~kSelfTestCheckpointMagic;
        selfTestCheckpoint.magic = kSelfTestCheckpointMagic;
        esp_rom_printf("%s\n", testOutput.data());
        esp_rom_printf("COMPONENT TESTS %s\n", componentPhasePassed ? "PASS" : "FAIL");
        delay(100);
        esp_restart();
        while (true) delay(1000);
    }

    const bool componentPassed = selfTestCheckpoint.componentPassed == 1U;
    selfTestCheckpoint.magic = 0;
    selfTestCheckpoint.inverseMagic = 0;
    selfTestCheckpoint.componentPassed = 0;
    selfTestCheckpoint.inverseComponentPassed = 0;
    testOutput.reset();
    const bool cleanBeforePure = !lv_is_initialized();
    const bool pureTestsPassed = runPureLogicTests(testOutput);
    const bool cleanAfterPure = !lv_is_initialized();
    const bool purePassed = cleanBeforePure && pureTestsPassed && cleanAfterPure;
    testOutput.printf(
        "[%s] pure logic leaves LVGL uninitialized before physical port setup\n",
        cleanBeforePure && cleanAfterPure ? "PASS" : "FAIL"
    );
#endif

    Board *board = new (std::nothrow) Board();
    if (board == nullptr || !board->init()) { fault("BOARD_INIT"); return; }
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    if (!board->begin()) { fault("BOARD_BEGIN"); return; }

    // Touch and pixels share the same mechanically corrected coordinate system.
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) { fault("LVGL_INIT"); return; }
    lvglReady = true;
    if (!hardwareInputBegin()) { fault("INPUT_INIT"); return; }

    const uint32_t startedMs = millis();
    appInit(app, startedMs);
    transport.begin(startedMs);
    if (!lvgl_port_lock(-1)) { fault("LVGL_LOCK"); return; }
    const bool rendererReady = uiRendererBegin();
    if (rendererReady) uiRendererRender(app, millis());
    lvgl_port_unlock();
    if (!rendererReady) { fault("UI_INIT"); return; }

#if GRIDOPOLY_SELF_TEST == 1
    // Leave enough time to attach a monitor after the uploader resets native USB.
    delay(12000);
    const CarouselPerfResult perf = runCarouselPerfFixture(app);
    const bool passed = purePassed && componentPassed && perf.passed;
    esp_rom_printf("%s\n", testOutput.data());
    esp_rom_printf(
        "CAROUSEL PERF frames=%u fps=%u max_gap_ms=%u %s\n",
        static_cast<unsigned>(perf.frames),
        static_cast<unsigned>(perf.fps),
        static_cast<unsigned>(perf.maxGapMs),
        perf.passed ? "PASS" : "FAIL"
    );
    esp_rom_printf("%s\n", passed ? "SELFTEST PASS" : "SELFTEST FAILED");
    if (!passed) while (true) delay(1000);
#endif

    ready = true;
    Serial.printf("Gridopoly Player Console ready; mechanical=%d firmware=%d\n",
                  kMechanicalCorrectionDeg, kFirmwareRotationDeg);
}

void loop()
{
    if (!ready) { delay(5); return; }
    const uint32_t nowMs = millis();
    InputEvent input{};
    TransportCommand command{};
    TransportEvent transportEvent{};
    while (hardwareInputPoll(input)) appHandleInput(app, input, nowMs);

    if (lvgl_port_lock(-1)) {
        TouchAction action = TouchAction::None;
        while (uiRendererPollTouch(action)) appHandleTouch(app, action, nowMs);
        lvgl_port_unlock();
    }

    while (appPollCommand(app, command)) transport.send(command, nowMs);
    transport.tick(nowMs);
    while (transport.poll(transportEvent)) appHandleTransportEvent(app, transportEvent, nowMs);
    appTick(app, nowMs);

    if (lvgl_port_lock(-1)) {
        uiRendererRender(app, nowMs);
        lvgl_port_unlock();
    }
    delay(2);
}
