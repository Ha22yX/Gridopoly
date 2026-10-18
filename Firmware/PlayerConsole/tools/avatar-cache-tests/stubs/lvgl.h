#pragma once
#include <cstdint>
constexpr uint32_t LV_IMG_CF_TRUE_COLOR = 4;
constexpr uint32_t LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED = 5;
struct lv_img_header_t { uint32_t cf{}, always_zero{}, reserved{}, w{}, h{}; };
struct lv_img_dsc_t {
    lv_img_header_t header{};
    uint32_t data_size{};
    const uint8_t *data{};
};
