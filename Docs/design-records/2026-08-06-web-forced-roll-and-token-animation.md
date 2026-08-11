# Web Forced Roll and Token Animation Design

## Goal

Give the test web authority two deterministic debugging aids without changing the player-console wire protocol:

1. An administrator can arm one player's next roll to land on a selected tile.
2. After an authoritative `ConfirmPosition`, the browser moves that player's token clockwise one tile at a time instead of teleporting it.

## Forced-roll rules

- The override is identified by player id and absolute target tile.
- The target must be 2 through 12 clockwise spaces from the player's current position.
- The player must exist, be solvent, be outside Hold, and either be inactive or be the active player in `AwaitRoll`.
- The service derives two legal six-sided dice. It prefers a non-double pair, so only totals 2 and 12 necessarily create doubles.
- A 2- or 12-space target is rejected when the player already has two consecutive doubles because Monopoly sends the third double to Hold instead of the target.
- The override is consumed only after the matching `Roll` succeeds. Duplicate UDP action requests are already deduplicated above `AuthorityService` and therefore cannot consume it twice.
- Human and bot turns use the same execution helper. On a bot turn, the timed bot step calls the forced roll instead of random `runBots()` for that one step.
- A new game clears the override. A service restart restores it from the backwards-compatible authority metadata extension and revalidates it against restored game state before exposing or executing it.
- The override is an administrative control, not game state. It has a separate `controlVersion`; it does not increment `stateVersion` or trigger player-console resync.

## HTTP and web projection

- `POST /api/forced-roll?player=<id>&target=<tile>&expected=<stateVersion>` arms an override.
- `POST /api/forced-roll?cancel=1` clears it.
- Success returns the active override and `controlVersion`; validation failures return HTTP 409 and a stable error message.
- `/api/sync` includes `controlVersion` and a `forcedRoll` object. Conditional polling includes `control=<controlVersion>` so changes made by another browser invalidate a 204 response without touching game state.
- The operator right-clicks a player card and chooses **指定下一次目的地**. The board then highlights only that player's legal clockwise 2–12-space destinations; clicking a highlighted tile arms the one-shot override. `Esc`, the floating Cancel control, or the player's context-menu cancel action exits or clears the workflow. The armed player and target tile retain compact markers during normal polling.

## Token animation

- The browser maintains visual positions separately from authoritative player positions.
- A movement animation starts only when the same room/board changes a player from `AwaitMoveConfirm` to a different authoritative position. This identifies the normal `ConfirmPosition` transition and avoids animating Hold, cards, and other teleports.
- Each step advances clockwise after 180 ms and redraws only token chips. The authoritative state remains available immediately for panels and actions.
- Normal polling and full compact syncs do not reset an active animation whose destination still matches.
- A new room, board change, bankruptcy, non-movement position change, or conflicting newer destination cancels the animation and snaps to authority.
- Animation is presentation-only and never delays or mutates the game engine.

## Verification

- Host tests cover validation, exact dice totals, one-shot consumption, doubles safety, bots, restart persistence, new-game clearing, HTTP responses, and separate control-version conditional sync.
- Node web tests execute extracted pure destination/animation helpers with literal 16/24/32/40 fixtures and verify the generated gzip asset is current.
- The complete native Raspberry Pi suite and web asset checks must pass before a deployment window is requested.
