# Avatar V2 — a shared fitting system

Status: candidate in development; V1 remains the live catalog until V2 passes visual and runtime review.

Every combination uses the same head registration, one shared neck, and front-facing clothing. Face presets retain their personality through eyes, brows, cheeks and jaw detail, not arbitrary skull scale or neck width. F07 no longer has a larger head; F07 and F10 no longer carry their own neck.

## Layer contract

The order is **shared neck → face → outfit → hair**. All sources are genuine transparent PNGs. The source neck is shared by all ten faces and outfits. Clothes contain no skin and face layers end at the chin. All shoulders are level; the body and collar are centered and frontal. Asymmetric garment details are allowed; a rotated torso is not.

The 320×320 registration canvas, 220×300 editor crop at (50,10), and 128×128 final portrait are checked separately. Registration is performed during asset compilation, not differently on each device. Existing preset IDs and 20 hair / 8 skin color selections remain available.

## Hair must fit the inner contour

A hair layer enclosing the skull is not enough. The face's full bounding box includes ears and is wider than its temples. Using that box for the hair opening leaves visible gaps, even when every scalp-envelope check passes.

Short Coils (H06) and Braided Crown (H08) need a compact outer silhouette and an inner edge that overlaps the common temple contour slightly. Long Straight (H10) needs its inner curtains aligned independently of its outer width and length: excessive cheek overlap is also a fitting failure. The compiler records explicit temple/jaw landmarks, measures gaps at the actual join, and retains before/after evidence. No per-face runtime offsets are used to compensate for a poorly fitting hairstyle.

## Skin materials

V1 guessed skin from luminance and saturation at runtime. This can leave pale highlights and edge pixels unchanged, especially with Ebony. V2 carries an explicit per-pixel skin material mask. Skin and neck use the same neutral shading reference. Eye whites, pupils, brows and ink are reviewed separately. Transparent edges must not contain white or magenta matte contamination.

GAVC uses the existing 32-byte header with schema 2, encoding 2. RLE samples are RGBA + skinMask (5 bytes); decodedBytes and CRC cover all five bytes. Kind 4 / preset 1 is `neck/n1.gavc`. Skin shading uses neutral R=G=B, base 178; tint is round-half-up(target × shade / 178), clamped to 255, then mixed with the original pixel using skinMask/255. Alpha is unchanged. V1 decoding and tint remain available for existing avatars.

## Release and checks

- Immutable catalog V2 paths; never overwrite V1 components or preview caches.
- 31 components, each ≤128 KiB; total compressed components ≤2,095,296 bytes.
- All 1,000 structural combinations, 100 hair/face pairs, 100 face/outfit pairs and all eight skin tones are checked. Recoloring must not change coverage.
- Source registration, skin masks and both full and cropped previews are reviewable artifacts. Mechanical checks alone do not prove visual quality.
- Existing confirmed avatars remain unchanged. A candidate build or image sheet is not evidence of physical-screen acceptance.

Artwork is generated with the built-in image generation tool; prompts and selected source files are recorded in `generation-log.json` and `hair-fit-generation-log.json`. The compiler performs reproducible registration, material encoding and compositing.
