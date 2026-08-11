#pragma once

#include "transport_types.h"

class PlayerConsoleTransport {
public:
    virtual ~PlayerConsoleTransport() = default;
    virtual void begin(uint32_t nowMs) = 0;
    virtual bool send(const TransportCommand &command, uint32_t nowMs) = 0;
    virtual void tick(uint32_t nowMs) = 0;
    virtual bool poll(TransportEvent &event) = 0;
};
