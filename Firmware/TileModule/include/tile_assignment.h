#pragma once

#include <cstdint>

#include "tile_model.h"

namespace gridopoly::tile {

struct TileAssignmentDto {
  bool assigned = false;
  bool manual = false;
  std::uint8_t map_index = 0;
  char tile_id[16]{};
  char display_name[25]{};
  char kind[20]{};
  std::uint32_t accent_rgb = 0;
  char artwork_key[40]{};
  std::uint16_t purchase_price = 0;
  std::uint8_t owner_player = 0;
  char owner_display_name[21]{};
  std::uint32_t owner_rgb = 0;
  std::uint64_t revision = 0;
};

TileKind tileKindFromServer(const char *kind);
ArtworkId artworkFromServerKey(const char *key);
const char *tileSubtitleFromAssignment(const TileAssignmentDto &assignment);
bool applyTileAssignment(const TileAssignmentDto &assignment, TileState &state);

}  // namespace gridopoly::tile
