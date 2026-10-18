// Include the actual adapter in this test TU so saturation can be exercised
// without billions of callbacks or production-only test mutation APIs.
#include "hardware_input.cpp"
#include "input_dispatch.h"
#include <cstdio>
#include <vector>

namespace {
unsigned failed = 0, checks = 0;
void check(bool ok, const char *label)
{
    ++checks; failed += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
void raw(unsigned phase, unsigned samples = 1)
{
    for (unsigned i = 0; i < samples; ++i) {
        fakeGpio = (((phase >> 1U) & 1U) << 6U) | ((phase & 1U) << 5U);
        ++fakeMs; fakeUs += 1000; sampleCallback(nullptr);
    }
}
void reset()
{
    raw(3, 2);
    hardwareInputTestReset();
}
std::vector<InputEvent> drain()
{
    std::vector<InputEvent> events;
    InputEvent event;
    while (hardwareInputPoll(event)) events.push_back(event);
    return events;
}
}

int main()
{
    fakeGpio = (1U << 6U) | (1U << 5U);
    check(hardwareInputBegin(), "production adapter initializes with isolated timer/GPIO stubs");
    reset();
    for (int i = 0; i < 10; ++i) { raw(1, 2); raw(0, 2); raw(2, 2); raw(3, 2); }
    auto events = drain();
    bool ordered = events.size() == 20;
    for (auto event : events) ordered &= event.delta == -1;
    check(ordered, "20 qualified detents produce 20 correctly ordered events");

    reset();
    for (int i = 0; i < 5; ++i) { raw(1); raw(0, 50); raw(2); raw(3, 50); }
    events = drain();
    HardwareInputDiagnostic diagnostic;
    unsigned invalid = 0;
    while (hardwareInputPollDiagnostic(diagnostic)) invalid = diagnostic.invalidTransitions;
    check(events.empty() && invalid == 10,
          "synthetic 1ms intermediate phases and 50ms detent dwell yield 0 steps and 10 invalids");
    reset(); fakeMs += 25; fakeUs += 25000; raw(0, 2);
    events = drain(); invalid = 0;
    while (hardwareInputPollDiagnostic(diagnostic)) invalid = diagnostic.invalidTransitions;
    check(events.empty() && invalid == 1, "missed intermediate phase does not guess a direction");

    reset();
    for (int i = 0; i < 500; ++i)
        hardwareInputTestEnqueue({InputKind::Rotate, 1, static_cast<uint32_t>(i)});
    unsigned consumed = 0, turns = 0, maxBatch = 0;
    while (hardwareInputPending()) {
        const auto batch = dispatchInputBatch(hardwareInputPoll, [&](const InputEvent &event) {
            ++consumed; if (event.delta != 1) ++failed;
        });
        if (batch > maxBatch) maxBatch = batch;
        ++turns;
    }
    check(consumed == 500 && turns == 32 && maxBatch == 16,
          "500 same-direction events survive merging and bounded 16-event dispatch");
    reset();
    for (int i = 0; i < 20; ++i)
        hardwareInputTestEnqueue({InputKind::Rotate, static_cast<int16_t>(i % 2 ? -1 : 1), static_cast<uint32_t>(i)});
    events = drain(); ordered = events.size() == 20;
    for (size_t i = 0; i < events.size(); ++i) ordered &= events[i].delta == (i % 2 ? -1 : 1);
    check(ordered, "20 alternating events retain every reversal below queue capacity");
    reset();
    for (int i = 0; i < 40; ++i)
        hardwareInputTestEnqueue({InputKind::Rotate, static_cast<int16_t>(i % 2 ? -1 : 1), static_cast<uint32_t>(i)});
    events = drain();
    int net = 0; for (auto event : events) net += event.delta;
    check(events.size() == 36 && net == -4,
          "40 alternating events at full queue lose 4 positive reversals; later negative steps merge");
    check(hardwareInputStatistics().inputQueueDropped == 4, "real input overflow has a distinct cumulative counter");
    hardwareInputClearDiagnostics();
    check(hardwareInputStatistics().inputQueueDropped == 4, "trace clear preserves boot overflow evidence");

    reset(); raw(1, 2); raw(0, 2); raw(1, 2); raw(3, 2);
    const auto before = hardwareInputStatistics();
    hardwareInputClearDiagnostics();
    auto stats = hardwareInputStatistics(); events = drain();
    check(events.size() == 2 && events[0].delta == -1 && events[1].delta == 1 &&
          stats.decodedSteps == 2 && stats.positiveSteps == 1 && stats.negativeSteps == 1 && stats.samples == before.samples,
          "trace clear preserves queued events, decoder and signed step/sample totals");
    raw(1, 2); hardwareInputClearDiagnostics(); raw(0, 2); events = drain();
    check(events.size() == 1 && events[0].delta == -1, "trace clear midway through a path preserves decoder state");

    reset(); fakeUs = UINT32_MAX - 500LL; lastRotarySampleUs = static_cast<uint32_t>(fakeUs); raw(3);
    stats = hardwareInputStatistics();
    check(stats.lastSampleGapUs == 1000 && stats.maxSampleGapUs == 1000, "microsecond gap survives 32-bit rollover");
    fakeUs += 19000; raw(3); stats = hardwareInputStatistics();
    check(stats.lastSampleGapUs == 20000 && stats.maxSampleGapUs == 20000, "stationary samples record scheduling gaps");
    raw(3); hardwareInputClearDiagnostics(); stats = hardwareInputStatistics();
    check(stats.lastSampleGapUs == 1000 && stats.maxSampleGapUs == 20000, "boot maximum survives normal samples and trace clear");

    inputStatistics.samples = inputStatistics.decodedSteps = inputStatistics.negativeSteps = UINT32_MAX;
    inputStatistics.invalidTransitions = UINT32_MAX;
    raw(1, 2); raw(0, 2); raw(3, 2); stats = hardwareInputStatistics();
    check(stats.samples == UINT32_MAX && stats.decodedSteps == UINT32_MAX &&
          stats.negativeSteps == UINT32_MAX && stats.invalidTransitions == UINT32_MAX,
          "sample, negative-step and invalid counters saturate");
    rotaryDecoder.reset(3, fakeMs); inputStatistics.positiveSteps = UINT32_MAX;
    raw(2, 2); raw(0, 2);
    check(hardwareInputStatistics().positiveSteps == UINT32_MAX, "positive-step counter saturates independently");
    reset();
    for (int i = 0; i < 32; ++i) hardwareInputTestEnqueue({InputKind::ButtonDown, 0, 0});
    inputStatistics.inputQueueDropped = UINT32_MAX - 1;
    hardwareInputTestEnqueue({InputKind::ButtonUp, 0, 0});
    hardwareInputTestEnqueue({InputKind::ButtonUp, 0, 0});
    check(hardwareInputStatistics().inputQueueDropped == UINT32_MAX, "real input queue drops saturate");
    diagnosticCount = 64; diagnosticDropped = UINT32_MAX - 1; raw(1); raw(0);
    check(hardwareInputStatistics().droppedTraces == UINT32_MAX, "raw trace loss saturates independently of input loss");
    std::printf("SIZES diagnostic=%zu boot_statistics=%zu\n", sizeof(HardwareInputDiagnostic), sizeof(HardwareInputStatistics));
    std::printf("RESULT checks=%u failed=%u; synthetic inputs, no device timing measurement\n", checks, failed);
    return failed ? 1 : 0;
}
