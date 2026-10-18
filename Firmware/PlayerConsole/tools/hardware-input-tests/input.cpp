// Include the actual adapter in this test TU so saturation can be exercised
// without billions of callbacks or production-only test mutation APIs.
// Expose only this header's access control in the isolated host test. Seed
// its counter boundary without adding a production mutation API or changing
// the decoder layout; all behavior still runs through the actual adapter.
#define private public
#include "rotary_input_filter.h"
#undef private
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
    raw(3, 6);
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
    for (int i = 0; i < 10; ++i) { raw(1, 6); raw(0, 6); raw(2, 6); raw(3, 6); }
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
    ordered = events.size() == 10;
    for (auto event : events) ordered &= event.delta == -1;
    check(ordered && invalid == 0,
          "synthetic 1ms intermediate phases and 50ms detent dwell retain all 10 intended steps");
    // Actual decoder and event queue: a provisional endpoint is never visible
    // to the UI, so a bounce cannot need a compensating opposite event.
    reset(); raw(2); raw(0, 2); raw(2); raw(3, 6);
    events = drain();
    check(events.empty() && hardwareInputStatistics().decodedSteps == 0,
          "two-sample endpoint round trip creates neither an event nor a compensating rollback");
    raw(2); raw(0, 6); raw(2); raw(3, 6); events = drain();
    check(events.size() == 2 && events[0].delta == 1 && events[1].delta == -1,
          "one-sample intermediates preserve immediate reversals between stable detents in the real queue");

    reset(); raw(0, 6); hardwareInputTestReset();
    // Seed the known zero anchor; exact captured RAW observations through
    // 273014, with an explicit unsampled 273009 rather than a filled row.
    fakeMs = 273007; fakeUs = 273007000; hardwareInputTestReset();
    raw(1); ++fakeMs; fakeUs += 1000; raw(3); raw(3); raw(0); raw(1); raw(0);
    events = drain();
    check(events.empty() && hardwareInputStatistics().invalidTransitions == 1,
          "captured ce37ad05 prefix does not place the premature forward step into the real queue");
    // Explicit synthetic continuation; not a reconstruction of the dropped RAW.
    raw(0, 6); raw(1); raw(3, 6); events = drain();
    check(events.size() == 1 && events[0].delta == 1,
          "synthetic stable continuation after the captured bounce enqueues exactly one forward step");

    reset();
    for (int i = 0; i < 10; ++i) { raw(2); raw(0); raw(1); raw(3); }
    check(!hardwareInputPending(), "unsettled multi-cycle burst is not prematurely delivered to the real queue");
    raw(3, 24); events = drain(); ordered = events.size() == 20;
    for (auto event : events) ordered &= event.delta == 1;
    check(ordered && hardwareInputStatistics().decodedSteps == 20,
          "all 20 pending short-detent steps drain through the real adapter after settling");

    reset(); raw(0, 6); hardwareInputTestReset();
    raw(1); raw(3); // odd pending displacement: published 0, tentative 3
    rotaryDecoder.pendingSteps_ = INT32_MAX;
    raw(2); raw(0, 6); events = drain();
    check(events.empty() && rotaryDecoder.pendingSteps_ == 0,
          "injected INT32_MAX pending displacement is discarded instead of overflowing into reverse steps");
    reset(); raw(1); raw(0); raw(2); raw(3); // even pending displacement
    rotaryDecoder.pendingSteps_ = INT32_MIN;
    raw(1); raw(0, 6); events = drain();
    check(events.empty() && rotaryDecoder.pendingSteps_ == 0,
          "injected INT32_MIN pending displacement is discarded instead of overflowing into forward steps");
    // Opposite motion at a boundary is subtraction, not overflow.
    reset(); raw(0, 6); hardwareInputTestReset(); raw(1); raw(3);
    rotaryDecoder.pendingSteps_ = INT32_MAX;
    raw(1); raw(0);
    check(rotaryDecoder.pendingSteps_ == INT32_MAX - 1 && !hardwareInputPending(),
          "reverse motion safely cancels positive pending displacement at the counter boundary");
    reset(); raw(1); raw(0); raw(2); raw(3);
    rotaryDecoder.pendingSteps_ = INT32_MIN;
    raw(2); raw(0);
    check(rotaryDecoder.pendingSteps_ == INT32_MIN + 1 && !hardwareInputPending(),
          "forward motion safely cancels negative pending displacement at the counter boundary");

    reset(); fakeMs += 25; fakeUs += 25000; raw(0, 6);
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

    reset(); raw(1, 6); raw(0, 6); raw(1, 6); raw(3, 6);
    const auto before = hardwareInputStatistics();
    hardwareInputClearDiagnostics();
    auto stats = hardwareInputStatistics(); events = drain();
    check(events.size() == 2 && events[0].delta == -1 && events[1].delta == 1 &&
          stats.decodedSteps == 2 && stats.positiveSteps == 1 && stats.negativeSteps == 1 && stats.samples == before.samples,
          "trace clear preserves queued events, decoder and signed step/sample totals");
    raw(1, 6); hardwareInputClearDiagnostics(); raw(0, 6); events = drain();
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
    raw(1, 6); raw(0, 6); raw(3, 6); stats = hardwareInputStatistics();
    check(stats.samples == UINT32_MAX && stats.decodedSteps == UINT32_MAX &&
          stats.negativeSteps == UINT32_MAX && stats.invalidTransitions == UINT32_MAX,
          "sample, negative-step and invalid counters saturate");
    rotaryDecoder.reset(3, fakeMs); inputStatistics.positiveSteps = UINT32_MAX;
    raw(2, 6); raw(0, 6);
    check(hardwareInputStatistics().positiveSteps == UINT32_MAX, "positive-step counter saturates independently");
    reset();
    for (int i = 0; i < 32; ++i) hardwareInputTestEnqueue({InputKind::ButtonDown, 0, 0});
    inputStatistics.inputQueueDropped = UINT32_MAX - 1;
    hardwareInputTestEnqueue({InputKind::ButtonUp, 0, 0});
    hardwareInputTestEnqueue({InputKind::ButtonUp, 0, 0});
    check(hardwareInputStatistics().inputQueueDropped == UINT32_MAX, "real input queue drops saturate");
    diagnosticCount = 64; diagnosticDropped = UINT32_MAX - 1; raw(1); raw(0);
    check(hardwareInputStatistics().droppedTraces == UINT32_MAX, "raw trace loss saturates independently of input loss");
    std::printf("SIZES diagnostic=%zu boot_statistics=%zu decoder=%zu\n",
                sizeof(HardwareInputDiagnostic), sizeof(HardwareInputStatistics), sizeof(rotaryDecoder));
    std::printf("RESULT checks=%u failed=%u; synthetic inputs, no device timing measurement\n", checks, failed);
    return failed ? 1 : 0;
}
