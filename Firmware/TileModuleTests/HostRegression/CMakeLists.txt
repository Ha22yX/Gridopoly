cmake_minimum_required(VERSION 3.20)
project(GridopolyTileHostRegression LANGUAGES C CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(TILE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../TileModule" ABSOLUTE)
set(UNITY_ROOT "${TILE_ROOT}/.pio/libdeps/native/Unity" CACHE PATH "Unity library root")
add_library(unity STATIC "${UNITY_ROOT}/src/unity.c")
target_include_directories(unity PUBLIC "${UNITY_ROOT}/src")
add_library(tile_model STATIC
  "${TILE_ROOT}/src/tile_model.cpp"
  "${TILE_ROOT}/src/tile_assignment.cpp"
  "${TILE_ROOT}/src/tile_connection_view.cpp"
  "${TILE_ROOT}/src/demo_engine.cpp")
target_include_directories(tile_model PUBLIC "${TILE_ROOT}/include")
target_compile_features(tile_model PUBLIC cxx_std_17)
foreach(suite tile_model hitag_s_decoder hitag_s_protocol tag_presence_filter)
  add_executable(test_${suite} "${TILE_ROOT}/test/test_${suite}/test_main.cpp")
  target_link_libraries(test_${suite} PRIVATE tile_model unity)
  add_test(NAME ${suite} COMMAND test_${suite})
endforeach()
# Compile the actual firmware routines with deterministic serial/RTOS/RMT
# boundaries. This leaves production wiring and LED behavior untouched.
set(FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/firmware_fixture.inc")
add_custom_command(OUTPUT "${FIXTURE}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract_fixture.py"
          "${TILE_ROOT}" "${FIXTURE}"
  DEPENDS "${TILE_ROOT}/src/main.cpp" "${TILE_ROOT}/src/tile_network.cpp"
          "${CMAKE_CURRENT_LIST_DIR}/extract_fixture.py"
  VERBATIM)
add_executable(test_firmware_boundaries firmware_boundaries.cpp "${FIXTURE}")
target_include_directories(test_firmware_boundaries PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_firmware_boundaries PRIVATE tile_model unity)
add_test(NAME firmware_boundaries COMMAND test_firmware_boundaries)
if(MSVC)
  target_compile_definitions(tile_model PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(test_firmware_boundaries PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(tile_model PRIVATE /W4 /WX)
  target_compile_options(test_firmware_boundaries PRIVATE /W4 /WX)
else()
  target_compile_options(tile_model PRIVATE -Wall -Wextra -Werror)
  target_compile_options(test_firmware_boundaries PRIVATE -Wall -Wextra -Werror)
endif()
