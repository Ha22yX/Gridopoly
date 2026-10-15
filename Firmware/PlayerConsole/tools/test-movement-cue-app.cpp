#include "movement_cue_app_tests.h"
#include <cstdio>
int main() {
    bool passed = true;
    for (uint8_t size : {16, 24}) {
        passed &= runMovementCueAppTests([&](bool ok, const char *name) {
            std::printf("[%s] board=%u %s\n", ok ? "PASS" : "FAIL", size, name); return ok;
        }, size);
    }
    return passed ? 0 : 1;
}
