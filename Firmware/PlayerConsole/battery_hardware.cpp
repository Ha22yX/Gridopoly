#include "battery_hardware.h"

#if GRIDOPOLY_BATTERY_ENABLED
#include <Arduino.h>
#include <HardwareSerial.h>
#include "battery_diagnostics.h"

namespace {
// UART1 is routed through the GPIO matrix; USB HWCDC remains the log/flash port.
HardwareSerial batterySerial(1);
gridopoly::player_console::BatteryTelemetry telemetry;
BatteryDiagnostics diagnostics;
class HardwareBatteryUart final : public gridopoly::player_console::BatteryUart {
public:
    int readByte() override { return batterySerial.read(); }
    size_t writableBytes() override { return static_cast<size_t>(batterySerial.availableForWrite()); }
    size_t writeBytes(const uint8_t *bytes, size_t count) override { return batterySerial.write(bytes, count); }
} uart;
}

void batteryHardwareBegin(uint32_t)
{
    batterySerial.setRxBufferSize(256);
    batterySerial.setTxBufferSize(256);
    batterySerial.begin(9600, SERIAL_8N1, kBatteryUartRxGpio, kBatteryUartTxGpio);
    const uint32_t nowMs = millis();
    telemetry.begin(nowMs);
    diagnostics.begin(nowMs);
}
void batteryHardwareTick(uint32_t nowMs) {
    telemetry.tick(uart, nowMs);
    diagnostics.tick(Serial, telemetry.status(nowMs), nowMs);
}
gridopoly::player_console::BatteryStatus batteryHardwareStatus(uint32_t nowMs) { return telemetry.status(nowMs); }
#else
// No UART object, constructor, initialization, I/O or logging in this profile.
void batteryHardwareBegin(uint32_t) {}
void batteryHardwareTick(uint32_t) {}
gridopoly::player_console::BatteryStatus batteryHardwareStatus(uint32_t) { return {}; }
#endif
