#pragma once
#include "battery_telemetry.h"
#include <cstdio>

// Ten independent short lines share a sample id. Each line is written in one
// call only when USB has space for the WHOLE line; never resume half a line.
// No wait, flush or allocation. Busy/disconnected USB expires the sample.
class BatteryDiagnostics {
public:
    static constexpr uint32_t kWindowMs = 120000, kPeriodMs = 5000, kSampleWindowMs = 1000;
    static constexpr size_t kMaxWriteBytes = 63;
    static constexpr uint8_t kLineCount = 10;
    void begin(uint32_t nowMs) { started_ = last_ = nowMs; id_ = 0; row_ = kLineCount; }
    template <typename Sink>
    void tick(Sink &sink, const gridopoly::player_console::BatteryStatus &status, uint32_t nowMs) {
        if (!status.enabled || static_cast<uint32_t>(nowMs - started_) >= kWindowMs) { row_ = kLineCount; return; }
        if (row_ == kLineCount && static_cast<uint32_t>(nowMs - last_) >= (id_ ? kPeriodMs : 1000u)) {
            last_ = nowMs; ++id_; row_ = 0; snapshot_ = status;
        }
        if (row_ == kLineCount) return;
        if (static_cast<uint32_t>(nowMs - last_) >= kSampleWindowMs) { row_ = kLineCount; return; }
        char line[kMaxWriteBytes + 1]{};
        int n = 0;
        const auto &s = snapshot_;
        switch (row_) {
        case 0:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u ms=%lu link=%u mv_ok=%u io_ok=%u e=%u i=%02X\n",
                id_, static_cast<unsigned long>(last_), s.linkReady, s.measurementValid, s.gpioValid,
                static_cast<unsigned>(s.lastError), s.lastI2cStatus); break;
        case 1:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u rv=%04X rs=%04X mv=%u pct=%u age=%lu\n",
                id_, s.rawVoltage, s.rawSoc, s.voltageMv, s.percent,
                static_cast<unsigned long>(last_ - s.measurementAtMs)); break;
        case 2:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u gpio=%02X b0=%u b1=%u age=%lu\n",
                id_, s.rawGpio, s.rawGpio & 1u, (s.rawGpio >> 1) & 1u,
                static_cast<unsigned long>(last_ - s.gpioAtMs)); break;
        case 3:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u bridge=%.15s gauge=%04X\n", id_,
                s.bridgeVersion[0] ? s.bridgeVersion : "unknown", s.gaugeVersion); break;
        case 4:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u mode=%04X config=%04X cached=1\n", id_, s.gaugeMode, s.gaugeConfig); break;
        case 5:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u svc=%u gap=%lu to=%lu\n", id_, s.serviceStarted,
                static_cast<unsigned long>(s.maxServiceGapMs), static_cast<unsigned long>(s.timeoutCount)); break;
        case 6:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u fail=%u op=%u rx=%u/%u at=%lu\n", id_,
                s.failedAction, s.failedOperation, s.failedReceived, s.failedExpected,
                static_cast<unsigned long>(s.failureAtMs)); break;
        case 7:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u svc_age=%lu i_seen=%u i_at=%lu\n", id_,
                static_cast<unsigned long>(last_ - s.serviceAtMs), s.i2cStatusSeen,
                static_cast<unsigned long>(s.i2cStatusAtMs)); break;
        case 8:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u cfg_seen=%u to_reg=%02X hw_to=%u at=%lu\n", id_,
                s.bridgeConfigSeen, s.bridgeTimeoutRegister, s.bridgeSclTimeoutEnabled,
                static_cast<unsigned long>(s.bridgeConfiguredAtMs)); break;
        case 9:
            n = std::snprintf(line, sizeof(line), "GP_BAT id=%u first=%u/%u e=%u rx=%u/%u at=%lu\n", id_,
                s.firstAction, s.firstOperation, static_cast<unsigned>(s.firstError), s.firstReceived, s.firstExpected,
                static_cast<unsigned long>(s.firstFailureAtMs)); break;
        }
        if (n <= 0 || static_cast<size_t>(n) > kMaxWriteBytes) { ++row_; return; }
        if (sink.availableForWrite() < n) return;
        sink.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(n));
        ++row_;
    }
private:
    gridopoly::player_console::BatteryStatus snapshot_{};
    uint32_t started_ = 0, last_ = 0;
    unsigned id_ = 0;
    uint8_t row_ = kLineCount;
};
