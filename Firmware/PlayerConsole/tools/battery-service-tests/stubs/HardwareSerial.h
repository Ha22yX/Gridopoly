#pragma once
#include "Arduino.h"
#include "battery-test-device.h"
namespace host { inline Device device; }
class HardwareSerial {
public:
    explicit HardwareSerial(unsigned port) { host::uartAccess(); ++host::constructed; if (port != 1) ++host::violations; }
    size_t setRxBufferSize(size_t n) { host::uartAccess(); return host::failBuffer ? 0 : n; }
    size_t setTxBufferSize(size_t n) { host::uartAccess(); return n; }
    void begin(unsigned baud, uint32_t, int rx, int tx) {
        host::uartAccess(); installed_ = !host::failUart;
        if (baud != 9600 || rx != 44 || tx != 43) ++host::violations;
    }
    explicit operator bool() const { host::uartAccess(); return installed_; }
    void end() { host::uartAccess(); ++host::ended; installed_ = false; }
    int read() { host::uartAccess(); host::device.now = millis(); return host::device.readByte(); }
    int availableForWrite() { host::uartAccess(); return static_cast<int>(host::device.writableBytes()); }
    size_t write(const uint8_t *p, size_t n) {
        host::uartAccess(); host::device.now = millis(); return host::device.writeBytes(p, n);
    }
private:
    bool installed_ = false;
};
