#include "rotary_input_tests.h"
#include <cstdio>
int main() {
    return runRotaryInputDecoderTests([](bool ok, const char *name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); return ok;
    }) ? 0 : 1;
}
