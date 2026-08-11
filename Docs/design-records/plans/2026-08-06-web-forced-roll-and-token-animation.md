# Web Forced Roll and Token Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent one-shot, rules-correct next-roll destination control and an interruption-safe per-tile browser token animation.

**Architecture:** `AuthorityService` owns a small administrative override and a separate control revision, while `GameEngine` remains unchanged. `HttpServer` validates the admin route and includes control revision in conditional sync. `WebUi` presents legal targets and keeps visual token positions separate from authority state.

**Tech Stack:** C++17, GridopolyCore, native Raspberry Pi HTTP server, embedded vanilla JavaScript, Node.js asset tests.

## Global Constraints

- Do not change the UDP/ESP-NOW player protocol or regular State/Authority/Roster payloads.
- Do not restart or modify Raspberry Pi production services or COM4 during offline implementation.
- Preserve existing room/state files and backwards compatibility with 12-byte and 16-byte authority metadata.
- Accept only clockwise distances 2 through 12 and retain Monopoly doubles/third-double behavior.
- Do not let browser animation mutate or delay authority state.

---

### Task 1: Authority forced-roll state machine

**Files:**
- Modify: `Server/RaspberryPi/src/AuthorityService.h`
- Modify: `Server/RaspberryPi/src/AuthorityService.cpp`
- Modify: `tests/host/authority_persistence_tests.cpp`

**Interfaces:**
- Produces: `ForcedRollState`, `setForcedRollTarget(playerId, target, expectedVersion)`, `clearForcedRollTarget()`, `forcedRollState()`, and `controlVersion()`.
- Consumes: `GameState`, `GamePhase`, `PlayerState`, and `GameEngine::roll(playerId, dieA, dieB)`.

- [ ] Write failing authority tests with literal target positions and expected dice sums for valid human and bot rolls.
- [ ] Run the authority test and confirm compilation/assertion failure before production changes.
- [ ] Implement validation, deterministic dice selection, matching human/bot execution, and one-shot clearing.
- [ ] Add restart, legacy metadata, third-double rejection, and new-game clearing tests.
- [ ] Run the authority tests until all cases pass.

### Task 2: HTTP control and conditional sync

**Files:**
- Modify: `Server/RaspberryPi/src/HttpServer.cpp`
- Modify: `tests/host/http_asset_integration_tests.cpp`

**Interfaces:**
- Consumes: Task 1 authority methods.
- Produces: `POST /api/forced-roll`, forced-roll JSON in compact sync, and control-aware HTTP 204 behavior.

- [ ] Add failing real-socket integration requests for valid set, invalid set, cancel, and control-version invalidation.
- [ ] Run the HTTP integration test and confirm the new route fails.
- [ ] Implement the route and the extra conditional-sync comparison.
- [ ] Run the integration test and confirm all route/status/body assertions pass.

### Task 3: Player context menu, board target picker, and token movement model

**Files:**
- Modify: `Firmware/TestGameServer/src/WebUi.h`
- Modify: `Firmware/TestGameServer/tools/test-web-ui-layout.mjs`
- Regenerate: `Firmware/TestGameServer/src/WebUiGzip.h`

**Interfaces:**
- Consumes: `/api/sync.controlVersion`, `/api/sync.forcedRoll`, and `POST /api/forced-roll`.
- Produces: a right-click player action, legal clickable board destinations, and a visual-position scheduler advancing one clockwise tile every 180 ms.

- [ ] Add failing Node behavior tests for 16/24/32/40 wraparound targets and ConfirmPosition-only animation.
- [ ] Run the web test and confirm it fails because the helpers/context-menu/board-selection controls are absent.
- [ ] Implement the player context menu, legal board target projection, set/cancel requests, visual positions, cancellation rules, and chip-only redraw.
- [ ] Regenerate `WebUiGzip.h` and rerun the web test.

### Task 4: Full offline verification and documentation

**Files:**
- Modify: `Docs/firmware/raspberry-pi-server.md`

**Interfaces:**
- Consumes: Tasks 1-3.
- Produces: deployment-ready local artifacts and operator documentation.

- [ ] Document endpoint, constraints, persistence, bot behavior, and visual-only animation semantics.
- [ ] Run Node layout/asset checks.
- [ ] Run the complete native Raspberry Pi build-and-test suite, including strict warning flags.
- [ ] Inspect `git diff --check` for touched files and report exact results.
- [ ] Request a separate production deployment window; do not deploy as part of offline verification.
