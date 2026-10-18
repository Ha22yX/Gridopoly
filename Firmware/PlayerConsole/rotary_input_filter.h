#pragma once
#include <stdint.h>

namespace gridopoly::player_console {

// Sample the two phases together. The installed encoder reports a detent at
// both equal-phase positions, matching the previous driver's half-step scale.
// Adjacent contact bounce cancels electrically; never discard just one sign.
class RotaryQuadratureDecoder {
public:
    void reset(uint8_t phases, uint32_t atMs = 0)
    {
        stable_ = candidate_ = detent_ = phases & 3U;
        candidateAtMs_ = atMs;
        quarters_ = 0;
        synchronized_ = isDetent(stable_);
        invalidTransitions_ = 0;
    }

    int8_t sample(uint8_t phases, uint32_t atMs)
    {
        phases &= 3U;
        if (phases != candidate_) {
            // A short intermediate phase may have only one sample before the
            // next detent arrives. Preserve that OBSERVED legal Gray path:
            // detent -> intermediate -> opposite detent. The intermediate
            // emits nothing; the endpoint still needs its own later sample.
            // A direct two-bit jump or a return to the same detent is never
            // repaired by inventing an edge. Same-time catch-up is not proof.
            if (isDetent(stable_) && !isDetent(candidate_) &&
                phases == (stable_ ^ 3U) &&
                static_cast<uint32_t>(atMs - candidateAtMs_) >= 1U) {
                acceptPhase(candidate_);
            }
            candidate_ = phases;
            candidateAtMs_ = atMs;
            return 0;
        }
        // A fresh observation at least 1 ms later is required. Timer catch-up
        // calls at the same time cannot turn one observation into debounce.
        if (phases == stable_ ||
            static_cast<uint32_t>(atMs - candidateAtMs_) < 1U) return 0;
        return acceptPhase(phases);
    }

    uint32_t invalidTransitions() const { return invalidTransitions_; }
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
            synchronized_ = isDetent(phases);
            // Keep the last detent reached by a legal path. The unknown jump
            // must not make a return to that same detent count as another step.
            return 0;  // A missed/invalid pair cannot establish direction.
        }
        if (!synchronized_) {
            if (isDetent(phases)) {
                synchronized_ = true;
                // Startup between detents has no anchor yet. Recovery from a
                // missed edge must retain an existing one across partial paths.
                if (!isDetent(detent_)) detent_ = phases;
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

    uint8_t stable_ = 3, candidate_ = 3, detent_ = 3;
    uint32_t candidateAtMs_ = 0, invalidTransitions_ = 0;
    int8_t quarters_ = 0;
    bool synchronized_ = true;
};

} // namespace gridopoly::player_console
