# Gridopoly Avatar V2

**A common head, one neck, and clothes that face the same way.**

This is the next avatar catalog, currently a candidate. The live game continues to use V1 until the new server, player firmware and artwork are deployed together. Existing confirmed avatars are preserved.

The current 31-file pack is **1,353,679 bytes**. Digital geometry, material and browser checks pass; physical-screen acceptance is still pending. The [review record](acceptance.json) binds the exact source, registration, mask and component hashes.

[Try the combinations](review/index.html) · [Design rules](DESIGN.md) · [Generation prompts](generation-log.json) · [Hair fitting revision](hair-fit-generation-log.json) · [Build evidence](review/build-report.json)

## What changed

- Ten newly drawn, neck-free faces share the same registered head silhouette, height, temple width and eye line. F07 no longer has an oversized head. F07 and F10 no longer bring their own wider necks.
- One independent neck sits behind every face and outfit. It uses the same skin material as the face.
- All ten outfits were redrawn facing forward: level shoulders, centered collar, straight body axis. Garment colors and their existing IDs remain familiar.
- Short Coils, Braided Crown and Long Straight were redrawn and registered using the inner temple/cheek contour. The other seven hairstyles retain their artwork. Hair openings are checked independently of the outer scalp envelope.
- An explicit skin mask replaces runtime color guessing. Ebony tints skin highlights and edge pixels while retaining eye whites and facial ink.

There are still **10 hair × 10 face × 10 outfit = 1,000 structural combinations**, with 20 hair colors and 8 skin tones. The common neck is not another choice the player has to configure.

## Look inside

| Folder | Contents |
| --- | --- |
| `source/` | 24 selected transparent images: ten faces, ten outfits, one neck and three hairstyles; the earlier H08 candidate is also retained |
| `tools/` | Reproducible registration, material encoding, GAVC compilation and review-page generation |
| `device/registered/` | Registered artwork before material conversion |
| `device/layers/`, `device/masks/` | Neutral runtime layers and inspectable skin masks |
| `runtime/components-v2/` | 31 versioned components: ten faces, ten outfits, ten hairstyles, one neck |
| `review/` | Interactive editor, combination matrices, seam diagnostics and build evidence |

Generation used the built-in image tool. Original generated files are retained; the selected copies and their exact prompts are included here. Seven reused hairstyles are traced to V1 by the build manifest. No V1 source or component is overwritten.

## Build and review

Use Python with Pillow and NumPy. From the repository root:

```powershell
python Assets/GridCity/Avatars/V2/tools/build_avatar_v2.py --review
python -m unittest discover -s tests/assets -p 'test_avatar_v2*.py'
```

After inspecting the actual output, record the reviewer and scope in `acceptance.json`, starting from `review/acceptance-candidate.json`. `build_avatar_v2.py --review --require-reviewed` fails unless the approval matches the exact current inputs and components. A previous approval cannot silently carry over to changed artwork.

The compiler checks all 31 component files, their decoded checksums, shared face coverage, skin-color coverage invariance, scalp fit and neck seams. All components together must fit within 2,095,296 bytes; each must remain below 128 KiB. Missing artwork and failed checks cannot be called a complete pack.

The review page includes full-size art, the editor crop, small avatars, the separate skin layers and masks. Inspect the 100 hair/face joins, 100 face/outfit joins and all 80 face/skin combinations. Visual approval and physical-screen acceptance are separate from a successful build.

The additional hair-fitting revision has 18 before/after combinations: [Short Coils](review/hair-fit/h06-comparison.png), [Braided Crown](review/hair-fit/h08-comparison.png), [Long Straight](review/hair-fit/h10-comparison.png). The dedicated inner-contour audit checks these three hairstyles against every face; the outer scalp check alone is insufficient.

The user confirmed these three corrected styles fit normally in the browser review. This is recorded separately from the pending physical-screen check.

## Runtime compatibility

V2 uses `catalogVersion=2` and GAVC schema/encoding 2. A sample is RGBA plus a skin-material byte. Composition is **neck → face → outfit → hair**. V1 retains its three-layer format and its existing URLs.

New V2 URLs and caches are separate, so an old downloaded layer cannot be mixed into a new portrait. The server defaults to V1; `GRIDOPOLY_AVATAR_CATALOG_VERSION=2` is a coordinated rollout setting, not something to enable before the screens support V2. See the [streaming contract](../../../../Docs/firmware/avatar-component-protocol.md) for routes and byte-level rules.
