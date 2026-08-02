#pragma once

#include <stdint.h>

struct UiRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

constexpr UiRect kNormalListViewport{78, 128, 324, 184};
constexpr UiRect kNormalListFocus{104, 195, 272, 50};
constexpr UiRect kNormalListProgress{168, 320, 144, 8};
constexpr UiRect kNormalListCount{202, 104, 76, 24};
constexpr UiRect kNormalFooter{152, 352, 176, 56};
constexpr UiRect kModalRect{104, 106, 272, 268};
constexpr UiRect kModalConfirm{128, 256, 224, 64};
constexpr UiRect kModalCancel{152, 328, 176, 40};

constexpr bool uiRectInsideCircle(UiRect rect, int16_t radius = 192)
{
    const int64_t radiusSquared = static_cast<int64_t>(radius) * radius;
    const int32_t xs[] = {rect.x, static_cast<int32_t>(rect.x) + rect.w};
    const int32_t ys[] = {rect.y, static_cast<int32_t>(rect.y) + rect.h};
    for (uint8_t xIndex = 0; xIndex < 2; ++xIndex) {
        for (uint8_t yIndex = 0; yIndex < 2; ++yIndex) {
            const int32_t dx = xs[xIndex] - 240;
            const int32_t dy = ys[yIndex] - 240;
            if (static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy > radiusSquared) {
                return false;
            }
        }
    }
    return true;
}
