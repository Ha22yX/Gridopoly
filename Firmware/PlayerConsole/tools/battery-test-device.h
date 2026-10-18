#pragma once
#include "battery_telemetry.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    // Model the NXP-confirmed failure: any clock/timeout write can freeze the bridge.
    bool freezeOnClockWrite = false, frozen = false, ignorePortWrites = false;
    unsigned forbiddenBridgeWrites = 0;
    void resetBridge() {
        const uint8_t defaults[] = {0x55, 0x55, 0x13, 0, 0x66};
        std::copy(defaults, defaults + 5, ports); frozen = false;
    }
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
        if (frozen) return count;
        if (c == std::vector<uint8_t>{0x56, 0x50}) {
            const char *id = badBridge ? "SC18IM700 9.9.9" : "SC18IM704 9.9.9";
            std::vector<uint8_t> data(id, id + 16);
            if (truncateVersion) data.resize(7);
            if (extraVersion) data.push_back(0x42);
            reply(data, count);
        } else if (c[0] == 0x57) {
            const uint8_t regs[] = {2, 3, 7, 8, 9};
            for (size_t i = 1; i + 1 < count - 1; i += 2) {
                if (c[i] >= 7 && c[i] <= 9) {
                    ++forbiddenBridgeWrites;
                    if (freezeOnClockWrite) { frozen = true; return count; }
                }
                for (size_t j = 0; j < 5; ++j) if (c[i] == regs[j] && !(ignorePortWrites && j < 2)) ports[j] = c[i + 1];
            }
        } else if (c == std::vector<uint8_t>{0x52, 2, 3, 7, 8, 9, 0x50}) {
            std::vector<uint8_t> data(ports, ports + 5);
            if (badConfigReadback) data[2] = 0x12;
            reply(data, count);
        } else if (c == std::vector<uint8_t>{0x52, 4, 2, 0x50}) {
            if (resetOnGpio) { resetBridge(); reply({0x4F, 0x4B}, count); resetOnGpio = false; }
            else reply({gpio(now), ports[0]}, count);
        } else if (c == std::vector<uint8_t>{0x52, 2, 0x50}) {
            reply({ports[0]}, count);
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
            if (c[3] == 4 && resetOnSoc) { resetBridge(); reply({0x4F, 0x4B}, count); resetOnSoc = false; }
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
