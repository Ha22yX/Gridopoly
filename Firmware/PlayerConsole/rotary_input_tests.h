#pragma once
#include "rotary_input_filter.h"
#include "app_state.h"
#include "test_fixture.h"

template<class Check> bool runRotaryInputDecoderTests(Check check)
{
    using gridopoly::player_console::RotaryQuadratureDecoder;
    bool ok = true;
    RotaryQuadratureDecoder decoder;
    uint32_t now = 0;
    int total = 0, events = 0;
    auto sample = [&](uint8_t phases) {
        int step = decoder.sample(phases, now++);
        step += decoder.sample(phases, now++);
        total += step;
        if (step != 0) ++events;
        return step;
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
    // Actual COM7 raw records from 2026-09-26, firmware efbe3d08, ms 431493
    // through 431539. Production originally emitted +1 at 431498 and -1 at
    // 431539 after the illegal 3 -> 0 at 431502. Missing polls are filled with
    // the previous raw value at 1 ms; the old decoder matches this excerpt.
    struct RawRecord { uint32_t at; uint8_t phases; };
    const RawRecord recorded[] = {
        {431493, 1}, {431497, 3}, {431498, 3}, {431501, 0},
        {431502, 0}, {431521, 2}, {431528, 3}, {431529, 2},
        {431534, 3}, {431535, 2}, {431538, 3}, {431539, 3}
    };
    const uint32_t scales[] = {1U, 10U};
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
                if (!swap && !complement && scale == 1) actualTrace &= pass;
                else if (scale == 10) slowTrace &= pass;
                else mirroredTrace &= pass;
            }
        }
    }
    ok &= check(actualTrace, "rotary: actual COM7 illegal-return excerpt cannot undo the row change");
    ok &= check(mirroredTrace, "rotary: captured illegal-return recovery is symmetric in phase and direction");
    ok &= check(slowTrace, "rotary: captured recovery does not depend on a short reversal timeout");
    reset(1); sample(0);
    ok &= check(total == 0, "rotary: startup between detents emits no partial step");
    sample(2); sample(3);
    ok &= check(total == -1, "rotary: startup resynchronizes at the first detent");
    reset();
    decoder.sample(1, 10); decoder.sample(1, 10); decoder.sample(3, 10);
    decoder.sample(3, 11); sample(3);
    ok &= check(decoder.stablePhases() == 3 && total == 0,
                "rotary: same-time catch-up samples cannot qualify a transient");
    decoder.reset(3, UINT32_MAX - 3U);
    decoder.sample(1, UINT32_MAX); decoder.sample(1, 0);
    decoder.sample(0, 1);
    ok &= check(decoder.sample(0, 2) == -1,
                "rotary: joint debounce survives millis wrap");
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
        while (++traceAt < record.at) dispatchSample(traceRaw, traceAt);
        traceRaw = record.phases;
        dispatchSample(traceRaw, traceAt);
    }
    ok &= check(state.focus == 2 && state.nav.current.focus == 2 &&
                state.identity.focus == 2 && !state.identity.editingValue &&
                state.identity.draftRecipe.hairPresetId == 1 && state.commandCount == 0,
                "rotary: actual COM7 recovery preserves the Avatar row without editing a value");
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
