#pragma once
#include <cstdint>
struct WiFiClient {
    void setTimeout(uint32_t) {}
    bool connected() const { return true; }
    void setNoDelay(bool) {}
    void stop() {}
};
