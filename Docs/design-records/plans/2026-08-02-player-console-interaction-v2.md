# Player Console Interaction V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver and flash a deterministic Gridopoly player-console Demo whose rotary, press, touch, list, payment, dice, and physical-movement interactions match the approved 480 x 480 round-screen specification.

**Architecture:** Keep `AppState` as the authoritative navigation and workflow model, add a transport-neutral command/event boundary with a deterministic `DemoTransport`, and keep all animation progress inside retained LVGL view components. Page changes may rebuild the scene, but same-page focus changes update retained carousel/list objects so selection visibly travels instead of teleporting.

**Tech Stack:** Arduino ESP32-S3, C++17-compatible Arduino core, LVGL 8.4, ESP32 Display Panel, ESP_Knob, Button, PowerShell, Python 3, Arduino CLI

## Global Constraints

- The approved design source is `Docs/design-records/2026-08-02-player-console-interaction-v2-design.md`; where older documents disagree, this V2 design wins.
- This is a player-console Demo, not a local Monopoly rules engine. `DemoTransport` emits authoritative-looking events; future Raspberry Pi logic must be able to replace it without changing UI state transitions.
- Logical and physical display coordinates remain `480 x 480` with center `(240, 240)` and a primary safe radius of `192 px`.
- The stand supplies `60 degrees` clockwise mechanical correction and firmware remains at `0 degrees`; rotation direction is inverted only in `hardware_input.cpp`.
- Home focus motion lasts `220 ms` with ease-out. Center-list motion lasts `180-220 ms` with ease-out.
- Carousel center/adjacent/outer opacity targets are `100% / 34% / 6%`; adjacent and outer scale targets are about `83% / 70%`.
- Dangerous confirmation requires an uninterrupted `1200 ms` hold. Early release clears progress and does not submit.
- Forced payment has a `10 s` authorization window. Rent claiming retains its `20 s` window.
- Dice presentation lasts `1800-2000 ms`; final faces always come from a transport event and are never rerolled locally.
- RFID fallback appears after `12000 ms`; manual arrival requires a `1200 ms` hold, while RFID remains active.
- Correct arrival feedback lasts about `600 ms`; wrong-tile RFID never changes the authoritative player position.
- Normal non-home pages have a fixed, focusable Back button. Forced payment, debt resolution, irreversible submission, dice/movement authority waits, and bankruptcy do not expose Back.
- A bottom button has width at most `176 px`, bottom edge at or above logical `y=408`, and all four corners inside radius `192 px`.
- Adjacent controls have at least `8 px` visual separation. Labels, icons, progress fills, and animated children stay inside their own containers.
- The home carousel has no persistent operation hint. A first-use hint may appear as a top toast for `1500 ms`.
- Target performance is `30 FPS`, sustained at least `24 FPS`, with local input feedback under `80 ms` where hardware permits.
- No new runtime library is introduced. All queues and strings use bounded storage suitable for ESP32-S3 memory.
- Source text remains UTF-8. Every new non-ASCII glyph must pass `tools/verify-ui-glyphs.py`, and generated font files remain checked in.
- Preserve unrelated dirty files. Each commit stages only paths explicitly listed by its task.
- The final production image is compiled and uploaded to `COM4`; self-test images must not be left on the device.

---

## File And Responsibility Map

### Existing files to modify

- `Firmware/PlayerConsole/app_types.h`: authoritative pages, navigation, modal, debt, dice, movement, UI-event, and protocol-facing state structures.
- `Firmware/PlayerConsole/app_state.h`: public state-machine queries and command/event entry points.
- `Firmware/PlayerConsole/app_state.cpp`: navigation, focus, hold, payment, debt, dice, RFID, and timeout transitions.
- `Firmware/PlayerConsole/app_config.h`: exact V2 timing and geometry constants; hardware pins remain unchanged.
- `Firmware/PlayerConsole/demo_data.h`: bounded counts and deterministic scenario declarations.
- `Firmware/PlayerConsole/demo_data.cpp`: eight-asset scrolling fixture, player data, eligibility reasons, and fixed target tiles.
- `Firmware/PlayerConsole/PlayerConsole.ino`: connect input, app command outbox, transport, transport events, ticks, and renderer in one nonblocking loop.
- `Firmware/PlayerConsole/ui_renderer.h`: render lifecycle and typed touch-event polling.
- `Firmware/PlayerConsole/ui_renderer.cpp`: page-scene orchestration only; no component-specific animation math.
- `Firmware/PlayerConsole/logic_tests.cpp`: deterministic state, protocol, geometry, animation, and timeout regression tests.
- `Firmware/PlayerConsole/tools/compile.ps1`: keep glyph/build guards and add the V2 layout guard without changing the board FQBN.
- `Firmware/PlayerConsole/tools/verify-ui-layout.py`: verify the circular safe area, centered footer, carousel symmetry, retained rendering, and non-overlap contracts.
- `Firmware/PlayerConsole/src/fonts/ui_glyphs.txt`: generated glyph manifest.
- `Firmware/PlayerConsole/src/fonts/ui_font_14.c`: generated compact UI font.
- `Firmware/PlayerConsole/src/fonts/ui_font_16.c`: generated primary UI font.
- `Firmware/PlayerConsole/README.md`: current controls, V2 flows, build, test, and upload commands.
- `Docs/firmware/main-controller-protocol.md`: add batch mortgage and manual movement command semantics used by the Demo boundary.
- `Docs/player-console/player-console-demo-scenarios.md`: replace electronic step movement with physical movement/RFID scenarios.
- `Docs/player-console/player-console-acceptance-tests.md`: add retained-motion, fixed-Back, debt list, dice, RFID, and forced-flow tests.

### New files to create

- `Firmware/PlayerConsole/transport_types.h`: fixed-size command/event envelopes shared by app state and transports.
- `Firmware/PlayerConsole/player_console_transport.h`: transport interface used by `PlayerConsole.ino`.
- `Firmware/PlayerConsole/demo_transport.h`: deterministic transport class and Demo scenario controls.
- `Firmware/PlayerConsole/demo_transport.cpp`: scheduled command responses, idempotency, payment/debt, roll, mortgage, and RFID fixtures.
- `Firmware/PlayerConsole/ui_layout.h`: named round-safe rectangles and pure geometry checks.
- `Firmware/PlayerConsole/ui_motion.h`: pure carousel, list, and dice pose declarations.
- `Firmware/PlayerConsole/ui_motion.cpp`: integer/deterministic pose calculations shared by tests and views.
- `Firmware/PlayerConsole/ui_primitives.h`: palette, object helpers, encoded UI events, and touch binding declarations.
- `Firmware/PlayerConsole/ui_primitives.cpp`: LVGL box/label/event helpers with no page policy.
- `Firmware/PlayerConsole/ui_carousel.h`: retained home-carousel handle and update API.
- `Firmware/PlayerConsole/ui_carousel.cpp`: five reusable item objects and interrupted 220 ms focus animation.
- `Firmware/PlayerConsole/ui_center_list.h`: retained center-lock list, progress, count, and footer API.
- `Firmware/PlayerConsole/ui_center_list.cpp`: track movement, selected frame, touch/swipe, and fixed footer rendering.
- `Firmware/PlayerConsole/ui_modal.h`: top-level modal handle and dynamic update API.
- `Firmware/PlayerConsole/ui_modal.cpp`: dim layer, confirm/cancel focus, countdown, and hold fill.
- `Firmware/PlayerConsole/ui_dice_stage.h`: vector dice and movement-wait scene API.
- `Firmware/PlayerConsole/ui_dice_stage.cpp`: two-die animation, final reveal, target, RFID, and manual fallback visuals.
- `Firmware/PlayerConsole/tools/run-selftest.ps1`: compile, upload, reconnect, capture, and assert the hardware self-test marker.

### Files deliberately not changed

- `Firmware/PlayerConsole/hardware_input.cpp`: its existing left/right inversion remains the single direction correction boundary.
- `Firmware/PlayerConsole/lvgl_v8_port.*`: panel, framebuffer, touch, and tearing setup remain hardware infrastructure.
- Raspberry Pi, grid-tile LED, and real RFID drivers: V2 exercises only the shared semantics through `DemoTransport`.

---

### Task 1: Track The Existing Firmware Baseline And Automate Device Self-Tests

**Files:**
- Modify: `.gitignore`
- Create: `Firmware/PlayerConsole/tools/run-selftest.ps1`
- Track: `Firmware/PlayerConsole/PlayerConsole.ino`
- Track: `Firmware/PlayerConsole/*.h`
- Track: `Firmware/PlayerConsole/*.cpp`
- Track: `Firmware/PlayerConsole/tools/*`
- Track: `Firmware/PlayerConsole/src/fonts/*`
- Track: `Firmware/PlayerConsole/src/assets/*`
- Track: `Firmware/PlayerConsole/assets/*`

**Interfaces:**
- Consumes: existing `compile.ps1 -SelfTest`, `upload.ps1 -Port COM4`, and the `SELFTEST PASS` / `SELFTEST FAILED` markers emitted by `PlayerConsole.ino`.
- Produces: `run-selftest.ps1 -Port COM4 -TimeoutSeconds 35` returning exit code `0` only after seeing `SELFTEST PASS`.

- [ ] **Step 1: Add a failing invocation that proves no runner exists yet**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4
```

Expected: FAIL because `run-selftest.ps1` does not exist.

- [ ] **Step 2: Ignore generated build artifacts before tracking the baseline**

Add exactly this entry to `.gitignore`:

```gitignore
/Firmware/PlayerConsole/build/
```

Then verify:

```powershell
git check-ignore -v Firmware\PlayerConsole\build\PlayerConsole.ino.bin
```

Expected: the new `.gitignore` rule is printed.

- [ ] **Step 3: Implement the bounded serial self-test runner**

Create `tools/run-selftest.ps1` with this public contract and sequence:

```powershell
[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
& powershell -ExecutionPolicy Bypass -File (Join-Path $tools 'compile.ps1') -SelfTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& powershell -ExecutionPolicy Bypass -File (Join-Path $tools 'upload.ps1') -Port $Port
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$captured = [Text.StringBuilder]::new()
$serial = $null
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -eq $serial) {
            try {
                $serial = [IO.Ports.SerialPort]::new(
                    $Port,
                    115200,
                    [IO.Ports.Parity]::None,
                    8,
                    [IO.Ports.StopBits]::One
                )
                $serial.ReadTimeout = 250
                $serial.DtrEnable = $false
                $serial.RtsEnable = $false
                $serial.Open()
            } catch {
                if ($null -ne $serial) { $serial.Dispose() }
                $serial = $null
                Start-Sleep -Milliseconds 250
                continue
            }
        }

        try {
            $chunk = $serial.ReadExisting()
            if ($chunk.Length -eq 0) {
                Start-Sleep -Milliseconds 50
                continue
            }
            Write-Host -NoNewline $chunk
            [void]$captured.Append($chunk)
            if ($captured.ToString().Contains('SELFTEST FAILED')) { exit 1 }
            if ($captured.ToString().Contains('SELFTEST PASS')) { exit 0 }
        } catch {
            $serial.Close()
            $serial.Dispose()
            $serial = $null
            Start-Sleep -Milliseconds 250
        }
    }
    Write-Error "Timed out waiting for a self-test marker on $Port."
    exit 1
} finally {
    if ($null -ne $serial) {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}
```

Keep the absolute timeout and `finally` cleanup exactly as shown so a failed device run cannot leave COM4 locked.

- [ ] **Step 4: Run the existing self-tests through the new runner**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4 -TimeoutSeconds 35
```

Expected: compile and upload succeed; captured output ends in `SELFTEST PASS`.

- [ ] **Step 5: Track only the player-console baseline and test runner**

Run:

```powershell
git add .gitignore Firmware/PlayerConsole
git status --short
```

Expected: `Firmware/PlayerConsole/build/` is absent from the staged list; unrelated `Docs`, `PCB Files`, `Assets`, and `Mechanical` changes remain unstaged.

- [ ] **Step 6: Commit the reproducible baseline**

```powershell
git commit -m "chore: track player console firmware baseline"
```

---

### Task 2: Add The Transport-Neutral Demo Protocol Boundary

**Files:**
- Create: `Firmware/PlayerConsole/transport_types.h`
- Create: `Firmware/PlayerConsole/player_console_transport.h`
- Create: `Firmware/PlayerConsole/demo_transport.h`
- Create: `Firmware/PlayerConsole/demo_transport.cpp`
- Modify: `Firmware/PlayerConsole/app_types.h`
- Modify: `Firmware/PlayerConsole/demo_data.h`
- Modify: `Firmware/PlayerConsole/demo_data.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: fixed demo player/asset data and monotonic `nowMs` supplied by the main loop.
- Produces: `PlayerConsoleTransport::begin`, `send`, `tick`, and `poll`; `DemoTransport::setScenario`; fixed-size `TransportCommand` and `TransportEvent` values.

Define the transport types exactly as follows, adding only payload fields required by the listed event kinds:

```cpp
enum class TransportCommandKind : uint8_t {
    RollRequest,
    PayNow,
    ClaimRent,
    TradeCreate,
    MortgageBatchRequest,
    MoveManualConfirmRequest,
};

enum class TransportEventKind : uint8_t {
    None,
    ConnectionLost,
    StateSnapshotApplied,
    RollResult,
    MoveGuidanceStarted,
    RfidPositionConfirmed,
    RfidPositionRejected,
    PaymentRequired,
    PaymentCompleted,
    DebtResolutionRequired,
    MortgageBatchCompleted,
    BankruptcyResolved,
    CommandRejected,
};

enum class TransportError : uint8_t {
    None,
    InsufficientCash,
    AssetChanged,
    StaleState,
    ActionNotAllowed,
};

struct TransportCommand {
    TransportCommandKind kind{};
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t transactionId = 0;
    uint32_t clientTimeMs = 0;
    uint32_t assetMask = 0;
    uint8_t targetPosition = 0;
};

struct TransportEvent {
    TransportEventKind kind = TransportEventKind::None;
    TransportError error = TransportError::None;
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t transactionId = 0;
    uint32_t deadlineMs = 0;
    int32_t amount = 0;
    int32_t cash = 0;
    uint32_t assetMask = 0;
    uint8_t dieA = 0;
    uint8_t dieB = 0;
    uint8_t playerPosition = 0;
    uint8_t targetPosition = 0;
    uint8_t observedPosition = 0;
    const char *targetName = "";
    bool manual = false;
};
```

Use a 32-bit monotonic local `requestId`. A future WebSocket serializer combines it with `device_id`; the app never compares display strings. `clientTimeMs` and `targetPosition` carry the manual-movement audit fields; device/player identity comes from the bound transport session.

Define the complete Demo scenario selector in `demo_transport.h`:

```cpp
enum class DemoScenario : uint8_t {
    Waiting,
    NextTurn,
    MyTurn,
    RentClaim,
    PaymentCash,
    PaymentDebt,
    DebtMortgage,
    DebtBankruptcy,
    RollRfidSuccess,
    RollRfidWrongThenSuccess,
    RollRfidTimeout,
    RollAuthorityDelayed,
    ConnectionDropAndRecover,
};
```

- [ ] **Step 1: Write failing deterministic transport tests**

Add these cases to `runLogicTests`:

```cpp
DemoTransport transport;
transport.begin(0);
TransportCommand roll{TransportCommandKind::RollRequest, 7, 41, 0, 0};
ok &= expect(out, transport.send(roll, 10), "demo accepts roll request");
TransportEvent event{};
transport.tick(129);
ok &= expect(out, !transport.poll(event), "roll result is not emitted early");
transport.tick(130);
ok &= expect(out, transport.poll(event) &&
                  event.kind == TransportEventKind::RollResult &&
                  event.requestId == 7 && event.dieA == 3 && event.dieB == 4,
             "demo emits deterministic authoritative dice");
transport.tick(160);
ok &= expect(out, transport.poll(event) &&
                  event.kind == TransportEventKind::MoveGuidanceStarted &&
                  event.targetPosition == 24,
             "demo starts physical movement guidance");
```

Add a second case sending the same request ID twice and assert that the result remains `3 + 4`, never advances the fixed sequence, and never creates a second transaction.

- [ ] **Step 2: Run the red test**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4
```

Expected: FAIL during compile because `DemoTransport` and transport types are undefined.

- [ ] **Step 3: Implement the fixed-capacity transport interface**

Create:

```cpp
class PlayerConsoleTransport {
public:
    virtual ~PlayerConsoleTransport() = default;
    virtual void begin(uint32_t nowMs) = 0;
    virtual bool send(const TransportCommand &command, uint32_t nowMs) = 0;
    virtual void tick(uint32_t nowMs) = 0;
    virtual bool poll(TransportEvent &event) = 0;
};
```

`DemoTransport` uses an eight-entry scheduled-event array and an eight-entry output ring. `send` must:

- schedule `RollResult` at `nowMs + 120` and `MoveGuidanceStarted` at `nowMs + 150`;
- preserve the result for duplicate `requestId` values;
- reject a second different roll while one is unresolved with `ActionNotAllowed`;
- schedule forced payment, debt, mortgage, bankruptcy, and RFID outcomes selected by `DemoScenario`;
- never call `AppState` or LVGL functions.

The connection-drop scenario emits `ConnectionLost`, waits `3000 ms`, then emits `StateSnapshotApplied` with the authoritative cash, position, and state version. It does not replay a dangerous command queued while offline.

- [ ] **Step 4: Add eight scrollable demo assets and eligibility data**

Extend `AssetData` with exactly `id`, `mortgageValue`, `buildingLevel`, and `ruleLocked` fields in addition to the existing name/group/value/rent/mortgaged fields. The eight rows must include:

```text
霓虹港湾      eligible, mortgage $130
光栅公寓      eligible, mortgage $150
数据高地      eligible, mortgage $170
云轨总站      already mortgaged
量子电网      eligible, mortgage $75
天穹广场      has buildings
旧城芯廊      eligible, mortgage $120
极光码头      rule locked
```

Keep values as integer dollars and expose a stable asset ID, mortgage value, building level, mortgaged flag, and rule-locked flag.

- [ ] **Step 5: Run the green transport test**

Run the self-test runner again.

Expected: `SELFTEST PASS`, including deterministic dice, idempotent request, and scheduled guidance cases.

- [ ] **Step 6: Commit the transport boundary**

```powershell
git add Firmware/PlayerConsole/transport_types.h Firmware/PlayerConsole/player_console_transport.h Firmware/PlayerConsole/demo_transport.h Firmware/PlayerConsole/demo_transport.cpp Firmware/PlayerConsole/demo_data.h Firmware/PlayerConsole/demo_data.cpp Firmware/PlayerConsole/app_types.h Firmware/PlayerConsole/logic_tests.cpp
git commit -m "feat: add deterministic player console transport"
```

---

### Task 3: Replace Ad Hoc Back Behavior With A Navigation Stack And Explicit Footer Focus

**Files:**
- Modify: `Firmware/PlayerConsole/app_types.h`
- Modify: `Firmware/PlayerConsole/app_state.h`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: `ScreenPage`, `HomePhase`, hardware `InputEvent`, and typed `UiEvent` values.
- Produces: `appCanNavigateBack`, `appPageContentCount`, `appFocusCount`, `appFocusIsFooter`, `appHandleUiEvent`, and deterministic parent-focus restoration.

Use these public navigation types and queries:

```cpp
struct NavigationEntry {
    ScreenPage page = ScreenPage::Home;
    uint8_t focus = 0;
    uint8_t listAnchor = 0;
};

struct NavigationState {
    NavigationEntry current{};
    NavigationEntry stack[4]{};
    uint8_t depth = 0;
};

enum class UiEventKind : uint8_t {
    ActivateFocused,
    SelectHomeAction,
    SelectListItem,
    Back,
    HoldDown,
    HoldUp,
    ListPrevious,
    ListNext,
};

struct UiEvent {
    UiEventKind kind = UiEventKind::ActivateFocused;
    int16_t value = 0;
};

bool appCanNavigateBack(const AppState &state);
uint8_t appPageContentCount(const AppState &state);
uint8_t appFocusCount(const AppState &state);
bool appFocusIsFooter(const AppState &state);
void appHandleUiEvent(AppState &state, const UiEvent &event, uint32_t nowMs);
```

Before adding feature cases, replace the existing generic `press` helper in `logic_tests.cpp` with this shared helper and use it in Tasks 3-11:

```cpp
void shortPress(AppState &state, uint32_t downMs, uint32_t upMs)
{
    appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, downMs}, downMs);
    appTick(state, upMs);
    appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, upMs}, upMs);
}
```

`ScreenPage` must include `DiceStage`, `MoveGuide`, `DebtAssets`, and `Bankruptcy`. Only ordinary pages push/pop navigation entries. Authority-wait and forced pages replace the current page without creating an escapable stack entry.

- [ ] **Step 1: Write failing navigation and footer tests**

Add tests that:

```cpp
appInit(state, 0);
state.homePhase = HomePhase::Waiting;
appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 10}, 10);
appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 100}, 100);
ok &= expect(out, state.nav.current.page == ScreenPage::Assets,
             "home asset action opens assets");
ok &= expect(out, appFocusCount(state) == kAssetCount + 1,
             "assets include fixed back footer");

state.nav.current.focus = kAssetCount;
appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 200}, 200);
appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 300}, 300);
ok &= expect(out, state.nav.current.page == ScreenPage::Home &&
                  state.nav.current.focus == 0,
             "footer back restores home asset focus");
```

Also cover AssetDetail -> Assets, PlayerDetail -> Players, Trade -> Home Trade, More -> Home More, and DemoLab -> Home More. Set each forced page and assert `!appCanNavigateBack(state)` and that 800 ms / 3 s holds do not change the page.

- [ ] **Step 2: Run the red navigation test**

Expected: compile or self-test FAIL because `NavigationState` and footer-aware functions do not exist.

- [ ] **Step 3: Implement stack navigation and page-specific list anchors**

Store these anchors in `AppState`:

```cpp
uint8_t assetListIndex = 0;
uint8_t playerListIndex = 0;
uint8_t demoListIndex = 0;
uint8_t debtListIndex = 0;
```

Rules:

- home carousel wraps cyclically;
- ordinary lists clamp between first item and fixed footer;
- moving to footer leaves the last selected list anchor unchanged;
- Back pops exactly one entry and restores both focus and list anchor;
- touching a nonfocused row selects it; touching the already focused row activates it;
- the 800 ms hold remains an optional ordinary-page shortcut, but forced/authority pages ignore both 800 ms and 3 s exits;
- the Home 3 s gesture still enters Demo Lab.

- [ ] **Step 4: Run the green navigation test**

Expected: every parent restoration and forced-page lock test prints PASS.

- [ ] **Step 5: Commit navigation behavior**

```powershell
git add Firmware/PlayerConsole/app_types.h Firmware/PlayerConsole/app_state.h Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/logic_tests.cpp
git commit -m "feat: add explicit player console navigation"
```

---

### Task 4: Implement Modal Focus, Commands, Forced Payment, And Authority Events

**Files:**
- Modify: `Firmware/PlayerConsole/app_types.h`
- Modify: `Firmware/PlayerConsole/app_state.h`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/PlayerConsole.ino`
- Modify: `Firmware/PlayerConsole/demo_transport.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: `TransportCommand`, `TransportEvent`, modal hold inputs, and absolute deadlines.
- Produces: app command outbox, voluntary modal cancellation, forced-payment states, and transport-driven money/version changes.

Add these exact public functions:

```cpp
bool appPollCommand(AppState &state, TransportCommand &command);
void appHandleTransportEvent(AppState &state, const TransportEvent &event, uint32_t nowMs);
uint16_t appHoldProgressPermille(const AppState &state, uint32_t nowMs);
uint32_t appModalRemainingMs(const AppState &state, uint32_t nowMs);
```

Add `bool authorityOnline = true` to `AppState`. `ConnectionLost` sets it false and prevents Roll, PayNow, ClaimRent, TradeCreate, MortgageBatchRequest, and MoveManualConfirmRequest from entering the command outbox. `StateSnapshotApplied` applies its money/position/version fields, clears stale submitting flags, then sets it true.

Define modal policy explicitly:

```cpp
enum class ModalKind : uint8_t {
    None,
    CollectRent,
    ForcedPayment,
    VoluntaryMortgage,
    TradeCreate,
    DebtMortgageConfirm,
};

enum class ModalFocus : uint8_t { Confirm, Cancel, ResolveAssets };

struct ModalState {
    ModalKind kind = ModalKind::None;
    ModalFocus focus = ModalFocus::Confirm;
    bool cancelAllowed = false;
    bool holding = false;
    bool submitting = false;
    bool insufficient = false;
    uint32_t transactionId = 0;
    uint32_t deadlineMs = 0;
    uint32_t holdStartMs = 0;
    int32_t amount = 0;
    const char *title = "";
    const char *counterparty = "";
    const char *purpose = "";
};
```

Add these exact private fixtures to `logic_tests.cpp`:

```cpp
void openVoluntaryMortgageFixture(AppState &state, uint32_t nowMs)
{
    appInit(state, nowMs);
    state.selectedAsset = 0;
    state.nav.current = NavigationEntry{ScreenPage::AssetDetail, 0, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}

void openTradeFixture(AppState &state, uint32_t nowMs)
{
    appInit(state, nowMs);
    state.nav.current = NavigationEntry{ScreenPage::Trade, 2, 0};
    shortPress(state, nowMs + 10, nowMs + 100);
}
```

- [ ] **Step 1: Write failing modal and authority tests**

Cover all of these assertions:

```cpp
// Voluntary operation: Cancel is focusable and restores source focus.
openVoluntaryMortgageFixture(state, 1000);
appHandleInput(state, InputEvent{InputKind::Rotate, 1, 1110}, 1110);
shortPress(state, 1120, 1200);
ok &= expect(out, state.modal.kind == ModalKind::None &&
                  state.nav.current.page == ScreenPage::AssetDetail,
             "voluntary modal cancels to source");

// Early release clears progress but leaves the modal open.
openTradeFixture(state, 2000);
appHandleInput(state, InputEvent{InputKind::ButtonDown, 0, 2200}, 2200);
appTick(state, 2799);
appHandleInput(state, InputEvent{InputKind::ButtonUp, 0, 2799}, 2799);
ok &= expect(out, state.modal.kind == ModalKind::TradeCreate &&
                  appHoldProgressPermille(state, 2799) == 0,
             "early modal release resets without closing");

// Forced payment cannot cancel and does not mutate cash until an event arrives.
TransportEvent payment{TransportEventKind::PaymentRequired};
payment.transactionId = 90;
payment.amount = 680;
payment.cash = 240;
payment.deadlineMs = 13000;
appHandleTransportEvent(state, payment, 3000);
ok &= expect(out, state.modal.kind == ModalKind::ForcedPayment &&
                  !state.modal.cancelAllowed,
             "forced payment is not cancelable");
```

Hold for 1200 ms, poll one `PayNow` command, inject `CommandRejected/InsufficientCash`, and assert the modal becomes red with `ResolveAssets`. Verify `appTick` no longer decrements cash locally and that an injected `PaymentCompleted` event performs the only balance change.

Inject `ConnectionLost`, attempt a dangerous action, and assert no command is queued. Inject `StateSnapshotApplied` with version `9`, money `$1775`, and position `17`; assert the snapshot replaces local authority fields and actions unlock only afterward.

- [ ] **Step 2: Run the red modal test**

Expected: FAIL because modal focus, command outbox, and transport-event handling are absent.

- [ ] **Step 3: Implement a four-entry command outbox and idempotent pending request**

Use bounded storage in `AppState`:

```cpp
TransportCommand commandQueue[4]{};
uint8_t commandHead = 0;
uint8_t commandTail = 0;
uint8_t commandCount = 0;
uint32_t nextRequestId = 1;
uint32_t stateVersion = 1;
```

An action queues at most one command, sets `submitting`, and ignores repeat activation until a matching event or rejection arrives. Applying an event with lower `stateVersion` is a no-op; a higher version updates `stateVersion` exactly once.

For `PaymentCash`, `DemoTransport` emits `PaymentCompleted` at the earlier of a valid PayNow response (`nowMs + 120`) or the absolute 10 s deadline. For `PaymentDebt`, PayNow emits `CommandRejected/InsufficientCash` followed by `DebtResolutionRequired`; the app pauses the displayed deadline and never subtracts cash itself. Preserve the existing 20 s rent behavior: a confirmed ClaimRent is transport-driven and an unclaimed window expires without changing money.

- [ ] **Step 4: Wire transport polling into the nonblocking main loop**

Use this order in `loop()`:

```cpp
InputEvent input{};
UiEvent uiEvent{};
TransportCommand command{};
TransportEvent transportEvent{};
while (hardwareInputPoll(input)) appHandleInput(app, input, nowMs);
while (uiRendererPollTouch(uiEvent)) appHandleUiEvent(app, uiEvent, nowMs);
while (appPollCommand(app, command)) transport.send(command, nowMs);
transport.tick(nowMs);
while (transport.poll(transportEvent)) appHandleTransportEvent(app, transportEvent, nowMs);
appTick(app, nowMs);
uiRendererRender(app, nowMs);
```

All LVGL calls remain under the existing LVGL lock; `transport.tick` and state logic must not call LVGL.

- [ ] **Step 5: Run the green modal test**

Expected: voluntary cancellation, early release, forced no-cancel, single PayNow, insufficient cash, and event-only cash mutation all PASS.

- [ ] **Step 6: Commit modal and authority state logic**

```powershell
git add Firmware/PlayerConsole/app_types.h Firmware/PlayerConsole/app_state.h Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/PlayerConsole.ino Firmware/PlayerConsole/demo_transport.cpp Firmware/PlayerConsole/logic_tests.cpp
git commit -m "feat: model player console payment authority"
```

---

### Task 5: Implement Forced Debt Multi-Select And Bankruptcy Branches

**Files:**
- Modify: `Firmware/PlayerConsole/app_types.h`
- Modify: `Firmware/PlayerConsole/app_state.h`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/demo_transport.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: `DebtResolutionRequired`, asset eligibility, fixed footer activation, and `MortgageBatchCompleted`.
- Produces: selected asset mask, live proceeds/balance queries, guarded batch command, fresh payment window, and bankruptcy terminal state.

Add these queries:

```cpp
bool appDebtAssetEligible(const AppState &state, uint8_t assetIndex);
bool appDebtAssetSelected(const AppState &state, uint8_t assetIndex);
int32_t appDebtShortfall(const AppState &state);
int32_t appDebtSelectedProceeds(const AppState &state);
int32_t appDebtPostMortgageBalance(const AppState &state);
bool appDebtCanConfirm(const AppState &state);
```

The debt state owns `transactionId`, `amountDue`, `cashBefore`, `selectedMask`, and `eligibleMask`; it never mutates the shared asset fixtures.

Add these exact private fixtures to `logic_tests.cpp`:

```cpp
void openDebtFixture(AppState &state, int32_t amountDue, int32_t cash,
                     uint32_t eligibleMask)
{
    appInit(state, 0);
    TransportEvent event{};
    event.kind = TransportEventKind::DebtResolutionRequired;
    event.transactionId = 90;
    event.stateVersion = 2;
    event.amount = amountDue;
    event.cash = cash;
    event.assetMask = eligibleMask;
    appHandleTransportEvent(state, event, 0);
}

void focusDebtAsset(AppState &state, uint8_t index)
{
    state.nav.current.focus = index;
    state.debtListIndex = index;
}
```

- [ ] **Step 1: Write failing debt-selection tests**

Use the eight-asset fixture and assert:

```cpp
openDebtFixture(state, /*amountDue=*/680, /*cash=*/240, /*eligibleMask=*/0x55);
ok &= expect(out, appDebtShortfall(state) == 440,
             "debt computes exact shortfall");

focusDebtAsset(state, 0);
shortPress(state, 10, 100);
focusDebtAsset(state, 2);
shortPress(state, 110, 200);
ok &= expect(out, appDebtSelectedProceeds(state) == 300 &&
                  !appDebtCanConfirm(state),
             "debt sums multiple mortgages below threshold");

focusDebtAsset(state, 4);
shortPress(state, 210, 300);
ok &= expect(out, appDebtSelectedProceeds(state) == 375 &&
                  appDebtPostMortgageBalance(state) == 615,
             "debt preview updates after every toggle");
```

Select asset 6 after the shown assertions so proceeds become `$495` and cross the `$440` shortfall. Assert already mortgaged, building, and rule-locked rows cannot be toggled. Assert the footer does nothing below the threshold, opens `DebtMortgageConfirm` at/above it, Cancel returns only to `DebtAssets`, and a 1200 ms hold queues exactly one `MortgageBatchRequest`.

Inject `MortgageBatchCompleted`; assert the original `ForcedPayment` returns with a new full `10000 ms` deadline. Inject a fixture whose total legal mortgage value is below the shortfall; assert it goes directly to `Bankruptcy` with no Back.

- [ ] **Step 2: Run the red debt test**

Expected: FAIL because eligibility, selection, and bankruptcy transitions are missing.

- [ ] **Step 3: Implement selection and exact integer calculations**

Use these formulas:

```cpp
shortfall = max(0, amountDue - cashBefore);
selectedProceeds = sum(mortgageValue for every selected eligible asset);
postMortgageBalance = cashBefore + selectedProceeds;
canConfirm = selectedProceeds >= shortfall;
```

When refreshed asset data invalidates a selected bit, clear it, recalculate immediately, and set the toast to `资产状态已变化，请重新选择`.

- [ ] **Step 4: Implement accepted batch and bankruptcy outcomes in DemoTransport**

For the normal debt scenario, emit `MortgageBatchCompleted` after `150 ms` with the same transaction ID and selected mask. For the bankruptcy fixture, emit `BankruptcyResolved` and leave the app in a read-only terminal state.

- [ ] **Step 5: Run the green debt test**

Expected: all selection, disabled-row, confirmation, refreshed deadline, and terminal bankruptcy tests PASS.

- [ ] **Step 6: Commit debt workflow logic**

```powershell
git add Firmware/PlayerConsole/app_types.h Firmware/PlayerConsole/app_state.h Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/demo_transport.cpp Firmware/PlayerConsole/logic_tests.cpp
git commit -m "feat: add forced debt resolution workflow"
```

---

### Task 6: Add Round-Safe Layout, Motion Math, And Reusable LVGL Primitives

**Files:**
- Create: `Firmware/PlayerConsole/ui_layout.h`
- Create: `Firmware/PlayerConsole/ui_motion.h`
- Create: `Firmware/PlayerConsole/ui_motion.cpp`
- Create: `Firmware/PlayerConsole/ui_primitives.h`
- Create: `Firmware/PlayerConsole/ui_primitives.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/tools/verify-ui-layout.py`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: LVGL objects and typed `UiEvent` values.
- Produces: one palette/helper layer, named layout rectangles, event bindings, and pure motion targets used by all retained widgets.

Define these pure types:

```cpp
struct UiRect { int16_t x, y, w, h; };
struct CarouselPose { int16_t centerX; uint16_t zoom; uint8_t opacity; };
struct DicePose { int16_t x, y; int16_t angleTenths; uint16_t zoom; uint8_t face; };

constexpr bool uiRectInsideCircle(UiRect rect, int16_t radius = 192);
CarouselPose uiCarouselPose(int8_t relativeSlot);
int16_t uiCenterListTrackY(uint8_t selectedIndex, int16_t selectedCenterY,
                           int16_t rowStride);
uint16_t uiListProgressPermille(uint8_t selectedIndex, uint8_t count);
DicePose uiDicePose(uint32_t elapsedMs, uint8_t dieIndex, uint8_t finalFace);
uint8_t uiMoneyFontPx(int32_t amount);
```

Named V2 rectangles must include:

```cpp
constexpr UiRect kNormalListViewport{78, 128, 324, 184};
constexpr UiRect kNormalListFocus{104, 195, 272, 50};
constexpr UiRect kNormalFooter{152, 352, 176, 56};
constexpr UiRect kModalRect{104, 106, 272, 268};
constexpr UiRect kModalConfirm{128, 256, 224, 64};
constexpr UiRect kModalCancel{152, 328, 176, 40};
```

The modal child coordinates may be parent-relative in rendering, but the verifier evaluates their absolute rectangles.

- [ ] **Step 1: Write failing pure geometry and motion tests**

Add:

```cpp
ok &= expect(out, uiRectInsideCircle(kNormalFooter),
             "footer stays inside round safe area");
ok &= expect(out, uiRectInsideCircle(kModalRect),
             "modal corners stay inside round safe area");
ok &= expect(out, uiCarouselPose(0).centerX == 240 &&
                  uiCarouselPose(0).opacity == 255 &&
                  uiCarouselPose(1).opacity == 87 &&
                  uiCarouselPose(2).opacity == 15,
             "carousel uses approved opacity hierarchy");
ok &= expect(out, uiCenterListTrackY(3, 220, 58) == 46,
             "center list track locks selected row");
ok &= expect(out, uiListProgressPermille(0, 8) == 0 &&
                  uiListProgressPermille(7, 8) == 1000,
             "list progress spans complete item range");
ok &= expect(out, uiMoneyFontPx(1860) == 40 &&
                  uiMoneyFontPx(12345678) == 24,
             "large balances select a round-safe font size");
```

Test `uiDicePose(1900, die, finalFace)` returns the final face and settled pose for both dice.

- [ ] **Step 2: Run the red geometry test**

Expected: compile FAIL because layout and motion types do not exist.

- [ ] **Step 3: Implement integer-safe geometry and motion functions**

`uiRectInsideCircle` tests all four rectangle corners with squared integer distance. `uiCarouselPose` returns exact targets for relative slots `-2..2`; outside slots use opacity `0`. `uiDicePose` is deterministic and piecewise across `0-150`, `150-1050`, `1050-1500`, and `1500-1900 ms` and never chooses the final face itself. `uiMoneyFontPx` returns `40` through six display characters, `32` through nine, and `24` above nine, counting sign and separators.

- [ ] **Step 4: Move shared LVGL helpers out of the renderer**

`ui_primitives` owns:

```cpp
using UiEventSink = void (*)(const UiEvent &event);
void uiSetEventSink(UiEventSink sink);
lv_obj_t *uiBox(lv_obj_t *parent, UiRect rect, uint32_t bg, uint32_t border,
                uint8_t radius);
lv_obj_t *uiLabel(lv_obj_t *parent, const char *text, UiRect rect,
                  const lv_font_t *font, uint32_t color,
                  lv_text_align_t align = LV_TEXT_ALIGN_CENTER);
void uiBindTap(lv_obj_t *object, UiEventKind kind, int16_t value = 0);
void uiBindHold(lv_obj_t *object);
```

Encode event kind/value in `intptr_t` user data; do not heap-allocate event bindings. Labels use `LV_LABEL_LONG_DOT` and never receive click events.

- [ ] **Step 5: Expand the Python layout guard**

Make `verify-ui-layout.py` read `ui_layout.h` as UTF-8 and fail unless:

- named footer and modal rectangles are found;
- all four corners are within radius `192`;
- footer center is exactly `x=240`, width is at most `176`, and bottom is at most `408`;
- list focus, progress, count, and footer share center `x=240`;
- modal confirm/cancel have at least `8 px` vertical separation;
- `ui_renderer.cpp` does not call `lv_obj_clean(root)` in the same-page focus-update branch.

- [ ] **Step 6: Run green self-tests and compile guards**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\compile.ps1
```

Expected: geometry/motion tests PASS; `UI LAYOUT CHECK PASS`, `UI GLYPH CHECK PASS`, and build-output check PASS.

- [ ] **Step 7: Commit the UI foundation**

```powershell
git add Firmware/PlayerConsole/ui_layout.h Firmware/PlayerConsole/ui_motion.h Firmware/PlayerConsole/ui_motion.cpp Firmware/PlayerConsole/ui_primitives.h Firmware/PlayerConsole/ui_primitives.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/tools/verify-ui-layout.py Firmware/PlayerConsole/logic_tests.cpp
git commit -m "refactor: add round screen ui foundations"
```

---

### Task 7: Build The Retained Horizontal Home Carousel

**Files:**
- Create: `Firmware/PlayerConsole/ui_carousel.h`
- Create: `Firmware/PlayerConsole/ui_carousel.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`
- Modify: `Firmware/PlayerConsole/tools/verify-ui-layout.py`

**Interfaces:**
- Consumes: `HomeAction` sequence, current focus, `CarouselPose`, and `UiEventSink`.
- Produces: five retained action objects, interruptible 220 ms transitions, special Dice focus, and first-use-only toast.

Expose:

```cpp
struct UiCarousel {
    lv_obj_t *container = nullptr;
    lv_obj_t *items[5]{};
    lv_obj_t *labels[5]{};
    uint8_t count = 0;
    uint8_t selected = 0;
};

void uiCarouselCreate(UiCarousel &carousel, lv_obj_t *parent,
                      const HomeAction *actions, uint8_t count, uint8_t selected);
void uiCarouselSetSelection(UiCarousel &carousel, uint8_t selected,
                            bool animate);
void uiCarouselDestroy(UiCarousel &carousel);
```

- [ ] **Step 1: Write failing home-action tests**

Add tests that waiting/next phases expose exactly `Assets, Players, Trade, More`, while MyTurn exposes exactly `Dice, Assets, Players, Trade, More`. Assert the initial toast expires at `1500 ms` and no persistent hint string remains in the renderer source.

- [ ] **Step 2: Run the red carousel test**

Expected: FAIL because the new home action queries/component do not exist.

- [ ] **Step 3: Create all carousel objects once per Home scene**

Use fixed vertical bands:

```text
phase/title     y=66..110
cash label      y=132..154
cash amount     y=158..206
location        y=226..254
divider         y=270
carousel        y=300..374
```

The center action is fully inside the safe circle. Adjacent and outer items clip inside the carousel container, not the screen root. Every item keeps icon/text inside its own box.

- [ ] **Step 4: Animate every visible item from its current pose to the new target**

On selection changes:

- read each object's current `x`, zoom, and opacity;
- delete only that object's previous property animations;
- animate all three properties to `uiCarouselPose(relativeSlot)` over `220 ms` ease-out;
- if a new rotary delta arrives mid-animation, restart from current visual values toward the newest legal focus;
- never call `lv_obj_clean` or recreate item objects for a same-page focus change.

Dice uses yellow fill/border, a double outline, and a small opacity pulse only when centered. Other actions use signal cyan. No bounce-only focus effect remains.

- [ ] **Step 5: Implement touch synchronization**

Tapping a side item only moves it to center. Tapping the already centered item activates it. A knob short press activates the centered item immediately.

- [ ] **Step 6: Run self-test and static retained-scene guard**

Expected: action order and toast tests PASS; layout guard confirms the same-page focus path calls `uiCarouselSetSelection` without scene rebuild.

- [ ] **Step 7: Commit the retained carousel**

```powershell
git add Firmware/PlayerConsole/ui_carousel.h Firmware/PlayerConsole/ui_carousel.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/logic_tests.cpp Firmware/PlayerConsole/tools/verify-ui-layout.py
git commit -m "feat: animate the home action carousel"
```

---

### Task 8: Build Center-Locked Lists With Fixed Footers And Swipe Input

**Files:**
- Create: `Firmware/PlayerConsole/ui_center_list.h`
- Create: `Firmware/PlayerConsole/ui_center_list.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`
- Modify: `Firmware/PlayerConsole/tools/verify-ui-layout.py`

**Interfaces:**
- Consumes: page item views, selected anchor, footer focus, `ListPrevious/ListNext`, and motion helpers.
- Produces: retained row track, fixed selected frame, centered short progress/count, fixed Back/Confirm footer, and discrete swipe-to-row behavior.

Use this item and component API:

```cpp
struct UiListItemView {
    const char *title;
    const char *meta;
    const char *disabledReason;
    bool enabled;
    bool checked;
};

struct UiCenterList {
    lv_obj_t *viewport = nullptr;
    lv_obj_t *track = nullptr;
    lv_obj_t *focusFrame = nullptr;
    lv_obj_t *progressFill = nullptr;
    lv_obj_t *countLabel = nullptr;
    lv_obj_t *footer = nullptr;
    uint8_t selected = 0;
    uint8_t count = 0;
};

void uiCenterListCreate(UiCenterList &list, lv_obj_t *parent,
                        const UiListItemView *items, uint8_t count,
                        uint8_t selected, const char *footerText,
                        bool footerEnabled, bool footerFocused);
void uiCenterListUpdate(UiCenterList &list, const UiListItemView *items,
                        uint8_t count, uint8_t selected,
                        const char *footerText, bool footerEnabled,
                        bool footerFocused, bool animate);
```

- [ ] **Step 1: Write failing list focus and scroll tests**

Assert:

```cpp
state.nav.current.page = ScreenPage::Assets;
state.nav.current.focus = 0;
appHandleInput(state, InputEvent{InputKind::Rotate, 20, 10}, 10);
ok &= expect(out, state.nav.current.focus == kAssetCount,
             "long asset delta clamps at fixed back footer");
appHandleInput(state, InputEvent{InputKind::Rotate, -1, 20}, 20);
ok &= expect(out, state.nav.current.focus == kAssetCount - 1 &&
                  state.assetListIndex == kAssetCount - 1,
             "reverse from footer restores last asset row");
```

Send `UiEventKind::ListNext` and `ListPrevious` and assert they move one row and clamp. Touch a nonfocused row and assert focus synchronizes without activation; touch it again and assert it opens detail.

- [ ] **Step 2: Run the red list test**

Expected: FAIL until clamp/swipe/touch behavior is implemented.

- [ ] **Step 3: Create a clipped row track and fixed center frame**

Show at most three full rows around the center. Move `track.y` over `200 ms` ease-out; do not animate row heights or viewport size. Keep the frame at `kNormalListFocus`. Disabled rows include a textual reason; checked rows include a visible check symbol independent of color.

Convert a touch drag into one discrete list step only when vertical displacement is at least `36 px` and `abs(dy) >= abs(dx) + 12 px`. Emit the step on release, then let the list snap to the center item; a single gesture never emits multiple steps.

- [ ] **Step 4: Add centered progress and remove the vertical scrollbar**

Use a `72 x 4 px` progress track centered at `x=240` and the `current / total` text centered below it. Do not create a right-side scrollbar object. The progress and count remain tied to the selected row when the footer is focused.

- [ ] **Step 5: Integrate all ordinary lists**

Use the component for Assets, Players, and Demo Lab. Give each page a fixed Back footer. Keep Trade and details as compact forms, but their fixed Back action must use the same `kNormalFooter` rectangle and centerline.

- [ ] **Step 6: Run green self-tests and layout guard**

Expected: eight assets scroll without wrapping, footer focus works in both directions, swipe steps discretely, no vertical scrollbar exists, and all centerline checks PASS.

- [ ] **Step 7: Commit reusable list behavior**

```powershell
git add Firmware/PlayerConsole/ui_center_list.h Firmware/PlayerConsole/ui_center_list.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/logic_tests.cpp Firmware/PlayerConsole/tools/verify-ui-layout.py
git commit -m "feat: add center locked round screen lists"
```

---

### Task 9: Render Top-Level Confirmations, Debt Selection, And Bankruptcy

**Files:**
- Create: `Firmware/PlayerConsole/ui_modal.h`
- Create: `Firmware/PlayerConsole/ui_modal.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/tools/verify-ui-layout.py`

**Interfaces:**
- Consumes: `ModalState`, hold/countdown queries, debt queries, and fixed `UiEvent` bindings.
- Produces: dimmed top-level modal, cancel/confirm focus, red insufficient state, debt selection page, and read-only bankruptcy page.

Expose:

```cpp
struct UiModal {
    lv_obj_t *shade = nullptr;
    lv_obj_t *panel = nullptr;
    lv_obj_t *confirm = nullptr;
    lv_obj_t *confirmFill = nullptr;
    lv_obj_t *cancel = nullptr;
    lv_obj_t *countdown = nullptr;
};

void uiModalShow(UiModal &modal, lv_obj_t *root, const AppState &state,
                 uint32_t nowMs);
void uiModalUpdate(UiModal &modal, const AppState &state, uint32_t nowMs);
void uiModalHide(UiModal &modal);
```

- [ ] **Step 1: Add a static red test for modal layout and layering**

Extend `verify-ui-layout.py` so it fails while `ui_modal.cpp` is absent, then checks:

- shade opacity is `72%` or the nearest LVGL 8-bit value;
- shade is created after the page scene and before the panel;
- panel equals `kModalRect`;
- hold fill is created before the hold label, so fill cannot cover text;
- cancel is absent when `cancelAllowed == false`;
- background objects are not clickable while modal exists.
- an offline authority shade blocks dangerous page controls and remains until `StateSnapshotApplied`.

- [ ] **Step 2: Run the red layout guard**

Run `compile.ps1` and expect `UI LAYOUT CHECK FAIL` because the V2 modal component is missing.

- [ ] **Step 3: Implement voluntary and forced modal variants**

Voluntary modal:

- confirm and cancel are both in the rotary focus sequence;
- only confirm starts hold progress;
- cancel closes to the exact source page/focus;
- release before 1200 ms resets fill to zero but keeps the modal open.

Forced payment modal:

- has no cancel;
- shows recipient, purpose, amount, and remaining time;
- insufficient state changes ring/title/action to red and replaces Confirm with `处理资产`;
- entering debt hides the modal and pauses its deadline.

- [ ] **Step 4: Render the compact forced debt page**

Use a fixed summary band for amount due, shortfall, selected proceeds, and post-mortgage balance; use a slightly shorter center-list viewport underneath; put `还差 $N` or `抵押 $X` in the fixed footer. No Back action or long-press escape is bound.

- [ ] **Step 5: Render bankruptcy as read-only terminal output**

Use a red outer ring, cause, creditor, and liquidation status. Do not draw Back, Continue, or a status strip styled as a button.

- [ ] **Step 6: Run green build guards and hardware self-tests**

Expected: all layout/layer checks PASS and existing modal/debt state tests remain PASS.

- [ ] **Step 7: Commit forced-flow views**

```powershell
git add Firmware/PlayerConsole/ui_modal.h Firmware/PlayerConsole/ui_modal.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/tools/verify-ui-layout.py
git commit -m "feat: render modal and debt authority flows"
```

---

### Task 10: Implement Authoritative Two-Dice Animation And Physical RFID Movement

**Files:**
- Create: `Firmware/PlayerConsole/ui_dice_stage.h`
- Create: `Firmware/PlayerConsole/ui_dice_stage.cpp`
- Modify: `Firmware/PlayerConsole/app_types.h`
- Modify: `Firmware/PlayerConsole/app_state.h`
- Modify: `Firmware/PlayerConsole/app_state.cpp`
- Modify: `Firmware/PlayerConsole/demo_transport.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/logic_tests.cpp`

**Interfaces:**
- Consumes: MyTurn Dice action, `RollResult`, `MoveGuidanceStarted`, RFID events, and manual hold.
- Produces: one roll request, deterministic 1.9 s two-die presentation, target guidance, wrong-tile warning, 12 s fallback, and event-only position update.

Add exact workflow types:

```cpp
enum class DicePhase : uint8_t { Idle, Requesting, Animating, Revealed };
enum class MovePhase : uint8_t {
    None,
    AwaitingRfid,
    ManualAvailable,
    ManualSubmitting,
    Confirmed,
};

struct DiceState {
    DicePhase phase = DicePhase::Idle;
    uint32_t requestId = 0;
    uint32_t startedMs = 0;
    bool resultReceived = false;
    bool guidanceReceived = false;
    uint8_t dieA = 0;
    uint8_t dieB = 0;
    uint8_t targetPosition = 0;
    const char *targetName = "";
};

struct MoveState {
    MovePhase phase = MovePhase::None;
    uint32_t startedMs = 0;
    uint32_t successUntilMs = 0;
    uint8_t targetPosition = 0;
    uint8_t wrongPosition = 0;
    bool wrongTileVisible = false;
};
```

Add these exact private test fixtures:

```cpp
void openMyTurnFixture(AppState &state)
{
    appInit(state, 0);
    state.homePhase = HomePhase::MyTurn;
    state.nav.current = NavigationEntry{ScreenPage::Home, 0, 0};
}

TransportEvent rollResultFixture(uint8_t dieA, uint8_t dieB,
                                 uint8_t targetPosition)
{
    TransportEvent event{};
    event.kind = TransportEventKind::RollResult;
    event.requestId = 1;
    event.stateVersion = 2;
    event.dieA = dieA;
    event.dieB = dieB;
    event.targetPosition = targetPosition;
    event.targetName = "极光码头";
    return event;
}

TransportEvent guidanceFixture(uint8_t targetPosition)
{
    TransportEvent event{};
    event.kind = TransportEventKind::MoveGuidanceStarted;
    event.requestId = 1;
    event.stateVersion = 3;
    event.targetPosition = targetPosition;
    event.targetName = "极光码头";
    return event;
}
```

- [ ] **Step 1: Write failing dice and RFID state tests**

Cover this sequence:

```cpp
openMyTurnFixture(state);
shortPress(state, 10, 100);
TransportCommand command{};
ok &= expect(out, appPollCommand(state, command) &&
                  command.kind == TransportCommandKind::RollRequest,
             "dice queues one authoritative roll request");
shortPress(state, 110, 180);
ok &= expect(out, !appPollCommand(state, command),
             "dice ignores duplicate input while requesting");

appHandleTransportEvent(state, rollResultFixture(3, 4, 24), 220);
appHandleTransportEvent(state, guidanceFixture(24), 250);
appTick(state, 1999);
ok &= expect(out, state.nav.current.page == ScreenPage::DiceStage,
             "dice result does not reveal early");
appTick(state, 2000);
ok &= expect(out, state.nav.current.page == ScreenPage::MoveGuide &&
                  state.position == 17,
             "dice reveals target without moving electronic token");
```

Inject wrong RFID at tile 23 and assert position stays 17. At `startedMs + 12000`, assert manual becomes available. Short press must not confirm; 1200 ms hold queues `MoveManualConfirmRequest`. Inject correct RFID before manual response and assert correct RFID wins. Position changes to 24 only on `RfidPositionConfirmed`, then success remains visible for approximately 600 ms.

- [ ] **Step 2: Run the red dice/RFID test**

Expected: FAIL until workflow types and event handling exist.

- [ ] **Step 3: Implement roll and movement state transitions**

Rules:

- Dice activation immediately changes to `DiceStage` and locks actions.
- Early RollResult stores faces but does not shorten animation.
- At 1900 ms, if result and guidance exist, enter `MoveGuide`; if either is missing, show an authority-wait state and never invent data.
- No local tick changes `state.position`.
- Wrong RFID stores a temporary red warning and keeps the target green.
- Manual fallback appears after 12000 ms and remains subordinate to a later correct RFID event.

- [ ] **Step 4: Draw two flat vector dice and update them every frame**

Expose:

```cpp
struct UiDiceStage {
    lv_obj_t *die[2]{};
    lv_obj_t *pip[2][6]{};
    lv_obj_t *sum = nullptr;
    lv_obj_t *target = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *manual = nullptr;
};

void uiDiceStageCreate(UiDiceStage &view, lv_obj_t *parent,
                       const AppState &state, uint32_t nowMs);
void uiDiceStageUpdate(UiDiceStage &view, const AppState &state,
                       uint32_t nowMs);
```

Both dice fall from different directions, rotate, bounce, and settle using `uiDicePose`. During motion, intermediate faces use a deterministic visual sequence; at/after 1500 ms the face is the transport result. Show sum/target only in the reveal phase. Use no raster dice image and no electronic token movement animation.

- [ ] **Step 5: Add DemoTransport RFID fixtures**

Provide Demo Lab scenarios for:

- correct target after 2500 ms;
- wrong tile followed by correct target;
- no RFID, exposing manual fallback;
- manual confirm accepted;
- roll event delayed beyond animation, proving no local result is invented.

- [ ] **Step 6: Run green self-tests and production compile**

Expected: roll idempotency, reveal timing, wrong tile, timeout, manual race, and event-only position update all PASS; production compile passes layout/glyph/build guards.

- [ ] **Step 7: Commit dice and physical movement**

```powershell
git add Firmware/PlayerConsole/ui_dice_stage.h Firmware/PlayerConsole/ui_dice_stage.cpp Firmware/PlayerConsole/app_types.h Firmware/PlayerConsole/app_state.h Firmware/PlayerConsole/app_state.cpp Firmware/PlayerConsole/demo_transport.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/logic_tests.cpp
git commit -m "feat: add dice and physical movement demo"
```

---

### Task 11: Complete Demo Lab, UTF-8 Glyphs, Documentation, And Acceptance Matrix

**Files:**
- Modify: `Firmware/PlayerConsole/demo_data.h`
- Modify: `Firmware/PlayerConsole/demo_data.cpp`
- Modify: `Firmware/PlayerConsole/ui_renderer.cpp`
- Modify: `Firmware/PlayerConsole/src/fonts/ui_glyphs.txt`
- Modify: `Firmware/PlayerConsole/src/fonts/ui_font_14.c`
- Modify: `Firmware/PlayerConsole/src/fonts/ui_font_16.c`
- Modify: `Firmware/PlayerConsole/README.md`
- Modify: `Docs/firmware/main-controller-protocol.md`
- Modify: `Docs/player-console/player-console-demo-scenarios.md`
- Modify: `Docs/player-console/player-console-acceptance-tests.md`
- Modify: `Docs/design-records/2026-08-02-player-console-interaction-v2-design.md`

**Interfaces:**
- Consumes: all V2 pages, commands, events, and scenario selectors.
- Produces: a scrollable Demo Lab catalog, complete font coverage, protocol/document parity, and a hardware acceptance checklist.

- [ ] **Step 1: Add a failing scenario/catalog test**

Assert the Demo Lab exposes at least these named fixtures and therefore requires center-list scrolling:

```text
等待回合
临近回合
我的回合
20 秒收租
10 秒付款
资金不足
批量抵押
无法筹款
掷骰 + RFID 成功
RFID 错格后成功
RFID 超时手动确认
主控断线并恢复快照
```

Each scenario reset must restore money `$1,860`, position `17`, no modal, no selected debt assets, no pending command, and request sequence `1`.

- [ ] **Step 2: Run the red catalog test**

Expected: FAIL until the complete scenario list/reset function exists.

- [ ] **Step 3: Implement deterministic scenario reset and selection**

`DemoTransport::setScenario` clears scheduled/output queues and duplicate-request cache before installing the selected fixture. It emits standard events only; it never calls a page draw function.

- [ ] **Step 4: Regenerate the CJK subset after final copy is present**

Run:

```powershell
& 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\Firmware\PlayerConsole\tools\generate-ui-fonts.py
& 'C:\Users\Administrator\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' .\Firmware\PlayerConsole\tools\verify-ui-glyphs.py
```

Expected: generator reports both fonts, then verifier reports every source glyph covered. Do not hand-edit generated `.c` font output.

- [ ] **Step 5: Synchronize protocol and scenario documentation**

Update `main-controller-protocol.md` with:

- `MORTGAGE_BATCH_REQUEST` carrying transaction ID and asset IDs;
- `MOVE_MANUAL_CONFIRM_REQUEST` carrying device, player, target tile, and timestamp;
- `RFID_POSITION_CONFIRMED` and `RFID_POSITION_REJECTED` result semantics;
- request ID/state version idempotency for all four messages.
- `ConnectionLost` freezes dangerous input and only a complete `StateSnapshotApplied` unfreezes it.

Update Demo scenarios so B07 and all movement cases explicitly say the physical piece moves, target tile lighting is external, and no on-screen step animation occurs. Add the three RFID fixtures and debt-list fixtures.

Update acceptance tests with IDs for:

- retained 220 ms carousel travel;
- centered list/no vertical scrollbar/fixed footer;
- explicit Back on every ordinary page;
- forced-page Back rejection;
- voluntary modal Cancel and early-release behavior;
- eight-asset debt scrolling and disabled reasons;
- 1.9 s two-die authoritative reveal;
- wrong/correct/timeout/manual RFID paths;
- production firmware restored after self-test.
- disconnect shade, dangerous-input freeze, and snapshot recovery.

- [ ] **Step 6: Update firmware README and design status**

README must describe rotary, short press, fixed Back, 1200 ms confirm, touch/swipe, Demo Lab, mechanical `60` / firmware `0`, self-test, production compile, and upload commands. Change the design status from `待书面规格复核` to `规格已确认，实施计划已批准` only after the implementation plan has been selected for execution.

- [ ] **Step 7: Run green scenario, glyph, layout, and production checks**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\compile.ps1
```

Expected: every logic test PASS; glyph, layout, and build-output checks PASS.

- [ ] **Step 8: Commit Demo Lab and documentation parity**

```powershell
git add Firmware/PlayerConsole/demo_data.h Firmware/PlayerConsole/demo_data.cpp Firmware/PlayerConsole/ui_renderer.cpp Firmware/PlayerConsole/src/fonts/ui_glyphs.txt Firmware/PlayerConsole/src/fonts/ui_font_14.c Firmware/PlayerConsole/src/fonts/ui_font_16.c Firmware/PlayerConsole/README.md Docs/firmware/main-controller-protocol.md Docs/player-console/player-console-demo-scenarios.md Docs/player-console/player-console-acceptance-tests.md Docs/design-records/2026-08-02-player-console-interaction-v2-design.md
git commit -m "docs: align player console v2 demo and protocol"
```

---

### Task 12: Full Verification, Production Flash, And Hardware Handoff

**Files:**
- Verify: `Firmware/PlayerConsole/build/PlayerConsole.ino.bin`
- Verify: `Firmware/PlayerConsole/build/PlayerConsole.ino.map`
- Verify: all committed source and documentation from Tasks 1-11
- Do not commit: generated `Firmware/PlayerConsole/build/` output

**Interfaces:**
- Consumes: complete V2 implementation and the connected round display on `COM4`.
- Produces: passing evidence, production firmware on hardware, hash/size record, and a short residual physical-inspection list.

- [ ] **Step 1: Verify the worktree scope before final tests**

Run:

```powershell
git status --short
git diff --check
git diff --cached --name-only
```

Expected: no staged unrelated files; `git diff --check` returns no whitespace errors. Existing unrelated dirty files may remain but must not be staged or edited by this implementation.

- [ ] **Step 2: Run the complete hardware self-test image**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\run-selftest.ps1 -Port COM4 -TimeoutSeconds 35
```

Expected: all named tests print PASS and output ends with `SELFTEST PASS`.

- [ ] **Step 3: Rebuild the production image without self-test flags**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\compile.ps1
```

Expected: UI glyph, UI layout, Arduino compile, and linked HWCDC build-output checks all PASS. Record reported sketch and global-memory usage.

- [ ] **Step 4: Upload the production image to COM4**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Firmware\PlayerConsole\tools\upload.ps1 -Port COM4
```

Expected: upload exits `0` and the device resets into the normal Demo, not the self-test wait loop.

- [ ] **Step 5: Capture startup identity and binary hash**

Open COM4 at `115200` after reset and capture:

```text
Gridopoly Player Console ready; mechanical=60 firmware=0
```

Then run:

```powershell
Get-FileHash .\Firmware\PlayerConsole\build\PlayerConsole.ino.bin -Algorithm SHA256
```

Record the SHA-256 and firmware Git commit in the final report.

- [ ] **Step 6: Perform the real-screen interaction pass**

On the connected module, verify in this order:

1. Rotate both ways: the carousel visibly travels with the bezel direction and never teleports.
2. My Turn: Dice shares the carousel and is visibly yellow/emphasized without overlapping cash or location.
3. Assets: eight rows center-scroll; progress/count/footer remain centered; no right scrollbar exists.
4. Every ordinary page: fixed Back is centered and restores the correct parent focus.
5. Voluntary trade/mortgage: Cancel works; releasing before 1.2 s clears only progress.
6. Forced payment: no Cancel/Back; insufficient cash enters debt; disabled reasons and batch total are readable.
7. Dice: two dice animate for about 2 s; no electronic piece walks across tiles.
8. RFID Demo scenarios: target wait, wrong red warning, correct success, and 12 s manual fallback all appear.
9. Inspect top/bottom chord areas for clipping, text overlap, or controls outside the circular glass.

If any visual defect is found, return to the owning task, add a failing guard/test where practical, fix, rerun Tasks 12.2-12.6, and upload production again.

- [ ] **Step 7: Run final verification once more after any hardware adjustment**

Run production compile and `git diff --check` again. Do not claim completion from an earlier pre-fix result.

- [ ] **Step 8: Commit only final source corrections, if any**

If Step 6 required a correction, return to the owning Task, repeat that Task's exact `git add` list and test cycle, and make a scoped `fix:` commit there. At this step run `git status --short` and verify `Firmware/PlayerConsole/build/` is ignored and no unrelated path is staged.

---

## Completion Evidence

The implementation is complete only when all of the following are available:

- `run-selftest.ps1` output ending in `SELFTEST PASS`.
- Production `compile.ps1` output showing glyph, layout, Arduino, and HWCDC checks PASS.
- Successful production upload to `COM4` after the final self-test upload.
- Startup line proving `mechanical=60 firmware=0`.
- SHA-256 of the flashed production `.bin` and the firmware Git commit.
- Real-screen confirmation for carousel travel, centered lists/footer, modal layering, debt scrolling, dice animation, and RFID fallback.
- No staged or committed unrelated worktree changes.
