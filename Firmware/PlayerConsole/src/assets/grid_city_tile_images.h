#pragma once

#include <lvgl.h>

#include "../../grid_city_visual_catalog.h"

const char *gridCityArtworkAssetKey(GridCityArtwork artwork);
void gridCityArtworkPrefetch(const GridCityVisualDefinition &visual);
const lv_img_dsc_t *gridCityArtworkImage(const GridCityVisualDefinition &visual);
