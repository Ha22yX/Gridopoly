#pragma once

#include "tile_network.h"

namespace gridopoly::tile {

struct TileConnectionView {
  const char *eyebrow;
  const char *title;
  const char *detail;
  Rgb accent;
};

TileConnectionView tileConnectionView(TileNetworkLink link);

}  // namespace gridopoly::tile
