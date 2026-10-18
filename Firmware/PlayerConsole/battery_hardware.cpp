#include "battery_hardware.h"

#if GRIDOPOLY_BATTERY_ENABLED
#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "battery_diagnostics.h"
#include <type_traits>

namespace {
using namespace gridopoly::player_console;
constexpr uint32_t kServiceStackBytes = 3072;
constexpr UBaseType_t kServicePriority = 2;
constexpr BaseType_t kServiceCore = 0;
constexpr uint32_t kServicePeriodMs = 5;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
BatteryStatus published{};
BatteryDiagnostics diagnostics;
bool beginCalled = false; // main-loop owner only
static_assert(std::is_trivially_copyable<BatteryStatus>::value, "snapshot must have value ownership");

void publish(const BatteryStatus &next) {
    portENTER_CRITICAL(&snapshotMux);
    published = next;
    portEXIT_CRITICAL(&snapshotMux);
}
void publishUnavailable() {
    BatteryStatus next{};
    next.enabled = true;
    next.lastError = BatteryError::ServiceUnavailable;
    publish(next);
}
class HardwareBatteryUart final : public BatteryUart {
public:
    explicit HardwareBatteryUart(HardwareSerial &serial) : serial_(serial) {}
    int readByte() override { return serial_.read(); }
    size_t writableBytes() override { return static_cast<size_t>(serial_.availableForWrite()); }
    size_t writeBytes(const uint8_t *bytes, size_t count) override { return serial_.write(bytes, count); }
private:
    HardwareSerial &serial_;
};
void serviceTask(void *) {
    // Construction, driver/ISR installation and all UART I/O belong to core0.
    // No LVGL, HTTP, cache locks or USB diagnostics are used by this task.
    static HardwareSerial batterySerial(1);
    if (batterySerial.setRxBufferSize(256) != 256 || batterySerial.setTxBufferSize(256) != 256) {
        batterySerial.end(); publishUnavailable(); vTaskDelete(nullptr); return;
    }
    batterySerial.begin(9600, SERIAL_8N1, kBatteryUartRxGpio, kBatteryUartTxGpio);
    if (!batterySerial) {
        batterySerial.end(); publishUnavailable(); vTaskDelete(nullptr); return;
    }
    HardwareBatteryUart uart(batterySerial);
    BatteryTelemetry telemetry;
    telemetry.begin(millis());
    const TickType_t delayTicks = pdMS_TO_TICKS(kServicePeriodMs) ? pdMS_TO_TICKS(kServicePeriodMs) : 1;
    for (;;) {
        const uint32_t nowMs = millis();
        telemetry.tick(uart, nowMs);
        const BatteryStatus next = telemetry.status(millis());
        publish(next); // only a fixed-size copy is inside the short spinlock
        vTaskDelay(delayTicks); // no catch-up burst after a delayed service
    }
}
}

void batteryHardwareBegin(uint32_t) {
    if (beginCalled) return;
    beginCalled = true;
    BatteryStatus initial{}; initial.enabled = true; publish(initial);
    diagnostics.begin(millis());
    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCore(serviceTask, "grid-battery", kServiceStackBytes, nullptr,
                               kServicePriority, &task, kServiceCore) != pdPASS) publishUnavailable();
}

gridopoly::player_console::BatteryStatus batteryHardwareStatus(uint32_t) {
    BatteryStatus copy;
    portENTER_CRITICAL(&snapshotMux);
    copy = published;
    portEXIT_CRITICAL(&snapshotMux);
    // Read the clock AFTER copying: the worker can publish a newer timestamp
    // while the main loop waits to acquire the snapshot lock.
    return batteryStatusAt(copy, millis());
}
void batteryHardwareTick(uint32_t) {
    const auto snapshot = batteryHardwareStatus(0);
    diagnostics.tick(Serial, snapshot, millis());
}
bool batteryHardwareTraceStart(uint32_t nowMs) {
    if (!beginCalled) return false;
    diagnostics.begin(nowMs);
    return true;
}
void batteryHardwareTraceStop() { diagnostics.stop(); }
#else
// No UART object, task, initialization, I/O or logging in this profile.
void batteryHardwareBegin(uint32_t) {}
void batteryHardwareTick(uint32_t) {}
bool batteryHardwareTraceStart(uint32_t) { return false; }
void batteryHardwareTraceStop() {}
gridopoly::player_console::BatteryStatus batteryHardwareStatus(uint32_t) { return {}; }
#endif
