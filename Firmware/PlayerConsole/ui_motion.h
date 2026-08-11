#pragma once

#include <stdint.h>

struct CarouselPose {
    int16_t centerX;
    uint16_t zoom;
    uint8_t opacity;
};

struct DicePose {
    int16_t x;
    int16_t y;
    int16_t angleTenths;
    uint16_t zoom;
    uint8_t face;
};

CarouselPose uiCarouselPose(int8_t relativeSlot);
int16_t uiCenterListTrackY(uint8_t selectedIndex, int16_t selectedCenterY,
                           int16_t rowStride);
uint16_t uiListProgressPermille(uint8_t selectedIndex, uint8_t count);
DicePose uiDicePose(uint32_t elapsedMs, uint8_t dieIndex, uint8_t finalFace);
uint8_t uiMoneyFontPx(int32_t amount);
