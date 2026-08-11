#pragma once

#include <lvgl.h>

#include "grid_city_visual_catalog.h"

// Starts the low-priority HTTP loader. Artwork remains in PSRAM until reboot.
void remoteTileCacheBegin();

// Queues artwork without pinning it as the currently displayed image.
void remoteTileCachePrefetch(GridCityArtwork artwork, const char *key);

// Returns a ready descriptor or nullptr while the asset is queued/downloading.
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork artwork, const char *key);

// Consumes a cache publication edge so the UI can rebuild with the real image.
bool remoteTileCacheConsumeUpdate();
