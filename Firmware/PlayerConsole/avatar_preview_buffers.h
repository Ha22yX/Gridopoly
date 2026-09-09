#pragma once

#include <cstdint>

// Call under the avatar cache mutex. Only the LVGL owner may acquire a completed
// buffer: until then both the displayed front and completed back stay immutable.
class AvatarPreviewBuffers {
public:
    bool begin(uint32_t generation, uint8_t &output)
    {
        if (composing_ || pending_) return false;
        composing_ = true;
        activeGeneration_ = generation;
        output = static_cast<uint8_t>(front_ ^ 1u);
        return true;
    }

    bool finish(uint32_t generation, uint32_t desiredGeneration, bool success)
    {
        if (!composing_ || generation != activeGeneration_) return false;
        composing_ = false;
        pending_ = success && generation == desiredGeneration;
        if (pending_) pendingGeneration_ = generation;
        return pending_;
    }

    bool acquire(uint32_t desiredGeneration)
    {
        if (!pending_) return false;
        pending_ = false;
        if (pendingGeneration_ != desiredGeneration) return false;
        front_ ^= 1u;
        return true;
    }

    void discardPending() { pending_ = false; }
    bool composing() const { return composing_; }
    bool pending() const { return pending_; }
    uint8_t front() const { return front_; }

private:
    uint32_t activeGeneration_ = 0;
    uint32_t pendingGeneration_ = 0;
    uint8_t front_ = 0;
    bool composing_ = false;
    bool pending_ = false;
};
