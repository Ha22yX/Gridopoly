#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
using String = std::string;
inline uint32_t avatarTestNow = 1000;
inline uint32_t millis() { return avatarTestNow; }
struct AvatarTestSerial {
    void println(const char *) {}
    template<class... Args> void printf(const char *, Args...) {}
};
inline AvatarTestSerial Serial;
