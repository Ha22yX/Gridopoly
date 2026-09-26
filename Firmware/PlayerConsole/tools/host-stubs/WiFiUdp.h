#pragma once
#include <cstdint>
class IPAddress {
    std::uint32_t value_;
public:
    IPAddress(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
        : value_((std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | d) {}
    bool operator==(const IPAddress &other) const { return value_ == other.value_; }
    bool operator!=(const IPAddress &other) const { return !(*this == other); }
};
class WiFiUDP {};
