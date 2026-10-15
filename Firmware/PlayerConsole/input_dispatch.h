#pragma once

#include "app_types.h"

// Drain a ready burst without requiring one rendered frame per detent. Preserve
// each signed step and its timestamp; never sum across reversals or buttons.
// A release may request handwriting work or change pages, so service/render it
// before consuming later input. Bound the batch to keep transport/timers live.
template <typename Poll, typename Consume>
uint8_t dispatchInputBatch(Poll poll, Consume consume)
{
    InputEvent event{};
    uint8_t consumed = 0;
    while (consumed < 16 && poll(event)) {
        consume(event);
        ++consumed;
        if (event.kind == InputKind::ButtonUp) break;
    }
    return consumed;
}
