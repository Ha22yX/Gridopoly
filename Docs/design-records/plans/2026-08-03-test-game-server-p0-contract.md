# Gridopoly P0-Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze every shared protocol, event, state, admin, HTTP, trigger, bot, and content identity contract into canonical machine-readable schemas and independently reproducible exact-byte vectors before any server firmware is implemented.

**Architecture:** Human-authored JSON contract files use one small meta-schema with explicit numeric IDs, stable names, wire types, flags, cardinality, ranges, enums, ordering, visibility, and defaults. A deterministic Python standard-library generator validates the contracts, emits canonical JSON/hashes and C++ ID/layout headers, and builds binary goldens with an independent test decoder; no Arduino or game implementation is linked in P0.

**Tech Stack:** Python 3 standard library (`json`, `hashlib`, `struct`, `unittest`, `zlib` with explicit CRC wrappers), UTF-8 canonical JSON, generated C++17 headers, PowerShell entry scripts.

## Global Constraints

- The sole semantic source is `Docs/design-records/2026-08-02-esp32-test-game-server-design.md`; schemas make its missing byte-level choices explicit but may not change its semantics.
- IDs are explicit integers, never array indexes or language enum order.
- All multi-byte wire values are little-endian; bool is u8 0/1; strings are UTF-8 with explicit byte limits.
- CRC-32 is ISO-HDLC where the design says CRC32; SHA-256 covers exact canonical bytes described by each vector.
- Canonical JSON is UTF-8 without BOM/newline dependence, sorted object keys, compact separators, integer-only numeric schema values, and arrays kept in declared semantic order.
- Generated output is never hand-edited. `generate.py --check` must reproduce tracked bytes exactly.
- P0 creates no `.ino`, Wi-Fi, ESP-NOW, HTTP server, NVS/LittleFS writer, game engine, or COM5 image.

---

### Task 1: Contract meta-schema and deterministic canonicalizer

**Files:**
- Create: `GameData/schemas/contract-meta-v1.json`
- Create: `Tools/contracts/contract_model.py`
- Create: `Tools/contracts/canonical_json.py`
- Create: `Tools/contracts/generate.py`
- Test: `Tools/contracts/tests/test_contract_meta.py`
- Test: `Tools/contracts/tests/test_canonical_json.py`

**Interfaces:**
- Produces: `load_contract(path: Path) -> ContractDocument`
- Produces: `canonical_bytes(value: object) -> bytes`
- Produces CLI: `python Tools/contracts/generate.py --root . [--check]`
- Defines wire types: `U8`, `I8`, `U16`, `I16`, `U32`, `I32`, `U64`, `I64`, `BOOL`, `BYTES`, `UTF8`, `RECORD`, `RECORD_LIST`, `U8_LIST`, `U16_LIST`, `U32_LIST`.

- [ ] **Step 1: Write the failing meta-schema tests**

```python
def test_rejects_duplicate_field_ids(self):
    doc = fixture(fields=[field(1, "room_id", "U64"), field(1, "seat_id", "U8")])
    with self.assertRaisesRegex(ContractError, "duplicate field id 1"):
        validate_contract(doc)

def test_rejects_implicit_enum_values(self):
    doc = fixture(fields=[field(1, "phase", "U8", enum=["LOBBY", "IN_GAME"])])
    with self.assertRaisesRegex(ContractError, "explicit enum value"):
        validate_contract(doc)
```

- [ ] **Step 2: Run tests and verify RED**

Run:

```powershell
python -m unittest discover -s Tools/contracts/tests -p 'test_contract_meta.py' -v
```

Expected: import failure for absent `Tools.contracts.contract_model`, not a syntax or fixture error.

- [ ] **Step 3: Implement the minimal immutable model and validator**

Require exact top-level keys `contract_schema`, `contract_name`, `contract_version`, `namespace`, `definitions`; reject floats, duplicate IDs/names, missing range/max/cardinality, invalid defaults, unordered definitions, and unknown keys.

- [ ] **Step 4: Write canonical JSON behavior tests**

```python
def test_canonical_bytes_are_key_sorted_utf8_and_compact(self):
    value = {"中文": "格", "a": [2, 1], "z": {"b": 1, "a": 0}}
    self.assertEqual(
        canonical_bytes(value),
        b'{"a":[2,1],"z":{"a":0,"b":1},"\xe4\xb8\xad\xe6\x96\x87":"\xe6\xa0\xbc"}',
    )
```

- [ ] **Step 5: Run the canonicalizer test and verify RED, then GREEN**

Run the one test before implementation, implement with `json.dumps(..., ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)`, and run the full two-file suite.

- [ ] **Step 6: Add generator check mode**

`--check` writes into a temporary directory, compares relative file sets and bytes, prints only differing paths, and returns 1 on drift. It must never rewrite tracked files in check mode.

- [ ] **Step 7: Commit Task 1 files when permitted**

```powershell
git add -- GameData/schemas/contract-meta-v1.json Tools/contracts
git commit -m "feat: add deterministic contract model"
```

### Task 2: Domain-event and nested-record contract

**Files:**
- Create: `GameData/schemas/domain-events-v1.json`
- Create: `Tools/contracts/domain_events.py`
- Create: `GameData/tests/contracts/domain-events-required.json`
- Test: `Tools/contracts/tests/test_domain_events.py`
- Generate: `Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/domain_event_ids.h`
- Generate: `Docs/firmware/generated/domain-events-v1.md`

**Interfaces:**
- Produces event definitions keyed by explicit `event_type u16`.
- Every event contains `stable_name`, `critical`, `terminal_role`, `visibility`, and ordered field definitions.
- RECORD_LIST definitions name an explicit reusable item layout with its own field IDs and maxima.

- [ ] **Step 1: Write the failing required-registry test**

The literal required fixture lists every event from ROOM_CREATED through BUILDING_DEMAND_STATE, including lifecycle, seat, connection/controller, movement, purchase, auction, rent/payment/debt, trade, mortgage/building, cards, bankruptcy, recovery, and game-over events. The test asserts exact set equality, not a minimum count.

- [ ] **Step 2: Verify RED because `domain-events-v1.json` is absent**

```powershell
python -m unittest Tools.contracts.tests.test_domain_events -v
```

- [ ] **Step 3: Encode lifecycle and seat events first**

Use explicit IDs from the approved registry, then run the test and observe the expected missing-event set shrink. Add no placeholder fields.

- [ ] **Step 4: Encode turn, movement, economy, workflow, recovery, and terminal events**

For each field freeze wire type, required/critical/private flags, min/max, enum values, sort key, and count relationship. Encode complete item layouts for event batches, payment allocations, trade assets/cards, ranking lists, building demand entries, and bankruptcy transfers.

- [ ] **Step 5: Generate the C++ enum and Markdown table**

The header uses `enum class DomainEventType : std::uint16_t` with explicit hexadecimal values and `constexpr` layout descriptors. The documentation table is generated from the same schema.

- [ ] **Step 6: Run mutation-focused negative tests**

Tests independently alter duplicate IDs, an event name, field order, a PRIVATE bit, a RECORD_LIST max, and a terminal role; each mutation must fail with one precise first error.

- [ ] **Step 7: Commit Task 2 files when permitted**

### Task 3: Protocol semantic registry and snapshot-view boundary

**Files:**
- Create: `GameData/schemas/protocol-semantics-v1.json`
- Create: `GameData/tests/contracts/protocol-semantics-required.json`
- Test: `Tools/contracts/tests/test_protocol_semantics.py`
- Generate: `Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/protocol_semantics.h`
- Generate: `Docs/firmware/generated/protocol-semantics-v1.md`

**Interfaces:**
- Freezes all protocol message IDs, legal direction, authentication/encryption requirements, flags, header version/state rules, body field order/types/ranges, request domain, terminal carrier, and retry/drain behavior.
- Freezes two distinct structures: the 12-section internal `AuthorityStateBlob` and the 11-section per-console `STATE_SNAPSHOT` view. They may share field definitions but never share one section-count or root type.

- [ ] **Step 1: Write failing exact-registry tests**

The required fixture lists every registered message from LINK/PAIR/session through player commands, terminal outcomes, domain events, `STATE_SNAPSHOT`, `STATE_PATCH`, and `VERSION_ADVANCE`. It also lists every reserved gap and asserts that no message falls through a numeric range check.

- [ ] **Step 2: Verify RED because `protocol-semantics-v1.json` is absent**

```powershell
python -m unittest Tools.contracts.tests.test_protocol_semantics -v
```

- [ ] **Step 3: Encode header/direction/body contracts and error-carrier mapping**

Include 40/48-byte headers, 250-byte physical limit, request IDs, state-version sources, fragmentation eligibility, fixed 24-byte error body, direct versus asynchronous session revoke, and post-sync eligibility.

- [ ] **Step 4: Encode the 11-section console snapshot view separately**

Each section freezes visibility, sort key, maximum record count, redaction, and source AuthorityState section. A test must fail if the console view is changed to 12 sections or if a PRIVATE field becomes visible.

- [ ] **Step 5: Add negative tests for direction, reserved ID, wrong flags, ambiguous terminal carrier, and Authority/view root reuse**
- [ ] **Step 6: Generate the semantic header/document and run the full suite**
- [ ] **Step 7: Commit Task 3 files when permitted**

### Task 4: Twelve-section AuthorityState contract

**Files:**
- Create: `GameData/schemas/game-state-v1.json`
- Create: `GameData/tests/contracts/game-state-required.json`
- Test: `Tools/contracts/tests/test_game_state.py`
- Generate: `Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/game_state_fields.h`
- Generate: `Docs/firmware/generated/game-state-v1.md`

**Interfaces:**
- Produces exactly 12 critical sections with explicit IDs 1–12:
  `ROOM_CONFIG_AND_CONTENT`, `PLAYERS_AND_SEATS`, `ASSETS_AND_BUILDING_SUPPLY`,
  `DECK_ORDER_HELD_CARDS_AND_DRAW_CURSOR`, `RULE_RNG_AND_ACTIVE_DICE`,
  `TURN_AND_ROUND`, `BLOCKING_TRANSACTION_AND_CONTINUATION`,
  `DEADLINES_ORIGINAL_AND_REMAINING`, `MULTI_PLAYER_CARD_PROGRESS`,
  `BOT_POLICY_PRNG_AND_PENDING_ACTION`, `BOARD_POSITION_ADAPTER`,
  `COUNTERS_AND_ACTIVE_SOURCE_IDS`.

- [ ] **Step 1: Write a failing exact-section test**

Assert the section ID/name set, critical flag, schema version, ascending order, and required `room_lifecycle U8`/`room_seed U64` fields.

- [ ] **Step 2: Verify RED against the absent schema**
- [ ] **Step 3: Encode room, players/seats, assets/supply, decks/cards, RNG/dice, and turn/round**
- [ ] **Step 4: Encode blocking transactions and every continuation variant**

Include trade, auction, purchase, payment/debt, building demand, mortgage takeover, card multi-payment, bankruptcy system auction, original successor, revisions, instance IDs, and maximum list counts.

- [ ] **Step 5: Encode deadlines, multi-player card progress, bot state, board adapter, and counters**
- [ ] **Step 6: Add default/zero invariants and worst-case size accounting**

Tests construct canonical NO_ROOM and a six-player/40-tile maximal semantic fixture and assert the declared maximum is at most 24,576 bytes without calling a future game engine.

- [ ] **Step 7: Generate field descriptors and run all state tests**
- [ ] **Step 8: Commit Task 4 files when permitted**

### Task 5: Admin projection, HTTP events, system triggers, and bot actions

**Files:**
- Create: `GameData/schemas/admin-projection-v1.json`
- Create: `GameData/schemas/http-events-v1.json`
- Create: `GameData/schemas/system-triggers-v1.json`
- Create: `GameData/schemas/bot-actions-v1.json`
- Test: `Tools/contracts/tests/test_adapter_contracts.py`
- Generate: `Firmware/TestGameServer/libraries/GridopolyGenerated/src/gridopoly/generated/adapter_contracts.h`
- Generate: `Docs/firmware/generated/admin-http-contract-v1.md`

**Interfaces:**
- Admin projection freezes every summary/player/asset/transaction/auction/card/deadline/peer/operation key, JSON type, nullable encoding, redaction, sort key, count, and maximum response contribution.
- HTTP events freeze every route event/result/error mapping and distinguish cached terminal, running, and non-cached/no-watermark outcomes.
- System triggers and bot actions use explicit numeric actor/action IDs and payload layouts, never strings inside the C++ core.

- [ ] **Step 1: Write failing exact-key tests from the specification tables**
- [ ] **Step 2: Verify RED for four absent schemas**
- [ ] **Step 3: Encode admin sections and fixed JSON ordering/redaction**
- [ ] **Step 4: Encode HTTP operation/event/error mappings and status codes**
- [ ] **Step 5: Encode system-trigger and bot-action registries**
- [ ] **Step 6: Add negative tests for a missing nullable key, wrong redaction, duplicate sort key, and error-carrier ambiguity**
- [ ] **Step 7: Generate headers/docs and run the adapter-contract suite**
- [ ] **Step 8: Commit Task 5 files when permitted**

### Task 6: Exact-byte event/state/delta goldens

**Files:**
- Create: `Tools/contracts/wire_codec.py`
- Create: `Tools/contracts/crc.py`
- Create: `Tools/contracts/goldens.py`
- Create: `GameData/tests/goldens/event-batch-simple-v1.json`
- Create: `GameData/tests/goldens/event-batch-max-v1.json`
- Create: `GameData/tests/goldens/authority-no-room-v1.json`
- Create: `GameData/tests/goldens/authority-max-workflow-v1.json`
- Create: `GameData/tests/goldens/state-delta-roundtrip-v1.json`
- Test: `Tools/contracts/tests/test_exact_byte_goldens.py`

**Interfaces:**
- `encode_field_tlv`, `encode_event_batch`, `encode_authority_blob`, `apply_state_delta` consume only validated schema descriptors.
- Every golden stores semantic input, complete lowercase hex, byte length, CRC values, SHA-256 values, and decoded result.

- [ ] **Step 1: Write failing literal primitive/TLV tests**

```python
def test_u32_field_tlv_is_little_endian(self):
    self.assertEqual(
        encode_field_tlv(field_id=2, wire_type="U32", flags=1, value=0x0A0B0C0D).hex(),
        "02000501040000000d0c0b0a",
    )
```

- [ ] **Step 2: Verify RED and implement only primitive encoding/decoding**
- [ ] **Step 3: Add the simple event-batch semantic fixture and hand-derived expected hex**
- [ ] **Step 4: Add a maximum batch containing PRIVATE, unknown optional, and RECORD_LIST fields**
- [ ] **Step 5: Add canonical NO_ROOM AuthorityState and maximal active-workflow blobs**
- [ ] **Step 6: Add delta → post-state hash → snapshot CRC round trip**
- [ ] **Step 7: Decode every generated hex with an independent bounds-checking reader and compare semantic literals**
- [ ] **Step 8: Commit Task 6 files when permitted**

### Task 7: Content identity and manifest goldens

**Files:**
- Create: `GameData/tests/goldens/content-identity-16-v1.json`
- Create: `GameData/tests/goldens/content-identity-40-v1.json`
- Create: `GameData/tests/goldens/decks-ce-cf-v1.json`
- Create: `GameData/tests/goldens/content-manifest-v1.json`
- Test: `Tools/contracts/tests/test_content_identity.py`

**Interfaces:**
- Produces exact ordered asset IDs for 16 and 40 tiles, exact ordered CE/CF card IDs, resolved-deck hash inputs, map release status, and six-field manifest entry bytes.

- [ ] **Step 1: Write failing literal ID-list tests**
- [ ] **Step 2: Verify RED for absent goldens**
- [ ] **Step 3: Encode the complete 16/40 asset and card identity lists from the approved design**
- [ ] **Step 4: Freeze canonical preimage hex and SHA-256 for maps, decks, schema roots, and the manifest root**
- [ ] **Step 5: Mutate order, release status, revision, or one ID and prove the expected root changes**
- [ ] **Step 6: Run the full content identity suite**
- [ ] **Step 7: Commit Task 7 files when permitted**

### Task 8: A-level protocol entry documents, closure report, regeneration gate, and independent audit

**Files:**
- Create: `Docs/design-records/schema-closure-report.md`
- Create: `Docs/firmware/test-game-server-espnow-protocol.md`
- Create: `Docs/firmware/espnow-transport-v1.md`
- Modify: `Docs/firmware/main-controller-protocol.md`
- Modify: `Docs/README.md`
- Create: `Tools/contracts/check.ps1`
- Test: `Tools/contracts/tests/test_repository_closure.py`

**Interfaces:**
- `check.ps1` runs all contract tests, generator check, UTF-8/line-ending checks, generated-file drift, JSON parse/number checks, placeholder scan, and real-secret scan.
- The two new ESP-NOW documents are A-level entry points generated or cross-checked against the protocol schema. The existing main-controller document only links the shared semantic schema and remains the Raspberry Pi/WebSocket adapter entry.
- Closure report maps every semantic specification reference to a schema definition and golden/vector path.

- [ ] **Step 1: Write a failing repository-closure test**

The test requires all eight schemas, all five exact-byte/state goldens, all four content goldens, generated headers/docs, both A-level ESP-NOW entries, navigation/cross-links, and the closure report; it rejects unfinished markers, placeholder field names, implicit enum values, duplicate IDs, unreferenced definitions, and unresolved specification references.

- [ ] **Step 2: Verify RED because the closure report and/or mappings are absent**
- [ ] **Step 3: Generate the report from a checked mapping file, then manually review every mapping**
- [ ] **Step 4: Run the complete P0 gate twice and compare hashes**

```powershell
powershell -ExecutionPolicy Bypass -File Tools/contracts/check.ps1
python Tools/contracts/generate.py --root . --check
```

Expected: both exit 0; a second run changes no file and yields identical schema/golden hashes.

- [ ] **Step 5: Run an independent contract audit**

The reviewer checks exact registry coverage, field/type consistency, first-error behavior, byte offsets, lengths, CRC/SHA vectors, generated drift, placeholder absence, and that no firmware implementation exists.

- [ ] **Step 6: Record PASS evidence in the closure report**
- [ ] **Step 7: Commit all and only P0 files when Git write permission is available**

```powershell
git add -- GameData Tools/contracts Firmware/TestGameServer/libraries/GridopolyGenerated Docs/firmware/generated Docs/firmware/test-game-server-espnow-protocol.md Docs/firmware/espnow-transport-v1.md Docs/firmware/main-controller-protocol.md Docs/README.md Docs/design-records/schema-closure-report.md
git commit -m "feat: close Gridopoly byte contracts"
```

## P0 Exit Gate

P0 is complete only when every Task 1–8 checkbox is evidenced, all tests pass from a clean temporary generation directory, tracked generated bytes reproduce exactly, the independent audit records PASS, the repository contains no real secret, and there is still no executable TestGameServer firmware. Only then may P0A begin.
