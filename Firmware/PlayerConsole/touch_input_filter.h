#pragma once
#include <stdint.h>

// -1 means communication failed, not a physical release. Keep the last UI
// state briefly, then release controls without inventing an ink sample.
struct TouchInputState {
    int16_t x = 0, y = 0;
    uint32_t lastValidMs = 0;
    bool pressed = false;
    bool update(int count, int16_t nextX, int16_t nextY, uint32_t nowMs) {
        if (count < 0 || (count > 0 &&
            (nextX < 0 || nextY < 0 || nextX >= 480 || nextY >= 480))) {
            if (static_cast<uint32_t>(nowMs - lastValidMs) >= 100) pressed = false;
            return false;
        }
        lastValidMs = nowMs;
        pressed = count > 0;
        if (pressed) { x = nextX; y = nextY; }
        return true;
    }
};
