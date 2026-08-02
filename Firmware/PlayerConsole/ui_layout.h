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

static_assert(kNormalListViewport.x == 78 && kNormalListViewport.y == 128 &&
              kNormalListViewport.w == 324 && kNormalListViewport.h == 184,
              "normal list viewport must retain its V2 rectangle");
static_assert(kNormalListFocus.x == 104 && kNormalListFocus.y == 195 &&
              kNormalListFocus.w == 272 && kNormalListFocus.h == 50,
              "normal list focus must retain its V2 rectangle");
static_assert(kNormalListProgress.x == 168 && kNormalListProgress.y == 320 &&
              kNormalListProgress.w == 144 && kNormalListProgress.h == 8,
              "normal list progress must retain its V2 rectangle");
static_assert(kNormalListCount.x == 202 && kNormalListCount.y == 104 &&
              kNormalListCount.w == 76 && kNormalListCount.h == 24,
              "normal list count must retain its V2 rectangle");
static_assert(kNormalFooter.x == 152 && kNormalFooter.y == 352 &&
              kNormalFooter.w == 176 && kNormalFooter.h == 56,
              "normal footer must retain its V2 rectangle");
static_assert(kModalRect.x == 104 && kModalRect.y == 106 &&
              kModalRect.w == 272 && kModalRect.h == 268,
              "modal must retain its V2 rectangle");
static_assert(kModalConfirm.x == 128 && kModalConfirm.y == 256 &&
              kModalConfirm.w == 224 && kModalConfirm.h == 64,
              "modal confirmation must retain its V2 rectangle");
static_assert(kModalCancel.x == 152 && kModalCancel.y == 328 &&
              kModalCancel.w == 176 && kModalCancel.h == 40,
              "modal cancellation must retain its V2 rectangle");
static_assert(kNormalListFocus.x + kNormalListFocus.w / 2 == 240 &&
              kNormalListProgress.x + kNormalListProgress.w / 2 == 240 &&
              kNormalListCount.x + kNormalListCount.w / 2 == 240 &&
              kNormalFooter.x + kNormalFooter.w / 2 == 240,
              "normal controls must remain centered");
static_assert(kModalCancel.y - (kModalConfirm.y + kModalConfirm.h) >= 8,
              "modal controls require an 8px vertical gap");
static_assert(uiRectInsideCircle(kNormalFooter) && uiRectInsideCircle(kModalRect) &&
              uiRectInsideCircle(kModalConfirm) && uiRectInsideCircle(kModalCancel),
              "named V2 rectangles must fit the round safe area");
