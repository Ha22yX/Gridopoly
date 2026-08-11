# Cross-Device Game Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the frozen Avatar Setup, Name Setup, Ready, and server-timed countdown lifecycle to every new web-created room.

**Architecture:** Identity is a separately persisted low-frequency authority projection carried by `IdentityRequest` and `IdentitySnapshot`. The game engine remains in Lobby until the persisted five-second deadline expires. A native avatar worker composes immutable PNG/RGB565 files from a deterministic build-time V1 layer package.

**Tech Stack:** C++17, GridopolyProtocol, GridopolyCore, authenticated Wi-Fi UDP, native HTTP server, Python/Pillow build-time asset tooling, CMake/CTest, ARM64 Raspberry Pi OS.

## Global Constraints

- Do not edit `Firmware/PlayerConsole`.
- Do not access, reset, flash, or deploy to COM4 or the Raspberry Pi during implementation.
- Human and Bot seats are frozen at room creation; total players are 2–6 and at least one is human.
- `ConfirmName` is the Ready action; there is no separate Ready mutation.
- Mutations require exact nonzero state version and exact seat revision; Query permits zero.
- Identity does not enlarge State/Auth/Roster payloads.
- Every production behavior is implemented test-first and its test must be observed failing for the missing behavior.

---

### Task 1: Freeze identity wire codecs

**Files:**
- Modify: `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.h`
- Modify: `Firmware/libraries/GridopolyProtocol/src/gridopoly/protocol/Protocol.cpp`
- Modify: `tests/host/protocol_tests.cpp`
- Modify: `Docs/firmware/wifi-udp-player-protocol.md`

**Interfaces:**
- Produces: `IdentityOperation`, `IdentityRoomPhase`, `IdentitySeatStage`, `IdentityResultCode`, `AvatarRecipe`, `IdentityRequest`, `IdentitySeatRecord`, `IdentitySnapshot`, and four codec functions.

- [ ] Write literal-offset tests for the 44-byte request and 182-byte six-seat snapshot, including reserved bytes and maximum values.
- [ ] Run `gridopoly_protocol_tests` and observe missing identity symbols/codec failure.
- [ ] Add the enums, structs, constants, and minimal little-endian codecs.
- [ ] Re-run the protocol target and the existing full host test set.
- [ ] Record the exact byte table in the canonical UDP protocol document.

### Task 2: Add identity validation and persistence

**Files:**
- Create: `Server/RaspberryPi/src/IdentityModel.h`
- Create: `Server/RaspberryPi/src/IdentityModel.cpp`
- Create: `Server/RaspberryPi/src/FileIdentityStore.h`
- Create: `Server/RaspberryPi/src/FileIdentityStore.cpp`
- Create: `tests/host/identity_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `validateUtf8Name`, `caseFoldIdentityName`, `validAvatarRecipe`, `IdentityRoomState`, and `FileIdentityStore::restore/save/clear`.

- [ ] Write failing table tests for legal recipes, invalid UTF-8, control characters, empty/overlength names, and case-insensitive duplicates.
- [ ] Write failing schema/CRC/truncation tests and a restart fixture containing a generating request and countdown deadline.
- [ ] Run the new test target and confirm failures are caused by missing production types.
- [ ] Implement the smallest deterministic validation and atomic schema-1 store.
- [ ] Run the new target plus authority persistence regression tests.

### Task 3: Build and render V1 avatar assets

**Files:**
- Create: `Assets/GridCity/Avatars/V1/tools/build_runtime_layer_pack.py`
- Create: `Assets/GridCity/Avatars/V1/runtime/avatar-v1.layers`
- Create: `Assets/GridCity/Avatars/V1/runtime/avatar-v1.layers.json`
- Create: `Server/RaspberryPi/src/AvatarRenderer.h`
- Create: `Server/RaspberryPi/src/AvatarRenderer.cpp`
- Create: `tests/host/avatar_renderer_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `AvatarRenderer::render(roomId, playerId, avatarRevision, recipe)` returning atomically published PNG/RGB565 paths and `contentHash64`, plus `renderPreview(recipe)` returning the immutable 220x300 RGB565 cache key/path.

- [ ] Write failing tests using a tiny real layer-pack fixture: layer order, circular alpha, RGB565 byte order/length, PNG dimensions, stable hash, exact 220x300 preview crop/background, invalid/traversal paths, and no publication after an injected write failure.
- [ ] Run the renderer target and observe missing implementation failures.
- [ ] Add the deterministic packer and generate the V1 runtime package from the canonical manifest.
- [ ] Implement package validation, three-layer alpha composition, uncompressed standards-compliant PNG encoding, RGB565 encoding, fsync, and atomic rename.
- [ ] Re-run renderer tests, regenerate twice, and byte-compare both manifests/packages.

### Task 4: Gate Authority lifecycle and identity mutations

**Files:**
- Modify: `Server/RaspberryPi/src/AuthorityService.h`
- Modify: `Server/RaspberryPi/src/AuthorityService.cpp`
- Modify: `Server/RaspberryPi/src/main.cpp`
- Create: `tests/host/identity_authority_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `newGame(boardSize, humanCount, botCount)`, `handleIdentityRequest`, `makeIdentitySnapshot`, `isHumanSeat`, and identity revision/lifecycle accessors.

- [ ] Write failing tests for frozen seats, deterministic Bots, private generating state, exact version gates, name rules, ConfirmName-as-Ready, and gameplay action rejection before Active.
- [ ] Write failing fake-clock tests for one five-second deadline, disconnect behavior, and restart before/after expiry.
- [ ] Run the authority target and confirm lifecycle assertions fail against immediate `engine.start()` behavior.
- [ ] Implement new-room Lobby construction, Bot identities, request cache handling, renderer completion polling, countdown creation, and exactly-once `engine.start()`.
- [ ] Re-run identity, trade, persistence, and core tests.

### Task 5: Integrate authenticated UDP projection and seat allocation

**Files:**
- Modify: `Server/RaspberryPi/src/UdpPlayerServer.h`
- Modify: `Server/RaspberryPi/src/UdpPlayerServer.cpp`
- Modify: `tests/host/udp_server_integration_tests.cpp`

**Interfaces:**
- Consumes: identity request/snapshot codecs and Authority identity APIs.
- Produces: authenticated identity mutation routing, cached replay, public change fan-out, identity-first full resync, and human-only seat claims.

- [ ] Add failing two-client integration cases for human-only pairing, unavailable/offline seats, exact-byte replay/collision, realtime generating/final/name projections, and a shared countdown deadline.
- [ ] Run the UDP integration target and observe missing 0x29/0x2A traffic.
- [ ] Add per-session cached request bytes/results and identity revision delivery tracking.
- [ ] Insert IdentitySnapshot before State/Auth/Roster in full resync and fan it out on revision changes.
- [ ] Re-run UDP integration repeatedly and verify Heartbeat/Action/Detail/Trade regressions remain green.

### Task 6: Add HTTP avatar delivery and web lobby

**Files:**
- Modify: `Server/RaspberryPi/src/HttpServer.h`
- Modify: `Server/RaspberryPi/src/HttpServer.cpp`
- Modify: `Firmware/TestGameServer/src/WebUi.h`
- Modify: `Firmware/TestGameServer/tools/test-web-ui-layout.mjs`
- Modify: `tests/host/http_asset_integration_tests.cpp`
- Modify: `Server/RaspberryPi/deploy/install.sh`
- Modify: `Server/RaspberryPi/deploy/server.env.example`

**Interfaces:**
- Produces: `/api/new?size=&humans=&bots=`, `/api/identity`, immutable `/assets/avatars/<room>/<key>.png|rgb565`, and lazy `/assets/avatar-previews/v1/<recipe>.rgb565`.

- [ ] Write failing HTTP tests for count validation, identity 200/204/409 behavior, both final-avatar media types, exact 32768-byte final and 132000-byte preview lengths, strong ETag/304, single-generation concurrent preview cache fill, immutable caching, malformed preview keys, and traversal rejection.
- [ ] Write failing DOM/layout tests for separate count selectors, invalid-total prevention, disabled gameplay controls, all lobby statuses, final-only image exposure, and server deadline rendering.
- [ ] Run HTTP and Node UI tests and record the expected failures.
- [ ] Implement the HTTP routes, writable avatar directory, and low-frequency identity projection.
- [ ] Implement the lobby presentation and selectors without changing active-game board behavior.
- [ ] Re-run HTTP/UI tests and 16/24/32/40 board layout checks.

### Task 7: Compatibility, documentation, and strict verification

**Files:**
- Modify: `Docs/player-console/player-avatar-setup-spec.md`
- Modify: `Docs/player-console/player-console-ui-spec.md`
- Modify: `Docs/player-console/player-console-acceptance-tests.md`
- Modify: `Docs/README.md`
- Modify: `Server/RaspberryPi/README.md`

**Interfaces:**
- Produces: canonical implementation status, exact client handoff, migration rules, and reproducible deployment prerequisites.

- [ ] Add a failing legacy fixture test proving a restored active room never re-enters identity setup.
- [ ] Implement the compatibility projection and verify the fixture passes.
- [ ] Update canonical docs from future status to implemented protocol/server status without editing PlayerConsole source.
- [ ] Run `git diff --check`, native strict build, full CTest, repeated UDP integration, avatar pack verification, web asset/UI tests, and Arduino shared-library compile checks.
- [ ] On an isolated ARM64 environment only, run the complete strict suite; do not deploy or restart the production Pi service.
- [ ] Prepare the player task handoff with exact enums, offsets, retry/idempotency rules, URLs, changed shared files, and test output.
