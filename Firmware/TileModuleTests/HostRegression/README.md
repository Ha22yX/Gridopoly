# Tile firmware host regression

This standalone CMake project runs the four existing Unity native suites and
tests selected production firmware routines with deterministic RTOS, Serial,
and RMT boundaries. It does not connect to a device or server.

`extract_fixture.py` copies the actual inventory publication, network-cache
update, movement-change comparison, and LED-rendering routines into the build
directory. Only hardware boundaries are stubbed. Expected source boundaries
are explicit; extraction fails if they move. The output is generated and must
not be committed. This test does not compile the entire Arduino application,
exercise real scheduler contention, or verify emitted LED electrical signals.

The fault-injection cases cover a failed cache lock followed by an identical
inventory, replacement with the latest complete inventory, and preservation of
reader state/UID/overflow/revision consistency. The pre-fix V0.27 source fails
the first two cases. LED cases cover all ten pixels, channel limits, green
double flashes, orange breathing, unchanged movement through a resync, and
return to the normal tile color.

Use a dedicated output directory. Unity defaults to the dependency installed
by `pio test -d Firmware/TileModule -e native`; use `-DUNITY_ROOT=...` if needed.

```powershell
cmake -S Firmware/TileModuleTests/HostRegression -B C:/GridopolyBuilds/tile-native -DCMAKE_BUILD_TYPE=Debug
cmake --build C:/GridopolyBuilds/tile-native --config Debug
ctest --test-dir C:/GridopolyBuilds/tile-native -C Debug --output-on-failure
```

The normal ESP32 build remains required before deploying firmware.
Debug is sufficient for these deterministic host checks and avoids the long
MSVC Release optimization of the header-only decoder fixture.
