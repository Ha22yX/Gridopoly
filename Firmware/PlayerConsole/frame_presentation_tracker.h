#pragma once
#include <stdint.h>

// Caller provides synchronization. A framebuffer switch alone is insufficient:
// vendor completion may mean DMA queued, not that the final rows were scanned.
// Observe two subsequent frame-complete boundaries to cover that pipeline.
struct FramePresentationTracker {
    uint32_t requested = 0;
    uint32_t switched = 0;
    uint32_t queued = 0;
    uint32_t presented = 0;
    uint32_t request() {
        if (++requested == 0) ++requested;
        return requested;
    }
    void bufferSwitchCompleted(bool accepted) {
        if (accepted) switched = requested;
    }
    void hardwareFrameBoundary() { presented = queued; queued = switched; }
    bool completed(uint32_t ticket) const {
        return ticket != 0 && presented != 0 &&
               static_cast<int32_t>(presented - ticket) >= 0;
    }
};
