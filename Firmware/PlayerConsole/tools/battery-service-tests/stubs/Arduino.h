#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace host {
inline std::atomic<uint32_t> clockMs{0};
inline std::atomic<unsigned> violations{0}, uartCalls{0}, constructed{0}, ended{0};
inline std::thread::id mainThread, workerThread;
inline thread_local unsigned criticalDepth = 0;
inline bool failCreate = false, failUart = false, failBuffer = false;
inline std::vector<std::string> usbLines;
inline void uartAccess() {
    ++uartCalls;
    if (std::this_thread::get_id() != workerThread || criticalDepth) ++violations;
}
inline void usbAccess() {
    if (std::this_thread::get_id() != mainThread || criticalDepth) ++violations;
}
}
inline uint32_t millis() { return host::clockMs.load(); }
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE *m) { m->lock(); ++host::criticalDepth; }
inline void portEXIT_CRITICAL(portMUX_TYPE *m) { --host::criticalDepth; m->unlock(); }
constexpr uint32_t SERIAL_8N1 = 0;
struct UsbSerial {
    int availableForWrite() { host::usbAccess(); return 64; }
    size_t write(const uint8_t *p, size_t n) {
        host::usbAccess(); host::usbLines.emplace_back(reinterpret_cast<const char *>(p), n); return n;
    }
};
inline UsbSerial Serial;
