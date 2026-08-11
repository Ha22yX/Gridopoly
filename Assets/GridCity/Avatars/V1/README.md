# Gridopoly Avatar V1 Prototype

This prototype validates independent avatar composition for the player console.

## Layers

- Hair presets: `H01` angular crop, `H02` asymmetric city bob, `H03` textured quiff,
  `H04` curly fade, `H05` classic side part, `H06` short coils, `H07` shoulder waves,
  `H08` braided crown, `H09` high bun, `H10` long straight
- Face presets: `F01` angular oval, `F02` soft square, `F03` round youthful,
  `F04` heart taper, `F05` elegant oblong, `F06` diamond angular, `F07` broad strong,
  `F08` soft triangle, `F09` narrow refined, `F10` mature sculpted
- Outfit presets: `O01` utility bomber, `O02` operations jacket, `O03` streetline hoodie,
  `O04` metro varsity, `O05` civic blazer, `O06` workshop vest, `O07` signal knit,
  `O08` denim commuter, `O09` rainline shell, `O10` gala tailored
- Hair colors (20): graphite, copper, raven, espresso, chestnut, golden, platinum,
  city teal, ash brown, auburn, mahogany, honey blonde, strawberry blonde, silver,
  snow white, burgundy, rose pink, violet, cobalt blue and emerald
- Skin tones: porcelain, fair, warm, golden, olive, bronze, deep and ebony

The fixed back-to-front composition order is `face -> outfit -> hair`. Per-preset registration
transforms align the eye line, chin, neckline and shoulders before compositing. Every source layer uses
the same 1254 x 1254 registration canvas. Device layers are exported as transparent
320 x 320 PNG build inputs. The canonical runtime output is 30 cropped neutral GAVC components.
The player console downloads only the selected Hair, Face, and Outfit structures, applies HairColor
and SkinTone locally with the frozen fixed-point algorithm, and composites the 220 x 300 preview.
The Raspberry Pi uses those exact same components and integer operations for final avatars.

## Paths

- `source/layers/`: lossless transparent masters and retained chroma-key sources
- `device/layers/`: runtime-sized transparent layers
- `device/avatar-*.png`: 271 pairwise structural previews; all 1,000 combinations remain live-composable
- `avatar-layers.manifest.json`: layer IDs, colors, canvas sizes and file mapping
- `runtime/avatar-v1.layers`: canonical 290-layer service-side runtime pack
- `runtime/avatar-v1.layers.json`: pack count, byte length and digest used by deployment checks
- `runtime/components-v1/`: 30 canonical neutral GAVC v1 component files
- `runtime/avatar-components-v1.json`: per-file/decoded SHA-256, geometry, sizes and cache bound
- `review/avatar-customizer.html`: interactive local prototype
- `review/avatar-console-ui-concepts.html`: three interactive 480 x 480 player-console UI concepts
- `review/avatar-name-flow-preview.html`: interactive name review, full-surface handwriting and server-synchronized player readiness flow
- `review/avatar-layer-combinations-v1.png`: 271 pairwise structural samples
- `review/avatar-face-presets-v1.png`: compact F01-F10 comparison sheet
- `review/avatar-outfit-presets-v1.png`: compact O01-O10 neckline and shoulder comparison sheet
- `review/avatar-color-combinations-v1.png`: programmatic recoloring proof
- `review/avatar-skin-tones-v1.png`: all eight skin tones on identical face geometry

Run `python tools/build_avatar_prototype.py` after replacing or adding a source
layer. Hair and skin colors are derived programmatically while ink, eyes and
shading remain intact.

Run `python tools/build_runtime_layer_pack.py` after the manifest/device layers change. The service-side
HTTP outputs are frozen as follows:

Run `python tools/build_avatar_components.py` after neutral source geometry changes, then run
`python ../../../../Server/RaspberryPi/tools/verify-avatar-components.py`. Component routes, binary
layout, palettes, integer tint math, composition, hashes and memory limits are frozen in
`../../../../Docs/firmware/avatar-component-protocol.md`. The 290-layer GAVL pack remains only for
rollback compatibility; new preview and final rendering does not read it.

- recipe preview: `220 x 300`, RGB565 little-endian, 132000 bytes, background `#061017`;
- final console avatar: `128 x 128`, RGB565 little-endian, 32768 bytes;
- final web avatar: `128 x 128`, circular RGBA PNG.

Preview and final files are generated lazily, atomically published, and cached by immutable recipe or
room/player/revision/content-hash keys. See `Docs/firmware/wifi-udp-player-protocol.md`, section 9.3, for
the exact public routes and cache headers.

The local customizer stores independent X/Y calibration values for every preset.
Use `COPY ALL OFFSETS` to export the complete schema 2 JSON after visual calibration.
Accepted values are baked into `deviceOffsets` in the manifest; the customizer then
shows those absolute baseline values while applying only the difference from the baked
baseline, so the same correction cannot be applied twice. `H05`, `H06`, and `H08` use
their narrower `v2` source masters.
See `OUTFIT_GENERATION_SPEC.md` for the mandatory neck-safe asset contract.
The canonical generation, layer, recolor and per-preset calibration rules are in
`../../../../Docs/player-console/player-avatar-setup-spec.md`, section 7.
