#pragma once

#include <stdint.h>

#include <lvgl.h>

#include "app_types.h"

enum class HomeAction : uint8_t {
    Dice,
    ExtraRoll,
    EndTurn,
    Assets,
    Players,
    Trade,
};

enum class CarouselPath : uint8_t {
    Linear,
    WrapLeftToRight,
    WrapRightToLeft,
};

struct UiCarousel {
    lv_obj_t *container = nullptr;
    lv_obj_t *items[5]{};
    lv_obj_t *actionTargets[5]{};
    lv_obj_t *previousTarget = nullptr;
    lv_obj_t *nextTarget = nullptr;
    HomeAction actions[5]{};
    int16_t startX[5]{};
    int16_t targetX[5]{};
    uint16_t startZoom[5]{};
    uint16_t targetZoom[5]{};
    uint16_t currentZoom[5]{};
    uint8_t startOpacity[5]{};
    uint8_t targetOpacity[5]{};
    uint8_t currentOpacity[5]{};
    CarouselPath paths[5]{};
    lv_point_t swipeStart{};
    bool swipeTracking = false;
    bool suppressClick = false;
    uint8_t count = 0;
    uint8_t selected = 0;
#if GRIDOPOLY_SELF_TEST == 1
    uint32_t staticStyleConfigurations = 0;
    uint32_t selectionStyleUpdates = 0;
#endif
};

uint8_t uiCarouselActions(HomePhase phase, HomeAction (&actions)[5]);
void uiCarouselCreate(UiCarousel &carousel, lv_obj_t *parent,
                      const HomeAction *actions, uint8_t count, uint8_t selected);
void uiCarouselSetSelection(UiCarousel &carousel, uint8_t selected,
                            bool animate);
void uiCarouselSetEndTurnExitProgress(UiCarousel &carousel, uint16_t progressPermille);
void uiCarouselDestroy(UiCarousel &carousel);
int8_t uiCarouselSwipeStep(int16_t dx, int16_t dy);
