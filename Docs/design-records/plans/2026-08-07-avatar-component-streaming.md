# Avatar Component Streaming Implementation Plan

> Scope: local repository implementation and verification only. Do not deploy Raspberry Pi services and do not read, reset, or flash COM4.

**Goal:** Serve 30 neutral compressed avatar structure layers, recolor and compose them identically on ESP32-S3 and Raspberry Pi, while keeping the legacy whole-preview route compatible.

**Architecture:** A deterministic generator converts the registered 320x320 neutral source layers into cropped 220x300 GAVC v1 files and an exact hash manifest. A shared C++ codec owns validation, RLE decoding, palettes, fixed-point tint, and source-over. `AvatarRenderer` consumes only those components for both legacy preview and final output. `HttpServer` serves immutable component files without invoking the compositor.

**Constraints:** C++17, Python/Pillow generator, one component HTTP response per preset change, 384 KiB client avatar cache, no UDP/Core wire changes.

## Task 1: Lock the binary and pixel contract with failing tests

Files:

- Create `tests/host/avatar_component_codec_tests.cpp`
- Modify `CMakeLists.txt`
- Modify `Server/RaspberryPi/tools/build-and-test-native.sh`

Steps:

1. Add tests for the 32-byte header, malformed/truncated RLE, CRC, bounds, kind/preset checks, palette tables, tint golden vectors, source-over vectors, and exact decode size.
2. Add the new test target before adding the implementation.
3. Run the target and confirm it fails because `AvatarComponentCodec` does not exist.

## Task 2: Generate deterministic neutral GAVC assets

Files:

- Create `Assets/GridCity/Avatars/V1/tools/build_avatar_components.py`
- Create `Assets/GridCity/Avatars/V1/runtime/components-v1/{hair,face,outfit}/*.gavc`
- Create `Assets/GridCity/Avatars/V1/runtime/avatar-components-v1.json`
- Create `Server/RaspberryPi/tools/verify-avatar-components.py`

Steps:

1. Load the existing manifest-selected neutral registered layers.
2. Crop to preview canvas, trim transparent bounds, encode GAVC RLE, and write atomically.
3. Record per-file SHA-256, CRC-32, rectangle, decoded and encoded sizes, totals, and worst three-layer cache combination.
4. Add a verifier that rebuilds expectations and rejects missing, extra, modified, or malformed assets.
5. Generate twice and assert byte-identical outputs.

## Task 3: Implement the shared server codec and compositor primitives

Files:

- Create `Server/RaspberryPi/src/AvatarComponentCodec.h`
- Create `Server/RaspberryPi/src/AvatarComponentCodec.cpp`
- Modify `CMakeLists.txt`
- Modify `Server/RaspberryPi/tools/build-and-test-native.sh`

Steps:

1. Parse and validate GAVC headers and paths.
2. Decode bounded RLE and verify CRC.
3. Implement frozen palette lookup and integer Hair/Skin tint.
4. Implement canvas placement and straight-alpha source-over.
5. Run the new target and keep implementation minimal until all byte-vector tests pass.

## Task 4: Make preview and final rendering use the same components

Files:

- Modify `Server/RaspberryPi/src/AvatarRenderer.h`
- Modify `Server/RaspberryPi/src/AvatarRenderer.cpp`
- Modify `Server/RaspberryPi/src/AuthorityService.h`
- Modify `Server/RaspberryPi/src/AuthorityService.cpp`
- Modify `Server/RaspberryPi/src/main.cpp`
- Modify `tests/host/avatar_renderer_tests.cpp`
- Modify identity/authority tests that construct renderer options

Steps:

1. Point `AvatarRenderer` at the component root.
2. Compose face, outfit, and hair in the frozen order, applying only Skin/Hair tint.
3. Render the 220x300 compatibility preview and 128x128 final from that same composite.
4. Add exact whole-output hashes for representative recipes and cache/idempotency tests.
5. Test injected publish failure and stale files as before.

## Task 5: Add immutable component HTTP routes

Files:

- Modify `Server/RaspberryPi/src/HttpServer.cpp`
- Modify `tests/host/http_asset_integration_tests.cpp`

Steps:

1. Add 30 successful route cases and validate MIME, Content-Length, immutable cache, ETag, and 304.
2. Add path traversal, leading-zero, wrong-kind, out-of-range, wrong suffix, truncated, and header/path mismatch cases.
3. Run the HTTP test first and confirm the missing route fails.
4. Implement strict canonical parsing and serve only validated GAVC files.

## Task 6: Stage component assets for future atomic deployment

Files:

- Modify `Server/RaspberryPi/deploy/install.sh`
- Modify `Server/RaspberryPi/deploy/server.env.example`
- Modify `Server/RaspberryPi/tools/build-and-test-native.sh`
- Modify `Server/RaspberryPi/README.md`
- Modify `Docs/firmware/README.md`

Steps:

1. Verify exactly 30 GAVC files before installation.
2. Stage into a sibling directory, set ownership/mode, atomically switch, and remove the old directory only after success.
3. Add `GRIDOPOLY_AVATAR_COMPONENT_ROOT` with the canonical installed path.
4. Keep the old GAVL pack installed for rollback and the legacy URL for client compatibility.

## Task 7: Complete verification and player-team handoff

Steps:

1. Run generator and verifier twice.
2. Run focused codec, renderer, authority, HTTP, UDP, persistence, trade, protocol, and core tests.
3. Build the native server with warnings as errors.
4. Run repository whitespace checks for touched text files.
5. Report exact route/format, palette and golden-vector location, manifest SHA-256, maximum compressed working set, output hashes, test commands, and the explicit fact that Pi/COM4 were not touched.
