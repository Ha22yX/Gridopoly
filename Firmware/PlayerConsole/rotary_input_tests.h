#pragma once
#include "rotary_input_filter.h"
#include "app_state.h"
#include "test_fixture.h"
#include <initializer_list>

template<class Check> bool runRotaryShortPhaseTests(Check check)
{
    using gridopoly::player_console::RotaryQuadratureDecoder;
    bool ok = true;
    RotaryQuadratureDecoder decoder;
    uint32_t now = 0;
    int total = 0, count = 0;
    auto observe = [&](uint8_t phase, uint32_t advance = 1U) {
        now += advance;
        const int step = decoder.sample(phase, now);
        total += step; count += step != 0;
        return step;
    };
    auto hold = [&](uint8_t phase, unsigned samples = 6U) {
        for (unsigned i = 0; i < samples; ++i) observe(phase);
    };
    auto reset = [&](uint8_t phase, uint32_t at = 0U) {
        now = at; total = count = 0; decoder.reset(phase, now);
    };

    // Exact three recorded observations from 6c4e5729. No missing polls are
    // interpolated. This establishes a short electrical path, not a settled
    // mechanical detent; the later held samples below are explicitly synthetic.
    reset(0, 314890);
    observe(2); observe(3); observe(3);
    ok &= check(count == 0 && decoder.invalidTransitions() == 0,
                "rotary: captured short intermediate is retained without prematurely publishing its endpoint");
    hold(3, 4);
    ok &= check(total == -1 && count == 1,
                "rotary: synthetic continued endpoint dwell commits the captured short path exactly once");
    observe(2); hold(0);
    ok &= check(total == 0 && count == 2,
                "rotary: reversal immediately after a committed detent is accepted without a direction lock");

    // Exact consecutive records from ce37ad05: ms 273010..273014, tx_drop=19.
    // State is seeded from the preceding known zero anchor. The 273008 raw=1
    // observation starts the path; no claim is made about the missing 273009.
    // The negative event at 273015 was logged by the app, but its RAW row was
    // dropped. It must NOT be inserted here as a fabricated captured sample.
    reset(0, 273007);
    observe(1); observe(3, 2); observe(3); observe(0); observe(1); observe(0);
    ok &= check(count == 0 && decoder.invalidTransitions() == 1,
                "rotary: captured two-sample endpoint and raw two-bit jump cannot publish the observed false forward step");
    hold(0);
    ok &= check(count == 0,
                "rotary: synthetic settling at the original anchor after the captured excursion emits no rollback");
    observe(1); hold(3);
    ok &= check(total == 1 && count == 1,
                "rotary: a synthetic subsequent stable forward detent emits only one step after the captured excursion");

    bool threshold = true, bounce = true, mirrorReversal = true;
    bool rawChangesRestart = true, fullShortReturn = true;
    for (uint8_t anchor : {uint8_t{0}, uint8_t{3}}) {
        for (uint8_t middle : {uint8_t{1}, uint8_t{2}}) {
            const uint8_t opposite = anchor ^ 3U;
            reset(anchor); observe(middle); observe(opposite);
            hold(opposite, 4);
            threshold &= count == 0;
            observe(opposite);
            threshold &= count == 1;
            const int forward = total;
            observe(middle); hold(anchor);
            mirrorReversal &= count == 2 && total == 0 && forward != 0;

            // A complete electrical out-and-back whose first endpoint is only
            // observed twice is not a pair of qualified mechanical detents.
            reset(anchor); observe(middle); hold(opposite, 2);
            observe(middle); hold(anchor);
            fullShortReturn &= count == 0 && decoder.invalidTransitions() == 0;

            reset(anchor);
            observe(middle); observe(anchor); hold(anchor);
            bounce &= count == 0 && decoder.invalidTransitions() == 0;
            observe(middle); observe(opposite); observe(middle); hold(opposite);
            bounce &= count == 1 && decoder.invalidTransitions() == 0;

            reset(anchor); observe(middle); hold(opposite, 5);
            observe(middle); observe(opposite); hold(opposite, 4);
            rawChangesRestart &= count == 0;
            observe(opposite);
            rawChangesRestart &= count == 1;
        }
    }
    ok &= check(threshold, "rotary: both directions require a full 5ms unchanged endpoint before the first event");
    ok &= check(mirrorReversal, "rotary: committed fast reversals preserve both signs at both detents");
    ok &= check(fullShortReturn, "rotary: uncommitted two-sample endpoint round trips cancel in every phase mirror");
    ok &= check(bounce, "rotary: intermediate and endpoint bounce produce no duplicate event");
    ok &= check(rawChangesRestart, "rotary: every raw phase change restarts the endpoint stability interval");

    reset(3); observe(2); observe(0); hold(0, 3);
    observe(0, 100);
    ok &= check(count == 0, "rotary: a long sampling gap cannot substitute for continuous endpoint observations");
    hold(0, 5);
    ok &= check(count == 1 && total == 1,
                "rotary: endpoint confirmation restarts after a sampling gap without inventing movement");
    reset(3); observe(2); observe(0);
    for (int i = 0; i < 20; ++i) observe(0, 0);
    ok &= check(count == 0, "rotary: same-time timer catch-up cannot advance endpoint debounce");
    hold(0, 5);
    ok &= check(count == 1 && total == 1, "rotary: fresh observations can confirm a path after same-time calls");

    reset(3, UINT32_MAX - 3U); observe(2); hold(0);
    ok &= check(total == 1 && count == 1 && decoder.invalidTransitions() == 0,
                "rotary: endpoint stability and path accumulation survive millis wrap");
    reset(3); observe(2); observe(0); observe(3); hold(3);
    ok &= check(count == 0 && decoder.invalidTransitions() == 1,
                "rotary: an illegal jump discards uncommitted displacement and retains the published anchor");
    observe(2); hold(0);
    ok &= check(count == 1 && total == 1,
                "rotary: the next complete stable path works after an illegal jump during pending movement");

    bool speeds = true, delayedFastBurst = true;
    for (bool swap : {false, true}) {
        for (bool invert : {false, true}) {
            auto transform = [&](uint8_t phase) {
                if (swap) phase = ((phase & 1U) << 1U) | ((phase & 2U) >> 1U);
                return static_cast<uint8_t>(phase ^ (invert ? 3U : 0U));
            };
            // These are synthetic rates, not measurements of the installed
            // encoder. Each endpoint has at least 5ms of observed stability.
            for (unsigned dwell : {6U, 9U, 49U, 199U}) {
                reset(transform(3));
                for (int round = 0; round < 10; ++round) {
                    observe(transform(2)); hold(transform(0), dwell);
                    observe(transform(1)); hold(transform(3), dwell);
                }
                speeds &= count == 20 && total == (swap ? -20 : 20) &&
                    decoder.invalidTransitions() == 0;
            }
            // No endpoint in this burst is qualified. Preserve displacement
            // across complete cycles, then drain it after the final settling.
            reset(transform(3));
            for (int round = 0; round < 10; ++round) {
                observe(transform(2)); observe(transform(0));
                observe(transform(1)); observe(transform(3));
            }
            delayedFastBurst &= count == 0;
            hold(transform(3), 24);
            delayedFastBurst &= count == 20 && total == (swap ? -20 : 20) &&
                decoder.invalidTransitions() == 0;
        }
    }
    ok &= check(speeds, "rotary: short intermediates retain all stable detents across synthetic speeds and phase mirrors");
    ok &= check(delayedFastBurst, "rotary: an unsettled same-direction burst retains whole cycles and drains exact count after settling");

    reset(3);
    for (int i = 0; i < 3; ++i) {
        observe(2); observe(0); observe(1); observe(3);
    }
    hold(3, 5); // first of six pending steps has just been published
    const int beforeCatchup = count;
    for (int i = 0; i < 20; ++i) observe(3, 0);
    ok &= check(beforeCatchup == 1 && count == beforeCatchup,
                "rotary: repeated calls at the commit timestamp cannot flush additional pending steps");
    hold(3, 5);
    ok &= check(total == 6 && count == 6,
                "rotary: remaining confirmed burst steps drain only on fresh timestamps");

    reset(3);
    for (int i = 0; i < 3; ++i) {
        observe(2); observe(0); observe(1); observe(3);
    }
    observe(0); observe(3); hold(3);
    ok &= check(total == 0 && count == 0 && decoder.invalidTransitions() == 2,
                "rotary: diagonal noise during an unsettled burst discards pending motion instead of guessing its missing path");
    observe(2); hold(0);
    ok &= check(total == 1 && count == 1,
                "rotary: stable movement recovers after discarding a noisy unsettled burst");

    reset(3); observe(2); hold(0); // one already published forward step
    observe(1); observe(3); observe(2); observe(0); // two pending steps
    observe(3); observe(0); hold(0);
    ok &= check(total == 1 && count == 1 && decoder.invalidTransitions() == 2,
                "rotary: illegal noise clears only pending motion and never retracts an already published step");

    reset(3);
    observe(2); observe(0); observe(1); observe(3); // two pending forward steps
    observe(1); hold(0); // reverse one before any endpoint is confirmed
    ok &= check(total == 1 && count == 1,
                "rotary: an uncommitted reverse step cancels one pending step without discarding an entire cycle");
    reset(3); observe(2); observe(0);
    for (int i = 0; i < 3; ++i) observe(0, 2);
    ok &= check(total == 1 && count == 1,
                "rotary: 1ms timer samples straddling millis boundaries still qualify an endpoint");

    reset(3);
    const uint8_t phases[] = {3, 1, 0, 2};
    uint32_t random = 0x617ac531U;
    int index = 0, quarters = 0;
    bool ordered = true;
    for (int i = 0; i < 4000; ++i) {
        random = random * 1664525U + 1013904223U;
        const int direction = (random & 0x80000000U) ? 1 : -1;
        index = (index + direction + 4) % 4; quarters += direction;
        observe(phases[index]);
        if (phases[index] == 0 || phases[index] == 3) {
            hold(phases[index], 5);
            ordered &= total == -quarters / 2;
        }
    }
    ok &= check(ordered && decoder.invalidTransitions() == 0,
                "rotary: 4000 short-phase moves and reversals preserve exact order at qualified endpoints");
    return ok;
}

template<class Check> bool runRotaryInputDecoderTests(Check check)
{
    using gridopoly::player_console::RotaryQuadratureDecoder;
    bool ok = true;
    RotaryQuadratureDecoder decoder;
    uint32_t now = 0;
    int total = 0, events = 0;
    auto sample = [&](uint8_t phases) {
        int result = 0;
        // Six 1ms observations include a full 5ms unchanged endpoint. The
        // separate short-phase tests keep intermediates at a single sample.
        for (int i = 0; i < 6; ++i) {
            const int step = decoder.sample(phases, now++);
            total += step; result += step; events += step != 0;
        }
        return result;
    };
    auto reset = [&](uint8_t phases = 3) {
        now = 0; total = events = 0; decoder.reset(phases, now);
    };
    reset(); sample(1); sample(0);
    ok &= check(total == -1 && events == 1,
                "rotary: A-leading half cycle is exactly one installed-direction step");
    sample(2); sample(3);
    ok &= check(total == -2 && events == 2,
                "rotary: alternating detents preserve the existing mechanical scale");
    sample(2); sample(0);
    ok &= check(total == -1 && events == 3,
                "rotary: immediate intentional reversal is not discarded");
    reset(); sample(1); sample(3); sample(1); sample(0);
    ok &= check(total == -1 && events == 1,
                "rotary: contact backtracking before a detent cannot add steps");
    reset(); sample(2); sample(0); sample(2); sample(3); sample(2); sample(0);
    ok &= check(total == 1,
                "rotary: forward reverse forward has net one, never two");
    reset(); sample(0);
    ok &= check(total == 0 && decoder.invalidTransitions() == 1,
                "rotary: simultaneous A/B change does not guess a direction");
    sample(2); sample(3);
    ok &= check(total == 0 && events == 0,
                "rotary: invalid jump then return to the trusted detent is not a step");
    sample(2); sample(0);
    ok &= check(total == 1 && events == 1,
                "rotary: the next complete half-cycle after recovery counts immediately");
    reset(0); sample(3); sample(2); sample(0);
    ok &= check(total == 0 && events == 0,
                "rotary: invalid-return rejection is symmetric at both detents");
    sample(2); sample(3); sample(2); sample(0);
    ok &= check(total == 0 && events == 2,
                "rotary: legal immediate reversal still counts both steps after recovery");
    reset(); sample(0); sample(3); sample(0); sample(3);
    sample(2); sample(0);
    ok &= check(total == 1 && events == 1 && decoder.invalidTransitions() == 4,
                "rotary: repeated invalid detent jumps do not discard the next trusted path");
    reset(); sample(1); sample(2); sample(0);
    ok &= check(total == 0 && events == 0 && decoder.invalidTransitions() == 1,
                "rotary: invalid intermediate phases discard the incomplete movement");
    sample(2); sample(3);
    ok &= check(total == 0 && events == 0,
                "rotary: partial recovery cannot replace an existing trusted detent");
    sample(2); sample(0); sample(2); sample(3);
    ok &= check(total == 0 && events == 2,
                "rotary: complete movement and immediate reversal work after partial recovery");
    reset(0); sample(1); sample(2); sample(3); sample(2); sample(0);
    ok &= check(total == 0 && events == 0,
                "rotary: partial recovery preserves the trusted detent in the mirrored direction");
    reset(); sample(0); sample(2); sample(0); sample(2); sample(3);
    ok &= check(total == 0 && events == 0,
                "rotary: zero-net bounce at an invalid endpoint cannot create a trusted detent");
    sample(2); sample(0); sample(2); sample(3);
    ok &= check(total == 0 && events == 2,
                "rotary: complete movement and reversal work after invalid-endpoint bounce");
    reset(0); sample(3); sample(2); sample(3); sample(2); sample(0);
    ok &= check(total == 0 && events == 0,
                "rotary: invalid-endpoint bounce rejection is symmetric at both detents");
    // Synthetic timing fixtures derived from the historical 2026-09-26
    // illegal-return pattern. The original first endpoint lasted only 4ms;
    // stretch time by 2x/10x so it qualifies under the new 5ms commit contract.
    // Held values are explicitly synthetic, not recovered missing RAW records.
    // The ce37ad05 dropped-row capture is only used in the exact prefix above.
    struct RawRecord { uint32_t at; uint8_t phases; };
    const RawRecord recorded[] = {
        {431493, 1}, {431497, 3}, {431498, 3}, {431501, 0},
        {431502, 0}, {431521, 2}, {431528, 3}, {431529, 2},
        {431534, 3}, {431535, 2}, {431538, 3}, {431539, 3}
    };
    const uint32_t scales[] = {2U, 10U};
    bool actualTrace = true, mirroredTrace = true, slowTrace = true;
    for (uint8_t swap = 0; swap < 2; ++swap) {
        for (uint8_t complement = 0; complement < 2; ++complement) {
            for (uint32_t scale : scales) {
                auto transform = [&](uint8_t phase) {
                    if (swap) phase = ((phase & 1U) << 1U) | ((phase & 2U) >> 1U);
                    return static_cast<uint8_t>(phase ^ (complement ? 3U : 0U));
                };
                decoder.reset(transform(0), 0);
                int traceTotal = 0, traceEvents = 0;
                uint8_t raw = 0;
                uint32_t at = 0;
                for (const auto &record : recorded) {
                    const uint32_t target = (record.at - 431492U) * scale;
                    while (++at < target) {
                        const int step = decoder.sample(transform(raw), at);
                        traceTotal += step; traceEvents += step != 0;
                    }
                    raw = record.phases;
                    const int step = decoder.sample(transform(raw), at);
                    traceTotal += step; traceEvents += step != 0;
                }
                const bool pass = traceTotal == (swap ? -1 : 1) && traceEvents == 1 &&
                    decoder.invalidTransitions() == 1;
                if (!swap && !complement && scale == 2) actualTrace &= pass;
                else if (scale == 10) slowTrace &= pass;
                else mirroredTrace &= pass;
            }
        }
    }
    ok &= check(actualTrace, "rotary: timing-qualified historical illegal-return pattern cannot undo the row change");
    ok &= check(mirroredTrace, "rotary: historical illegal-return path is symmetric in phase and direction");
    ok &= check(slowTrace, "rotary: slow illegal-return recovery does not depend on a reversal timeout");
    reset(1); sample(0);
    ok &= check(total == 0, "rotary: startup between detents emits no partial step");
    sample(2); sample(3);
    ok &= check(total == -1, "rotary: startup resynchronizes at the first detent");
    reset();
    decoder.sample(1, 10); decoder.sample(1, 10); decoder.sample(3, 10);
    decoder.sample(3, 11); now = 12; sample(3);
    ok &= check(decoder.stablePhases() == 3 && total == 0,
                "rotary: same-time catch-up samples cannot qualify a transient");
    decoder.reset(3, UINT32_MAX - 3U);
    decoder.sample(1, UINT32_MAX - 2U); decoder.sample(0, UINT32_MAX - 1U);
    int wrapped = 0;
    for (uint32_t offset = 1; offset <= 5; ++offset)
        wrapped += decoder.sample(0, (UINT32_MAX - 1U) + offset);
    ok &= check(wrapped == -1, "rotary: qualified detent commit survives millis wrap");
    reset();
    const uint8_t phases[4] = {3, 1, 0, 2};
    int index = 0, quarterTotal = 0;
    uint32_t random = 0x317dac29U;
    bool ordered = true;
    for (int i = 0; i < 4000; ++i) {
        random = random * 1664525U + 1013904223U;
        const int direction = (random & 0x80000000U) ? 1 : -1;
        index = (index + direction + 4) % 4;
        quarterTotal += direction;
        sample(phases[index]);
        if (phases[index] == 0 || phases[index] == 3)
            ordered = ordered && total == -quarterTotal / 2;
    }
    ok &= check(ordered && decoder.invalidTransitions() == 0,
                "rotary: 4000 quick legal transitions preserve bidirectional order");
    ok &= runRotaryShortPhaseTests(check);

    TestFixture<AppState> storage;
    if (!storage) return check(false, "rotary: Avatar Setup fixture allocation");
    AppState &state = *storage;
    appInit(state, 0);
    state.page = state.nav.current.page = ScreenPage::AvatarSetup;
    state.focus = state.nav.current.focus = state.identity.focus = 1;
    state.identity.editingValue = false;
    state.identity.draftInitialized = true;
    state.identity.phase = IdentityClientPhase::AvatarEditing;
    state.identity.draftRecipe.hairPresetId = 1;
    decoder.reset(0, 431492);
    uint32_t traceAt = 431492;
    uint8_t traceRaw = 0;
    auto dispatchSample = [&](uint8_t raw, uint32_t at) {
        const int step = decoder.sample(raw, at);
        if (step) appHandleInput(state,
            InputEvent{InputKind::Rotate, static_cast<int16_t>(step), at}, at);
    };
    for (const auto &record : recorded) {
        const uint32_t target = 431492U + (record.at - 431492U) * 2U;
        while (++traceAt < target) dispatchSample(traceRaw, traceAt);
        traceRaw = record.phases;
        dispatchSample(traceRaw, traceAt);
    }
    ok &= check(state.focus == 2 && state.nav.current.focus == 2 &&
                state.identity.focus == 2 && !state.identity.editingValue &&
                state.identity.draftRecipe.hairPresetId == 1 && state.commandCount == 0,
                "rotary: timing-qualified illegal-return fixture preserves the Avatar row without editing a value");
    state.focus = state.nav.current.focus = state.identity.focus = 0;
    state.identity.editingValue = true;
    auto apply = [&](uint8_t phasesValue) {
        const int step = sample(phasesValue);
        if (step) appHandleInput(state,
            InputEvent{InputKind::Rotate, static_cast<int16_t>(step), now}, now);
    };
    reset(); apply(2); apply(0); apply(2); apply(3); apply(2); apply(0);
    ok &= check(state.identity.draftRecipe.hairPresetId == 2 &&
                state.nav.current.focus == 0 && state.commandCount == 0,
                "rotary: decoded bounce/reversal cannot skip Avatar draft presets");
    apply(2); apply(3);
    ok &= check(state.identity.draftRecipe.hairPresetId == 1,
                "rotary: Avatar preset reversal restores the previous exact value");
    const uint8_t maxima[5] = {10, 20, 10, 8, 10};
    uint8_t *fields[5] = {&state.identity.draftRecipe.hairPresetId,
        &state.identity.draftRecipe.hairColorId, &state.identity.draftRecipe.facePresetId,
        &state.identity.draftRecipe.skinToneId, &state.identity.draftRecipe.outfitPresetId};
    bool wraps = true;
    for (uint8_t field = 0; field < 5; ++field) {
        state.focus = state.nav.current.focus = field;
        *fields[field] = maxima[field];
        appHandleInput(state, InputEvent{InputKind::Rotate, 1, now}, now);
        wraps = wraps && *fields[field] == 1;
        appHandleInput(state, InputEvent{InputKind::Rotate, -1, now}, now);
        wraps = wraps && *fields[field] == maxima[field];
    }
    ok &= check(wraps && state.commandCount == 0,
                "rotary: all five Avatar fields wrap in exact forward/reverse order");
    return ok;
}
