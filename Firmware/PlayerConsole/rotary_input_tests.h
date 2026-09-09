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
    ok &= check(total == -1,
                "rotary: a legal detent after invalid input resynchronizes correctly");
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
    state.focus = state.nav.current.focus = 0;
    state.identity.editingValue = true;
    state.identity.draftInitialized = true;
    state.identity.phase = IdentityClientPhase::AvatarEditing;
    state.identity.draftRecipe.hairPresetId = 1;
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
