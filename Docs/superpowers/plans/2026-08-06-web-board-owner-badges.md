# Web Board Owner Badges Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a compact, color-coded `P<ownerId>` badge on every purchased web-board asset while preserving tile artwork, property-group colors, player chips, and the 40-tile layout.

**Architecture:** Keep the existing compact `/api/sync` and `/api/board` projections unchanged. Add pure browser helpers that resolve an owner ID to a stable player color and full roster name, then consume that model in the existing tile renderer. Regenerate the embedded gzip asset, exercise the production HTML through the existing board-matrix server, and deploy the tested ARM64 binary with the existing backup/restart procedure.

**Tech Stack:** C++17 raw embedded HTML, vanilla JavaScript/CSS, Node.js assertions, gzip/ETag generation, ARM64 native build, systemd, HTTP.

## Global Constraints

- Do not change Core, game rules, persistence schemas, HTTP JSON schemas, UDP, or PlayerConsole firmware.
- Render a badge only when a purchasable tile has a nonzero owner ID.
- Display only `P<ownerId>` inside the tile; expose the full roster name through hover and the accessible label.
- Do not add full-tile ownership tint, a bottom ownership strip, or a new legend.
- Preserve mortgage/building information and remove the redundant textual owner fragment from the metadata line.
- Use stable colors for P1 through P6 and reuse those colors for player cards and board chips.
- Preserve all unrelated dirty-worktree changes and do not operate COM4.

---

### Task 1: Test and implement the ownership presentation model

**Files:**
- Modify: `Firmware/TestGameServer/tools/test-web-ui-layout.mjs`
- Modify: `Firmware/TestGameServer/src/WebUi.h:116-144`

**Interfaces:**
- Consumes: projected tiles shaped as `{asset, owner, mortgaged}` and roster entries shaped as `{id, name}`.
- Produces: `playerColor(playerId): string` and `ownerBadgeModel(tile, players): null | {id, label, name, color}`.

- [ ] **Step 1: Add the failing helper tests**

Extend the existing extracted helper return value with `playerColor` and `ownerBadgeModel`, then add literal fixtures proving:

```js
assert.equal(helpers.ownerBadgeModel({asset: 0, owner: 0}, players), null);
assert.equal(helpers.ownerBadgeModel({asset: 255, owner: 2}, players), null);
assert.deepEqual(helpers.ownerBadgeModel({asset: 3, owner: 2, mortgaged: true}, players), {
  id: 2, label: 'P2', name: 'Bot 1', color: '#ff8a72',
});
assert.deepEqual(helpers.ownerBadgeModel({asset: 4, owner: 6}, players), {
  id: 6, label: 'P6', name: 'P6', color: '#ff7f9f',
});
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
node Firmware/TestGameServer/tools/test-web-ui-layout.mjs
```

Expected: FAIL because `playerColor`/`ownerBadgeModel` do not exist in the production helper block.

- [ ] **Step 3: Add the minimal pure helpers**

Inside the existing testable helper block, implement a six-color cyclic palette and return `null` for `asset === 255` or `owner <= 0`. Resolve the full name from `players`; fall back to the visible label when no matching roster entry exists.

- [ ] **Step 4: Run the test and verify GREEN**

Run the same Node command and require `GRIDOPOLY_WEB_UI_LAYOUT_TESTS_PASS`.

---

### Task 2: Render and visually verify owner badges

**Files:**
- Modify: `Firmware/TestGameServer/tools/test-web-ui-layout.mjs`
- Modify: `Firmware/TestGameServer/src/WebUi.h:12-36,204-220,307-336`
- Modify: `Server/RaspberryPi/tools/serve-web-board-matrix.mjs`
- Regenerate: `Firmware/TestGameServer/src/WebUiGzip.h`

**Interfaces:**
- Consumes: `ownerBadgeModel()` from Task 1.
- Produces: `.owner-badge`, `.tile.owned`, and `--player-color` rendering states in the production HTML.

- [ ] **Step 1: Prepare a failing browser fixture**

Update each 16/24/32/40 matrix fixture to include at least four purchasable tiles with these literal asset states:

```js
assets: [[0, 0, 0], [1, 0, 0], [2, 2, 0], [3, 0, 1]]
```

Include roster names `Player Console`, `Bot 1`, and `Bot 2`. Start the matrix server and inspect `/16` before production changes; require zero `.owner-badge` elements, establishing the missing behavior.

- [ ] **Step 2: Implement the minimal renderer and CSS**

In `WebUi.h`:

- Add a compact upper-right `.owner-badge` using `background:var(--player-color)`.
- Reserve right padding in `.tile.owned .n` and reduce badge dimensions for `[data-size="40"]`.
- Resolve the owner model for every tile, add `.owned`, set `--player-color`, and render escaped badge/name attributes only when the model is non-null.
- Add the full player name to the tile `title` and `aria-label`.
- Remove `P${tile.owner||'-'}` from the metadata text.
- Set the same `--player-color` on player cards and token chips.

- [ ] **Step 3: Regenerate the compressed web asset**

Run:

```powershell
node Firmware/TestGameServer/tools/generate-web-assets.mjs
```

Require a new content-derived `kWebUiEtag` and no stale-gzip failure.

- [ ] **Step 4: Run static and production-artifact tests**

Run:

```powershell
node Firmware/TestGameServer/tools/test-web-ui-layout.mjs
python Server/RaspberryPi/tools/test-stage-web-tile-assets.py
```

Require the layout test and the exact 36 PNG/36 RGB565 staging checks to pass.

- [ ] **Step 5: Run browser matrix verification**

Serve the production HTML with:

```powershell
node Server/RaspberryPi/tools/serve-web-board-matrix.mjs 18765
```

For `/16`, `/24`, `/32`, and `/40`, verify:

- exactly three owner badges (`P1`, `P2`, `P3`), while the unowned asset has none;
- each owned tile title contains the correct full roster name;
- the mortgaged P3 asset retains its badge;
- badges do not overlap the number row, artwork, property strip, or player chips;
- horizontal and vertical overflow are zero and browser console errors are zero.

Mutate the fixture state from owner P1 to P2 and call the real renderer again; require the same tile badge and tooltip to change without a page reload.

---

### Task 3: ARM64 validation, controlled deployment, and live regression

**Files:**
- Execute: `Server/RaspberryPi/tools/build-and-test-native.sh`
- Deploy: `/usr/local/bin/gridopoly_server`
- Back up: current binary, `/var/lib/gridopoly`, and active web assets.

**Interfaces:**
- Consumes: regenerated `WebUiGzip.h` and unchanged service configuration.
- Produces: production Gridopoly web UI with owner badges and unchanged gameplay/network state.

- [ ] **Step 1: Run complete local preflight**

Run `git diff --check` on the three modified source/test files, rerun the layout test, and confirm only the planned web UI/test/generated files changed in this feature.

- [ ] **Step 2: Build and test the candidate on ARM64 in isolation**

Upload a source archive excluding secrets and build outputs, extract it under a new `/tmp/gridopoly-owner-badges-src-*` directory, and run:

```bash
GRIDOPOLY_NATIVE_BUILD_DIR=/tmp/gridopoly-owner-badges-build bash Server/RaspberryPi/tools/build-and-test-native.sh
```

Require every native host/integration test and final server link to pass; record the candidate SHA-256.

- [ ] **Step 3: Back up and deploy without restarting the Pi or AP**

Record `/health`, `/api/sync`, the active binary hash, room, version, and peers. Create a timestamped `/var/backups/gridopoly/web-owner-badges-*` backup, atomically replace only the verified server binary, and restart only `gridopoly.service`.

- [ ] **Step 4: Verify live UI and protocol stability**

Require the production page to return the new ETag. If the restored game already contains a purchased asset, verify its live tile badge and tooltip; otherwise rely on the production-HTML browser matrix rather than mutating the saved game. Sample `/health` twice for at least 25 seconds and require:

- unchanged room and restored game state;
- `peers=1` and the original Wi-Fi UDP player connected in seat 1;
- equal positive Heartbeat and Ack deltas;
- fixed resync count in an idle-version window;
- zero authentication failures, replay drops, and transmit errors;
- no HTTP timeouts or send errors introduced by the deployment.

- [ ] **Step 5: Record completion evidence**

Report the production binary SHA-256, backup path, room/version/peers, Heartbeat/Ack deltas, resync/errors, ETag, four board-size browser results, and explicitly confirm that COM4 was not accessed.
