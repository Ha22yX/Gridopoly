# Automatic new-game map compatibility — 2026-09-26

## Confirmed failure and change

The Web panel offered 16/24/32/40-tile maps while sending `autoPlayers=1` for each. The server rejected every automatic map except 32 with HTTP 409 and `automatic_players_require_32_tiles`. The Web request handler then refreshed the page status and overwrote the visible error. These were two distinct issues: a server map restriction and an error-display lifetime problem.

`AuthorityService::newAutoGame` now validates the requested map against `BoardCatalog`. All four supported sizes use the same authenticated screen selection, frozen seats, bot fill and Tag onboarding. Unsupported map sizes are rejected before mutation. The directed 32-module ring rules remain specific to ring mapping and do not gate game creation or reader selection. The Web correction was separately committed as `c68fa71` and is included in this server candidate.

## Reproduction and targeted checks

The deployed b15562bf server sources were rebuilt in an isolated Pi directory with a copy of production state and the production avatar component files. The probe uses only loopback and random HTTP/UDP ports; its screen discovery is an isolated test fixture. It never sends a live game/Tag/assignment request.

The actual copied room was 993580108, because the user had already created a new room by the time the data copy completed. The original isolated data was preserved for both variants.

| HTTP request on isolated data | Deployed-source behavior | Corrected-source behavior |
| --- | --- | --- |
| POST /api/new?size=24&autoPlayers=1 | 409, automatic_players_require_32_tiles; room/version unchanged | 200 |
| POST /api/new?size=32&autoPlayers=1 | 200 | 200 |

Three relevant Linux native targets passed with assertions enabled and `-Wall -Wextra -Werror`: Tag authority (16/16 cases), HTTP integration, and onboarding HTTP/UDP network integration. The normal server binary also built successfully. This targeted run supplements the earlier complete 19-target release validation; it is not described as a second complete-suite run.

The added network regression runs each of 16, 24, 32 and 40 from an old 24-tile legacy-seat scenario. A new full-MAC screen is discoverable despite rejected old-seat admission. An automatic new game admits it as the sole human, fills three bots, and successfully completes fresh-empty → Tag bind → take-back on one unassigned, unanchored long reader with no ORDER neighbors. Existing in-place inventory cannot bind without the fresh empty scan. Identity editing becomes available after binding, and no gameplay settlement occurs. For every supported map case, an unsupported 23-tile request with an eligible screen also proves room, game state and metadata bytes remain unchanged.

## Evidence and current live-state boundary

Reproduction evidence: `C:/Users/kicof/AppData/Local/Temp/gridopoly-newgame-debug-20260926/clone-before.log`, `clone-candidate.log`, and their `before-*` / `candidate-*` JSON snapshots. Build evidence: the same directory's `native/native-20260926-204800.log` and frozen `source-manifest.json` / `sources.tar.gz`.

During investigation, live read-only observations independently showed room 993580108, a frozen online GP-DCB4D902D1DC in seat 1, bound Tag 8EFA24DF, completed pairing, and a confirmed player avatar. COM6 remained unassigned, unanchored and outside a closed ring. The user reported that recreating the game now worked. This is evidence that the existing 32-tile flow already supports a real single reader; no task created that live room or injected its binding.

A brief player disconnection was adjacent to a captured USB_UART_CHIP_RESET. The device returned online without a network change. It is not treated as the original map-selection failure or as a persistent AP problem.

Candidate and deployment preparation are archived in `C:/Users/kicof/GridopolyReleases/server-newgame-maps-20260926-205040`. The deployment record there remains the authority for binary hash, verification and actual deployment status. Preparation backs up the current live room, not an older room-107 image. No live new-game, Tag, assignment or AP operation is part of this correction.
