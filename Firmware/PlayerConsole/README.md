# Gridopoly Player Console

Arduino firmware skeleton for the 480 x 480 Viewe UEDX48480021-MD80ET ESP32-S3 round display.

Production builds default to the Raspberry Pi Wi-Fi/UDP transport. The console joins the
`gridopoly` access point and sends one existing Gridopoly binary frame per authenticated
UDP datagram to `10.42.0.1:4242`. ESP-NOW remains available as a compile-time fallback;
self-test builds retain `DemoTransport`, so UI and state-machine tests require no radio peer.

## Raspberry Pi connection

1. Copy `config/secrets.example.h` to the ignored `config/secrets.local.h`.
2. Set the AP password and `GRIDOPOLY_WIFI_UDP_PSK` to the local values deployed on the Pi.
3. Start the Pi AP and server, then start the player console. The console joins the AP,
   validates pairing-key discovery, obtains a nonzero session id, derives its session key,
   and applies only authenticated authoritative projections.
4. Confirm `GRIDOPOLY_UDP ready`, `GRIDOPOLY_UDP paired`, `GRIDOPOLY_SNAPSHOT`,
   `GRIDOPOLY_AUTH`, and `GRIDOPOLY_ROSTER` diagnostics.

Each session uses a truncated HMAC-SHA256 envelope tag, a 64-bit packet sequence, and a
64-packet replay window. Retries retain the original inner frame sequence and request id
while receiving a new UDP packet sequence, preserving server-side exactly-once action
semantics. A server or AP restart invalidates the old room/session and automatically
returns the console to authenticated discovery and seat restoration.

If no valid server frame arrives for 9 seconds, the console disables authoritative
actions and shows a red crossed Wi-Fi badge at the screen's 12 o'clock edge above every
page and modal. After 15 seconds it resets the session and resumes channel scanning. The
9-second threshold tolerates several isolated heartbeat-ACK losses without flashing a
false offline warning. The badge disappears automatically when any authenticated server
frame restores the link.

Per-frame ESP-NOW TX/RX diagnostics are disabled in the fallback because an unread USB CDC
stream can back-pressure the display loop and delay heartbeats. Define
`GRIDOPOLY_ESPNOW_VERBOSE_DIAGNOSTICS=1` only for a session with an actively consumed
serial monitor. Link transitions, peer setup, command transactions, and send failures
remain logged in normal builds. HWCDC uses a zero-millisecond TX timeout, so even those
logs are dropped instead of blocking the game loop when no serial monitor consumes them.

To build the fallback explicitly, define `GRIDOPOLY_USE_ESPNOW=1` (or set
`GRIDOPOLY_PLAYER_TRANSPORT=GRIDOPOLY_TRANSPORT_ESPNOW`). The default formal build uses
`GRIDOPOLY_TRANSPORT_WIFI_UDP`; `GRIDOPOLY_SELF_TEST=1` always selects `DemoTransport`.

Creating a new game changes the server `roomId` and may reset `stateVersion` to a lower
value. The server sends an immediate `FlagResync` snapshot to every encrypted peer. The
console keeps the existing encrypted peer, treats the unencrypted discovery only as a
pending room hint, and commits the room change only after an authenticated resync snapshot.
It then clears old-game navigation, modals, debt, dice, and pending commands before
rebuilding Home. Ordinary lower-version snapshots remain rejected. `playerCount`, active
seat, bankruptcy flags, and the filled/hollow Home queue dots are therefore recalculated
together without a slow channel scan and re-pair cycle.

Wire definitions remain single-source in `Firmware/libraries/GridopolyProtocol` and
`Docs/firmware/test-game-server-espnow-protocol.md`. Each server update is consumed as a
four-part transaction: personal `StateSnapshot`, global `AuthoritySnapshot`,
`RosterSnapshot`, then ordered `GameEvent` batches. The console uses the full player and
asset projections for live UI state, resolves static names and economy values through the
hash-checked `GridopolyCore` `BoardCatalog`, and acknowledges the applied state version and
event sequence in its heartbeat. A sequence gap requests a complete resync instead of
guessing missing game state.

The formal Wi-Fi/UDP build implements the authoritative bidirectional trade workflow from
`Docs/firmware/trade-protocol.md`. Home Trade edits the receiver, assets, and cash;
Player Detail locks the receiver; Asset Detail preselects the current asset. The recipient
can accept the current revision, open a receiver-locked counteroffer, or reject it. Every
mutation carries the current nonzero `stateVersion`, `tradeId`, and revision as applicable;
the transport retries an identical request id for at most four seconds. Active offers are
restored by an on-demand `TradeResponse` after full sync, while an explicit
`NoActiveTrade` projection clears stale trade UI after disconnects or bot decisions.

The ESP-NOW fallback client retains the shared 0x27/0x28 codec for future compatibility,
but the ESP32 `TestGameServer` does not yet implement that backend. Its Trade entry is
therefore blocked with `TRADE REQUIRES WI-FI SERVER` instead of sending a request that the
test server would silently ignore.

Home exposes only `ASSETS`, `PLAYERS`, and `TRADE` while waiting; `DICE` or `END TURN` is
inserted as the priority action during the local turn. `More` is not a visible menu.
Demo Lab remains available through the three-second Home hold.
The three-action waiting carousel always wraps on both sides, so selecting `ASSETS` shows
`TRADE` on the left and `PLAYERS` on the right. After `END TURN`, the priority action fades
and shrinks for 320 ms while the remaining actions settle into place. A 1.2-second local
presentation hold prevents a fast bot round from instantly stealing focus back to `DICE`.

Selecting a player sends one on-demand detail request. The response is correlated by
request id and target player and supplies current position/cash, up to 28 owned assets,
and the latest 10 finance records. The console does not subscribe or poll while the page
is open; `REFRESH` sends a new request. Only the selected player's detail is cached, and
leaving the detail page, disconnect, room resync, or selecting another player invalidates
it. These details are therefore never added to recurring State, Authority, or Roster
snapshots.

The room-ready surface renders every authority-final avatar together with the player's
name and `READY`, `SETTING UP`, or `OFFLINE` state. It uses an adaptive 2x2 layout for up
to four players and a 3x2 layout for five or six players, keeping the portraits large
enough to identify at arm's length. Player Detail reuses the same immutable
`room/player/revision/hash` final-avatar cache beside the on-demand cash and position
summary. Download completion invalidates both pages, while Player Detail still makes no
recurring detail request and never embeds avatar bytes in gameplay projections.
The Players list itself only shows seat identity, connection state, and the current-turn
marker; economic and location values appear only after the explicit detail response.

## Passive table activity

Ordered public `GameEvent` records from other seats are stored in a fixed 20-entry local
activity ring. Ordinary pages may show the latest event in a 2.8-second banner inside
the round panel's top safe area, but the banner never changes the page, focus, modal,
hold progress, or inline trade editing. A newer event immediately replaces the current
banner instead of waiting in a playback queue; every replaced record remains in history.
Replacement uses a fixed-geometry cross-slide: the outgoing banner moves left while
fading away and the latest banner enters from the right after a short overlap. The new
record is still immediate and clickable; the transition never captures knob focus or
blocks the page beneath it.
Any knob, press, or touch input dismisses the transient banner while still performing
the original input. Tapping the banner or its compact centered `ACT` button opens `LIVE ACTIVITY`, a normal
scrollable center list with fixed `BACK`; returning restores the exact source page and
focus. Same-room projection resync preserves this page, selected row, scroll anchor, and
Back stack; a new room or mandatory authority decision may replace it.

Activity text always resolves `actorId` and `targetId` through the authoritative Roster
display names. A target matching the viewing console is rendered as `YOU`; other players
use their real display names. It never exposes `P1`, `P2`, or another seat's private card payload. Public
 movement, dice, purchase, rent/fees, card result, mortgage/building, auction, bankruptcy,
 turn, and trade events are included. Duplicate event sequences are ignored. A reconnect
 history batch is never played item by item: all unseen records enter `LIVE ACTIVITY`, while
 only the newest unseen eligible event becomes the current banner. On a cold connection to
 an existing room this same rule may show that room's latest retained event once. Changing
 room clears the complete feed and its event cursor.
When another player interacts with an asset owned by this console, both the transient
banner and the history row show a fixed-width `MY` pill. Ownership is captured when the
event arrives, so later trades do not rewrite history; the pill never changes the event
title, opens another page, or interrupts the current interaction. A move event's destination
tile is resolved through the active board catalog, so the arrival notice is marked before
any following rent or debt event even though that compact wire event has no asset index.

## Authoritative turn flow

- `AwaitRoll`: Home remains green, pulses gently, and gives `DICE` priority.
- A roll immediately opens a two-die animation. It keeps cycling while the console waits
  for the server, so transport latency never leaves a frozen or empty result page. Once
  the authoritative target arrives, the final faces animate for 1.9 seconds and the
  resolved `N SPACES` result remains readable for another 0.7 seconds.
- A doubles result is labeled `DOUBLES!` and reserves an extra roll without skipping the
  landing, purchase, auction, card, or debt flow. After the landing is fully settled, a
  dedicated 2-second reward stage shows both dice, `YOU ROLL AGAIN`, and the authoritative
  `DOUBLE STREAK n / 3`; double sixes receive the stronger `DOUBLE SIXES` treatment. Home
  then exposes a yellow `ROLL AGAIN` primary action. At streak two the reward warns that
  one more double sends the player to Hold. A third double or a double used to leave Hold
  never exposes another roll.
- Extra-roll presentation is local and idempotent. Same-room resync, reconnect, duplicate
  dice events, or State/GameEvent reordering restores the `ROLL AGAIN` state without
  replaying the reward. A room change or turn advance clears the marker.
- `AwaitMoveConfirm`: after the result presentation, the console shows the target tile
  and waits for RFID or the manual `I'M THERE` fallback.
- `AwaitPurchase`: a forced `BUY / AUCTION` decision replaces Home.
- `AwaitDebt`: when confirmed arrival creates rent or tax debt, a non-escapable
  Card Result-style tile-event page first names the tile, creditor, reason, amount,
  and available cash. `CONTINUE` then opens the forced payment modal or the
  non-escapable asset resolver. Card debt keeps its existing Card Result flow and
  the pre-move Hold release fee remains a direct payment.
  `PAYMENT REQUIRED` gives the creditor its own identity row above the amount. A
  player is represented by a deterministic color-and-initials badge plus the full
  roster name; a bank/system debt uses a gold `$` badge and explicit
  `PAY TO / CITY BANK` text. The badge and name share one compact identity card,
  while purpose and cash share a centered, readable metadata row. Static modal updates
  are idempotent: unchanged styles, flags, and hold geometry do not invalidate the RGB
  framebuffer, so only changing countdown text or hold progress is redrawn. This
  presentation does not require avatar assets and does not alter the authoritative
  creditor or payment transaction.
  When cash is insufficient, the authority replaces the payment hold with a forced
  asset resolver that has no Back action. Built properties expose `SELL +$...`; one
  building is sold per non-cancelable hold confirmation for half its building cost,
  and the server enforces even selling across the color group. A group containing any
  building cannot be mortgaged. Once its buildings are cleared, eligible properties can
  be selected and mortgaged in a batch. After every sale or mortgage, the newest
  Authority decides whether the console returns to payment, continues liquidation, or
  exposes bankruptcy. The console never predicts insolvency from local asset values;
  bankruptcy is shown only when the server explicitly allows `DeclareBankruptcy`.
- `AwaitAuction` opens with a 1.8-second lot reveal exactly once per
  `room + generation + asset`. Rendering that introduction lets each required real
  console send generation-bound `AuctionReady`. At 1.8 seconds the same visual stage
  always becomes Live; while the barrier remains closed it shows `READY n / total` with
  disabled controls instead of returning to another welcome page. A same-room reconnect
  or full resync never replays or truncates the introduction and retries Ready only when
  that seat's bit is still absent. Only a new room or auction generation gets a new reveal.
- Once the barrier opens, every console shows the live current bid, leading player, next
  bid, and player currently deciding. The decision player receives `BID / PASS` controls;
  after `PASS`, that console becomes a read-only spectator while continuing to receive
  quotes. Settlement remains visible for 3 seconds with the lot, winner, final price, or
  an explicit no-sale result before the already-authoritative next phase is shown.
- `TurnEnd`: Home remains green and replaces `DICE` with a pulsing `END TURN` action.

Chance and Community Chest use the shared two-stage card protocol. A private
`PlayerCardEvent/Drawn` supplies the exact deck, catalog, signed display amount, target,
and nonzero card instance. The console reveals that result only after the local draw
animation, then sends `CardContinue` with the same instance and waits in a non-interactive
settling state. The server may next open debt or movement state and finally publishes
`PlayerCardEvent/EffectApplied`. Authority schema v3 restores flags `0x03` directly as a
revealed card and flags `0x0F` directly as settling, without replaying the draw animation.
Generic event amounts are never used to infer card identity. The console also retains the
seen and completed `cardInstanceId + drawEventSequence` for the lifetime of the room, so a
duplicate or reordered `Drawn` frame cannot replay the animation during or after settlement;
only a genuinely new card key can start another reveal.

The authority projection carries the original two die values, pending movement, purchase,
debt, auction, all player summaries, and up to 28 asset states. Mortgage and build controls
therefore operate on real owned-asset indices. The server remains authoritative and rejects
a stale or illegal action; presentation events never mutate cash or ownership locally.

Asset Detail shows color-group ownership progress and builds its action grid from the
current authoritative state. `Mortgage` changes to `Redeem` while mortgaged. `Build` appears
only when the complete color group is owned; `Sell Building` appears when that property has
a building; `Trade` appears only for an unmortgaged asset. The final enablement remains
stricter: even building, no group mortgage, available cash, ownership, and the rule that a
traded color group has no buildings are all rechecked by the server. These actions never
reflow: Mortgage/Redeem is top-left, Build top-right, Sell Building bottom-left, and Trade
bottom-right. Hidden actions leave their slot empty, and rotary focus skips that slot.

## Controls

- Rotate: move the shared focus.
- Knob A/B is reversed at the hardware-input boundary for the installed orientation.
- Short press: activate.
- Hold 800 ms: back.
- Hold 3 s: open Demo Lab from Home, or return Home from another page.
- Dangerous modal: hold 1.2 s to confirm.
- Touch: direct selection; focus and page state stay synchronized with the knob.

The stand rotates its mating pattern clockwise by 60 degrees so M4 alignment
pin 3 sits exactly at six o'clock. Firmware rotation remains 0 degrees.

```powershell
.\tools\compile.ps1 -SelfTest
.\tools\compile.ps1
.\tools\upload.ps1
```

`compile.ps1` verifies the Chinese glyph subset and the focus-layout invariant before
invoking Arduino CLI. The home-wheel button centers must remain `159 / 240 / 321`,
and focus animation must refresh LVGL layout before reading the target coordinate.

## Street V3 Artwork

The player console requests the 36-image Grid City catalog from the Raspberry Pi as
128 x 128 RGB565 files. It contains 22 properties, four transit assets, two utilities,
shared Chance and Community Chest covers, two fee covers, and four corner images.
Board IDs select the artwork, so each tile keeps the same identity across the 16, 24,
32, and 40 tile board variants.

Mapped artwork appears as a dedicated 144-pixel Home image beside the cash and location
summary, and as enlarged artwork on the arrival checkpoint, purchase decision, auction
opening, and asset detail screens. As soon as an authoritative dice target is known, the
console prefetches that tile while the dice presentation is still running. A cache miss shows
only an animated loading ring in the artwork frame; it never substitutes unrelated artwork.
The low-priority loader requests `/assets/tiles/<key>.rgb565`; completed images trigger an
automatic visible-page rebuild. Board tiles use 640 KiB of the strict 1 MiB gameplay artwork
budget and retain up to 20 images with LRU eviction. Final player portraits use the remaining
384 KiB. Identity setup additionally opens one bounded 2 MiB transient pool: it downloads all
30 neutral Hair, Face, and Outfit GAVC components plus the current 220 x 300 RGB565 composition,
retains them through Avatar Setup, Name Setup, and Player Ready, then releases the entire pool
when gameplay starts. Displayed pixels are pinned, evicted artwork is downloaded again on demand,
and generated firmware pixel arrays remain zero.

Room setup uses the same low-priority HTTP path without embedding avatar combinations in the
firmware. Before the Focus Stack editor appears, a dedicated `PREPARING AVATAR` page fetches the
current recipe's three neutral layers first, then warms all 30 GAVC components from
`/assets/avatar-components/v1/...` while showing one 0/30 progress bar. After completion, preset
changes are cache-only; changing Hair Color or Skin Tone performs the canonical integer tint
locally and starts no HTTP request. Components are composed in `face -> outfit -> hair` order over
the exact `#061017` page background. A cold return from Name Setup also passes through the same
preparation page. The ready screen requests versioned `128 x 128` final portraits only after the
authority marks them final.
The component format, CRC, color tables, source-over math, routes, and golden vectors are frozen
in `Docs/firmware/avatar-component-protocol.md`.

During `AvatarSetup` and `Countdown`, only an applied `IdentitySnapshot` may advance the transport
applied-state version. A successful `ConfirmAvatar` can first return `AvatarGenerating`; a later
authoritative request-id-zero Identity projection with this player's final-avatar bit set clears
both transport and UI pending state and advances to Name Setup. State/Authority/Roster traffic
cannot acknowledge that Identity transition or leave the editor stuck on `SAVING`.
The name editor captures one or more touch strokes, groups strokes separated by less than
1.2 seconds, recognizes an uppercase block letter locally, and appends it to the private draft.
Pressing, moving, or releasing anywhere during handwriting restarts the complete 1.2-second
idle window; captured strokes remain visible until that uninterrupted idle window expires.
Recognition uses a real quantized neural network trained on the NIST EMNIST Letters split:
the captured strokes are aspect-preserving rasterized to `28 x 28`, then evaluated by a
`784 -> 128 ReLU -> 26` MLP entirely on the ESP32-S3. Its roughly 104 KiB of int8 weights stay
in flash; only the 128-neuron workspace is live in RAM. The exported model reaches 87.04%
accuracy over all 20,800 official test samples. Stroke-aware templates remain only as a
low-confidence fallback for ambiguous logits, including the common `R/K` and `Y/T` pairs.
The official dataset fetch, deterministic trainer, quantization, model hash, and regeneration
commands are documented in `Docs/firmware/player-console-handwriting-neural.md`.
Only `ConfirmName` publishes the final name to the room.

Regenerate and verify the server-delivered assets with:

```powershell
python .\tools\generate-tile-assets.py
python .\tools\verify-tile-assets.py
```
