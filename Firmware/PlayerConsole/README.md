# Gridopoly Player Console

Arduino firmware skeleton for the 480 x 480 Viewe UEDX48480021-MD80ET ESP32-S3 round display.

Production builds use the test server's ESP-NOW v1 transport. Self-test builds retain
`DemoTransport`, so UI and state-machine tests do not require a radio peer.

## Test server connection

1. Copy `config/secrets.example.h` to the ignored `config/secrets.local.h`.
2. Set `GRIDOPOLY_ESPNOW_TEST_PSK` to the same local value as TestGameServer.
3. Start TestGameServer, then start the player console. The console scans channels 1-13,
   pairs with the first valid server, promotes the peer to LMK encryption, and applies
   only authoritative state snapshots.
4. Confirm `GRIDOPOLY_LINK paired`, `GRIDOPOLY_SNAPSHOT`, and server `peers=1` diagnostics.

Wire definitions remain single-source in `Firmware/libraries/GridopolyProtocol` and
`Docs/firmware/test-game-server-espnow-protocol.md`. The current v1 snapshot supplies
cash, position, turn/decision state, current-tile data, and six player summaries. It does
not yet supply a complete owned-asset projection or trade draft, so those two screens
continue to use demo content until the server protocol adds those records.

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
