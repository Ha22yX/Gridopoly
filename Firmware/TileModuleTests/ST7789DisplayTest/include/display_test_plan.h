#pragma once

#include <cstddef>
#include <cstdint>

namespace tile_display_test {

enum class StageId : std::uint8_t {
  SolidRed,
  SolidGreen,
  SolidBlue,
  SolidWhite,
  SolidBlack,
  ColorBars,
  Geometry,
  GridAndGray,
  Diagnostics,
  BacklightRamp,
};

struct Stage {
  StageId id;
  const char *name;
  const char *expectation;
  std::uint32_t duration_ms;
};

std::size_t stageCount();
const Stage &stageAt(std::size_t index);
std::size_t nextStageIndex(std::size_t index);
bool stageElapsed(std::uint32_t now_ms, std::uint32_t started_ms,
                  std::uint32_t duration_ms);

}  // namespace tile_display_test
