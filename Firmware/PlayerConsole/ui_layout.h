#pragma once

#include <stdint.h>

struct UiRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

// Keep both rings geometrically centered on the 480x480 display raster.
constexpr UiRect kOuterRing{31, 31, 418, 418};
constexpr UiRect kInnerRing{44, 44, 392, 392};
constexpr UiRect kNormalListViewport{104, 137, 272, 166};
constexpr UiRect kNormalListFocus{104, 195, 272, 50};
constexpr UiRect kNormalListProgress{168, 320, 144, 8};
constexpr UiRect kNormalListCount{202, 328, 76, 16};
constexpr UiRect kNormalFooter{152, 352, 176, 56};
constexpr UiRect kPlayerDetailAvatar{104, 108, 88, 88};
constexpr UiRect kPlayerDetailSummary{204, 108, 168, 88};
constexpr UiRect kPlayerDetailAssets{74, 216, 156, 52};
constexpr UiRect kPlayerDetailFinance{250, 216, 156, 52};
constexpr UiRect kPlayerDetailTrade{74, 282, 156, 52};
constexpr UiRect kPlayerDetailRefresh{250, 282, 156, 52};
constexpr int16_t kAssetDetailHeaderBottom = 109;
constexpr UiRect kAssetDetailArtwork{198, 116, 84, 84};
constexpr UiRect kAssetDetailGroup{100, 201, 280, 18};
constexpr UiRect kAssetDetailMetrics{80, 220, 320, 18};
constexpr UiRect kAssetDetailCash{90, 240, 300, 18};
constexpr UiRect kAssetDetailAction0{88, 264, 144, 38};
constexpr UiRect kAssetDetailAction1{248, 264, 144, 38};
constexpr UiRect kAssetDetailAction2{88, 308, 144, 38};
constexpr UiRect kAssetDetailAction3{248, 308, 144, 38};
constexpr UiRect kAssetDetailSingleAction{152, 286, 176, 44};
constexpr UiRect kAssetDetailPairLeft{72, 286, 160, 44};
constexpr UiRect kAssetDetailPairRight{248, 286, 160, 44};
constexpr UiRect kAssetDetailTripleTopLeft{72, 264, 160, 38};
constexpr UiRect kAssetDetailTripleTopRight{248, 264, 160, 38};
constexpr UiRect kAssetDetailTripleBottom{152, 308, 176, 38};
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

static_assert(kNormalListViewport.x == 104 && kNormalListViewport.y == 137 &&
              kNormalListViewport.w == 272 && kNormalListViewport.h == 166,
              "normal list viewport must clip exactly three centered rows");
static_assert(kNormalListFocus.x == 104 && kNormalListFocus.y == 195 &&
              kNormalListFocus.w == 272 && kNormalListFocus.h == 50,
              "normal list focus must retain its V2 rectangle");
static_assert(kNormalListProgress.x == 168 && kNormalListProgress.y == 320 &&
              kNormalListProgress.w == 144 && kNormalListProgress.h == 8,
              "normal list progress must retain its V2 rectangle");
static_assert(kNormalListCount.x == 202 && kNormalListCount.y == 328 &&
              kNormalListCount.w == 76 && kNormalListCount.h == 16,
              "normal list count must retain its V2 rectangle");
static_assert(kNormalFooter.x == 152 && kNormalFooter.y == 352 &&
              kNormalFooter.w == 176 && kNormalFooter.h == 56,
              "normal footer must retain its V2 rectangle");
static_assert(kOuterRing.x + kOuterRing.w / 2 == 240 &&
              kInnerRing.x + kInnerRing.w / 2 == 240 &&
              kOuterRing.y + kOuterRing.h / 2 == 240 &&
              kInnerRing.y + kInnerRing.h / 2 == 240,
              "display rings must remain concentric at the raster center");
static_assert(kOuterRing.x >= 0 && kOuterRing.y >= 0 &&
              kOuterRing.x + kOuterRing.w <= 480 &&
              kOuterRing.y + kOuterRing.h <= 480,
              "calibrated outer ring must remain inside the display raster");
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
static_assert(kNormalListProgress.y + kNormalListProgress.h <= kNormalListCount.y &&
              kNormalListCount.y + kNormalListCount.h <= kNormalFooter.y,
              "normal list count must stay below progress and above footer");
static_assert(kPlayerDetailAvatar.y == kPlayerDetailSummary.y &&
              kPlayerDetailAvatar.y + kPlayerDetailAvatar.h ==
                  kPlayerDetailSummary.y + kPlayerDetailSummary.h &&
              kPlayerDetailAvatar.x + kPlayerDetailAvatar.w + 12 ==
                  kPlayerDetailSummary.x,
              "player detail avatar and summary must share one balanced header row");
static_assert(kPlayerDetailAvatar.y + kPlayerDetailAvatar.h + 20 ==
                  kPlayerDetailAssets.y,
              "player detail summary must stay clear of its action grid");
static_assert(kPlayerDetailAssets.x + kPlayerDetailAssets.w + 20 == kPlayerDetailFinance.x &&
              kPlayerDetailTrade.x + kPlayerDetailTrade.w + 20 == kPlayerDetailRefresh.x &&
              kPlayerDetailAssets.x + kPlayerDetailAssets.w / 2 +
                      kPlayerDetailFinance.x + kPlayerDetailFinance.w / 2 == 480,
              "player detail actions must remain horizontally symmetric");
static_assert(kPlayerDetailAssets.y + kPlayerDetailAssets.h + 14 == kPlayerDetailTrade.y &&
              kPlayerDetailFinance.y + kPlayerDetailFinance.h + 14 == kPlayerDetailRefresh.y &&
              kPlayerDetailTrade.y + kPlayerDetailTrade.h + 18 == kNormalFooter.y,
              "player detail actions require stable row and footer gaps");
static_assert(kAssetDetailArtwork.y >= kAssetDetailHeaderBottom + 7 &&
              kAssetDetailArtwork.y + kAssetDetailArtwork.h <= kAssetDetailGroup.y,
              "asset artwork must stay below the header and above group progress");
static_assert(kAssetDetailGroup.y + kAssetDetailGroup.h <= kAssetDetailMetrics.y &&
              kAssetDetailMetrics.y + kAssetDetailMetrics.h <= kAssetDetailCash.y &&
              kAssetDetailCash.y + kAssetDetailCash.h + 6 <= kAssetDetailAction0.y &&
              kAssetDetailAction2.y + kAssetDetailAction2.h + 2 <= kNormalFooter.y,
              "asset detail rows require stable vertical gaps");
static_assert(kAssetDetailArtwork.x + kAssetDetailArtwork.w / 2 == 240 &&
              kAssetDetailGroup.x + kAssetDetailGroup.w / 2 == 240 &&
              kAssetDetailMetrics.x + kAssetDetailMetrics.w / 2 == 240 &&
              kAssetDetailCash.x + kAssetDetailCash.w / 2 == 240,
              "asset detail content must remain centered");
static_assert(kAssetDetailAction0.x + kAssetDetailAction0.w + 16 == kAssetDetailAction1.x &&
              kAssetDetailAction0.x + kAssetDetailAction0.w / 2 +
                      kAssetDetailAction1.x + kAssetDetailAction1.w / 2 == 480 &&
              kAssetDetailAction0.x == kAssetDetailAction2.x &&
              kAssetDetailAction1.x == kAssetDetailAction3.x,
              "asset detail actions must remain horizontally symmetric");
static_assert(kAssetDetailSingleAction.x + kAssetDetailSingleAction.w / 2 == 240 &&
              kAssetDetailPairLeft.x + kAssetDetailPairLeft.w + 16 ==
                  kAssetDetailPairRight.x &&
              kAssetDetailTripleTopLeft.x == kAssetDetailPairLeft.x &&
              kAssetDetailTripleTopRight.x == kAssetDetailPairRight.x &&
              kAssetDetailTripleBottom.x + kAssetDetailTripleBottom.w / 2 == 240,
              "adaptive asset actions must stay balanced around the display center");
static_assert(kModalCancel.y - (kModalConfirm.y + kModalConfirm.h) >= 8,
              "modal controls require an 8px vertical gap");
static_assert(uiRectInsideCircle(kNormalFooter) && uiRectInsideCircle(kModalRect) &&
              uiRectInsideCircle(kModalConfirm) && uiRectInsideCircle(kModalCancel),
              "named V2 rectangles must fit the round safe area");
static_assert(uiRectInsideCircle(kPlayerDetailAssets) &&
              uiRectInsideCircle(kPlayerDetailFinance) &&
              uiRectInsideCircle(kPlayerDetailTrade) &&
              uiRectInsideCircle(kPlayerDetailRefresh),
              "player detail actions must fit the round safe area");
static_assert(uiRectInsideCircle(kPlayerDetailAvatar) &&
              uiRectInsideCircle(kPlayerDetailSummary),
              "player detail avatar and summary must fit the round safe area");
static_assert(uiRectInsideCircle(kAssetDetailArtwork) &&
              uiRectInsideCircle(kAssetDetailGroup) &&
              uiRectInsideCircle(kAssetDetailMetrics) &&
              uiRectInsideCircle(kAssetDetailCash) &&
              uiRectInsideCircle(kAssetDetailAction0) &&
              uiRectInsideCircle(kAssetDetailAction1) &&
              uiRectInsideCircle(kAssetDetailAction2) &&
              uiRectInsideCircle(kAssetDetailAction3) &&
              uiRectInsideCircle(kAssetDetailSingleAction) &&
              uiRectInsideCircle(kAssetDetailPairLeft) &&
              uiRectInsideCircle(kAssetDetailPairRight) &&
              uiRectInsideCircle(kAssetDetailTripleTopLeft) &&
              uiRectInsideCircle(kAssetDetailTripleTopRight) &&
              uiRectInsideCircle(kAssetDetailTripleBottom),
              "asset detail content must fit the round safe area");
