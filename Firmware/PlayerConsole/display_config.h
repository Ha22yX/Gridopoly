#pragma once

#include <stdint.h>

// Zero preserves the supported VIEWE board's 16 MHz vendor configuration.
// The 12 MHz override is selected explicitly for the validated COM29 profile.
#ifndef GRIDOPOLY_DISPLAY_PCLK_HZ
#define GRIDOPOLY_DISPLAY_PCLK_HZ 0
#endif
#if GRIDOPOLY_DISPLAY_PCLK_HZ != 0 && GRIDOPOLY_DISPLAY_PCLK_HZ != 12000000
#error "Unsupported player display clock profile"
#endif
constexpr uint32_t kDisplayRequestedPclkHz =
    GRIDOPOLY_DISPLAY_PCLK_HZ == 0 ? 16000000u : GRIDOPOLY_DISPLAY_PCLK_HZ;
