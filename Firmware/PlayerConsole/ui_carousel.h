#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "app_types.h"

enum class HomeAction : uint8_t {
    Dice,
    Assets,
    Players,
    Trade,
    More,
};

struct UiCarousel {
    lv_obj_t *container = nullptr;
    lv_obj_t *items[5]{};
    lv_obj_t *labels[5]{};
    uint8_t count = 0;
    uint8_t selected = 0;
};

uint8_t uiCarouselActions(HomePhase phase, HomeAction (&actions)[5]);
void uiCarouselCreate(UiCarousel &carousel, lv_obj_t *parent,
                      const HomeAction *actions, uint8_t count, uint8_t selected);
void uiCarouselSetSelection(UiCarousel &carousel, uint8_t selected,
                            bool animate);
void uiCarouselDestroy(UiCarousel &carousel);
