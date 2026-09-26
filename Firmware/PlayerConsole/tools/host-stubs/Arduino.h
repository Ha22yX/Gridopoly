#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
enum gpio_num_t { GPIO_NUM_0=0, GPIO_NUM_5=5, GPIO_NUM_6=6 };
using portMUX_TYPE = int;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0;
struct HostSerial {
    template<typename... Args> int printf(const char *, Args...) { return 0; }
};
inline HostSerial Serial;
