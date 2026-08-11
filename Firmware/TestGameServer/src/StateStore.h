#pragma once

#include <Preferences.h>
#include <gridopoly/core/GameEngine.h>

namespace gridopoly::server {

class StateStore {
 public:
  bool begin();
  bool restore(gridopoly::core::GameEngine& engine);
  bool save(const gridopoly::core::GameState& state);
  std::uint32_t loadBotActionIntervalMs(std::uint32_t fallback,
                                        std::uint32_t minimum,
                                        std::uint32_t maximum);
  bool saveBotActionIntervalMs(std::uint32_t intervalMs);
  void clear();

 private:
  Preferences preferences_;
  bool open_{};
};

}  // namespace gridopoly::server
