# Gridopoly build photos

[Browse the photo gallery](../../../Docs/gallery/README.md) · [English README](../../../README.md) · [中文README](../../../README.zh-CN.md)

Real prototype photographs supplied by the project author. These are build-log images, not assembly drawings; consult the hardware and mechanical documentation for the current revision.

## Added October 18, 2026

All 11 supplied HEIC photographs are published as unannotated JPEGs in [2026-10-18](2026-10-18/). Their original compositions are retained, with EXIF orientation applied, colors converted to sRGB, a maximum edge of 2400 pixels, and camera/location metadata omitted. Smaller thumbnails keep the gallery quick to load. Original HEIC files are not modified or included in the repository.

The date names this repository addition, not the capture date of every photo. [manifest.json](2026-10-18/manifest.json) maps original filenames to web copies, dimensions and SHA-256 hashes.

| Source | Web photo | Subject |
| --- | --- | --- |
| `IMG_1947.HEIC` | [player-console-side.jpg](2026-10-18/player-console-side.jpg) | A controller, from the side / 玩家控制器的侧面 |
| `IMG_1948.HEIC` | [tile-corner-assembly.jpg](2026-10-18/tile-corner-assembly.jpg) | How a corner joins the board / 角落与长边怎样相连 |
| `IMG_1949.HEIC` | [assembled-board-frame.jpg](2026-10-18/assembled-board-frame.jpg) | Sixteen modules, one frame / 16块模块，拼成一整盘 |
| `IMG_1950.HEIC` | [player-power-board-reverse-view.jpg](2026-10-18/player-power-board-reverse-view.jpg) | Another look at the power board / 换个角度看供电板 |
| `IMG_1951.HEIC` | [player-power-board.jpg](2026-10-18/player-power-board.jpg) | The player power board / 玩家供电板 |
| `IMG_1952.HEIC` | [long-tile-pcb.jpg](2026-10-18/long-tile-pcb.jpg) | Before fitting the display / 安装屏幕之前 |
| `IMG_1953.HEIC` | [long-tile-with-display.jpg](2026-10-18/long-tile-with-display.jpg) | With the display fitted / 装上屏幕之后 |
| `IMG_1954.HEIC` | [tile-display-side.jpg](2026-10-18/tile-display-side.jpg) | A little space underneath / 屏幕下面留出的空间 |
| `IMG_1955.HEIC` | [rfid-coil.jpg](2026-10-18/rfid-coil.jpg) | The coil around each tile / 每个格子的读卡线圈 |
| `IMG_1956.HEIC` | [unpopulated-tile-pcb.jpg](2026-10-18/unpopulated-tile-pcb.jpg) | Before the components go on / 还没有焊上元件的时候 |
| `IMG_1957.HEIC` | [gridopoly-workbench.jpg](2026-10-18/gridopoly-workbench.jpg) | The board on my workbench / 工作台上的整套原型 |

## Annotated README guides

Three separate edited versions add English labels and cyan leader lines to the player-console, corner-assembly and RFID-piece photos. They were produced with the **built-in image_gen tool**, checked against the source photos for the labelled parts, then encoded as JPEGs. They are explanatory edited images; use the unannotated photographs or design files to inspect fine hardware details.

- [Player console](2026-10-18/annotated/player-console-annotated.jpg): round display, power PCB, battery, printed stand; based on IMG_1947.
- [Tile corner](2026-10-18/annotated/tile-corner-annotated.jpg): long tile PCB, corner tile PCB, module connectors, printed base; based on IMG_1948.
- [Recognizable piece](2026-10-18/annotated/rfid-piece-annotated.jpg): printed figure above the black RFID tag; based on the earlier piece photograph.
- [Exact editing prompts](2026-10-18/annotated/prompts.json). The two new-photo edits used 1400-pixel sRGB preview copies; the piece edit used the existing JPEG. All three edits requested landscape crops, preserved physical parts and annotation overlays only. Thumbnails are separate from the larger annotated versions.

## Earlier photographs

The four October 16 additions are preserved as originally supplied and remain accessible in the gallery:

- `gridopoly-16-tile-board.jpg`: the 12-long-tile / 4-corner prototype on the workbench.
- `gridopoly-pcbs-and-base.jpg`: connected tile PCBs on a printed corner fixture.
- `gridopoly-player-console-and-power.jpg`: player screen, power PCB and battery during development.
- `gridopoly-rfid-piece.jpg`: pink printed figure above a black RFID tag.
