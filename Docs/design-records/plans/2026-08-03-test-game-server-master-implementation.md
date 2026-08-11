# Gridopoly ESP32-S3 Test Game Server Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, verify, and flash a complete modular Gridopoly test game server to the ESP32-S3 on COM5, with a reusable pure C++ rules core, five maps, bots, persistence, HTTP simulation, and encrypted multi-console ESP-NOW.

**Architecture:** `GridopolyCore` and `GridopolyProtocol` are Arduino-free C++17 libraries tested on Windows and compiled unchanged for ESP32. `TestGameServer` supplies Arduino adapters for Wi-Fi STA, ESP-NOW, HTTP, NVS/LittleFS, clocks, entropy, and COM5 diagnostics; all authoritative mutations pass through one serial command bus. Contract JSON is the source for generated C++ IDs, codecs, TypeScript/web reducers, documentation tables, and golden vectors.

**Tech Stack:** Python 3 contract/data generators, JSON contract files, C++17, Visual Studio Build Tools CMake/Ninja host tests, Arduino CLI with ESP32 Arduino Core 3.3.11, ESP-NOW, WiFi/WebServer, NVS, LittleFS, PowerShell build/upload/self-test scripts.

## Global Constraints

- The approved semantic specification is `Docs/design-records/2026-08-02-esp32-test-game-server-design.md`.
- P0-Contract is the first strictly serial deliverable. No game, radio, web, recovery, or COM5 production firmware is implemented before its closure report passes.
- COM5 is explicit. Never select the first serial port. The probed device is CH343 `VID_1A86&PID_55D3`, ESP32-S3 rev 0.2, 16 MiB Quad Flash, and 8 MiB PSRAM.
- Do not reuse the player-console FQBN without a target-specific profile. Test server scripts own their FQBN and custom partition CSV.
- The pure core and protocol libraries include no Arduino, Wi-Fi, ESP-NOW, filesystem, wall-clock, or heap-dependent APIs.
- Wi-Fi credentials and every real key/token live only under ignored local paths. They never appear in tracked source, command lines, logs, HTTP responses, fixtures, or generated firmware artifacts intended for publication.
- Preserve all pre-existing dirty worktree changes. Do not restore, delete, stage, or rewrite unrelated PCB, player-console, documentation, or mechanical files.
- Every behavior change follows RED → verified failure → minimal GREEN → full regression. Generated files are checked by regeneration and byte comparison.
- Every phase ends at its stated gate; a later phase may not compensate for a failed earlier gate.
- Runtime correctness must not require PSRAM. PSRAM may cache web/static data only after the no-PSRAM fixed-pool profile passes.
- ESP-NOW supports six encrypted logical peers; one real player console plus five simulated peers is the final acceptance topology.

## Locked File Structure

```text
GameData/
  schemas/
    contract-meta-v1.json
    protocol-semantics-v1.json
    domain-events-v1.json
    game-state-v1.json
    admin-projection-v1.json
    http-events-v1.json
    system-triggers-v1.json
    bot-actions-v1.json
  maps/
  cards/
  bots/
  manifests/
  tests/
    contracts/
    goldens/
    scenarios/
Tools/
  contracts/
  game-data/
Firmware/TestGameServer/
  TestGameServer.ino
  config/
    app_config.h
    secrets.example.h
    secrets.local.h            # ignored
  partitions/
  libraries/
    GridopolyCore/src/gridopoly/core/
    GridopolyProtocol/src/gridopoly/protocol/
    GridopolyGenerated/src/gridopoly/generated/
  src/
    app/
    adapters/
    web/
  web/
  tools/
  build/                       # ignored
Host/
  CMakeLists.txt
  tests/
build-host/                    # ignored
Docs/firmware/
Docs/player-console/
Docs/game/balance-reports/
```

Do not use recursive globs over all `Firmware/`; the existing player-console tree contains Arduino/LVGL sources and duplicate generated asset names. Host targets list only the three test-server libraries explicitly.

## Phase Plans and Gates

### Task 1: P0-Contract byte-contract closure

**Detailed plan:** `Docs/design-records/plans/2026-08-03-test-game-server-p0-contract.md`

**Produces:** Eight schema files, canonicalizer/validators, generated C++/JSON documentation indexes, exact-byte goldens, content identity vectors, two A-level ESP-NOW entry documents, and `Docs/design-records/schema-closure-report.md`.

**Gate:** Contract unit tests, regeneration check, schema closure audit, placeholder scan, and secret scan all pass; `Firmware/TestGameServer` still has no executable firmware.

### Task 2: P0A repository and local-secret boundary

**Files:**
- Modify: `.gitignore`
- Create: `Firmware/TestGameServer/config/secrets.example.h`
- Local only: `Firmware/TestGameServer/config/secrets.local.h`
- Create: `Firmware/TestGameServer/tools/verify-secrets.ps1`
- Create: `Firmware/TestGameServer/tools/test-verify-secrets.ps1`

**Interfaces:**
- Produces: `gridopoly::config::WifiCredentials loadLocalWifiCredentials()` at compile time through the ignored header.
- Guarantees: tracked scans contain no real SSID/password, PMK, LMK, seat token key, or admin secret.

- [ ] Write a PowerShell test that creates a temporary fake repository and proves tracked/local/generated secret paths are classified correctly.
- [ ] Run it and verify failure because `verify-secrets.ps1` and ignore rules are absent.
- [ ] Add exact ignore rules for `/.local-secrets/`, `/build-host/`, test-server build/runtime/local config, and player-console local config/runtime.
- [ ] Implement the verifier using `git check-ignore`, `git ls-files`, and exact local-path allowlists; it must never print secret values.
- [ ] Re-run the isolated test and the real repository scan.
- [ ] Commit only P0A files when Git write permission is available.

**Gate:** Every real local secret path is ignored, no ignored example file is required by a clean clone, and the repository scan reports zero secret-value matches.

### Task 3: P0B COM5 build and hardware baseline

**Files:**
- Create: `Firmware/TestGameServer/TestGameServer.ino`
- Create: `Firmware/TestGameServer/config/app_config.h`
- Create: `Firmware/TestGameServer/partitions/server_16m.csv`
- Create: `Firmware/TestGameServer/tools/compile.ps1`
- Create: `Firmware/TestGameServer/tools/upload.ps1`
- Create: `Firmware/TestGameServer/tools/probe-target.ps1`
- Create: `Firmware/TestGameServer/tools/run-selftest.ps1`
- Test: `Firmware/TestGameServer/tools/test-probe-target.ps1`

**Interfaces:**
- Produces serial line: `GRIDOPOLY_BOOT schema=1 build=<hex> chip=ESP32-S3 flash=<bytes> psram=<bytes> littlefs=<bytes> selftest=<PASS|FAIL>`.
- Consumes generated contract hash from P0 and local credentials only after P0A.

- [ ] Write probe-parser tests for the exact COM5 VID/PID, wrong-port rejection, flash-size mismatch, and no-PSRAM fallback.
- [ ] Verify tests fail because the probe script is absent.
- [ ] Implement target probing with the Arduino15 esptool 5.3.1 path and explicit `--port COM5`; no erase/write command is accepted by the probe wrapper.
- [ ] Create a hardware `server_16m_ota.csv` layout with NVS, OTA data, two application slots of at least 3 MiB, and LittleFS of at least 1 MiB; also create a compatibility `server_4m_no_ota.csv` with one bounded app and at least 1 MiB LittleFS. Validate offsets and exact total sizes in script tests.
- [ ] Build the minimal sketch with Arduino ESP32 Core 3.3.11 and no game/radio/web behavior, using the probed 16 MiB/8 MiB profile for COM5 and the 4 MiB/no-PSRAM profile as a compile-time compatibility gate.
- [ ] Upload to COM5, capture the boot line, and verify chip, Flash, PSRAM, LittleFS mount/readback, and secret redaction.
- [ ] Also compile the fixed-pool profile with PSRAM disabled to prove correctness does not depend on PSRAM.

**Gate:** Explicit COM5 probe, compile, upload, reset, serial self-test, and LittleFS non-formatting mount test pass.

### Task 4: P1 host build, generated content identity, and library shells

**Files:**
- Create: `Host/CMakeLists.txt`
- Create: `Host/tests/test_main.cpp`
- Create: `Firmware/TestGameServer/libraries/GridopolyCore/src/gridopoly/core/types.h`
- Create: `Firmware/TestGameServer/libraries/GridopolyProtocol/src/gridopoly/protocol/types.h`
- Create: `Tools/game-data/generate.py`
- Create: `Tools/game-data/validate.py`

**Interfaces:**
- Produces CMake targets: `gridopoly_core`, `gridopoly_protocol`, `gridopoly_tests`.
- Produces `ContentManifest` and stable SHA-256 roots from P0 canonical schemas.

- [ ] Test a clean configure/build and deterministic second generation.
- [ ] Verify failure with missing targets/generated outputs.
- [ ] Add explicit source lists and small platform-free interfaces.
- [ ] Generate twice and compare every byte and hash.
- [ ] Run CTest with no Arduino headers in the include graph.

**Gate:** Host build and generator check pass from a clean directory.

### Task 5: P2 five maps, economy, cards, and content validator

**Produces:** 16/24/28-v2/32/40 map packages, legacy 28-v1 recovery package, two 16-card decks, six player identities, bot policies, manifest entries.

- [ ] Write literal table-driven tests for tile counts/order, group sizes, asset ID continuity, prices/rents/mortgages, total purchasable values, card references, and release status.
- [ ] Verify failing tests against absent data.
- [ ] Add canonical JSON data and `MapValidator` until all exact totals pass: 1900, 3040, 4550, 4550, 5690.
- [ ] Generate C++ fixed arrays and prove 16/40 identity goldens match P0.

**Gate:** All map/content validators and canonical hash vectors pass.

### Task 6: P3–P6 pure C++ game engine

Create separate detailed plans before each subphase:

- P3 turn, deterministic dice, doubles, movement, start reward, hold area, position confirmation, second landing.
- P4 purchase, rent, payment, debt fundraising, deadlines, idempotent command admission.
- P5 auction, building/selling, mortgage/redeem, trade, building demand and rollback.
- P6 hold exit, multi-recipient card payments, bankruptcy, asset transfer/auction, rank and game over.

**Common files:** `GridopolyCore` command/event/state/engine/workflow modules and `Host/tests/core_*_tests.cpp`.

**Interfaces:**
- Consumes: `CommandEnvelope`, `AuthorityState`, injected `IRandomSource`, `IClock`, and `ICommitSink`.
- Produces: `Decision { accepted, terminal, candidate_state, domain_events, error }` without mutating live state before commit.

**Gate:** Literal event traces, invariants, duplicate-command tests, overflow tests, and 2–6 player deterministic completion scenarios pass with fixed capacity.

### Task 7: P7 semantic protocol and codecs

**Files:** `GridopolyProtocol` header/body/TLV/fragment/snapshot/patch/error codecs plus host golden tests.

- [ ] Write decode rejection tests for every truncation boundary, invalid flag, unknown critical field, direction error, and oversized message.
- [ ] Implement only enough codec for each failing vector.
- [ ] Add exact ROLL, PING/PONG, session, snapshot, patch, error, request-terminal and SESSION_REVOKED drain vectors.
- [ ] Cross-check generated C++ and Python decoders byte-for-byte.

**Gate:** Every positive and negative golden has one first error and both implementations agree.

### Task 8: P8 reliable ESP-NOW-neutral transport

**Files:** fixed TX/RX pools, ACK window, fragmentation/reassembly, terminal cache, pending slots, six-peer scheduler, crypto-provider interface, and fault-injection tests.

**Gate:** Six logical peers pass loss/duplicate/reorder/disconnect/channel-change simulations within the 180 KiB budget; `ReliableFrameIdentity` is exactly 40 bytes and its table exactly 15,360 bytes.

### Task 9: P9 web API, bots, scenario runner, and board simulator

**Files:** platform-neutral admin projection/reducer, bounded JSON writer, HTTP operation/idempotency gate, PCG bot planner, BoardPositionAdapter, static web assets, host HTTP-contract tests.

**Gate:** All routes, error priorities, pagination epochs, replay export status, fixed bot decisions, and simulator traces match P0 schemas; generated web bundle fits the partition budget.

### Task 10: P10 NVS/LittleFS persistence and recovery

**Files:** streaming AuthorityState encoder, WAL/journal, double-bank snapshots, metadata, replay export, reset/recovery state machines, filesystem fault harness.

**Gate:** Power-loss injection at every write/flush/meta boundary recovers only the exact state before or after a commit; corruption never auto-formats storage.

### Task 11: P11 Arduino Wi-Fi, HTTP, ESP-NOW, and COM5 integration

**Files:** Arduino adapters for STA, channel tracking, encrypted peers, entropy, clocks, NVS, LittleFS, HTTP, serial diagnostics, and `ServerApp` composition root.

**Gate:** COM5 connects to the configured STA network without exposing credentials, serves health/state/admin pages, runs all five maps with bots, and passes six-peer ESP-NOW fault tests.

### Task 12: P12 player-console integration

**Files:** `PlayerConsoleTransport`, `EspNowTransport`, codec/reducer, provisioning UI/status, and `Docs/player-console/espnow-integration.md`.

**Gate:** One real console pairs, claims a seat, receives a snapshot, rolls, moves, buys, pays rent, recovers from a short outage, and performs full resync.

### Task 13: P13/P14 acceptance, endurance, and balance release

**Files:** deterministic scenario corpus, endurance harness, heap/queue telemetry, simulation reports, docs entry points.

**Gate:** One real console plus five logical peers passes the full acceptance matrix; 120-minute median heap loss is at most 2 KiB and largest-block loss at most 4 KiB; candidate map revisions are promoted only when every required balance cell passes.

## Execution Policy

Use a fresh implementation/review cycle per phase. Within a phase, parallel work is allowed only for files with independent ownership; generated contracts, shared IDs, and the command/event core remain single-writer. After each gate, update this plan and write the next detailed phase plan before touching later production code.
