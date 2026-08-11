# Gridopoly Modular Outfit Generation Contract

This contract applies to every outfit preset generated for the layered avatar system.

## Registration master

- Use the current front-facing base avatar as the only geometry reference.
- Preserve the 1254 x 1254 canvas, shoulder endpoints, torso scale and neckline position.
- Do not use an older complete character illustration as the registration reference.

## Mandatory neck-safe region

- Generate clothing only. Never generate head, face, ears, hair, skin or neck pixels.
- The complete neck silhouette and every pixel above the base avatar's original neckline must remain transparent.
- The garment opening must closely follow the base avatar's neck width. It must not use an oversized V opening.
- A collar or lapel may exist only outside the neck-safe alpha region. It must not cover the neck or chin.
- Outfit geometry may overlap the face layer only below the original neckline.
- `O01 Utility bomber v2` is the narrow-neck registration baseline.
- `O02 Operations jacket` is an accepted compliant preset and must be retained.

## Visual contract

- Front-facing orthographic 2D cel-shaded Gridopoly game art.
- Thick, clean dark contour with angular polygon shading.
- One garment centered on a uniform pure chroma magenta background before alpha extraction.
- No text, logo, body shadow or decorative object outside the garment.

## Composition order

Runtime composition is back-to-front: `face -> outfit -> hair`. This makes hair the
frontmost layer while the outfit remains above the face layer only in the torso region.

## Prompt suffix

Use this hard constraint in every outfit prompt:

> Clothing layer only. Match the supplied base avatar's exact neck width, neckline and
> shoulder registration. Keep the full neck and all pixels above the original neckline
> pure chroma magenta. No neck, skin or head pixels may appear. Any collar or lapel must
> stay outside the neck-safe alpha region and must not cover the neck or chin.
