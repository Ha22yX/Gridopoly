# Grid City Street Assets V3

## Production Set

The V3 set contains 32 unique images:

- 22 undeveloped street properties across eight color groups
- 4 MetroLink transit assets
- 2 civic utilities
- 1 shared Chance cover for all Chance tiles
- 1 shared Community Chest cover for all Community Chest tiles
- 2 distinct fee covers

The four corner tiles are outside this production batch.

## Property Meaning

A property image represents ownership of a street parcel at development level zero. It includes one small unfinished starter shell at the rear of the parcel. This shell explains the base rent but does not count as a built house. The foreground remains a clear development pad. Level 1-4 houses and the final landmark are separate overlays selected by the current building level.

## Shared Rendering Rules

- 1024 x 1024 source master and 160 x 160 device derivative.
- Fixed three-quarter isometric camera viewed slightly from above.
- Flat near-black `#080C0E` background.
- Bold dark outlines, broad shapes and restrained two-step cel shading.
- One centered subject occupying roughly 68-72 percent of the canvas.
- No text is baked into generated art; UI text supplies names and values.
- A property may use only its assigned group accent plus charcoal and off-white.

## Canonical Style Reference

`Assets/GridCity/StreetV3/source/a2-copper-lane.png` is the canonical visual reference for the production set. Every later property must use it as an image reference rather than relying on a text-only prompt. The locked traits are the three-quarter isometric camera, camera distance, long narrow parcel footprint, front and rear boundary positions, substantial border thickness, bold dark outline weight, broad flat colors, restrained two-step cel shading, subtle surface texture, and near-black background.

Text-only generation may be used for concept exploration, but its output must not enter the production asset directory. Formal generation is reference-locked so camera angle, material depth, parcel proportions, and line treatment cannot drift into a flatter or more top-down illustration style.

## Shared Property Prompt

```text
Use case: stylized-concept. Asset type: square game asset master for a 160px round-screen UI. Create a brand-new 1024x1024 Grid City street-property illustration at development level zero.

STYLE AND GEOMETRY LOCK: Match the supplied A2 Copper Lane reference's exact fixed three-quarter isometric camera, camera distance, long narrow parcel proportions, front curb position, rear boundary position, thick substantial border construction, bold crisp dark outline weight, detailed but simplified material treatment, broad flat colors, restrained two-step cel shading, subtle surface texture, perfectly flat near-black #080C0E background, and 68-72% subject occupancy. The new image must look like another tile from exactly the same illustrated asset set, not a flatter line drawing and not a more top-down view.

PROPERTY STATE: exactly one tiny low unfinished starter shell against the rear boundary, occupying no more than 20-22% of the parcel. It explains base rent but does not count as a player-built house. At least 55-58% is a broad completely empty foreground development pad for future Level 1-4 overlays.

STRICT: no completed house, apartment, hotel, tower, factory complex, second building, people, cars, neighborhood, skyline, text, letters, numbers, signage, logo, watermark, loose props, smoke, particles, frame, cast shadow, floor shadow, glow, gradient, fog, scenery or cropping.
```

The machine-readable catalog is `Assets/GridCity/StreetV3/manifests/grid-city-street-assets-v3.json`.
