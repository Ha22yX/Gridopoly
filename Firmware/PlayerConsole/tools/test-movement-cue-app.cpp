#include "movement_cue_app_tests.h"
#include <cstdio>
int main() {
    return runMovementCueAppTests([](bool ok, const char *name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); return ok;
    }) ? 0 : 1;
}
