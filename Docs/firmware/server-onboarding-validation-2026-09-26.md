# Server ORDER / onboarding validation and deployment — 2026-09-26

## Delivered behavior

The server derives a complete 32-module ORDER ring only after validating 28 long modules, four corners spaced every eight modules, one directed cycle, and consistent corner-phase anchors. Invalid or opened rings withdraw unsafe mappings. Legacy open segmented chains remain supported.

New automatic games freeze one to four authenticated full-MAC, Tag-capable screens and fill the remaining seats with bots. Zero or more than four eligible screens are rejected before room mutation. Tag binding uses fresh scan evidence, room/session/reader fences, explicit Retry after cancellation or timeout, duplicate-UID protection, and durable binding/removal state. A bound player may edit their identity while another player pairs. Pending removal survives reader reuse, cancellation and server restart; failed writes retain the fence and require another new empty scan.

Core changes fix bankruptcy turn progression, enforce shared building/landmark inventory, provide explicit whole-group liquidation through SellBuilding argument 1, and allow bounded bot unmortgage/build actions. Protocol details are in [the contract](ring-onboarding-contract-2026-09-26.md).

## Final validation

The final source snapshot completed all 19 Linux native test targets and the normal server build with g++ 14, C++17, `-O2 -Wall -Wextra -Werror -pthread`; assertions remained enabled. No production room actions were used for testing.

| Test targets | Result |
| --- | --- |
| Core rules, protocol, UDP envelope | 3 / 3 passed |
| Reliability policy, network recovery, player detail projection | 3 / 3 passed |
| Tile assignments / ORDER topology and projection | 1 / 1 passed |
| Tag model and authority | 2 / 2 targets; 17 + 16 cases passed |
| Identity model, avatar renderer/codec/client, identity authority | 5 / 5 passed |
| Authority persistence and trade authority | 2 / 2 passed |
| Existing UDP integration, existing HTTP assets, new onboarding network integration | 3 / 3 passed |

The new network suite runs actual HTTP and authenticated UDP on isolated loopback ports. It covers discovery for zero through five screens, rejected but discoverable devices, frozen/restarted seats, immediate pairing after a room change, cross-transport idempotence, identity gates, fresh/old scans, duplicate Tags, parallel removal prompts, malformed JSON, and 32-module HTTP ring reports with fault and repair.

The three Python checks preceding the native build also passed locally: web tile staging (36 PNG, 36 RGB565, four corners), avatar component verification (30 components), and avatar staging (31 files including the manifest). Avatar manifest SHA256: `5bcb9b6e35b512d1bab2b6e3e1a1c87140116a8c2b72870683c5ac328d59a8af`.

The metadata suite covers legacy restoration, the 295-byte ONB1/PND1 extension, atomic binding failure, pending-removal restoration, reader reuse followed by Retry to another reader, cancellation, failed empty-scan persistence, and avoiding unnecessary metadata writes.

## Candidate and evidence

Local release directory: `C:/Users/kicof/GridopolyReleases/server-onboarding-20260926-045820`.

| Artifact | File |
| --- | --- |
| Normal server binary | `gridopoly_server` |
| Per-file SHA256 of the 190-file build snapshot | `source-manifest.json` |
| Exact source snapshot | `sources.tar.gz` |
| Complete final Linux log | `native-20260926-045410.log` |
| Original business and health snapshot | `before.json` |
| Consecutive offline restore results | `restore-1.json`, `restore-2.json` |
| Immediate deployment comparison | `before-deploy.json`, `after-deploy.json` |
| Service restart and reconnection log | `production-service-after.txt` |
| Deployment record, hashes and backup path | `release.json` |

Candidate SHA256: `b15562bf7bb2c9b3ccaa678d024a56bbb95961e4754d3c9400d6808b78601e07`.

Server, shared Core/Protocol, and native test sources matched the snapshot at release preparation. The ongoing Player UI work changed `app_state.cpp`, `ui_renderer.cpp`, and `wifi_udp_player_transport.cpp` after the broad snapshot; these three files are not linked into this server or its native test targets. Their exact older snapshot content is retained rather than relabeled as the later Player candidate.

## Production deployment

Completed at `2026-09-26T04:59:35Z` on `10.0.0.124`. Only `/usr/local/bin/gridopoly_server` was atomically replaced and `gridopoly` restarted. No firmware was flashed and no AP, commissioning secret, registry, game creation or tile assignment operation was performed.

Before replacement, the old binary and the entire game data directory were copied to `/var/backups/gridopoly/server-onboarding-20260926-045820`. Old binary SHA256: `4095bd88d80b45047d48876709f7ba8a060c8cab7df8d0745280b74f15fa9b8a`.

A separate copy of that production data was restored, flushed and restored again with the new Authority code. Both restores retained the room, board, identity content, Tags, cash, positions, ownership and pending business state. The live room was checked again immediately before replacement.

After deployment, service PID `1802163` was active/running, the previous process reported `STOP saved=1`, and the new process reported READY/ALIVE. The existing player screen reconnected within the 30-second verification window; peers returned to one. The legacy device registry hash and empty assignment list remained unchanged.

Room `993580107` remains the existing 24-tile identity-setup room with one human and three bots. Every player retains cash 800 and position 0; Tags and ownership remain empty, and no movement/debt/auction/card workflow is active. Identity content, names and avatar references match the pre-deployment snapshot. State version changed from 9 to 12 and identity revision from 5 to 7 through shutdown, restoration and reconnection; these are version/connection changes, not game settlement. `tagPairing.enabled` remains false for the legacy room.

## Remaining hardware boundaries

The 32-module loopback suite is not evidence of a physical 32-board ring. No 36-device AP capacity claim is made. New full-MAC Player firmware may receive a rejected PairAccept in the existing legacy room while still appearing as eligible discovery; the user must explicitly create a new automatic game to freeze those new IDs. This deployment does not infer identity migration or replace the existing room.

The existing uint8 round counter still wraps after 255 rounds. Bot games with no complete color groups can continue for many rounds; no forced ending or new negotiation strategy was introduced.
