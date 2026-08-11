#pragma once

#include <cstdint>
#include <gridopoly/core/GameModel.h>
#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::server {

// Builds the on-demand player projection from the bounded authoritative game
// state. It does not mutate or subscribe to the state and therefore adds no
// bytes to the regular snapshot broadcasts.
bool makePlayerDetailProjection(const gridopoly::core::GameState& state,
                                std::uint32_t requestId,
                                std::uint8_t targetPlayerId,
                                std::uint32_t requestedStateVersion,
                                gridopoly::protocol::PlayerDetailResponse& output);

}  // namespace gridopoly::server
