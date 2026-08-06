# Web Board Owner Badge Design

## Goal

Make purchased assets immediately identifiable on the test web board without obscuring the existing property-group color, tile artwork, player tokens, or compact 40-tile layout.

## Scope

- Change only the browser UI and its browser/static verification.
- Reuse the existing `ownerId` already present in `/api/sync`; do not change Core, persistence, HTTP schemas, UDP, or PlayerConsole firmware.
- Render ownership only for purchasable asset tiles.
- Preserve the existing mortgage and building-level presentation.

## Visual contract

- An unowned asset and every non-asset tile display no owner badge.
- An owned asset displays one compact badge containing `P<ownerId>`.
- The badge sits at the upper-right of the tile, below the property-group strip, and reserves enough horizontal space in the tile-number row to avoid overlap.
- Player colors are stable and shared with the corresponding player card and board token:
  - P1: cyan
  - P2: coral
  - P3: violet
  - P4: green
  - P5: amber
  - P6: rose
- The 40-tile layout uses a smaller badge and font.
- Ownership does not tint the full tile and does not add an ownership rail.
- The existing textual `P1` owner fragment is removed from the metadata line to avoid duplication.

## Accessible identity

- Color is supplementary; the visible `P1` text remains the primary compact identity.
- Hovering either the badge or its tile exposes the authoritative full player name in the native tooltip.
- The tile receives an ownership-aware accessible label containing the full player name.
- If an owner ID is temporarily absent from the player roster, the badge remains visible and falls back to `P<ownerId>` as its full identity.

## State and rendering

- `projectState()` continues deriving each tile's `owner` from the compact asset projection.
- Rendering resolves `ownerId` against the current `players` array on every state update.
- Purchase, auction settlement, trade, bankruptcy return, and resync therefore update or remove the badge without a separate subscription or cache.
- Mortgaged assets retain the badge because ownership has not changed.
- Player cards and board chips use the same player-color mapping so the badge can be matched to the player list at a glance.

## Verification

- Static tests must prove that owner badges are conditional on a nonzero owner, carry `P<ownerId>`, resolve the full player name, and omit the redundant owner fragment from metadata.
- Browser matrix verification must cover 16, 24, 32, and 40 tiles with unowned, owned, mortgaged, and multiple-player ownership states.
- Verify no badge overlaps tile number text, property-group strip, artwork, or player chips.
- Verify ownership changes on a later projection update the badge without a page reload.
- Regenerate the compressed web asset and verify its content-derived ETag.
- Deployment validation must keep the room and game version, return `peers=1`, preserve Heartbeat/Ack progress, keep resync stable in an idle window, and keep authentication/replay/transmit errors at zero.

## Non-goals

- No full-tile ownership tint.
- No bottom ownership strip.
- No owner-name text permanently printed inside a tile.
- No new legend, endpoint, protocol field, or gameplay rule.
