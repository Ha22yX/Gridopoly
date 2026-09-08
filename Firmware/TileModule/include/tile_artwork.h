#pragma once

#include <cstdint>

#include "tile_model.h"

namespace gridopoly::tile {

struct TileArtwork {
  std::uint16_t width;
  std::uint16_t height;
  const std::uint16_t *pixels;
};

const TileArtwork *tileArtwork(ArtworkId id);
const char *artworkKey(ArtworkId id);

}  // namespace gridopoly::tile

