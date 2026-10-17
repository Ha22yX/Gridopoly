#include "battery_telemetry.h"
#include "battery_hardware.h"
#include "battery_diagnostics.h"
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>
using namespace gridopoly::player_console;

#include "battery-test-device.h"

int main()
{
    unsigned failed = 0, passed = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); (ok ? passed : failed)++;
    };
    batteryHardwareBegin(0); batteryHardwareTick(1000);
    check(!batteryHardwareStatus(1000).enabled, "default hardware profile has no UART dependency or fake sample");
    {
        BatteryTelemetry d; Device io; io.run(d, 2000);
        check(io.commands.empty(), "unstarted driver does no I/O");
        d.begin(io.now); io.rx.push_back({io.now, 'O'}); io.rx.push_back({io.now, 'K'});
        io.run(d, 700);
        check(io.commands.empty(), "startup OK is optional and quiet synchronization waits 700 ms");
        io.run(d, 2000);
        const auto s = d.status(io.now);
        check(s.linkReady && s.measurementValid && s.rawVoltage == 47360 && s.voltageMv == 3700 &&
              s.rawSoc == 20096 && s.percent == 78, "big-endian VCELL/SOC conversion and coherent sample publish");
        check(std::strcmp(s.bridgeVersion, "SC18IM704 9.9.9") == 0 && s.gaugeVersion == 0x12,
              "bridge version validates structure without hard-coding a firmware revision");
        check(io.mode == 0 && io.config == 0xAB5F && io.ports[0] == 0x00 && io.ports[4] == 0x66,
              "sleep stays disabled, RCOMP/other CONFIG bits preserved, input-only reset marker and default timeout preserved");
        check(io.earlyStatus == 0 && io.overlapping == 0 && io.maxReads <= 32,
              "one transaction, bounded RX work, and no status read before write completion");
        check(s.gpioValid && s.external == ExternalPower::Absent && s.charge == ChargeIndicator::Inactive,
              "PGOOD high reports no valid external input without claiming a battery exists");
        const auto count = std::count_if(io.commands.begin(), io.commands.end(), [](const Device::Command& c) {
            return c.bytes.size() == 7 && c.bytes[0] == 0x53 && c.bytes[3] == 6;
        });
        io.run(d, 3000);
        const auto after = std::count_if(io.commands.begin(), io.commands.end(), [](const Device::Command& c) {
            return c.bytes.size() == 7 && c.bytes[0] == 0x53 && c.bytes[3] == 6;
        });
        check(count == 1 && after == 1, "normal polling never QuickStarts, resets, or rewrites MODE");
        check(!d.status(io.now + 3501).measurementValid && !d.status(io.now + 351).gpioValid,
              "stale cached samples become unknown even when tick stops");
    }
    for (uint8_t nack : {uint8_t(0xF1), uint8_t(0xF2), uint8_t(0xF8)}) {
        BatteryTelemetry d; Device io; d.begin(0); io.run(d, 2500);
        io.forcedStatus = nack; io.soc = 25600; io.run(d, 1100);
        const auto s = d.status(io.now);
        check(!s.measurementValid && !s.gpioValid && s.lastI2cStatus == nack && s.lastError == BatteryError::I2cStatus,
              "NACK/timeout rejects returned bytes and invalidates previous cached values");
    }
    {
        BatteryTelemetry d; Device io; d.begin(0); io.respond = false; io.run(d, 900);
        check(d.status(io.now).lastError == BatteryError::Timeout && !d.status(io.now).linkReady,
              "missing bridge times out without blocking");
        io.respond = true;
        const uint8_t late[] = {'S','C','1','8','I','M','7','0','4',' ', '1','.','0','.','1',0};
        for (uint8_t byte : late) io.rx.push_back({io.now, byte});
        io.run(d, 2500);
        check(d.status(io.now).measurementValid && io.maxReads <= 32,
              "late reply drains during quiet resync and a new version handshake recovers");
    }
    {
        BatteryTelemetry d; Device io; io.truncateVersion = true; d.begin(0); io.run(d, 1000);
        check(d.status(io.now).lastError == BatteryError::Timeout, "short fixed-length reply is rejected");
        io.truncateVersion = false; io.extraVersion = true; d.begin(io.now); io.run(d, 1000);
        const auto extra = d.status(io.now);
        check(!extra.linkReady && !extra.measurementValid &&
              (extra.firstError == BatteryError::Framing || extra.firstError == BatteryError::BridgeConfig),
              "extra version byte fails framing or exact configuration check before publishing any sample");
    }
    {
        BatteryTelemetry d; Device io; io.badBridge = true; d.begin(0); io.run(d, 900);
        check(d.status(io.now).lastError == BatteryError::BridgeVersion, "wrong bridge identity is not accepted");
        io.badBridge = false; io.badConfigReadback = true; d.begin(io.now); io.run(d, 1100);
        check(d.status(io.now).lastError == BatteryError::BridgeConfig, "unexpected clock readback blocks initialization without risky writes");
    }
    for (uint16_t voltage : {uint16_t(0), uint16_t(65535)}) {
        BatteryTelemetry d; Device io; io.voltage = voltage; d.begin(0); io.run(d, 1500);
        check(!d.status(io.now).measurementValid && d.status(io.now).lastError == BatteryError::Voltage,
              "impossible cell voltage produces unknown, never a fabricated zero percent");
    }
    {
        BatteryTelemetry d; Device io; io.soc = 120 * 256 + 128; d.begin(0); io.run(d, 2000);
        check(d.status(io.now).measurementValid && d.status(io.now).percent == 100 && d.status(io.now).rawSoc == io.soc,
              "display SOC clamps to 100 while retaining raw model estimate");
        io.soc = 0; io.run(d, 1100);
        check(d.status(io.now).measurementValid && d.status(io.now).percent == 0, "verified zero SOC remains distinct from unknown");
        io.gpio = [](uint32_t) -> uint8_t { return 0; }; io.run(d, 300);
        check(d.status(io.now).external == ExternalPower::Present && d.status(io.now).charge == ChargeIndicator::Active,
              "CHG low is an indicator alongside external supply, not measured net current");
        io.gpio = [](uint32_t) -> uint8_t { return 1; }; io.run(d, 300);
        check(d.status(io.now).charge == ChargeIndicator::Inactive && d.status(io.now).percent == 0,
              "CHG high never forces full charge or changes SOC");
        io.gpio = [](uint32_t) -> uint8_t { return 2; }; io.run(d, 300);
        check(d.status(io.now).charge == ChargeIndicator::Inconsistent, "PGOOD high plus CHG low is an anomalous combination");
    }
    {
        BatteryTelemetry d; Device io; d.begin(0); io.run(d, 2000);
        const uint32_t start = io.now;
        io.gpio = [start](uint32_t at) { return static_cast<uint8_t>(((at - start) / 250) & 1U); };
        io.run(d, 1900);
        check(d.status(io.now).charge != ChargeIndicator::FaultCandidate, "fault candidate needs at least two seconds of observed rhythm");
        io.run(d, 1200);
        check(d.status(io.now).charge == ChargeIndicator::FaultCandidate, "approximately 2 Hz CHG toggling is a fault candidate");
        io.now += 300; io.run(d, 200);
        check(d.status(io.now).charge != ChargeIndicator::FaultCandidate, "a sampling gap cannot preserve a claimed fault rhythm");
        io.gpio = [](uint32_t) -> uint8_t { return 1; }; io.run(d, 1000);
        check(d.status(io.now).charge == ChargeIndicator::Inactive, "stable CHG clears the fault candidate");
    }
    for (uint32_t halfPeriod : {125U, 500U}) {
        BatteryTelemetry d; Device io;
        io.gpio = [halfPeriod](uint32_t at) { return static_cast<uint8_t>((at / halfPeriod) & 1U); };
        d.begin(0); io.run(d, 7000);
        check(d.status(io.now).charge != ChargeIndicator::FaultCandidate, "other blink frequencies are not declared a 2 Hz fault");
    }
    {
        BatteryTelemetry d; Device io; io.now = UINT32_MAX - 1000U; d.begin(io.now); io.run(d, 3000);
        check(d.status(io.now).measurementValid && d.status(io.now).gpioValid, "deadlines and freshness survive millis wrap");
        io.respond = false; io.run(d, 2000);
        check(!d.status(io.now).measurementValid && !d.status(io.now).gpioValid, "link loss invalidates both kinds of telemetry");
    }
    {
        BatteryTelemetry d; Device io; io.capacity = 0; d.begin(0); io.run(d, 5000);
        check(io.commands.empty() && !d.status(io.now).measurementValid && io.maxReads <= 32,
              "full TX buffer never waits, writes a partial command, or invents data");
    }
    for (uint32_t interval : {20U, 33U, 40U}) {
        BatteryTelemetry d; Device io; d.begin(0);
        io.gpio = [](uint32_t at) { return static_cast<uint8_t>((at / 250) & 1U); };
        bool detected = false, held = true, externalHeld = true;
        for (unsigned i = 0; i < 12000 / interval; ++i) {
            io.run(d, interval, interval);
            const auto state = d.status(io.now);
            const bool candidate = state.charge == ChargeIndicator::FaultCandidate;
            if (detected) { held &= candidate; externalHeld &= state.external == ExternalPower::Present; }
            detected |= candidate;
        }
        check(detected && held && externalHeld && d.status(io.now).measurementValid,
              "20/33/40 ms cadence detects and continuously holds 2 Hz while PGOOD remains valid");
        io.gpio = [](uint32_t) -> uint8_t { return 1; }; io.run(d, 1000, interval);
        check(d.status(io.now).charge == ChargeIndicator::Inactive,
              "20/33/40 ms cadence exits the candidate after blinking stops");
    }
    {
        BatteryTelemetry d; Device io; d.begin(0); io.run(d, 2000);
        io.resetOnSoc = true;
        bool rejected = false, publishedOk = false;
        for (unsigned i = 0; i < 1200; ++i) {
            io.run(d, 1);
            const auto s = d.status(io.now);
            rejected |= s.lastError == BatteryError::BridgeConfig;
            publishedOk |= s.measurementValid && s.rawSoc == 0x4F4B;
        }
        check(rejected && !publishedOk, "bridge startup OK plus reset F0 cannot masquerade as SOC");
        io.run(d, 2000); io.soc = 0x4F4B; io.run(d, 1200);
        check(d.status(io.now).measurementValid && d.status(io.now).rawSoc == 0x4F4B,
              "real SOC 4F4B is accepted when the bridge configuration marker is intact");
        io.resetOnGpio = true; io.run(d, 150);
        check(!d.status(io.now).gpioValid && d.status(io.now).lastError == BatteryError::BridgeConfig,
              "bridge startup OK cannot masquerade as valid GPIO telemetry");
    }
    {
        BatteryTelemetry driver; Device io; driver.begin(0); io.run(driver, 3000);
        const auto state = driver.status(io.now);
        check(state.gaugeMode == io.mode && state.gaugeConfig == io.config && state.rawGpio == 3,
              "diagnostic MODE CONFIG and raw GPIO are verified readbacks");
        struct Sink {
            int capacity = 64;
            std::vector<std::string> lines;
            int availableForWrite() { return capacity; }
            size_t write(const uint8_t *bytes, size_t size) { lines.emplace_back(reinterpret_cast<const char *>(bytes), size); return size; }
        } sink;
        BatteryDiagnostics logs; logs.begin(0);
        for (uint32_t now = 0; now < 130000; now += 20) logs.tick(sink, state, now);
        bool whole = true; unsigned samples = 0;
        for (const auto &line : sink.lines) {
            whole &= line.size() <= 63 && line.back() == '\n' && line.find("GP_BAT id=") == 0;
            if (line.find(" ms=") != std::string::npos) ++samples;
        }
        check(whole && samples == 24 && sink.lines.size() == 240,
              "120-second window emits 24 ten-line samples atomically within 63 bytes");
        check(sink.lines[0].find("id=1 ") != std::string::npos && sink.lines[4].find("id=1 ") != std::string::npos &&
              sink.lines[4].find("mode=0000") != std::string::npos,
              "all telemetry detail lines share one sample ID and include gauge setup evidence");
        const auto ended = sink.lines.size(); logs.tick(sink, state, 500000);
        check(sink.lines.size() == ended, "diagnostic window cannot silently restart later");
        logs.begin(0); sink.lines.clear(); sink.capacity = 0;
        for (uint32_t now = 0; now < 7000; now += 20) logs.tick(sink, state, now);
        check(sink.lines.empty(), "full USB TX cannot trigger blocking or partial writes");
        sink.capacity = 64;
        for (uint32_t now = 7100; now < 11200; now += 20) logs.tick(sink, state, now);
        check(sink.lines.size() == 10 && sink.lines.front().find("id=3 ") != std::string::npos,
              "busy USB expires an old sample instead of resuming stale half-lines");
        logs.begin(UINT32_MAX - 2000); sink.lines.clear();
        for (uint32_t dt = 0; dt < 130000; dt += 20) logs.tick(sink, state, UINT32_MAX - 2000 + dt);
        check(sink.lines.size() == 240, "bounded diagnostic cadence survives millis rollover");
        auto disabled = state; disabled.enabled = false; logs.begin(0); sink.lines.clear(); logs.tick(sink, disabled, 1000);
        check(sink.lines.empty(), "disabled telemetry emits no diagnostics");
    }

    {
        BatteryTelemetry d; Device io; d.begin(0); io.run(d, 701);
        const auto initial = d.status(io.now);
        check(!initial.i2cStatusSeen && initial.lastI2cStatus == 0,
              "unread I2C status zero is explicitly different from a received zero");
        io.now += 250; d.tick(io, io.now);
        auto failureState = d.status(io.now);
        check(failureState.lastError == BatteryError::Timeout && failureState.timeoutCount == 1 &&
              failureState.failedAction == static_cast<uint8_t>(BatteryAction::BridgeVersion) &&
              failureState.failedOperation == failureState.failedAction && failureState.failedExpected == 16 && failureState.failedReceived == 16,
              "complete buffered response after 180ms is still rejected with frozen failure stage and RX counts");
        check(failureState.maxServiceGapMs == 251 && failureState.failureAtMs == io.now,
              "actual service gap and failure time are recorded");
        io.run(d, 2500); const auto recovered = d.status(io.now);
        check(recovered.measurementValid && recovered.timeoutCount == 1 && recovered.failureAtMs == failureState.failureAtMs &&
              recovered.i2cStatusSeen && recovered.i2cStatusAtMs > failureState.failureAtMs,
              "recovery preserves cumulative failure evidence and timestamps real I2C status");
        const auto stale = batteryStatusAt(recovered, io.now + 3501);
        check(!stale.measurementValid && !stale.gpioValid && stale.charge == ChargeIndicator::Unknown &&
              recovered.measurementValid, "copied snapshot freshness invalidates without mutating the owner");
    }
    {
        BatteryTelemetry d; Device io; io.forcedStatus = 0; d.begin(0); io.run(d, 1100);
        const auto s = d.status(io.now);
        check(s.i2cStatusSeen && s.lastI2cStatus == 0 && s.lastError == BatteryError::I2cStatus &&
              s.failedAction == static_cast<uint8_t>(BatteryAction::I2cStatus) &&
              s.failedOperation == static_cast<uint8_t>(BatteryAction::GaugeVersion) && s.failedReceived == 1 &&
              s.failedExpected == 1 && s.timeoutCount == 0,
              "actual I2C zero identifies the gauge operation and is not counted as UART timeout");
    }
    {
        struct Sink {
            std::vector<std::string> lines;
            int availableForWrite() { return 64; }
            size_t write(const uint8_t *p, size_t n) { lines.emplace_back(reinterpret_cast<const char *>(p), n); return n; }
        } sink;
        BatteryStatus s; s.enabled = true; s.serviceStarted = true; s.i2cStatusSeen = true;
        s.timeoutCount = s.maxServiceGapMs = s.failureAtMs = s.i2cStatusAtMs = UINT32_MAX;
        s.failedAction = s.failedOperation = s.failedExpected = s.failedReceived = 255;
        s.firstAction = s.firstOperation = s.firstExpected = s.firstReceived = 255;
        s.firstError = static_cast<BatteryError>(255);
        s.firstFailureAtMs = s.bridgeConfiguredAtMs = UINT32_MAX;
        s.bridgeConfigSeen = true; s.bridgeTimeoutRegister = 0x67; s.bridgeSclTimeoutEnabled = true;
        BatteryDiagnostics logs; logs.begin(UINT32_MAX - 1000);
        for (uint32_t dt = 1000; dt < 1200; dt += 20) logs.tick(sink, s, UINT32_MAX - 1000 + dt);
        bool whole = sink.lines.size() == 10;
        for (const auto &line : sink.lines) whole &= line.size() <= 63 && line.back() == '\n';
        check(whole && sink.lines[4].find("cached=1") != std::string::npos &&
              sink.lines[5].find("to=4294967295") != std::string::npos &&
              sink.lines[6].find("rx=255/255") != std::string::npos &&
              sink.lines[7].find("i_seen=1") != std::string::npos &&
              sink.lines[8].find("hw_to=1") != std::string::npos &&
              sink.lines[9].find("first=255/255") != std::string::npos,
              "maximum diagnostic counters fit whole short lines and distinguish cached configuration");
    }
    for (uint8_t timeout : {uint8_t(0x66), uint8_t(0x67)}) {
        BatteryTelemetry d; Device io; io.freezeOnClockWrite = true; io.ports[4] = timeout;
        d.begin(0); io.run(d, 5000); const auto s = d.status(io.now);
        check(s.measurementValid && !io.frozen && io.forbiddenBridgeWrites == 0,
              "write-freeze affected bridge initializes and polls without any 07/08/09 write");
        check(s.bridgeConfigSeen && s.bridgeTimeoutRegister == timeout &&
              s.bridgeSclTimeoutEnabled == ((timeout & 1) != 0) && io.ports[4] == timeout,
              "read-only timeout capability distinguishes default disabled from previously enabled");
        bool inputOnly = true;
        for (const auto &c : io.commands) if (c.bytes[0] == 0x57)
            inputOnly &= c.bytes == std::vector<uint8_t>{0x57, 2, 0, 3, 0x55, 0x50};
        check(inputOnly && io.ports[0] == 0 && io.ports[1] == 0x55,
              "configuration only changes input-only ports, preserving CHG PGOOD and GPIO6");
        io.respond = false; io.run(d, 1000); io.respond = true; io.run(d, 3000);
        check(d.status(io.now).measurementValid && !io.frozen && io.forbiddenBridgeWrites == 0,
              "retry paths also avoid forbidden clock and timeout writes");
    }
    {
        Device io; io.freezeOnClockWrite = true;
        const uint8_t old[] = {0x57,2,0x55,3,0x55,7,0x13,8,0,9,0x67,0x50};
        io.writeBytes(old, sizeof(old));
        check(io.frozen && io.forbiddenBridgeWrites == 1,
              "positive control reproduces documented register-write freeze with old setup");
    }
    for (unsigned index : {0U, 1U, 2U, 3U, 4U}) {
        BatteryTelemetry d; Device io; io.freezeOnClockWrite = true; io.ports[index] = 0x42;
        d.begin(0); io.run(d, 2000); const auto s = d.status(io.now);
        const bool wrote = std::any_of(io.commands.begin(), io.commands.end(), [](const auto &c) { return c.bytes[0] == 0x57; });
        check(!s.measurementValid && s.lastError == BatteryError::BridgeConfig &&
              s.failedAction == static_cast<uint8_t>(BatteryAction::InspectBridge) && !wrote,
              "unexpected port clock or timeout is rejected before configuration writes");
    }
    {
        BatteryTelemetry d; Device io; io.badConfigReadback = true; d.begin(0); io.run(d, 1000);
        const auto first = d.status(io.now); io.respond = false; io.run(d, 1500);
        const auto later = d.status(io.now);
        check(first.firstError == BatteryError::BridgeConfig &&
              first.firstAction == static_cast<uint8_t>(BatteryAction::InspectBridge) &&
              later.lastError == BatteryError::Timeout && later.firstError == first.firstError &&
              later.firstAction == first.firstAction && later.firstFailureAtMs == first.firstFailureAtMs,
              "later version timeout cannot erase first initialization failure");
        io.badConfigReadback = false; io.respond = true; io.run(d, 2500);
        check(d.status(io.now).measurementValid && d.status(io.now).firstError == first.firstError,
              "successful recovery preserves the first-failure latch");
    }
    std::printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
