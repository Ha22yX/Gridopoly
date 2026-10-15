#pragma once
#include <cstddef>
#include <cstdint>
#include <driver/rmt.h>

namespace gridopoly::tile::ws2812 {
class Esp32Driver {
 public:
  using Symbol = rmt_item32_t;
  int begin(std::uint32_t &clock_hz);
  int write(const Symbol *symbols, std::size_t count);
  int wait(unsigned timeout_ms);
  int stop();
  void holdLow(unsigned microseconds);
 private:
  bool configured_ = false;
};
}  // namespace gridopoly::tile::ws2812
