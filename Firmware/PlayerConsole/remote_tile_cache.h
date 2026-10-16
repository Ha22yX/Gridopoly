#pragma once
#include <lvgl.h>
#include "remote_artwork_request.h"

// One worker, twenty buffers including the in-flight image, RAM only.
void remoteTileCacheBegin();
// Update each UI frame, including pages without artwork. Only the visible
// request and roll target may download; new room/map rejects in-flight results.
void remoteTileCacheSetView(uint32_t room, uint8_t boardSize, GridCityArtwork artwork,
                           const char *key, uint8_t level = kArtworkLevelUnknown);
void remoteTileCachePrefetch(GridCityArtwork artwork, const char *key,
                            uint8_t level = kArtworkLevelUnknown);
// Each successful acquire owns a lease. Release on LV_EVENT_DELETE or object
// creation failure, on the LVGL task under its normal lock.
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork artwork, const char *key,
                                      uint8_t level = kArtworkLevelUnknown);
void remoteTileCacheReleaseImage(const lv_img_dsc_t *image);
bool remoteTileCacheConsumeUpdate();
