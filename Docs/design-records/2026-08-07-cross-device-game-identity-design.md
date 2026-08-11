# Gridopoly Cross-Device Game Identity Design

Status: approved for implementation

Date: 2026-08-07

## 1. Goal

Every web-created room freezes its human and bot seats before play. Human consoles complete
`Avatar Setup -> Name Setup -> Ready`; bots receive deterministic identities and are ready
immediately. The authority starts a single server-timed five-second countdown only after every
required human has a final generated avatar and a confirmed name. The game engine remains in
`GamePhase::Lobby` until that countdown expires.

Identity is a low-frequency control plane. It does not enlarge `StateSnapshot`,
`AuthoritySnapshot`, or `RosterSnapshot`. `RosterSnapshot` remains the single source for final
display names.

## 2. Frozen room and seat semantics

- Web creation accepts `humanCount` and `botCount`; `humanCount >= 1`, `botCount >= 0`, and
  `2 <= humanCount + botCount <= 6`.
- Human seats are player IDs `1..humanCount`. Bot seats follow them and never become console
  seats.
- A UDP device can claim only a human seat. Existing device-to-seat registry bindings are reused
  only when the bound seat is still a human seat in the current room.
- Unclaimed human seats remain offline and block readiness. Bots cannot replace them.
- New rooms clear all old identity drafts, request caches, avatar publication records, and
  countdown state. Normal same-room resync never clears them.

## 3. Protocol allocation

Two low-frequency message types are allocated in `GridopolyProtocol`:

| MessageType | Value | Direction | Meaning |
| --- | ---: | --- | --- |
| `IdentityRequest` | `0x29` | console -> authority | Query or mutate the authenticated seat identity |
| `IdentitySnapshot` | `0x2A` | authority -> console | Direct result or full/public identity projection |

No new `ActionCode` is allocated. Identity mutations do not share the gameplay action pending
slot.

### 3.1 Enums

```text
IdentityOperation
  Query          = 1
  ConfirmAvatar  = 2
  ConfirmName    = 3

IdentityRoomPhase
  AvatarSetup = 1
  Countdown   = 2
  Active      = 3

IdentitySeatStage
  AvatarSetup      = 1
  AvatarGenerating = 2
  NameSetup        = 3
  Ready            = 4
  Countdown        = 5
  Active           = 6

IdentityResultCode
  Ok                     = 0
  InvalidRequest         = 1
  Unauthorized           = 2
  StateVersionStale      = 3
  SeatRevisionStale      = 4
  CatalogMismatch        = 5
  InvalidRecipe          = 6
  InvalidName            = 7
  DuplicateName          = 8
  NotAllowed             = 9
  RequestIdConflict      = 10
  AvatarGenerationFailed = 11
```

Snapshot flags:

- bit 0 `IdentitySnapshotFlagReplay`
- bit 1 `IdentitySnapshotFlagResync`

Seat flags:

- bit 0 `Present`
- bit 1 `Human`
- bit 2 `Bot`
- bit 3 `AvatarGenerating`
- bit 4 `AvatarFinal`
- bit 5 `NameFinal`
- bit 6 `Ready`
- bit 7 `Connected`

### 3.2 `IdentityRequest`, fixed 44 bytes

All integers use existing little-endian protocol encoding. Reserved bytes must be zero.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | schema, fixed `1` |
| 1 | 1 | operation |
| 2 | 1 | playerId, must equal authenticated seat for mutations |
| 3 | 1 | flags, fixed `0` |
| 4 | 4 | requestId; nonzero for every client request |
| 8 | 4 | expectedStateVersion |
| 12 | 2 | expectedSeatRevision |
| 14 | 2 | avatarCatalogVersion |
| 16 | 1 | hairPresetId, V1 range `1..10` |
| 17 | 1 | hairColorId, V1 range `1..20` |
| 18 | 1 | facePresetId, V1 range `1..10` |
| 19 | 1 | skinToneId, V1 range `1..8` |
| 20 | 1 | outfitPresetId, V1 range `1..10` |
| 21 | 1 | nameLength, UTF-8 byte length `0..16` |
| 22 | 17 | name bytes followed by zero padding |
| 39 | 5 | reserved, all zero |

`Query` accepts zero `expectedStateVersion` and zero `expectedSeatRevision`; its recipe and name
fields must be zero. Mutations require nonzero exact `expectedStateVersion`, exact
`expectedSeatRevision`, and a nonzero request ID. `ConfirmAvatar` requires a complete valid recipe
and an empty name. `ConfirmName` requires an empty recipe, a final avatar, and a valid name.

Names are one to sixteen UTF-8 bytes after trimming ASCII surrounding whitespace. They must be
valid UTF-8, contain no Unicode or ASCII control characters, and be unique under Unicode-aware
case-insensitive comparison. Bot names participate in the duplicate check.

### 3.3 `IdentitySnapshot`, fixed 182 bytes

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | schema, fixed `1` |
| 1 | 1 | roomPhase |
| 2 | 1 | selfStage |
| 3 | 1 | result |
| 4 | 4 | requestId; `0` for unsolicited/full-resync projection |
| 8 | 4 | stateVersion |
| 12 | 4 | identityRevision |
| 16 | 8 | serverEpochMs |
| 24 | 8 | countdownDeadlineEpochMs, zero outside countdown |
| 32 | 2 | avatarCatalogVersion, fixed V1 value `1` |
| 34 | 1 | playerCount |
| 35 | 1 | selfPlayerId |
| 36 | 1 | requiredHumanMask |
| 37 | 1 | avatarFinalMask |
| 38 | 1 | nameFinalMask |
| 39 | 1 | readyMask |
| 40 | 1 | onlineMask |
| 41 | 1 | operationEcho; zero for unsolicited projection |
| 42 | 1 | snapshot flags |
| 43 | 1 | reserved, zero |
| 44 | 138 | six fixed 23-byte seat records |

Each seat record starts at `44 + index * 23`:

| Relative offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | playerId |
| 1 | 1 | seat flags |
| 2 | 1 | seatColorId |
| 3 | 1 | reserved, zero |
| 4 | 2 | seatRevision |
| 6 | 2 | avatarRevision |
| 8 | 8 | avatarContentHash64 |
| 16 | 2 | avatarCatalogVersion |
| 18 | 1 | hairPresetId |
| 19 | 1 | hairColorId |
| 20 | 1 | facePresetId |
| 21 | 1 | skinToneId |
| 22 | 1 | outfitPresetId |

Unoccupied or not-yet-final avatar records expose zero recipe, revision, and content hash. An
avatar-generating seat exposes only its generating flag. Temporary recipes are never broadcast.

## 4. Idempotency and synchronization

- The UDP frame room and authenticated session bind every request to the room, device, and seat.
- Each seat persists its last accepted request ID, exact 44-byte request image, and resulting
  snapshot metadata. Identical replay returns the first result with the replay flag and does not
  advance revisions. Reusing the ID with any byte difference returns `RequestIdConflict`.
- Pairing and full resync send `IdentitySnapshot(requestId=0, Resync)` before gameplay snapshots.
- Any public identity change schedules a new identity snapshot for every connected session.
- A new room invalidates all old sessions. A client must clear its draft as soon as the room ID
  changes, before applying any new projection.
- ESP-NOW can reject/disable identity capability while continuing to compile; Wi-Fi UDP is the
  authoritative production transport.

## 5. Avatar generation and HTTP assets

V1 recipe mapping is the existing `Assets/GridCity/Avatars/V1/avatar-layers.manifest.json`:

- 10 hair presets
- 20 hair colors
- 10 faces
- 8 skin tones
- 10 outfits
- composition order `face -> outfit -> hair`

A build-time tool packs the already-approved registered 320x320 face, outfit, and hair bases into
one deterministic RGBA layer package. Hair and skin recoloring uses the same manifest palettes and
the same deterministic algorithms as the existing V1 prototype. Runtime C++ reads only the
selected face, outfit, and hair blocks and performs alpha
composition. No runtime Pillow, ImageMagick, libpng, or network-installed image dependency is
required.

For a successful avatar confirmation the server writes both artifacts to temporary files, fsyncs
them, atomically renames them, and only then marks the avatar public:

```text
GET /assets/avatars/<roomId>/p<playerId>-a<avatarRevision>-<hash16>.png
GET /assets/avatars/<roomId>/p<playerId>-a<avatarRevision>-<hash16>.rgb565
```

- PNG: 128x128 RGBA with transparent pixels outside the circular portrait.
- RGB565: 128x128 little-endian, exactly 32768 bytes; the console applies its circular clip.
- HTTP cache: `public, max-age=31536000, immutable`.
- `hash16` is the lowercase sixteen-hex-digit `avatarContentHash64`.
- Seat color rings and player names are UI elements and are not baked into either artifact.
- A snapshot is never allowed to reference a file that has not been fully published.

The editor preview is a separate lazy HTTP resource and is never embedded in firmware:

```text
GET /assets/avatar-previews/v1/h<hair>-c<hairColor>-f<face>-s<skin>-o<outfit>.rgb565
```

- IDs are decimal manifest indices: hair `1..10`, hair color `1..20`, face `1..10`, skin `1..8`,
  and outfit `1..10`. Canonical keys do not contain leading zeroes.
- The server composes the same registered 320x320 canvas, takes the fixed pixel crop
  `x=50..269, y=10..309`, precomposes alpha against `#061017`, and returns 220x300 little-endian
  RGB565, exactly 132000 bytes.
- The response uses `application/octet-stream`, `public, max-age=31536000, immutable`, and a strong
  content ETag. `If-None-Match` returns 304.
- Only legal V1 combinations are accepted. Malformed keys, extra suffixes, traversal, unknown
  catalog versions, and out-of-range IDs return 404 without creating cache files.
- The disk cache is generated on first request with per-key duplicate-work suppression and atomic
  publication. A cache hit performs no recomposition.
- Client behavior is latest-recipe-wins: it shows a spinner while loading and must discard any
  response whose recipe key no longer equals the active draft. This is a PlayerConsole concern;
  the server contract supplies immutable keyed responses.

## 6. Authority lifecycle

1. `/api/new` validates the counts, increments room ID, resets the engine, adds all frozen human
   placeholders followed by fixed bots, and leaves the engine in `Lobby`.
2. Bot recipes are derived deterministically from persisted `roomSeed + playerId + catalogVersion`,
   generated, named `Bot 1`, `Bot 2`, and so on, and marked ready.
3. Human `ConfirmAvatar` validates and persists a private pending recipe, advances the seat to
   generating, and queues the native renderer.
4. Successful atomic publication advances `avatarRevision` and `seatRevision`, publishes the
   recipe/hash, and moves that seat to Name Setup. Failure retains Avatar Setup and reports
   `AvatarGenerationFailed`; no partial public asset remains.
5. Human `ConfirmName` updates the sole authoritative `PlayerState.name`, advances game state and
   seat revisions, and immediately marks that human Ready. There is no fourth confirmation.
6. Once every required human has final avatar + final name, the authority sets one persisted
   `countdownDeadlineEpochMs = now + 5000`. Disconnects do not pause or cancel it.
7. At or after the deadline, including after service restart, `engine.start()` runs exactly once,
   room phase becomes Active, and normal gameplay/bot ticking begins.

## 7. Persistence and compatibility

Identity uses an independent, checksummed, atomic `identity.bin` schema. It stores room ID, room
seed, room phase, countdown deadline, global revision, frozen counts, all seat records, private
pending recipes, and per-seat idempotency records. Game state remains in `state.bin`.

When an old installation has `state.bin` and no valid identity file, the authority creates a
compatibility identity in Active phase from the restored roster. It never sends an in-progress
legacy room back to Avatar Setup. The next web-created room uses the new lifecycle.

## 8. Web behavior

- The new-game panel has separate human and bot selectors and prevents invalid totals before POST.
- `/api/new` accepts `size`, `humans`, and `bots` and returns room, state version, identity revision,
  and lifecycle.
- `/api/identity?room=<id>&since=<identityRevision>` provides a low-frequency web projection;
  unchanged state returns 204.
- During setup the web page displays all frozen seats, final avatars only, final names only,
  `OFFLINE`, `GENERATING`, `NAME`, or `READY`, plus the server-deadline countdown.
- Gameplay controls remain disabled until room phase Active.

## 9. Required tests

- Exact protocol offsets, lengths, reserved-byte rejection, max six-seat round trip.
- Invalid UTF-8/control/empty/duplicate names, including Bot collisions and case variants.
- Exact state and seat revision gates for mutations; Query accepts zero versions.
- Exact-byte request replay, request-ID collision, and persistence across restart.
- No Bot replacement; offline human blocks readiness; device binding reuses only human seats.
- Generating recipes remain private; publication is atomic; HTTP MIME, length, immutable cache and
  path traversal rejection.
- Preview route legal/illegal parsing, exact 220x300 byte length, color/background/crop golden
  pixels, strong ETag/304 behavior, one-generation concurrent cache fill, and no stale partial file.
- Bot deterministic recipe/name persistence and collision-free legal ranges.
- Last human starts one five-second deadline; disconnect does not pause; restart before/after the
  deadline produces one game start.
- New room clears identity state; same-room resync preserves it.
- Dual-client UDP full resync, realtime updates, shared deadline, and active transition.
- Web create validation and 16/24/32/40 board compatibility.
