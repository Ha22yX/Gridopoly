# Avatar Component Streaming Protocol v1

Status: canonical and frozen on 2026-08-07.

This contract lets a player console fetch only the structural avatar layer that changed. Hair color and skin tone are applied locally without a network request. The Raspberry Pi final-avatar compositor uses the same neutral component bytes, palettes, fixed-point tint rules, and source-over blend rules, so preview and final output have one pixel source of truth.

## Routes

Canonical component URLs contain decimal IDs without leading zeroes:

- `GET /assets/avatar-components/v1/hair/h1.gavc` through `h10.gavc`
- `GET /assets/avatar-components/v1/face/f1.gavc` through `f10.gavc`
- `GET /assets/avatar-components/v1/outfit/o1.gavc` through `o10.gavc`

Only `GET` is supported. A valid component returns:

- `200 application/octet-stream`
- `Cache-Control: public, max-age=31536000, immutable`
- a strong ETag derived from the exact file bytes
- the exact GAVC file as the body

An exact `If-None-Match` returns `304` without a body. Unknown kinds, IDs outside 1..10, leading zeroes, suffix changes, separators, traversal, and malformed component files return `404`. The legacy whole-combination route remains supported:

`GET /assets/avatar-previews/v1/h<hair>-c<hairColor>-f<face>-s<skin>-o<outfit>.rgb565`

Final public avatars remain:

- `GET /assets/avatars/<room>/<key>.png`
- `GET /assets/avatars/<room>/<key>.rgb565`

## GAVC v1 binary format

All integers are little endian. The header is exactly 32 bytes.

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `GAVC` |
| 4 | 1 | schema | `1` |
| 5 | 1 | kind | face=`1`, outfit=`2`, hair=`3` |
| 6 | 1 | presetId | `1..10` |
| 7 | 1 | encoding | `1` (RGBA RLE) |
| 8 | 2 | canvasWidth | `220` |
| 10 | 2 | canvasHeight | `300` |
| 12 | 2 | x | cropped rectangle origin in the canvas |
| 14 | 2 | y | cropped rectangle origin in the canvas |
| 16 | 2 | width | cropped rectangle width |
| 18 | 2 | height | cropped rectangle height |
| 20 | 4 | decodedBytes | `width * height * 4` |
| 24 | 4 | encodedBytes | bytes following the header |
| 28 | 4 | crc32 | IEEE CRC-32 of decoded RGBA bytes |

The decoded format is straight-alpha `RGBA8888`, row-major, with no row padding. Coordinates refer to a 220x300 transparent preview canvas. Components compose in this fixed order: face, outfit, hair.

Encoding 1 is token RLE. Each token begins with a little-endian `uint16`:

- bit 15 clear: literal run; low 15 bits are pixel count, followed by `count * 4` RGBA bytes;
- bit 15 set: repeated run; low 15 bits are pixel count, followed by one RGBA pixel;
- count zero is invalid;
- decoding must consume exactly `encodedBytes` and produce exactly `decodedBytes`.

The decoder rejects overflow, trailing bytes, out-of-canvas rectangles, CRC mismatch, wrong kind/path, and wrong preset/path.

## Palettes

Palette indices and RGB values are protocol data. They must not be reordered without a new catalog version.

Hair colors:

| ID | Name | RGB |
|---:|---|---|
| 1 | graphite | 104,116,124 |
| 2 | copper | 176,83,43 |
| 3 | raven | 40,48,58 |
| 4 | espresso | 80,55,47 |
| 5 | chestnut | 142,90,60 |
| 6 | golden | 209,164,79 |
| 7 | platinum | 216,204,176 |
| 8 | city-teal | 45,132,138 |
| 9 | ash-brown | 112,92,82 |
| 10 | auburn | 139,54,42 |
| 11 | mahogany | 104,42,44 |
| 12 | honey-blonde | 200,151,78 |
| 13 | strawberry-blonde | 213,139,92 |
| 14 | silver | 166,178,188 |
| 15 | snow-white | 235,238,234 |
| 16 | burgundy | 117,37,63 |
| 17 | rose-pink | 194,85,119 |
| 18 | violet | 111,78,160 |
| 19 | cobalt-blue | 55,93,168 |
| 20 | emerald | 48,125,91 |

Skin tones:

| ID | Name | RGB |
|---:|---|---|
| 1 | porcelain | 239,202,173 |
| 2 | fair | 232,181,139 |
| 3 | warm | 224,158,100 |
| 4 | golden | 202,141,82 |
| 5 | olive | 173,128,83 |
| 6 | bronze | 161,96,62 |
| 7 | deep | 137,83,56 |
| 8 | ebony | 91,53,42 |

## Fixed-point recoloring

All division uses non-negative integer round-half-up:

`roundHalfUp(n, d) = (n + floor(d / 2)) / d`

Every output channel is clamped to 255. Alpha is never changed.

### Hair

For source `(r,g,b,a)`, compute `W = 54r + 183g + 19b`.

- If `a == 0` or `W < 6528`, preserve source RGB.
- Otherwise compute `N = 7 * 65280 + 21 * W`, `D = 1305600`.
- For target palette channel `t`, output `roundHalfUp(t * N, D)`.

This is the integer definition of strength `0.35 + 1.05*luminance`, with luminance measured against 65280.

### Skin

Compute `W = 54r + 183g + 19b`, `hi=max(r,g,b)`, and `lo=min(r,g,b)`. A pixel is skin-tintable only when all predicates hold:

- `W >= 23552`
- `5 * (hi - lo) >= hi`
- `25 * r >= 26 * g`
- `25 * g >= 23 * b`

Non-tintable pixels preserve source RGB. For tintable pixels, clamp the luminance ratio `W / 45568` into `[42/100, 135/100]`. For target channel `t`, output `roundHalfUp(t * numerator, denominator)` using the selected unclamped or boundary ratio.

## Source-over composition

Both client and server use straight-alpha source-over. Let `sa` and `da` be 0..255:

- `oa = sa + roundHalfUp(da * (255 - sa), 255)`
- for each RGB channel, `premul = src*sa + roundHalfUp(dst*da*(255-sa),255)`
- `out = roundHalfUp(premul, oa)` when `oa != 0`

Transparent source is a no-op; fully opaque source replaces the destination.

## Golden vectors

Implementations must reproduce these exact byte results:

- Hair tint `(100,120,140,173)` toward hair color 2 `(176,83,43)` becomes `(147,69,36,173)`.
- Hair tint `(5,7,9,255)` is below the luminance threshold and remains `(5,7,9,255)`.
- Skin tint `(190,140,100,211)` toward skin tone 4 `(202,141,82)` becomes `(167,117,68,211)`.
- Skin test `(240,240,240,255)` is not a warm saturated skin plane and remains unchanged.
- Source-over of source `(200,100,20,96)` onto destination `(10,30,50,128)` becomes `(114,68,34,176)`.
- Recipe `h7/c18/f4/s6/o9` produces legacy-preview FNV-1a-64 `2db09660e5c5fdfa` and SHA-256 `589778abed7e24b09ac8515ce6f51027fe64c6d13cb052197e7a15c4479de34f`.
- The same recipe's final 128x128 RGBA content has FNV-1a-64 `6895eae94a41c35f`.

The generated manifest `Assets/GridCity/Avatars/V1/runtime/avatar-components-v1.json` has SHA-256 `4154dca49550fd5fcf71b54077f741d63af4fb935b84ba62eeba314f4cc3f153`. It records the SHA-256 of every GAVC file and every decoded RGBA rectangle.

## Preview and final geometry

The component canvas is 220x300. The whole-preview compatibility route composes the three components there and flattens to little-endian RGB565 over `#061017`.

The final compositor places the same transparent 220x300 composite into a transparent 320x320 canvas at `(50,10)`, applies the existing 128x128 bilinear downsample and circular mask, then emits PNG RGBA plus little-endian RGB565. It does not read the old 290-entry pre-colored pack. Confirm Avatar therefore performs one final composition and publish operation.

## Client lifecycle and memory bound

- Before Avatar Setup becomes interactive, the console requests the current recipe's three
  components first, then preloads all 30 immutable `.gavc` files behind one 0/30 progress page.
- After that preload, Hair/Face/Outfit preset changes are cache-only for the rest of identity setup.
- HairColor and SkinTone changes make no HTTP request and immediately rerun local tint/composition.
- A late response may populate cache but may publish only if its kind/preset still matches the current recipe generation.
- The 30 compressed component files total 1,921,970 bytes. Together with one 132,000-byte RGB565
  preview they occupy 2,053,970 bytes, below the setup-only 2 MiB PSRAM cap. Decoding remains
  streaming; no transparent full-canvas scratch is retained.
- The setup pool remains resident through Avatar Setup, Name Setup, Player Ready, and Countdown so
  a deliberate Back action never returns to a blank editor. Entering Active gameplay releases all
  30 components and the preview in one lifecycle transition.
- Six final RGB565 avatars use 196,608 bytes, fitting the separately frozen 384 KiB gameplay avatar
  cache. The setup-only 2 MiB pool is not charged to the strict 1 MiB gameplay artwork budget.

## Compatibility and versioning

GAVC v1 and palette catalog v1 are immutable. Changes to pixel bytes, tint math, registration, crop geometry, or palette order require a new versioned directory and manifest. The old GAVL pack and whole-preview endpoint remain available during migration but are not the canonical source for new final avatars.
