#pragma once
#include <cstdint>
#include "order_link.h"

namespace gridopoly::tile {
struct OrderLinkObservation {
  std::uint64_t boot_id = 0;
  std::uint16_t tx_sequence = 0;
  order::Beacon upstream{};
  std::uint32_t age_ms = 0;
  std::uint32_t accepted = 0, rejected = 0, timing_drops = 0;
  bool valid = false, receiving = false, stuck_low = false;
  bool timer_ready = false, output_pulled_low = false;
};
bool initializeOrderLink();
OrderLinkObservation orderLinkObservation();
}  // namespace gridopoly::tile
