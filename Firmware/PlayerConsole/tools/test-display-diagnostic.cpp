#include "../display_diagnostic.h"
#include <cstdio>

int main()
{
    int checked = 0, failed = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checked;
        if (!ok) { ++failed; std::printf("FAIL %s\n", name); }
    };
    DisplayDiagnosticWindow window;
    check(!window.sampleDue(100), "no immediate startup print");
    check(!window.sampleDue(5099), "five-second sampling boundary");
    check(window.sampleDue(5100), "first sample");
    check(!window.sampleDue(5100), "no duplicate in same loop");
    check(window.sampleDue(50100), "one sample after delayed loop");
    check(!window.sampleDue(50101), "no catch-up burst");
    check(window.sampleDue(120100) && window.finished, "final sample at deadline");
    check(!window.sampleDue(125100), "no sampling after deadline");
    check(!window.sampleDue(100), "finished window cannot restart after wrap");

    DisplayDiagnosticWindow wrapped;
    const uint32_t start = UINT32_MAX - 1000u;
    check(!wrapped.sampleDue(start), "start near millis wrap");
    check(wrapped.sampleDue(start + 5000u), "sampling across wrap");
    check(wrapped.sampleDue(start + 120000u) && wrapped.finished,
          "deadline across wrap");

    DisplayDiagnosticWindow stalled;
    check(!stalled.sampleDue(0), "start at zero");
    check(stalled.sampleDue(180000) && stalled.finished,
          "a stalled loop still ends the window");
    check(!stalled.sampleDue(180001), "no burst after a stalled deadline");
    std::printf("DISPLAY DIAGNOSTIC %d/%d PASS\n", checked - failed, checked);
    return failed != 0;
}
