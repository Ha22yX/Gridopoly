#include "ui_motion.h"

namespace {

uint8_t rollingFace(uint32_t elapsedMs, uint8_t dieIndex, uint8_t finalFace)
{
    uint8_t face = static_cast<uint8_t>((elapsedMs / 90u + dieIndex * 3u) % 5u + 1u);
    if (finalFace >= 1 && finalFace <= 6 && face >= finalFace) ++face;
    return face;
}

int16_t diceBaseX(uint8_t dieIndex)
{
    return dieIndex == 0 ? 142 : 278;
}

}

CarouselPose uiCarouselPose(int8_t relativeSlot)
{
    switch (relativeSlot) {
        case -2: return CarouselPose{64, 179, 15};
        case -1: return CarouselPose{152, 213, 87};
        case 0: return CarouselPose{240, 256, 255};
        case 1: return CarouselPose{328, 213, 87};
        case 2: return CarouselPose{416, 179, 15};
        default: return CarouselPose{240, 0, 0};
    }
}

int16_t uiCenterListTrackY(uint8_t selectedIndex, int16_t selectedCenterY,
                           int16_t rowStride)
{
    return static_cast<int16_t>(selectedCenterY - static_cast<int32_t>(selectedIndex) * rowStride);
}

uint16_t uiListProgressPermille(uint8_t selectedIndex, uint8_t count)
{
    if (count <= 1) return 0;
    if (selectedIndex >= count - 1) return 1000;
    return static_cast<uint16_t>(static_cast<uint32_t>(selectedIndex) * 1000u / (count - 1u));
}

DicePose uiDicePose(uint32_t elapsedMs, uint8_t dieIndex, uint8_t finalFace)
{
    const int16_t baseX = diceBaseX(dieIndex);
    if (elapsedMs >= 1900u) return DicePose{baseX, 220, 0, 256, finalFace};

    const uint8_t face = rollingFace(elapsedMs, dieIndex, finalFace);
    if (elapsedMs < 150u) {
        const int16_t progress = static_cast<int16_t>(elapsedMs);
        return DicePose{static_cast<int16_t>(baseX + progress * 24 / 150),
                        static_cast<int16_t>(220 - progress * 42 / 150),
                        static_cast<int16_t>(progress * 36),
                        static_cast<uint16_t>(224 + progress * 64 / 150), face};
    }
    if (elapsedMs < 1050u) {
        const uint32_t progress = elapsedMs - 150u;
        return DicePose{static_cast<int16_t>(baseX + 24 - progress * 48 / 900),
                        static_cast<int16_t>(178 + progress * 22 / 900),
                        static_cast<int16_t>(5400 + progress * 72 / 10),
                        static_cast<uint16_t>(288 - progress * 24 / 900), face};
    }
    if (elapsedMs < 1500u) {
        const uint32_t progress = elapsedMs - 1050u;
        return DicePose{static_cast<int16_t>(baseX - 24 + progress * 24 / 450),
                        static_cast<int16_t>(200 + progress * 20 / 450),
                        static_cast<int16_t>(11880 + progress * 24 / 10),
                        static_cast<uint16_t>(264 - progress * 12 / 450), face};
    }

    const uint32_t progress = elapsedMs - 1500u;
    return DicePose{baseX, 220,
                    static_cast<int16_t>(960 - progress * 960 / 400),
                    static_cast<uint16_t>(252 + progress * 4 / 400), face};
}

uint8_t uiMoneyFontPx(int32_t amount)
{
    uint32_t magnitude = amount < 0 ? 0u - static_cast<uint32_t>(amount) : static_cast<uint32_t>(amount);
    uint8_t digits = 1;
    while (magnitude >= 10u) {
        magnitude /= 10u;
        ++digits;
    }
    const uint8_t characters = static_cast<uint8_t>(digits + (digits - 1u) / 3u + (amount < 0 ? 1u : 0u));
    if (characters <= 6) return 40;
    if (characters <= 9) return 32;
    return 24;
}
