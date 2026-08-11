#pragma once

#include <stdint.h>

struct AvatarComponentRgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

inline uint32_t avatarRoundHalfUp(uint64_t numerator, uint32_t denominator)
{
    return static_cast<uint32_t>((numerator + denominator / 2u) / denominator);
}

inline uint8_t avatarRoundHalfUpByte(uint64_t numerator, uint32_t denominator)
{
    const uint32_t value = avatarRoundHalfUp(numerator, denominator);
    return static_cast<uint8_t>(value > 255u ? 255u : value);
}

inline void avatarTintHairPixel(uint8_t pixel[4], const AvatarComponentRgb &target)
{
    const uint32_t weight =
        54u * pixel[0] + 183u * pixel[1] + 19u * pixel[2];
    if (pixel[3] == 0 || weight < 6528u) return;
    const uint32_t numerator = 7u * 65280u + 21u * weight;
    constexpr uint32_t denominator = 1305600u;
    pixel[0] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.r) * numerator, denominator);
    pixel[1] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.g) * numerator, denominator);
    pixel[2] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.b) * numerator, denominator);
}

inline void avatarTintSkinPixel(uint8_t pixel[4], const AvatarComponentRgb &target)
{
    const uint32_t red = pixel[0];
    const uint32_t green = pixel[1];
    const uint32_t blue = pixel[2];
    const uint32_t weight = 54u * red + 183u * green + 19u * blue;
    uint32_t high = red > green ? red : green;
    if (blue > high) high = blue;
    uint32_t low = red < green ? red : green;
    if (blue < low) low = blue;
    if (pixel[3] == 0 || weight < 23552u ||
        5u * (high - low) < high || 25u * red < 26u * green ||
        25u * green < 23u * blue) return;

    uint32_t numerator = weight;
    uint32_t denominator = 45568u;
    if (100u * numerator < 42u * denominator) {
        numerator = 42u;
        denominator = 100u;
    } else if (100u * numerator > 135u * denominator) {
        numerator = 135u;
        denominator = 100u;
    }
    pixel[0] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.r) * numerator, denominator);
    pixel[1] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.g) * numerator, denominator);
    pixel[2] = avatarRoundHalfUpByte(
        static_cast<uint64_t>(target.b) * numerator, denominator);
}

inline void avatarSourceOver(uint8_t destination[4], const uint8_t source[4])
{
    const uint32_t sourceAlpha = source[3];
    if (sourceAlpha == 0) return;
    if (sourceAlpha == 255) {
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = source[3];
        return;
    }
    const uint32_t destinationAlpha = destination[3];
    const uint32_t inverse = 255u - sourceAlpha;
    const uint32_t outputAlpha = sourceAlpha +
        avatarRoundHalfUp(static_cast<uint64_t>(destinationAlpha) * inverse, 255u);
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const uint64_t premultiplied =
            static_cast<uint64_t>(source[channel]) * sourceAlpha +
            avatarRoundHalfUp(
                static_cast<uint64_t>(destination[channel]) * destinationAlpha * inverse,
                255u);
        const uint32_t output = outputAlpha == 0 ? 0 :
            avatarRoundHalfUp(premultiplied, outputAlpha);
        destination[channel] = static_cast<uint8_t>(output > 255u ? 255u : output);
    }
    destination[3] = static_cast<uint8_t>(outputAlpha);
}
