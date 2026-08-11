# Income Tax and Luxury Tax Raspberry Pi Deployment Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate and deploy the renamed Income Tax and Luxury Tax artwork and web mappings to the Raspberry Pi without changing game rules, wire protocols, or COM4 firmware.

**Architecture:** Keep the authoritative tile IDs `FEE-CITY` and `FEE-DENSITY` unchanged while replacing only player-visible names and asset keys. Build the ARM64 server in an isolated directory, stage exactly 36 PNG plus 36 RGB565 assets, atomically replace the production asset directory and server binary, then validate all online routes and the existing UDP session.

**Tech Stack:** C++17, Bash, Python 3, Node.js verification tools, systemd, HTTP, Wi-Fi UDP.

## Global Constraints

- Preserve `FEE-CITY`, `FEE-DENSITY`, board positions, fee amounts, game state schema, UDP protocol, and Core behavior.
- Publish `cover-income-tax` and `cover-luxury-tax`; the old `cover-city-services` and `cover-skyline-levy` keys must not enter the new 72-file staging set.
- Stage exactly 36 PNG and 36 RGB565 files; each RGB565 payload is exactly 32,768 bytes.
- Use staging plus atomic replacement and create a complete rollback backup first.
- Restart only `gridopoly.service`; do not restart the Raspberry Pi, AP, or network stack.
- Do not open, reset, modify, or flash COM4.

---

### Task 1: Validate the canonical asset set locally

**Files:**
- Verify: `Assets/GridCity/StreetV3/manifests/grid-city-street-assets-v3.json`
- Verify: `Assets/GridCity/StreetV3/source/cover-income-tax.png`
- Verify: `Assets/GridCity/StreetV3/source/cover-luxury-tax.png`
- Test: `Firmware/PlayerConsole/tools/verify-tile-assets.py`
- Test: `Server/RaspberryPi/tools/test-stage-web-tile-assets.py`

**Interfaces:**
- Consumes: Street V3 manifest entries for `FEE-CITY` and `FEE-DENSITY`.
- Produces: An exact 36-PNG/36-RGB565 deployable set with the new cache keys.

- [ ] Run `python Firmware/PlayerConsole/tools/verify-tile-assets.py` and require PASS.
- [ ] Run `python Server/RaspberryPi/tools/test-stage-web-tile-assets.py` and require `png=36 rgb565=36 corners=4`.
- [ ] Run `node Firmware/TestGameServer/tools/generate-web-assets.mjs` and `node Firmware/TestGameServer/tools/test-web-ui-layout.mjs` and require PASS.
- [ ] Search the new staging truth and require the two new keys while rejecting both old keys.

### Task 2: Build and test on ARM64 in isolation

**Files:**
- Execute: `Server/RaspberryPi/tools/build-and-test-native.sh`
- Build: `Server/RaspberryPi/src/*.cpp`
- Test: `tests/host/*.cpp`

**Interfaces:**
- Consumes: The validated source tree and exact web assets from Task 1.
- Produces: A tested ARM64 `gridopoly_server` binary and route test executables.

- [ ] Upload a source archive that excludes secrets and build outputs.
- [ ] Extract it under a new `/tmp/gridopoly-income-luxury-src-*` directory.
- [ ] Set `GRIDOPOLY_NATIVE_BUILD_DIR` to a new `/tmp/gridopoly-income-luxury-build-*` directory.
- [ ] Run `bash Server/RaspberryPi/tools/build-and-test-native.sh` and require every native test plus the final server build to pass.
- [ ] Record the candidate binary SHA-256.

### Task 3: Create rollback backup and deploy atomically

**Files:**
- Replace: `/usr/local/bin/gridopoly_server`
- Replace atomically: the production web tile asset directory used by `gridopoly.service`
- Back up: `/usr/local/bin/gridopoly_server`, `/var/lib/gridopoly`, and the current production web assets.

**Interfaces:**
- Consumes: The ARM64 candidate and exact 72-file staging set from Task 2.
- Produces: A production service serving the new labels and cache keys while retaining the same room and game state.

- [ ] Record `/health`, `/api/sync`, service status, room, version, peers, and current binary hash.
- [ ] Stop only `gridopoly.service` and verify it is inactive.
- [ ] Create a timestamped `/var/backups/gridopoly/income-luxury-tax-*` backup.
- [ ] Install the candidate binary to a temporary path and verify its hash.
- [ ] Stage exactly 72 asset files and atomically switch the production asset directory, removing old extras from the new active set.
- [ ] Atomically move the verified candidate into `/usr/local/bin/gridopoly_server`.
- [ ] Start `gridopoly.service` and verify `GRIDOPOLY_READY`, restored room, and active status.

### Task 4: Verify all online routes and live clients

**Files:**
- Execute: `Server/RaspberryPi/tools/verify-web-tile-assets.mjs`
- Execute: `Server/RaspberryPi/tools/serve-web-board-matrix.mjs`

**Interfaces:**
- Consumes: The production service from Task 3.
- Produces: Evidence that browsers and player screens resolve the new assets with no protocol regressions.

- [ ] Require HTTP 200, immutable cache headers, correct MIME, exact length, and matching SHA-256 for all 36 PNG and 36 RGB565 routes.
- [ ] Explicitly require both `cover-income-tax.{png,rgb565}` and `cover-luxury-tax.{png,rgb565}`.
- [ ] Require both old key routes to return 404.
- [ ] Inspect 16/24/32/40 web boards and require the visible labels `Income Tax` and `Luxury Tax`, zero overflow, and zero console errors.
- [ ] Sample `/health` twice for at least 25 seconds and require peers=1, equal Heartbeat/Ack deltas, fixed resync in an idle version window, and zero auth/replay/tx errors.
- [ ] Confirm the room is unchanged and no forced-roll override or game rule changed.

### Task 5: Record deployment and rollback evidence

**Files:**
- Document: `Docs/firmware/raspberry-pi-server.md`

**Interfaces:**
- Consumes: Test and production evidence from Tasks 1-4.
- Produces: A user-facing deployment report with exact binary hash and backup path.

- [ ] Report the production binary SHA-256, room/version/peers, route counts, route hashes, and UDP counter deltas.
- [ ] Report the timestamped backup path and the exact rollback targets.
- [ ] State explicitly that COM4 was not accessed or flashed.
