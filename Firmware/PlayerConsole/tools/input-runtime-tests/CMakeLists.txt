cmake_minimum_required(VERSION 3.20)
project(gridopoly_input_runtime C CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(PLAYER "${REPO}/Firmware/PlayerConsole")
set(LVGL "${REPO}/Firmware/libraries/lvgl")
file(GLOB_RECURSE LVGL_SOURCES "${LVGL}/src/*.c")
add_library(lvgl_runtime STATIC ${LVGL_SOURCES})
target_include_directories(lvgl_runtime PUBLIC "${LVGL}" "${PLAYER}")
target_compile_definitions(lvgl_runtime PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lvgl_runtime PRIVATE /w /utf-8)
add_executable(input-runtime runtime.cpp
    "${PLAYER}/app_state.cpp" "${PLAYER}/demo_data.cpp"
    "${PLAYER}/grid_city_visual_catalog.cpp" "${PLAYER}/ui_carousel.cpp"
    "${PLAYER}/ui_center_list.cpp" "${PLAYER}/ui_modal.cpp"
    "${PLAYER}/ui_motion.cpp" "${PLAYER}/ui_primitives.cpp"
    "${PLAYER}/src/fonts/ui_font_14.c" "${PLAYER}/src/fonts/ui_font_16.c"
    "${PLAYER}/src/assets/sample_assets.c" "${PLAYER}/src/assets/grid_city_tile_images.cpp"
    "${REPO}/Firmware/libraries/GridopolyCore/src/gridopoly/core/BoardCatalog.cpp"
    "${REPO}/Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp")
target_include_directories(input-runtime BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${PLAYER}/tools/host-stubs" "${REPO}/Firmware/libraries/GridopolyCore/src"
    "${REPO}/Firmware/libraries/GridopolyProtocol/src")
target_compile_options(input-runtime PRIVATE /utf-8 "$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/stubs/font-linkage.h>")
target_link_libraries(input-runtime PRIVATE lvgl_runtime)
