#pragma once
#include "fakes.h"
#define LV_IMG_CF_TRUE_COLOR 4
struct lv_img_header_t { uint32_t cf = 0, always_zero = 0, reserved = 0, w = 0, h = 0; };
struct lv_img_dsc_t { lv_img_header_t header{}; uint32_t data_size = 0; const uint8_t *data = nullptr; };
inline void lv_img_cache_invalidate_src(const void *) { assert(criticalDepth == 0); ++invalidations; }
