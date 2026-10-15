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

The ORDER suite drives the production V1 transmitter/receiver at 1 ms intervals
with both 16 and 32 independent links. It checks direct neighbors, one broken
wire expiring without stopping other transmitters, reconnection with a newer
frame, and one restarted node changing only its successor's upstream identity.
This is a timing/state-machine simulation, not an electrical ring test.

The network-contract suite also extracts the production server tile projection,
HTTP assignment/heartbeat serialization, and device `parsePayload`. It links
the actual BoardCatalog and Grid City visual catalog, covering all 16 tiles
(including corners 0/4/8/12), artwork/prices, a changed manual anchor,
assignment revocation/recovery, and all active pairing states on an unassigned
reader suppressing movement. Only clock/RSSI/Serial boundaries are stubbed;
the private `projectTile` qualifier is removed to call its unchanged body.
The server topology solver, live HTTP, physical Tag operations, and display
appearance are outside this suite. No online heartbeats are fabricated.

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

The WS2812 suite uses the production encoder, maintenance parser/state and
transmission controller. It checks all byte values against the exact
WS2812B-B-V6 timing table, GRB/MSB ordering across ten pixels, buffer bounds,
dim single-channel/off commands, invalid input, permanent expiry across clock
wrap, and rejection of an overlong serial line with an otherwise valid prefix.
Fault injection covers wrong counter clock, initialization/write/completion
errors, retained stop errors and refusal to retry after a failed transmission.
The firmware-boundary suite also exercises the actual render path restoring
the current movement scene after maintenance expires without changing the
network snapshot. These checks do not measure GPIO21, the level shifter, DIN,
LED current, or visible color. A driver completion is not optical acceptance.
