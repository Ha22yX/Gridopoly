#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_rom_sys.h>
#include <lvgl.h>
#include <new>

#include "app_config.h"
#include "app_state.h"
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

namespace {

AppState app;
bool ready = false;
bool lvglReady = false;

#if GRIDOPOLY_SELF_TEST == 1
constexpr size_t kTestCapacity = 8192;
char testBuffer[kTestCapacity] = {};
RomConsoleStream testOutput(testBuffer, sizeof(testBuffer));
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
    // Leave enough time to attach a monitor after the uploader resets native USB.
    delay(12000);
    testOutput.reset();
    const bool passed = runLogicTests(testOutput);
    esp_rom_printf("%s%s\n", testOutput.data(), passed ? "SELFTEST PASS" : "SELFTEST FAILED");
    if (!passed) while (true) delay(1000);
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

    appInit(app, millis());
    if (!lvgl_port_lock(-1)) { fault("LVGL_LOCK"); return; }
    const bool rendererReady = uiRendererBegin();
    if (rendererReady) uiRendererRender(app, millis());
    lvgl_port_unlock();
    if (!rendererReady) { fault("UI_INIT"); return; }

    ready = true;
    Serial.printf("Gridopoly Player Console ready; mechanical=%d firmware=%d\n",
                  kMechanicalCorrectionDeg, kFirmwareRotationDeg);
}

void loop()
{
    if (!ready) { delay(5); return; }
    const uint32_t nowMs = millis();
    InputEvent event{};
    while (hardwareInputPoll(event)) appHandleInput(app, event, nowMs);

    if (lvgl_port_lock(-1)) {
        TouchAction action = TouchAction::None;
        while (uiRendererPollTouch(action)) appHandleTouch(app, action, nowMs);
        appTick(app, nowMs);
        uiRendererRender(app, nowMs);
        lvgl_port_unlock();
    }
    delay(2);
}
