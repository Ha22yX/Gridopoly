#pragma once
#include "development_artwork_cache.h"
#include "tile_model.h"

namespace gridopoly::tile {
void beginDevelopmentArtwork();
// Called only by the display owner; queues/allocations never wait for HTTP.
bool serviceDevelopmentArtwork(const TileState *authoritative, std::uint32_t now);
development::Cache::Lease acquireDevelopmentArtwork(const TileState &state);
bool developmentArtworkPreview(TileState &state);
bool developmentArtworkProbeActive();
bool developmentArtworkCommand(const char *command, std::uint32_t now);
void printDevelopmentArtworkStatus();
}  // namespace gridopoly::tile
