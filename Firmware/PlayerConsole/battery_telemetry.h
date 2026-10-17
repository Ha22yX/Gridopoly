#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gridopoly::player_console {

// No Arduino, allocation, delays or logging in the protocol/state layer.
class BatteryUart {
public:
    virtual ~BatteryUart() = default;
    virtual int readByte() = 0; // -1 when no byte is immediately available
    virtual size_t writableBytes() = 0;
    virtual size_t writeBytes(const uint8_t *bytes, size_t count) = 0;
};

enum class ExternalPower : uint8_t { Unknown, Absent, Present };
enum class ChargeIndicator : uint8_t { Unknown, Inactive, Active, Inconsistent, FaultCandidate };
enum class BatteryError : uint8_t {
    None, Timeout, Framing, BridgeVersion, BridgeConfig, GaugeVersion,
    I2cStatus, GaugeConfig, Voltage, TxIncomplete, ServiceUnavailable
};

// Stable diagnostic codes for the active wire transaction and checked operation.
enum class BatteryAction : uint8_t {
    Sync, BridgeVersion, ConfigureBridge, VerifyBridge, GaugeVersion,
    ClearMode, VerifyMode, ReadConfig, ClearSleep, VerifyConfig,
    Voltage, Soc, VerifySample, Gpio, Idle, I2cStatus, InspectBridge
};

struct BatteryStatus {
    bool enabled = false;
    bool linkReady = false;
    bool measurementValid = false;
    bool gpioValid = false;
    uint16_t rawVoltage = 0;
    uint16_t rawSoc = 0;
    uint16_t voltageMv = 0;
    uint8_t percent = 0;
    uint16_t gaugeVersion = 0;
    uint16_t gaugeMode = 0, gaugeConfig = 0;
    uint8_t rawGpio = 0;
    char bridgeVersion[16]{};
    uint32_t measurementAtMs = 0;
    uint32_t gpioAtMs = 0;
    ExternalPower external = ExternalPower::Unknown;
    ChargeIndicator charge = ChargeIndicator::Unknown;
    BatteryError lastError = BatteryError::None;
    uint8_t lastI2cStatus = 0;
    bool i2cStatusSeen = false, serviceStarted = false;
    uint32_t i2cStatusAtMs = 0, serviceAtMs = 0, maxServiceGapMs = 0;
    uint32_t timeoutCount = 0, failureAtMs = 0;
    uint8_t failedAction = 0xFF, failedOperation = 0xFF;
    uint8_t failedExpected = 0, failedReceived = 0;
    // Retain the first failure even when subsequent recovery attempts fail differently.
    BatteryError firstError = BatteryError::None;
    uint8_t firstAction = 0xFF, firstOperation = 0xFF, firstExpected = 0, firstReceived = 0;
    uint32_t firstFailureAtMs = 0;
    // Cached configuration evidence. A 0x66 readback means no hardware SCL timeout.
    bool bridgeConfigSeen = false, bridgeSclTimeoutEnabled = false;
    uint8_t bridgeTimeoutRegister = 0;
    uint32_t bridgeConfiguredAtMs = 0;
};

// Apply age limits to a copied, coherent sample without touching its owner.
BatteryStatus batteryStatusAt(BatteryStatus value, uint32_t nowMs);

class BatteryTelemetry {
public:
    void begin(uint32_t nowMs);
    void tick(BatteryUart &uart, uint32_t nowMs);
    BatteryStatus status(uint32_t nowMs) const;
    static constexpr uint32_t kMeasurementMaxAgeMs = 3000;
    static constexpr uint32_t kGpioMaxAgeMs = 350;

private:
    using Action = BatteryAction;
    bool start(BatteryUart &uart, Action action, uint32_t nowMs);
    void finish(Action action, uint16_t value, uint32_t nowMs);
    void fail(BatteryError error, uint32_t nowMs);
    void updateGpio(uint8_t raw, uint32_t nowMs);
    void resetGpioHistory();

    BatteryStatus status_{};
    Action next_ = Action::Sync, pending_ = Action::Sync, checked_ = Action::Sync;
    Action resume_ = Action::Idle;
    uint8_t response_[16]{};
    uint8_t expected_ = 0, received_ = 0;
    bool waiting_ = false, serviceSeen_ = false;
    uint32_t sentAt_ = 0, lastByteAt_ = 0, quietAt_ = 0;
    uint32_t nextVoltageAt_ = 0, nextGpioAt_ = 0;
    uint16_t checkedValue_ = 0, config_ = 0, voltage_ = 0, soc_ = 0;
    bool gpioSeen_ = false, faultCandidate_ = false;
    bool signalKnown_[2]{};
    uint8_t signalCandidate_[2]{}, signalStable_[2]{};
    uint32_t signalCandidateAt_[2]{}, signalStableAt_[2]{};
    uint8_t previousChg_ = 1, edgeCount_ = 0;
    uint32_t previousGpioAt_ = 0, edgeSeriesAt_ = 0;
    uint32_t edgeLower_[9]{}, edgeUpper_[9]{};
};

} // namespace gridopoly::player_console
