#pragma once

#include <stdint.h>

namespace gridopoly::player_console {

constexpr uint32_t kRotaryOppositeGlitchWindowMs = 35;

class RotaryInputFilter {
public:
    bool accept(int16_t delta, uint32_t atMs)
    {
        if (delta == 0) return false;

        const int8_t direction = delta > 0 ? 1 : -1;
        if (lastDirection_ != 0 && direction != lastDirection_ &&
            static_cast<uint32_t>(atMs - lastAcceptedAtMs_) <=
                kRotaryOppositeGlitchWindowMs) {
            return false;
        }

        lastDirection_ = direction;
        lastAcceptedAtMs_ = atMs;
        return true;
    }

    void reset()
    {
        lastDirection_ = 0;
        lastAcceptedAtMs_ = 0;
    }

private:
    int8_t lastDirection_ = 0;
    uint32_t lastAcceptedAtMs_ = 0;
};

} // namespace gridopoly::player_console
