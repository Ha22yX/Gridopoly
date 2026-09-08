#pragma once

#include <cstddef>
#include <cstdint>

#include "tile_model.h"

namespace gridopoly::tile {

std::size_t demoScenarioCount();
TileState makeDemoScenario(std::size_t index);

class DemoEngine {
 public:
  explicit DemoEngine(std::uint32_t interval_ms = 7000);

  const TileState &state() const { return state_; }
  std::size_t index() const { return index_; }
  bool autoplay() const { return autoplay_; }
  std::uint32_t intervalMs() const { return interval_ms_; }

  bool update(std::uint32_t now_ms);
  void next(std::uint32_t now_ms);
  void previous(std::uint32_t now_ms);
  void select(std::size_t index, std::uint32_t now_ms);
  void setAutoplay(bool enabled, std::uint32_t now_ms);

 private:
  void load(std::size_t index, std::uint32_t now_ms);

  std::size_t index_ = 0;
  std::uint32_t interval_ms_ = 7000;
  std::uint32_t changed_at_ms_ = 0;
  bool autoplay_ = true;
  TileState state_{};
};

}  // namespace gridopoly::tile

