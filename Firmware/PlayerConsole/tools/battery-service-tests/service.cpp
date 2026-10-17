#include "battery_hardware.h"
#include "Arduino.h"
#include "HardwareSerial.h"
#include "freertos/task.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace host {
std::mutex gate;
std::condition_variable cv;
std::thread worker;
bool waiting = false, resume = false, stopping = false, done = false;
unsigned created = 0, turns = 0;
uint32_t stackBytes = 0;
unsigned priority = 0;
int core = -1;
struct Stop {};
void waitForPause() {
    std::unique_lock<std::mutex> lock(gate);
    if (!cv.wait_for(lock, std::chrono::seconds(5), [] { return waiting || done; }))
        throw std::runtime_error("worker barrier timed out");
}
void advance(uint32_t delta = 5) {
    std::unique_lock<std::mutex> lock(gate);
    if (!waiting || done) throw std::runtime_error("worker is not paused");
    const auto before = turns;
    clockMs += delta; resume = true; cv.notify_all();
    if (!cv.wait_for(lock, std::chrono::seconds(5), [before] { return turns > before || done; }))
        throw std::runtime_error("worker did not service UART");
}
void stop() {
    { std::lock_guard<std::mutex> lock(gate); stopping = true; cv.notify_all(); }
    if (worker.joinable()) worker.join();
}
}
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t stack, void *,
                                 UBaseType_t priority, TaskHandle_t *, BaseType_t core) {
    ++host::created; host::stackBytes = stack; host::priority = priority; host::core = core;
    if (host::failCreate) return 0;
    host::worker = std::thread([fn] {
        host::workerThread = std::this_thread::get_id();
        try { fn(nullptr); } catch (const host::Stop &) {}
        std::lock_guard<std::mutex> lock(host::gate); host::done = true; host::cv.notify_all();
    });
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
    if (ticks != 5 || host::criticalDepth) ++host::violations;
    std::unique_lock<std::mutex> lock(host::gate);
    host::waiting = true; ++host::turns; host::cv.notify_all();
    host::cv.wait(lock, [] { return host::resume || host::stopping; });
    host::waiting = false; host::resume = false;
    if (host::stopping) throw host::Stop{};
}
void vTaskDelete(TaskHandle_t) { throw host::Stop{}; }

int main(int argc, char **argv) {
    using namespace gridopoly::player_console;
    host::mainThread = std::this_thread::get_id();
    const std::string scenario = argc > 1 ? argv[1] : "success";
    host::failCreate = scenario == "create-failure";
    host::failUart = scenario == "uart-failure";
    host::failBuffer = scenario == "buffer-failure";
    unsigned passed = 0, failed = 0;
    auto check = [&](bool value, const char *text) {
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", text); (value ? passed : failed)++;
    };
    try {
        batteryHardwareBegin(0);
#if GRIDOPOLY_BATTERY_ENABLED
        if (!host::failCreate) host::waitForPause();
        batteryHardwareBegin(1);
        check(host::created == 1 && host::stackBytes == 3072 && host::priority == 2 && host::core == 0,
              "one bounded 3072-byte priority-2 core0 service task, begin is idempotent");
        if (scenario != "success") {
            host::stop(); host::clockMs = 1000; batteryHardwareTick(0);
            const auto s = batteryHardwareStatus(0);
            check(s.enabled && !s.linkReady && !s.measurementValid && !s.gpioValid &&
                  !s.serviceStarted && s.lastError == BatteryError::ServiceUnavailable,
                  "task/UART initialization failure publishes explicit unknown and service error");
            check(host::failCreate ? host::constructed == 0 : host::ended == 1,
                  "failed task creates no UART; failed UART initialization releases driver resources");
        } else {
            for (unsigned i = 0; i < 1000; ++i) host::advance();
            auto s = batteryHardwareStatus(0);
            check(s.measurementValid && s.gpioValid && s.percent == 78 && s.voltageMv == 3700 &&
                  s.timeoutCount == 0 && s.serviceAtMs == 5000 && s.maxServiceGapMs == 5,
                  "real adapter keeps fresh UART telemetry while main loop never services it for 5 seconds");
            check(host::usbLines.empty() && host::constructed == 1,
                  "worker owns one UART and performs no USB diagnostics");
            const auto reads = host::uartCalls.load();
            const uint32_t staleCallerClock = 1;
            s = batteryHardwareStatus(staleCallerClock);
            const uint32_t renderClock = millis();
            check(s.measurementAtMs > staleCallerClock && batteryStatusAt(s, renderClock).measurementValid &&
                  batteryStatusAt(s, renderClock).gpioValid && host::uartCalls == reads,
                  "snapshot newer than caller clock remains fresh when render time is sampled after the copy");
            std::atomic<bool> stopReader{false};
            std::atomic<unsigned> inconsistent{0}, snapshots{0};
            std::thread reader([&] {
                while (!stopReader) {
                    const auto copy = batteryHardwareStatus(0); ++snapshots;
                    if (copy.measurementValid && (copy.voltageMv != (copy.rawVoltage * 5u + 32u) / 64u ||
                        copy.percent != copy.rawSoc / 256u)) ++inconsistent;
                }
            });
            for (unsigned i = 0; i < 1200; ++i) {
                // Worker is at the barrier; no concurrent access to the byte-level device fake.
                host::device.voltage = static_cast<uint16_t>(47000 + (i % 500));
                host::device.soc = static_cast<uint16_t>((30 + i % 50) * 256);
                host::advance();
            }
            stopReader = true; reader.join();
            check(snapshots > 0 && inconsistent == 0 && host::violations == 0,
                  "concurrent snapshot readers see complete voltage/SOC pairs and no I/O occurs inside critical sections");
            for (unsigned i = 0; i < 8; ++i) { batteryHardwareTick(0); host::advance(20); }
            check(host::usbLines.size() == 8 && host::usbLines[6].find(" rx=") != std::string::npos,
                  "main loop emits one bounded eight-line diagnostic group after resuming");
            while (host::device.rx.empty()) host::advance();
            host::advance(250); s = batteryHardwareStatus(0);
            check(s.lastError == BatteryError::Timeout && s.timeoutCount == 1 && s.maxServiceGapMs == 250 &&
                  s.failedReceived == s.failedExpected && !s.measurementValid,
                  "worker delay beyond 180ms rejects even complete buffered RX and records exact failure");
            for (unsigned i = 0; i < 600; ++i) host::advance();
            s = batteryHardwareStatus(0);
            check(s.measurementValid && s.timeoutCount == 1,
                  "service recovers from timeout without erasing cumulative evidence");
            host::stop(); host::clockMs += 351;
            check(!batteryHardwareStatus(0).gpioValid && batteryHardwareStatus(0).charge == ChargeIndicator::Unknown,
                  "stopped worker cannot leave stale power/charge indication valid");
            host::clockMs += 3001;
            check(!batteryHardwareStatus(0).measurementValid,
                  "stopped worker cannot leave old SOC valid");
        }
        check(host::violations == 0, "all UART access belongs to service and all USB access to main, outside snapshot locks");
#else
        host::clockMs = 5000; batteryHardwareTick(5000);
        check(!batteryHardwareStatus(5000).enabled && host::created == 0 && host::constructed == 0 &&
              host::uartCalls == 0 && host::usbLines.empty(),
              "disabled board creates no task, UART, I/O, diagnostics or fake sample");
#endif
    } catch (const std::exception &e) { std::printf("[FAIL] %s\n", e.what()); ++failed; }
    host::stop();
    std::printf("RESULT scenario=%s passed=%u failed=%u\n", scenario.c_str(), passed, failed);
    return failed ? 1 : 0;
}
