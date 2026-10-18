#pragma once
#include <stdint.h>

namespace gridopoly::player_console {

// Sample the two phases together. The installed encoder reports a detent at
// both equal-phase positions, matching the previous driver's half-step scale.
// Track the electrical path separately from publishing a mechanical detent.
// Uncommitted backtracking cancels; a committed step never locks out reversal.
class RotaryQuadratureDecoder {
public:
    // Initial software setting, not a measured specification of this encoder.
    static constexpr uint32_t kDetentStableMs = 5;

    void reset(uint8_t phases, uint32_t atMs = 0)
    {
        stable_ = detent_ = committedDetent_ = phases & 3U;
        endpointAtMs_ = lastSampleAtMs_ = atMs;
        quarters_ = 0;
        pendingSteps_ = 0;
        synchronized_ = isDetent(stable_);
        invalidTransitions_ = 0;
    }

    int8_t sample(uint8_t phases, uint32_t atMs)
    {
        phases &= 3U;
        const uint32_t sampleGap = atMs - lastSampleAtMs_;
        lastSampleAtMs_ = atMs;
        // A 1 ms timer can straddle millis boundaries. Longer blind intervals
        // cannot establish that a contact stayed stable while we were absent.
        if (sampleGap > 2U) endpointAtMs_ = atMs;
        if (phases != stable_) {
            endpointAtMs_ = atMs;
            const int8_t step = acceptPhase(phases);
            if ((step > 0 && pendingSteps_ == INT32_MAX) ||
                (step < 0 && pendingSteps_ == INT32_MIN)) {
                // An unbounded unsettled run cannot wrap its count
                // into the opposite direction. Discard it and reacquire.
                pendingSteps_ = 0;
                detent_ = committedDetent_;
                synchronized_ = false;
            } else pendingSteps_ += step;
            return 0;
        }
        if (sampleGap == 0 || !isDetent(phases) || pendingSteps_ == 0 ||
            static_cast<uint32_t>(atMs - endpointAtMs_) < kDetentStableMs) return 0;
        // Multiple short detents can precede the first settled endpoint. Keep
        // their signed displacement, then publish at most one step per sample
        // so the existing adapter's event/counter contract remains unchanged.
        const int8_t step = pendingSteps_ > 0 ? 1 : -1;
        pendingSteps_ -= step;
        committedDetent_ ^= 3U;
        return step;
    }

    uint32_t invalidTransitions() const { return invalidTransitions_; }
    // Electrical path state, not evidence that a detent has been published.
    uint8_t stablePhases() const { return stable_; }

private:
    static bool isDetent(uint8_t phases) { return phases == 0 || phases == 3; }

    int8_t acceptPhase(uint8_t phases)
    {
        const uint8_t previous = stable_;
        stable_ = phases;
        if ((previous ^ phases) == 3U) {
            ++invalidTransitions_;
            quarters_ = 0;
            pendingSteps_ = 0;
            detent_ = committedDetent_;
            synchronized_ = isDetent(phases);
            // Retain the published anchor, not an unconfirmed excursion. The
            // unknown jump must not turn a return to that anchor into a step.
            return 0;  // A missed/invalid pair cannot establish direction.
        }
        if (!synchronized_) {
            if (isDetent(phases)) {
                synchronized_ = true;
                // Startup between detents has no anchor yet. Recovery from a
                // missed edge must retain an existing one across partial paths.
                if (!isDetent(detent_)) detent_ = committedDetent_ = phases;
            }
            quarters_ = 0;
            return 0;
        }
        static constexpr int8_t transitions[16] = {
             0, -1,  1,  0,
             1,  0,  0, -1,
            -1,  0,  0,  1,
             0,  1, -1,  0
        };
        quarters_ += transitions[(previous << 2U) | phases];
        if (!isDetent(phases)) return 0;
        // Reverse once at the hardware boundary for the installed bezel.
        // A complete legal half-cycle must connect distinct trusted detents.
        // In particular, 3 -> (invalid 0) -> 2 -> 3 is a return, not a step.
        const int8_t step = phases == detent_ ? 0 :
            (quarters_ == 2 ? -1 : quarters_ == -2 ? 1 : 0);
        // A zero-net return at an untrusted endpoint cannot promote it into
        // an anchor. Only a complete half-cycle can validate that endpoint.
        if (quarters_ == 2 || quarters_ == -2) detent_ = phases;
        quarters_ = 0;
        return step;
    }

    uint8_t stable_ = 3, detent_ = 3, committedDetent_ = 3;
    uint32_t endpointAtMs_ = 0, lastSampleAtMs_ = 0, invalidTransitions_ = 0;
    int32_t pendingSteps_ = 0;
    int8_t quarters_ = 0;
    bool synchronized_ = true;
};

} // namespace gridopoly::player_console
