<h1 align="center">Gridopoly</h1>

<p align="center">
  A modular electronic board game platform with smart tiles, player control screens, physical pieces, and a Raspberry Pi authority server.
</p>

<p align="center">
  <a href="README.zh-CN.md">Chinese</a> &middot;
  <a href="#project-gallery">Gallery</a> &middot;
  <a href="#current-status">Status</a> &middot;
  <a href="#quickstart">Quickstart</a> &middot;
  <a href="Docs/README.md">Docs</a> &middot;
  <a href="Server/RaspberryPi/README.md">Raspberry Pi server</a>
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-integrated%20prototype-D97706?style=for-the-badge" />
  <img alt="Tile MCU" src="https://img.shields.io/badge/tile-ESP32--S3-205A4B?style=for-the-badge" />
  <img alt="Authority" src="https://img.shields.io/badge/authority-Raspberry%20Pi%205-A22846?style=for-the-badge&logo=raspberrypi&logoColor=white" />
  <img alt="Player console" src="https://img.shields.io/badge/player%20console-480x480%20round%20LCD-6B7FD7?style=for-the-badge" />
  <img alt="Bus" src="https://img.shields.io/badge/link-WiFi%20%2B%20ORDER-2563EB?style=for-the-badge" />
</p>

## Project Gallery

<table>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-tile-pcb.png" alt="Gridopoly smart tile PCB design" />
      <br />
      <strong>Smart tile PCB.</strong> ESP32-S3, display, RFID, RS485, 24 V power distribution, current sensing, and edge contacts in one rearrangeable grid module.
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-player-console.png" alt="Gridopoly player control screen design" />
      <br />
      <strong>Player control screen.</strong> Round 480x480 player terminal for turn actions, cash, assets, dice, trade, and game prompts.
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-tile-module.png" alt="Gridopoly physical smart tile module" />
      <br />
      <strong>Physical tile module.</strong> A real smart tile assembly with screen, USB-C, side contacts, LEDs, and board-level bring-up hardware.
    </td>
    <td width="50%" align="center">
      <img src=".github/assets/gridopoly-raspberry-pi.png" alt="Raspberry Pi 5 used as the Gridopoly game authority server" />
      <br />
      <strong>Raspberry Pi authority.</strong> The central game server for rules, accounts, board state, web UI, player synchronization, and persistence.
    </td>
  </tr>
</table>

## Why This Exists

Gridopoly explores what a physical board game can become when the board itself is programmable. Instead of a fixed printed board, the system is built from independent smart tiles that can display their own state, detect pieces, animate LEDs, and communicate with a central authority.

The goal is not to copy the brand, map, art, cards, or product appearance of any existing commercial board game. Gridopoly keeps the social feeling of moving pieces around a real table, while replacing paper money, manual rent calculation, and fixed board content with software-controlled rules, assets, player screens, and dynamic maps.

## Current Status

Updated October 15, 2026. The current milestone is a 16-space trial board (12 long tiles and four corners), with up to four player consoles. This is an integrated prototype; complete physical-game acceptance and manufacturing approval remain open.

- All 16 planned tiles have been flashed, verified and run in batches on V0.34. The release fixes WS2812B-B-V6 LED timing; 13 tiles have user-confirmed normal lighting, three await visual confirmation. RFID and a powered full ring still need testing with the 24 V supply.
- The deployed Raspberry Pi server maps complete 16/24/32/40-space ORDER rings only when every online tile belongs to the same ring, the count matches the active map, and the physical corner roles match. Consistent manual anchors rotate the assignment; open-chain testing remains available. RS485 stays disabled.
- Four player consoles have been discovered online. New games freeze 1–4 eligible human consoles and fill the remaining seats with bots. Sequential fresh-tag pairing precedes avatar/name setup; single-reader and open-chain setups are supported. A newly connected console waits for the next game instead of taking an existing seat.
- Input batching, stale-hold protection and focused-row redraws are implemented. The waiting-page redraw fix is installed on COM27/28/29; COM27/28 passed the user's visual check, while COM29 awaits its own feedback. COM7 retains the preceding normal build. Exact rotary detents, Avatar/game display and the Retarget frame-time limit remain to be verified.
- The latest frozen local package is **trial16-2026-10-15-v4**: V0.34 long/corner images, normal player `dfc4b7e5…`, and Pi `ea6e1f51…`. It is separate from the older September batch32-v4 package. Battery telemetry remains disabled and undeployed.
- Tile hardware uses direct regulated 24 V input. The current player power-board design candidate is **R1 RC6-TP**, adding eight test points to the corrected battery-board design. Three in-use power boards were reported repaired by the user; charging, cutoff, thermal and manufacturing acceptance remain open.
- The mechanical work includes an R2 one-piece three-tile fixture with geometry checks passed and printing/fit tests pending. The older CW47.5 console stand has user-confirmed fit; its successor remains in development.

See the [current handoff](Docs/CURRENT-DEVELOPMENT-HANDOFF.md), [device registry](Docs/firmware/current-device-connections.md), [16-space release and acceptance record](Docs/firmware/trial16-release-2026-10-15.md), and [mechanical progress](Docs/mechanical/README.md). Host tests, flashing and user confirmation of one page do not certify full-board gameplay, wireless capacity or electrical behavior.

## System Architecture

```mermaid
flowchart TB
    Pi["Raspberry Pi 5 authority<br/>rules, turns, accounts, persistence"]
    Web["Web board / test UI"]
    Console["Player round screens<br/>Wi-Fi/UDP actions + snapshots"]
    Bus["24 V + ORDER; RS485 reserved"]
    TileA["Smart tile A<br/>ESP32-S3 / LCD / LED / RFID"]
    TileB["Smart tile B"]
    TileN["Smart tile N"]
    Piece["Physical pieces<br/>125 kHz RFID tags"]

    Pi <--> Web
    Pi <-->|"WPA2 / HMAC UDP / HTTP assets"| Console
    Power["24 V DC input"] --> Bus
    Pi <-->|"Wi-Fi / HTTP"| TileA
    Bus --> TileA
    TileA --> TileB
    TileB --> TileN
    Piece -. placed on .-> TileA
```

## Core Features

- **Rearrangeable smart tiles**: each grid module can be manufactured, tested, replaced, and rearranged independently.
- **Physical-piece sensing**: each tile is designed around 125 kHz RFID detection for tagged game pieces.
- **Dynamic tile display**: ST7789 screens and WS2812 LEDs show ownership, events, status, and feedback.
- **Central game authority**: a Raspberry Pi owns the rules, turns, cash, assets, trades, auctions, debts, and persistence.
- **Player terminals**: round screens give each player their own cash, assets, dice, trade, payment, and reconnect interface.
- **Original Grid City content**: the repo includes board maps, visual systems, tile assets, avatars, contracts, and game rules.
- **Protocol-first design**: C++ core, binary protocol, UDP envelope, HTTP assets, and generated contracts are tested as first-class artifacts.

## Tech Stack

| Layer | Technology | Purpose |
| --- | --- | --- |
| Tile hardware | ESP32-S3, ST7789, WS2812, HTRC110, RS485, INA226 | Local display, lighting, RFID sensing, Wi-Fi/HTTP communication, reserved RS485, current monitoring. |
| Player console | ESP32-S3, 480x480 round LCD, LVGL-oriented UI specs, Wi-Fi/UDP | Per-player controls, snapshots, actions, reconnect, and UI workflows. |
| Authority server | Raspberry Pi 5, C++17, systemd | Rules, persistence, HTTP board UI, UDP player sessions, and deployment. |
| Game core | C++ libraries | Board catalog, game engine, protocol codec, state machine, trades, auctions, cards, and bots. |
| Assets/contracts | JSON schemas, generated docs, PNG/RGB565/GAVC assets | Stable game content, tile art, avatars, and protocol contracts. |
| Hardware design | EasyEDA snapshots; 6-layer tiles, 2-layer player battery board | Smart tile PCB design, BOM, power, bus, and connector documentation. |

## Quickstart

Build and run the host-side C++ tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Build and test the Raspberry Pi server using the native guard script:

```bash
GRIDOPOLY_NATIVE_BUILD_DIR=/tmp/gridopoly-build \
  Server/RaspberryPi/tools/build-and-test-native.sh
```

Deploy on Raspberry Pi after building the server binary:

```bash
sudo sh Server/RaspberryPi/deploy/install.sh build-pi/gridopoly_server
sudoedit /etc/gridopoly/server.env
sudoedit /etc/gridopoly/ap.env
sudo systemctl enable --now gridopoly-ap gridopoly-dnsmasq gridopoly-ap-watchdog gridopoly
```

The last recorded management-network address is `http://10.0.0.124/`; it may change with the network. The AP endpoints below are a separate device network. Optional external-AP migration is documented [here](Docs/firmware/external-ap-deployment-2026-09-26.md) and has not been applied on site.

Player network endpoints after deployment:

- Web board on the player AP: `http://10.42.0.1/`
- Player binary protocol: UDP `10.42.0.1:4242`

## Documentation Map

Start here when working on a specific area:

- [Docs center](Docs/README.md)
- [Product goals](Docs/product/product-goals.md)
- [Hardware baseline](Docs/hardware/hardware-baseline.md)
- [ESP32-S3 pin map](Docs/hardware/esp32-s3-pin-map.md)
- [Module interconnect](Docs/hardware/module-interconnect-5wire.md)
- [Firmware development guide](Docs/firmware/firmware-development-guide.md)
- [Raspberry Pi authority server](Docs/firmware/raspberry-pi-server.md)
- [Wi-Fi/UDP player protocol](Docs/firmware/wifi-udp-player-protocol.md)
- [Avatar components](Docs/firmware/avatar-component-protocol.md)
- [Player detail queries](Docs/firmware/player-detail-query-protocol.md)
- [Trade protocol](Docs/firmware/trade-protocol.md)
- [Player console UI spec](Docs/player-console/player-console-ui-spec.md)
- [Game rules](Docs/game/game-rules.md)

## Repository Layout

```text
Gridopoly/
|-- Assets/                  Grid City tile art, avatars, manifests, and generated assets
|-- Docs/                    Product, hardware, firmware, game, player-console, and design docs
|-- Firmware/                ESP32 test server, shared core/protocol libraries, LVGL baseline
|-- GameData/                Schemas, generated contracts, and game data artifacts
|-- Mechanical/              Player console stand models, scripts, tests, and reports
|-- PCB Files/               EasyEDA project snapshots and PCB backups
|-- Server/RaspberryPi/      Formal authority server, deploy scripts, web/UDP services
|-- Tools/                   Contract generation tools and tests
|-- tests/host/              Host C++ tests for core, protocol, authority, UDP, HTTP, avatars
`-- CMakeLists.txt
```

## Safety And Release Notes

Gridopoly is an integrated prototype, not a manufacturing release. Before treating a board revision as production-ready, the project still needs final DRC, no-unrouted checks, component orientation review, switching-regulator layout review, current-path calculations, RFID tuning, and measured multi-module testing.

No open-source license has been selected yet. Public code and design files are available for inspection, but they are not automatically licensed for reuse, modification, or commercial use.
