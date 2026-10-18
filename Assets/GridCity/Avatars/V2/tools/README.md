# Avatar V2 asset compiler

This tool compiles **authored transparent images**, rather than drawing replacement
art. V1 files are read-only. Every output remains a draft until its registration
and skin material have been reviewed. Missing source images are reported as
`blocked`; they are never silently copied from another face or outfit.

## Build and inspect

Run from the repository root with Python 3.12, Pillow and numpy installed:

```powershell
python Assets/GridCity/Avatars/V2/tools/build_avatar_v2.py --review
python -m unittest discover -s tests/assets -p test_avatar_v2_compiler.py
```

The default manifest is `tools/registration.example.json`. To create it in a fresh
checkout, use `--init-template`; this refuses to overwrite an existing file.
Use `--manifest path/to/registration.json` to choose a reviewed manifest.
`--require-reviewed` exits unsuccessfully until an exact visual acceptance record
matches. After review, copy `review/acceptance-candidate.json` to `acceptance.json`,
set `approved: true`, and record the reviewer and evidence. Its registration SHA
and all 31 source/material/encoded-file hashes bind the reviewed artwork; source,
mask, registration, or sample changes invalidate acceptance. Per-entry `reviewed`
booleans alone never grant release approval. The tool never approves its own work.
The compiler does not delete directories or alter original images.

Outputs:

| Path beneath V2 | Purpose |
| --- | --- |
| `device/registered/` | Registered authored colour before skin material inference |
| `device/layers/` | Neutral 320 × 320 production RGBA |
| `device/masks/` | Explicit 320 × 320 skin material, white means tint |
| `runtime/components-v2/` | 31 schema-2 GAVC files and manifest |
| `runtime/avatar-components-v2.json` | Build, geometry, source hashes, budgets, incomplete/review state |
| `review/index.html` | Self-contained offline interactive compositor, 220 × 300, 128 and 24 pixel views |
| `review/*-matrix.png` | 100 hair/face, 100 face/outfit, 80 skin/face inspection matrices |
| `review/geometry/` | Hair-envelope failures and all neck seam candidate pixels |

The HTML embeds and directly decodes its GAVC files, so its interactive controls
work without an HTTP server and without canvas PNG premultiply/unpremultiply
rounding. A known composite checks the JavaScript/Python raw cropped RGBA SHA-256.
Matrix links remain adjacent files. Browser-scaled editor/128/24 views illustrate
layout and are not claimed as pixel-identical LVGL screenshots.

For browser automation, `window.avatarV2Review.renderPixels(recipe)` returns the
raw 320 × 320 RGBA bytes and `renderCrop(recipe)` returns 220 × 300 RGBA bytes.
Recipes use numeric `hair`, `face`, `outfit`, and string `skin`, `hairColor`.
Use these arrays for hash checks, rather than reading canvas antialias pixels.

## Registration metadata

Each `components` entry has a stable `kind`, `presetId`, `source`, and
`registration`. Generated files use `source/face/f01.png` through `f10.png`,
`source/outfit/o01.png` through `o10.png`, `source/hair/h06.png`,
`source/hair/h08-fit2.png`, `source/hair/h10.png`, and `source/neck/n1.png`.
The other seven hairstyles explicitly reuse their V1 registration.

- `sourceBox`: optional authored crop in source pixels. Otherwise the largest
  alpha ≥ 32 connected component supplies its bounds. Adjacent antialias pixels
  survive; disconnected noise is removed only in compiled output and counted.
- `targetBox`: explicit rectangle in the common 320 × 320 canvas.
- `yAnchors`: optional `[sourceY, targetY]` pairs for eye/jaw alignment. The source
  top/bottom are already anchors. Anchor coordinates must increase monotonically.
- `method: common-face`: all faces use F01's exact alpha silhouette; per-row
  registration retains their internal authored features. F01 must exist first.
- `method: neckline`: uses the centered transparent opening in the already
  frontal outfit, maps its top/bottom to shared collar anchors, and registers the
  inner collar edges. This cannot turn a side-facing outfit into a frontal one.
- `neckline`: `topY`, `bottomY`, `leftX`, `rightX`, optionally explicit target rows
  `[y, left, right]`. Inspect changes to lapel/hood proportions after registration.
- `registration.reviewed`: manual evidence flag, never automatically set.

For H06/H08/H10, `registration.innerFit` separately registers the left/right
side-lock **inner** edges against the common head core, excluding the ears.
The face alpha box includes ears and cannot be used as the temple width.
`strengthAnchors` pairs `[y, strength]` blend the transform by height; all vertical
coordinates and the centre line remain fixed. `overlapPixels: 2` targets 2–3
pixels of coverage at the temples. Optional `outerAnchors` rows `[y,left,right]`
also control a compact haircut's outer width; omitting them preserves a long
style's outer contour and length. The authored forehead opening is left alone
until the declared blend range, rather than forcing it into a temple opening.

`auditRows` declares the side-lock region before its designed taper/end.
`minimumOverlap` and `maximumOverlap` reject both a disconnected inner edge and
hair pushed too far across the cheek. Every audited row and its actual transparent
slit count is recorded; missing side locks fail instead of being synthesized.
`review/geometry/hair-interior-*.png` shows the fixed head-core landmark in green,
gaps in red and excessive overlap in yellow. `review_hair_fit.py before/after`
captures 18 combinations and proves that only the three intended GAVC files changed.

Default head bounds are `(94,67)–(226,218)`, shared neck `(122,193)–(198,255)`.
Only the neck component contains neck artwork. Composition is strictly
**neck → face → outfit → hair**. F07 and F10 cannot introduce a wider neck.

## Explicit skin material and Ebony

V2 stores RGBA plus one skin-mask byte per pixel. Positive mask samples have
`R=G=B=shade`. Runtime uses `round(target × shade / 178)`, clamps to 255, then
mixes by the mask byte. All arithmetic uses round-half-up, including source-over.
Alpha is unchanged by colour selection. `rgbaSha256` retains the old manifest
field name but hashes all **five** sample bytes.

`skinMaterial.mask` can point to a manually reviewed, registered 320 × 320 mask.
Without it, inference includes **bright/desaturated skin edges**, protects dark
ink and declared eye regions, and leaves `reviewed: false`. This fixes the old
saturation rule's white-edge omission but is not a semantic guarantee. Inspect
all eight tones, especially Ebony, eye whites and facial details. Adjust explicit
`protectRegions` or supply a corrected mask where necessary.

`sourceLumaReference` sets the authored material baseline (default 178); face and
neck must use the same baseline for matching colour. A `protectRegions` entry is
`{"box":[x0,y0,x1,y1],"mode":"eye"}` for eye whites/irises or `mode: all` for a
fully protected feature. The compiled mask PNG and statistics are always saved.

## Gates and limits

- 31 unique entries; each ≤ 128 KiB; combined encoded files ≤ 2,095,296 bytes.
- Decode each encoded file, validate dimensions, run lengths, exact samples and CRC.
- All faces share exact alpha; all 8 skin and 20 hair recolours preserve alpha.
- Check 1,000 structural recipes, head/hair outer envelopes and neck continuity.
  Hair partings are permitted inside the outer envelope.
- Check declared temple/cheek inner edges for the three revised hairstyles, across
  all ten faces. Outer-envelope success alone does not prove the inner fit.
- Red neck seam pixels remain visible even if connected to outside background;
  enclosed holes fail the structural gate. The fixed audit region is not shrunk
  to conceal candidate gaps. External boundary pixels still require visual review.
- `blocked`, `failed`, `review_required`, and `passed` are distinct states. A
  passing structural gate does not attest to user approval or physical LCD quality.

V2 uses immutable versioned endpoints. This compiler does not deploy a server,
flash firmware, mutate a game, or replace V1 cached assets.
