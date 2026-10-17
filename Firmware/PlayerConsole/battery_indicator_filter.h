#pragma once
#include "battery_telemetry.h"

enum class UiBatteryIndicator : uint8_t { None, Charging, Warning };

// Presentation hysteresis only. Raw telemetry and its expiry/error reporting
// remain untouched; this filter never controls charging or the power hardware.
class BatteryIndicatorFilter {
public:
    static constexpr uint32_t kTransitionMs = 1500;
    static constexpr uint32_t kStaleHoldMs = 1000;

    UiBatteryIndicator update(const gridopoly::player_console::BatteryStatus &s, uint32_t nowMs) {
        using namespace gridopoly::player_console;
        if (!s.enabled || !s.linkReady) return clear();
        const bool fresh = s.gpioValid && nowMs - s.gpioAtMs <= BatteryTelemetry::kGpioMaxAgeMs;
        const bool fault = fresh && s.charge == ChargeIndicator::FaultCandidate;
        const bool known = fresh && (fault ||
            (s.external != ExternalPower::Unknown && s.charge != ChargeIndicator::Unknown));
        if (!known) {
            pending_ = false;
            // A brief missed GPIO frame need not flash the icon. A confirmed
            // link failure above clears it immediately; stale data has a limit.
            if (!seen_ || nowMs - lastSampleAt_ > kStaleHoldMs) return clear();
            return shown_;
        }
        const UiBatteryIndicator next = fault || s.charge == ChargeIndicator::Inconsistent
            ? UiBatteryIndicator::Warning
            : s.external == ExternalPower::Present && s.charge == ChargeIndicator::Active
                ? UiBatteryIndicator::Charging : UiBatteryIndicator::None;
        if (seen_ && s.gpioAtMs == lastSampleAt_) return shown_;
        if (!seen_ || s.gpioAtMs - lastSampleAt_ > BatteryTelemetry::kGpioMaxAgeMs) pending_ = false;
        seen_ = true; lastSampleAt_ = s.gpioAtMs;
        // The protocol already requires >=2 seconds of observed 2 Hz edges.
        // Do not debounce away that diagnostic by smoothing the raw signal.
        if (fault) { shown_ = UiBatteryIndicator::Warning; pending_ = false; return shown_; }
        if (next == shown_) { pending_ = false; return shown_; }
        if (!pending_ || next != candidate_) {
            candidate_ = next; candidateAt_ = s.gpioAtMs; pending_ = true;
        } else if (s.gpioAtMs - candidateAt_ >= kTransitionMs) {
            shown_ = next; pending_ = false;
        }
        return shown_;
    }

private:
    UiBatteryIndicator clear() { *this = BatteryIndicatorFilter{}; return shown_; }
    UiBatteryIndicator shown_ = UiBatteryIndicator::None, candidate_ = UiBatteryIndicator::None;
    bool pending_ = false, seen_ = false;
    uint32_t candidateAt_ = 0, lastSampleAt_ = 0;
};
