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

// A byte/timing fake of the documented device protocol, not a driver mock.
struct Device : BatteryUart {
    struct Byte { uint32_t at; uint8_t value; };
    struct Command { uint32_t at; std::vector<uint8_t> bytes; };
    std::deque<Byte> rx;
    std::vector<Command> commands;
    uint32_t now = 0, i2cDone = 0;
    unsigned reads = 0, maxReads = 0, overlapping = 0, earlyStatus = 0;
    size_t capacity = 256;
    bool respond = true, badBridge = false, badConfigReadback = false;
    bool truncateVersion = false, extraVersion = false, resetOnSoc = false, resetOnGpio = false;
    uint8_t forcedStatus = 0xF0;
    uint8_t ports[5] = {0x55, 0x55, 0x13, 0, 0x66};
    uint16_t mode = 0x2000, config = 0xABDF, version = 0x0012;
    uint16_t voltage = 47360, soc = 78 * 256 + 128;
    std::function<uint8_t(uint32_t)> gpio = [](uint32_t) -> uint8_t { return 3; };
    int readByte() override {
        ++reads;
        if (rx.empty() || static_cast<int32_t>(now - rx.front().at) < 0) return -1;
        const auto value = rx.front().value; rx.pop_front(); return value;
    }
    size_t writableBytes() override { return capacity; }
    void reply(const std::vector<uint8_t>& data, size_t commandBytes) {
        if (!respond) return;
        for (size_t i = 0; i < data.size(); ++i) rx.push_back({now + static_cast<uint32_t>(commandBytes + i + 1), data[i]});
    }
    size_t writeBytes(const uint8_t *bytes, size_t count) override {
        if (!rx.empty()) ++overlapping;
        commands.push_back({now, {bytes, bytes + count}});
        const auto &c = commands.back().bytes;
        if (c == std::vector<uint8_t>{0x56, 0x50}) {
            const char *id = badBridge ? "SC18IM700 9.9.9" : "SC18IM704 9.9.9";
            std::vector<uint8_t> data(id, id + 16);
            if (truncateVersion) data.resize(7);
            if (extraVersion) data.push_back(0x42);
            reply(data, count);
        } else if (c[0] == 0x57) {
            const uint8_t regs[] = {2, 3, 7, 8, 9};
            for (size_t i = 1; i + 1 < count - 1; i += 2)
                for (size_t j = 0; j < 5; ++j) if (c[i] == regs[j]) ports[j] = c[i + 1];
        } else if (c == std::vector<uint8_t>{0x52, 2, 3, 7, 8, 9, 0x50}) {
            std::vector<uint8_t> data(ports, ports + 5);
            if (badConfigReadback) data[4] &= 0xFE;
            reply(data, count);
        } else if (c == std::vector<uint8_t>{0x52, 4, 9, 0x50}) {
            if (resetOnGpio) { ports[4] = 0x66; reply({0x4F, 0x4B}, count); resetOnGpio = false; }
            else reply({gpio(now), ports[4]}, count);
        } else if (c == std::vector<uint8_t>{0x52, 9, 0x50}) {
            reply({ports[4]}, count);
        } else if (c == std::vector<uint8_t>{0x52, 0x0A, 0x50}) {
            if (static_cast<int32_t>(now - i2cDone) < 0) ++earlyStatus;
            reply({forcedStatus}, count);
        } else if (count == 8 && c[0] == 0x53 && c[1] == 0x6C && c[2] == 1 &&
                   c[4] == 0x53 && c[5] == 0x6D && c[6] == 2 && c[7] == 0x50) {
            uint16_t value = 0;
            switch (c[3]) {
            case 2: value = voltage; break; case 4: value = soc; break;
            case 6: value = mode; break; case 8: value = version; break; case 12: value = config; break;
            default: std::printf("Unexpected I2C read register\n"); std::abort();
            }
            i2cDone = now + static_cast<uint32_t>(count);
            if (c[3] == 4 && resetOnSoc) { ports[4] = 0x66; reply({0x4F, 0x4B}, count); resetOnSoc = false; }
            else reply({static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)}, count);
        } else if (count == 7 && c[0] == 0x53 && c[1] == 0x6C && c[2] == 3 && c[6] == 0x50) {
            const auto value = static_cast<uint16_t>((c[4] << 8) | c[5]);
            if (c[3] == 6) mode = value;
            else if (c[3] == 12) config = value;
            else { std::printf("Forbidden gauge write\n"); std::abort(); }
            i2cDone = now + 50; // Status would still be the preceding F0 before this.
        } else { std::printf("Unexpected UART command\n"); std::abort(); }
        return count;
    }
    void run(BatteryTelemetry &driver, uint32_t duration, uint32_t interval = 1) {
        for (uint32_t i = 0; i < duration; i += interval, now += interval) {
            reads = 0; driver.tick(*this, now); maxReads = std::max(maxReads, reads);
        }
    }
};

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
        check(io.mode == 0 && io.config == 0xAB5F && io.ports[4] == 0x67,
              "sleep stays disabled, RCOMP/other CONFIG bits preserved, SCL timeout enabled");
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
        check(d.status(io.now).lastError == BatteryError::Framing, "extra bytes do not spill into the next transaction");
    }
    {
        BatteryTelemetry d; Device io; io.badBridge = true; d.begin(0); io.run(d, 900);
        check(d.status(io.now).lastError == BatteryError::BridgeVersion, "wrong bridge identity is not accepted");
        io.badBridge = false; io.badConfigReadback = true; d.begin(io.now); io.run(d, 1100);
        check(d.status(io.now).lastError == BatteryError::BridgeConfig, "disabled SCL timeout readback blocks initialization");
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
        check(whole && samples == 24 && sink.lines.size() == 120,
              "120-second window emits 24 five-line samples atomically within 63 bytes");
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
        check(sink.lines.size() == 5 && sink.lines.front().find("id=3 ") != std::string::npos,
              "busy USB expires an old sample instead of resuming stale half-lines");
        logs.begin(UINT32_MAX - 2000); sink.lines.clear();
        for (uint32_t dt = 0; dt < 130000; dt += 20) logs.tick(sink, state, UINT32_MAX - 2000 + dt);
        check(sink.lines.size() == 120, "bounded diagnostic cadence survives millis rollover");
        auto disabled = state; disabled.enabled = false; logs.begin(0); sink.lines.clear(); logs.tick(sink, disabled, 1000);
        check(sink.lines.empty(), "disabled telemetry emits no diagnostics");
    }
    std::printf("RESULT passed=%u failed=%u\n", passed, failed);
    return failed ? 1 : 0;
}
