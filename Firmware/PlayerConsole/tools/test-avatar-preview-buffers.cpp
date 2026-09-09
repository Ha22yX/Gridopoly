#include "avatar_preview_buffers.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks;
static void expect(bool condition, const char *name)
{
    ++checks;
    if (!condition) { std::printf("FAIL %s\n", name); std::exit(1); }
}

int main()
{
    AvatarPreviewBuffers buffers;
    uint8_t output = 99;
    expect(buffers.begin(1, output) && output == 1, "compose only into back");
    expect(!buffers.begin(2, output), "only one compositor owns a buffer");
    expect(buffers.finish(1, 1, true), "complete current recipe");
    expect(buffers.front() == 0, "worker completion does not change LVGL front");
    expect(!buffers.begin(2, output), "completed back cannot be recycled before UI acquisition");
    expect(buffers.acquire(1) && buffers.front() == 1, "UI explicitly switches front");
    expect(!buffers.acquire(1), "publication is consumed once");
    expect(buffers.begin(2, output) && output == 0, "retired front becomes next back only after UI switch");
    expect(!buffers.finish(2, 3, true) && buffers.front() == 1, "late recipe never replaces current picture");
    expect(buffers.begin(3, output) && buffers.finish(3, 3, true), "new recipe completes");
    buffers.discardPending();
    expect(!buffers.acquire(4) && buffers.front() == 1, "desired recipe change discards unshown result");
    expect(buffers.begin(4, output) && !buffers.finish(4, 4, false), "failed or released composition never publishes");
    expect(buffers.begin(5, output) && buffers.finish(5, 5, true), "retry can complete after failure");
    expect(!buffers.acquire(6) && buffers.front() == 1, "generation is checked again on acquisition");

    // Deterministic interleavings exercise the production ownership class with
    // two real pixel values: worker writes may never change the visible value.
    buffers = AvatarPreviewBuffers{};
    uint32_t pixels[2] = {0, 0}, desired = 1, active = 0, shown = 0;
    uint8_t writing = 1;
    uint32_t random = 0x89127836;
    for (unsigned i = 0; i < 20000; ++i) {
        random = random * 1664525u + 1013904223u;
        switch ((random >> 24) % 4) {
            case 0:
                ++desired;
                buffers.discardPending();
                break;
            case 1:
                if (buffers.begin(desired, writing)) active = desired;
                break;
            case 2:
                if (buffers.composing()) {
                    expect(writing != buffers.front(), "worker never writes displayed pixels");
                    pixels[writing] = active;
                    buffers.finish(active, desired, true);
                }
                break;
            case 3:
                if (buffers.acquire(desired)) {
                    shown = desired;
                    expect(pixels[buffers.front()] == desired, "UI acquires exactly its requested generation");
                }
                break;
        }
        expect(pixels[buffers.front()] == shown, "front is immutable between UI acquisitions");
    }
    std::printf("PASS %u ownership checks across 20000 interleaved actions\n", checks);
    return 0;
}
