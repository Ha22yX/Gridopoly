#pragma once

#include <lvgl.h>

#include "../../remote_tile_cache.h"

const char *gridCityArtworkAssetKey(GridCityArtwork artwork);
void gridCityArtworkPrefetch(const GridCityVisualDefinition &visual, uint8_t level = kArtworkLevelUnknown);
const lv_img_dsc_t *gridCityArtworkImage(const GridCityVisualDefinition &visual, uint8_t level = kArtworkLevelUnknown);
