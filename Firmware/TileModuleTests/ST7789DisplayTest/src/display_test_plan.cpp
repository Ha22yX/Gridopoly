#include "display_test_plan.h"

#include <array>

namespace tile_display_test {
namespace {

constexpr std::array<Stage, 10> kStages = {{
    {StageId::SolidRed, "SOLID RED",
     "The full panel is evenly red with no blue swap.", 1'500},
    {StageId::SolidGreen, "SOLID GREEN",
     "The full panel is evenly green with no stuck rows.", 1'500},
    {StageId::SolidBlue, "SOLID BLUE",
     "The full panel is evenly blue with no red swap.", 1'500},
    {StageId::SolidWhite, "SOLID WHITE",
     "The full panel is evenly white without flicker.", 1'500},
    {StageId::SolidBlack, "SOLID BLACK",
     "The panel is black while the backlight remains on.", 1'500},
    {StageId::ColorBars, "RGB + CMY BARS",
     "Six labeled color bars appear in the documented order.", 3'500},
    {StageId::Geometry, "EDGES + CORNERS",
     "The border is complete and TL/TR/BL/BR are not mirrored.", 3'500},
    {StageId::GridAndGray, "GRID + GRAYSCALE",
     "Grid lines are stable and gray steps increase smoothly.", 3'500},
    {StageId::Diagnostics, "DIAGNOSTICS",
     "Pin data is readable and the frame counter advances.", 3'500},
    {StageId::BacklightRamp, "BACKLIGHT RAMP",
     "Brightness rises and falls smoothly without resets.", 5'000},
}};

}  // namespace

std::size_t stageCount() { return kStages.size(); }

const Stage &stageAt(std::size_t index) {
  return kStages[index % kStages.size()];
}

std::size_t nextStageIndex(std::size_t index) {
  return (index + 1U) % kStages.size();
}

bool stageElapsed(std::uint32_t now_ms, std::uint32_t started_ms,
                  std::uint32_t duration_ms) {
  return static_cast<std::uint32_t>(now_ms - started_ms) >= duration_ms;
}

}  // namespace tile_display_test
