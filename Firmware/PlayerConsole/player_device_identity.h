#pragma once

#include <stdint.h>

// ESP.getEfuseMac() stores the six MAC bytes from least to most significant.
// Hash all six: truncating to 32 bits loses the device-specific MAC suffix.
// This is a stable wire identifier, not an authentication credential.
inline constexpr uint32_t playerDeviceId(uint64_t efuseMac)
{
    uint32_t hash = 2166136261u;
    for (uint8_t index = 0; index < 6; ++index) {
        hash ^= static_cast<uint8_t>(efuseMac >> (index * 8u));
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

// Keep the full hardware identity in the existing 16-character discovery name.
// The server can distinguish an ID collision without changing the frame size.
inline void playerDeviceDiscoveryName(uint64_t efuseMac, char (&name)[17])
{
    constexpr char hex[] = "0123456789abcdef";
    name[0] = 'G'; name[1] = 'P'; name[2] = '-';
    for (uint8_t index = 0; index < 6; ++index) {
        const uint8_t byte = static_cast<uint8_t>(efuseMac >> (index * 8u));
        name[3 + index * 2] = hex[byte >> 4u];
        name[4 + index * 2] = hex[byte & 0x0fu];
    }
    name[15] = '\0'; name[16] = '\0';
}
