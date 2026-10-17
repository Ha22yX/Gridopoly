#include "battery_telemetry.h"
#include <string.h>

namespace gridopoly::player_console {
namespace {
constexpr uint32_t kQuietMs = 700; // SC18IM704 incomplete-command timeout is 655 ms.
constexpr uint32_t kResponseMs = 180;
constexpr uint32_t kWriteSettleMs = 100;
bool due(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
}

void BatteryTelemetry::resetGpioHistory()
{
    gpioSeen_ = faultCandidate_ = false;
    signalKnown_[0] = signalKnown_[1] = false;
    edgeCount_ = 0;
}

void BatteryTelemetry::begin(uint32_t nowMs)
{
    *this = BatteryTelemetry{};
    status_.enabled = true;
    quietAt_ = nowMs;
}

void BatteryTelemetry::fail(BatteryError error, uint32_t nowMs)
{
    status_.lastError = error;
    if (error == BatteryError::Timeout && status_.timeoutCount != UINT32_MAX) ++status_.timeoutCount;
    status_.failureAtMs = nowMs;
    status_.failedAction = static_cast<uint8_t>(pending_);
    status_.failedOperation = static_cast<uint8_t>(pending_ == Action::I2cStatus ? checked_ : pending_);
    status_.failedExpected = expected_; status_.failedReceived = received_;
    status_.linkReady = status_.measurementValid = status_.gpioValid = false;
    status_.external = ExternalPower::Unknown;
    status_.charge = ChargeIndicator::Unknown;
    waiting_ = false;
    next_ = Action::Sync;
    quietAt_ = nowMs;
    received_ = 0;
    resume_ = Action::Idle;
    resetGpioHistory();
}

BatteryStatus batteryStatusAt(BatteryStatus value, uint32_t nowMs)
{
    if (static_cast<uint32_t>(nowMs - value.measurementAtMs) > BatteryTelemetry::kMeasurementMaxAgeMs)
        value.measurementValid = false;
    if (static_cast<uint32_t>(nowMs - value.gpioAtMs) > BatteryTelemetry::kGpioMaxAgeMs) value.gpioValid = false;
    if (!value.gpioValid) {
        value.external = ExternalPower::Unknown;
        value.charge = ChargeIndicator::Unknown;
    }
    return value;
}

BatteryStatus BatteryTelemetry::status(uint32_t nowMs) const
{
    return batteryStatusAt(status_, nowMs);
}

bool BatteryTelemetry::start(BatteryUart &uart, Action action, uint32_t nowMs)
{
    uint8_t command[16]{};
    size_t count = 0;
    expected_ = 0;
    uint8_t reg = 0;
    bool gaugeRead = false, gaugeWrite = false;
    uint16_t writeValue = 0;
    switch (action) {
    case Action::BridgeVersion: command[0] = 0x56; command[1] = 0x50; count = 2; expected_ = 16; break;
    case Action::ConfigureBridge: {
        // Inputs only; ~99 kHz; enable the otherwise disabled SCL timeout.
        const uint8_t bytes[] = {0x57, 0x02, 0x55, 0x03, 0x55, 0x07, 0x13, 0x08, 0, 0x09, 0x67, 0x50};
        memcpy(command, bytes, sizeof(bytes)); count = sizeof(bytes); break;
    }
    case Action::VerifyBridge: {
        const uint8_t bytes[] = {0x52, 0x02, 0x03, 0x07, 0x08, 0x09, 0x50};
        memcpy(command, bytes, sizeof(bytes)); count = sizeof(bytes); expected_ = 5; break;
    }
    case Action::Gpio:
        command[0] = 0x52; command[1] = 0x04; command[2] = 0x09; command[3] = 0x50;
        count = 4; expected_ = 2; break;
    case Action::VerifySample:
        command[0] = 0x52; command[1] = 0x09; command[2] = 0x50; count = 3; expected_ = 1; break;
    case Action::I2cStatus: command[0] = 0x52; command[1] = 0x0A; command[2] = 0x50; count = 3; expected_ = 1; break;
    case Action::GaugeVersion: reg = 0x08; gaugeRead = true; break;
    case Action::VerifyMode: reg = 0x06; gaugeRead = true; break;
    case Action::ReadConfig: case Action::VerifyConfig: reg = 0x0C; gaugeRead = true; break;
    case Action::Voltage: reg = 0x02; gaugeRead = true; break;
    case Action::Soc: reg = 0x04; gaugeRead = true; break;
    case Action::ClearMode: reg = 0x06; gaugeWrite = true; writeValue = 0; break;
    case Action::ClearSleep: reg = 0x0C; gaugeWrite = true; writeValue = config_; break;
    default: return false;
    }
    if (gaugeRead) {
        const uint8_t bytes[] = {0x53, 0x6C, 0x01, reg, 0x53, 0x6D, 0x02, 0x50};
        memcpy(command, bytes, sizeof(bytes)); count = sizeof(bytes); expected_ = 2;
    } else if (gaugeWrite) {
        const uint8_t bytes[] = {0x53, 0x6C, 0x03, reg,
            static_cast<uint8_t>(writeValue >> 8), static_cast<uint8_t>(writeValue), 0x50};
        memcpy(command, bytes, sizeof(bytes)); count = sizeof(bytes);
    }
    // The adapter has a buffered UART. Never wait for TX space or flush it.
    if (uart.writableBytes() < count) return false;
    pending_ = action;
    sentAt_ = lastByteAt_ = nowMs;
    received_ = 0;
    if (uart.writeBytes(command, count) != count) { fail(BatteryError::TxIncomplete, nowMs); return false; }
    waiting_ = true;
    return true;
}

void BatteryTelemetry::tick(BatteryUart &uart, uint32_t nowMs)
{
    if (!status_.enabled) return;
    const uint32_t serviceGap = nowMs - status_.serviceAtMs;
    if (serviceSeen_ && serviceGap > status_.maxServiceGapMs) status_.maxServiceGapMs = serviceGap;
    serviceSeen_ = status_.serviceStarted = true;
    status_.serviceAtMs = nowMs;
    // A bounded drain also handles startup OK, a bridge reset and late replies.
    for (uint8_t i = 0; i < 32; ++i) {
        const int byte = uart.readByte();
        if (byte < 0) break;
        if (next_ == Action::Sync) { quietAt_ = nowMs; continue; }
        if (!waiting_ || received_ >= expected_) { fail(BatteryError::Framing, nowMs); continue; }
        response_[received_++] = static_cast<uint8_t>(byte);
        lastByteAt_ = nowMs;
    }
    if (next_ == Action::Sync) {
        if (static_cast<uint32_t>(nowMs - quietAt_) < kQuietMs) return;
        next_ = Action::BridgeVersion;
    }
    if (waiting_) {
        if (static_cast<uint32_t>(nowMs - sentAt_) > kResponseMs) { fail(BatteryError::Timeout, nowMs); return; }
        if (received_ != expected_) return;
        if (expected_ == 0) {
            // Internal register reads are immediate, so a write's status must
            // not be queried while its UART/I2C bytes could still be executing.
            // At clock=19 and I2CTO=67, continuous-low timeout is approximately
            // 35.5 ms (NXP section 9.2.6). The guard covers a normal MAX17048
            // write plus that single stall, not arbitrary repeated stretching.
            // Status AND register readback are still required before use.
            if (static_cast<uint32_t>(nowMs - sentAt_) < kWriteSettleMs) return;
        }
        waiting_ = false;
        const Action action = pending_;
        const uint16_t value = expected_ == 2
            ? static_cast<uint16_t>((response_[0] << 8) | response_[1]) : response_[0];
        if (action == Action::GaugeVersion || action == Action::ClearMode || action == Action::VerifyMode ||
            action == Action::ReadConfig || action == Action::ClearSleep || action == Action::VerifyConfig ||
            action == Action::Voltage || action == Action::Soc) {
            checked_ = action; checkedValue_ = value; next_ = Action::I2cStatus;
        } else if (action == Action::I2cStatus) {
            status_.lastI2cStatus = static_cast<uint8_t>(value);
            status_.i2cStatusSeen = true; status_.i2cStatusAtMs = nowMs;
            if (value != 0xF0) { fail(BatteryError::I2cStatus, nowMs); return; }
            finish(checked_, checkedValue_, nowMs);
        } else finish(action, value, nowMs);
    }
    if (next_ == Action::Sync) return;
    // Yield only after the complete I2C transaction AND its status check.
    // Keep the continuation so voltage/SOC chains cannot starve GPIO sampling.
    if (status_.linkReady && next_ != Action::Gpio && next_ != Action::I2cStatus && due(nowMs, nextGpioAt_)) {
        resume_ = next_;
        next_ = Action::Gpio;
    }
    if (next_ == Action::Idle) {
        if (due(nowMs, nextGpioAt_)) next_ = Action::Gpio;
        else if (due(nowMs, nextVoltageAt_)) next_ = Action::Voltage;
        else return;
    }
    start(uart, next_, nowMs);
}

void BatteryTelemetry::finish(Action action, uint16_t value, uint32_t nowMs)
{
    switch (action) {
    case Action::BridgeVersion: {
        if (memcmp(response_, "SC18IM704 ", 10) != 0 || response_[15] != 0) {
            fail(BatteryError::BridgeVersion, nowMs); return;
        }
        for (uint8_t i = 10; i < 15; ++i) {
            if (response_[i] < 0x20 || response_[i] > 0x7E) { fail(BatteryError::BridgeVersion, nowMs); return; }
        }
        memcpy(status_.bridgeVersion, response_, sizeof(response_));
        next_ = Action::ConfigureBridge; break;
    }
    case Action::ConfigureBridge: next_ = Action::VerifyBridge; break;
    case Action::VerifyBridge: {
        const uint8_t expected[] = {0x55, 0x55, 0x13, 0, 0x67};
        if (memcmp(response_, expected, sizeof(expected)) != 0) { fail(BatteryError::BridgeConfig, nowMs); return; }
        next_ = Action::GaugeVersion; break;
    }
    case Action::GaugeVersion:
        if ((value & 0xFFF0) != 0x0010) { fail(BatteryError::GaugeVersion, nowMs); return; }
        status_.gaugeVersion = value;
        next_ = Action::ClearMode; break;
    case Action::ClearMode: next_ = Action::VerifyMode; break;
    case Action::VerifyMode:
        status_.gaugeMode = value;
        if (value & 0x6000) { fail(BatteryError::GaugeConfig, nowMs); return; }
        next_ = Action::ReadConfig; break;
    case Action::ReadConfig:
        status_.gaugeConfig = value;
        config_ = value & static_cast<uint16_t>(~0x0080U);
        next_ = (value & 0x0080) ? Action::ClearSleep : Action::VerifyConfig; break;
    case Action::ClearSleep: next_ = Action::VerifyConfig; break;
    case Action::VerifyConfig:
        status_.gaugeConfig = value;
        // ALRT can assert autonomously between the read and verification.
        if ((value & 0xFFDF) != (config_ & 0xFFDF)) { fail(BatteryError::GaugeConfig, nowMs); return; }
        status_.linkReady = true; status_.lastError = BatteryError::None;
        nextVoltageAt_ = nextGpioAt_ = nowMs;
        next_ = Action::Idle; break;
    case Action::Voltage: {
        const uint32_t mv = (static_cast<uint32_t>(value) * 5U + 32U) / 64U;
        if (mv < 2500 || mv > 4500) { fail(BatteryError::Voltage, nowMs); return; }
        voltage_ = value;
        next_ = Action::Soc; break;
    }
    case Action::Soc:
        soc_ = value; next_ = Action::VerifySample; break;
    case Action::VerifySample:
        // A reset bridge emits OK (0x4F4B), which is also a legal SOC value,
        // and resets I2CStat to F0. Verify our nondefault timeout marker before
        // publishing; do not blacklist legitimate sensor values such as 4F4B.
        if (value != 0x67) { fail(BatteryError::BridgeConfig, nowMs); return; }
        status_.rawVoltage = voltage_; status_.rawSoc = soc_;
        status_.voltageMv = static_cast<uint16_t>((static_cast<uint32_t>(voltage_) * 5U + 32U) / 64U);
        status_.percent = soc_ >= 25600U ? 100 : static_cast<uint8_t>(soc_ / 256U);
        status_.measurementAtMs = nowMs; status_.measurementValid = true;
        nextVoltageAt_ = nowMs + 1000; next_ = Action::Idle; break;
    case Action::Gpio:
        if ((value & 0xFF) != 0x67) { fail(BatteryError::BridgeConfig, nowMs); return; }
        updateGpio(static_cast<uint8_t>(value >> 8), nowMs);
        nextGpioAt_ = nowMs + 75; next_ = resume_; resume_ = Action::Idle; break;
    default: break;
    }
}

void BatteryTelemetry::updateGpio(uint8_t raw, uint32_t nowMs)
{
    status_.rawGpio = raw;
    raw &= 3U;
    const uint8_t chg = raw & 1U;
    // Missing sampling intervals cannot serve as evidence of a 2 Hz signal.
    if (gpioSeen_ && static_cast<uint32_t>(nowMs - previousGpioAt_) > 175) resetGpioHistory();
    if (!gpioSeen_) {
        for (uint8_t bit = 0; bit < 2; ++bit) {
            signalCandidate_[bit] = (raw >> bit) & 1U;
            signalCandidateAt_[bit] = nowMs;
        }
        previousChg_ = chg;
        gpioSeen_ = true;
    } else {
        if (chg != previousChg_) {
            // The physical edge occurred somewhere between the previous and
            // current samples. At 33/40 ms loop cadence the observed timestamp
            // differences alone can be 132/360 ms for a real 250 ms half-cycle.
            if (edgeCount_) {
                const uint32_t shortest = previousGpioAt_ - edgeUpper_[edgeCount_ - 1];
                const uint32_t longest = nowMs - edgeLower_[edgeCount_ - 1];
                if (longest < 200 || shortest > 300) { edgeCount_ = 0; faultCandidate_ = false; }
            }
            if (!edgeCount_) edgeSeriesAt_ = nowMs;
            if (edgeCount_ == 9) {
                for (uint8_t i = 0; i < 8; ++i) { edgeLower_[i] = edgeLower_[i + 1]; edgeUpper_[i] = edgeUpper_[i + 1]; }
                --edgeCount_;
            }
            edgeLower_[edgeCount_] = previousGpioAt_;
            edgeUpper_[edgeCount_] = nowMs;
            ++edgeCount_;
            if (edgeCount_ == 9 && static_cast<uint32_t>(nowMs - edgeSeriesAt_) >= 2000) {
                // Eight intervals also constrain average frequency; broad
                // single-edge uncertainty is not enough to classify a blink.
                const uint32_t shortest = edgeLower_[8] - edgeUpper_[0];
                const uint32_t longest = edgeUpper_[8] - edgeLower_[0];
                faultCandidate_ = longest >= 1600 && shortest <= 2400;
            }
            previousChg_ = chg;
        }
    }
    if (edgeCount_ && static_cast<uint32_t>(nowMs - edgeUpper_[edgeCount_ - 1]) > 750) {
        faultCandidate_ = false; edgeCount_ = 0;
    }
    for (uint8_t bit = 0; bit < 2; ++bit) {
        const uint8_t value = (raw >> bit) & 1U;
        if (value != signalCandidate_[bit]) { signalCandidate_[bit] = value; signalCandidateAt_[bit] = nowMs; }
        if (static_cast<uint32_t>(nowMs - signalCandidateAt_[bit]) >= 75) {
            signalStable_[bit] = value; signalKnown_[bit] = true; signalStableAt_[bit] = nowMs;
        }
        if (static_cast<uint32_t>(nowMs - signalStableAt_[bit]) > kGpioMaxAgeMs) signalKnown_[bit] = false;
    }
    previousGpioAt_ = nowMs;
    // Freshness belongs to the verified frame, not to a static two-bit pattern.
    status_.gpioAtMs = nowMs; status_.gpioValid = true;
    status_.external = !signalKnown_[1] ? ExternalPower::Unknown :
        signalStable_[1] ? ExternalPower::Absent : ExternalPower::Present;
    status_.charge = faultCandidate_ ? ChargeIndicator::FaultCandidate :
        !signalKnown_[0] ? ChargeIndicator::Unknown :
        signalStable_[0] ? ChargeIndicator::Inactive :
        status_.external == ExternalPower::Unknown ? ChargeIndicator::Unknown :
        status_.external == ExternalPower::Absent ? ChargeIndicator::Inconsistent : ChargeIndicator::Active;
}

} // namespace gridopoly::player_console
