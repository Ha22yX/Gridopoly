# Gridopoly Player Console Stand CW47.5 Delivery

## Rotation Revision

Baseline: CW50 (`50.0 degrees clockwise`). Target: CW47.5 (`47.5 degrees
clockwise`). The exact change is `-2.5 degrees` clockwise, which is a 2.5
degree counterclockwise rotation square-on to the mounting face. The
authoritative parameter is `StandParameters.mating_pattern_rotation_clockwise_deg`.

The rigidly rotated mating/interface set comprises:

- all three 2.9 mm M4 locating/alignment pins;
- both 3.1 mm / 4.1 mm stand receiving holes and their paired 3.0 mm / 4.0 mm
  module-reference locating posts;
- the C-shaped interface surround, including its 9.0 mm opening;
- the FPC connector/cable groove, its cutout, and its pad-to-wedge transition;
- M4, locator, and FPC reference axes/path markers.

The targeted lower M4 locating pin moved from CW50 local center
`(1.562833599, -8.863269777) mm` to CW47.5 local center
`(1.947956525, -8.786664064) mm`; the precise-angle regression verifies this
additional 2.5 degree counterclockwise advancement.

## Unchanged Geometry

- stand body, flat print orientation, 35.0 degree wedge/mating angle;
- footprint `94.0 x 82.0 mm`, base thickness `4.0 mm`, and all clearances;
- M4 18.0 mm PCD, pin diameter `2.9 mm`, and pin height `3.0 mm`;
- locator 20.0 mm PCD, hole diameters `3.1 / 4.1 mm`, and depth `4.0 mm`;
- C-ring `34.5 mm ID`, `40.0 mm OD`, and `7.6 mm` height;
- FPC groove `9.0 mm` width and `2.0 mm` depth.

## Delivered Files

- `C:\Users\Administrator\Desktop\Gridopoly\Mechanical\PlayerConsoleStand\outputs\models\Gridopoly_Player_Console_Stand_CW47_5.stl`
- `C:\Users\Administrator\Desktop\Gridopoly\Mechanical\PlayerConsoleStand\outputs\models\Gridopoly_Player_Console_Stand_CW47_5.blend`
- `C:\Users\Administrator\Desktop\Gridopoly\Mechanical\PlayerConsoleStand\outputs\renders\Gridopoly_Player_Console_Stand_CW47_5_interface.png`

CW50 deliverables were preserved. The named CW47.5 STL and BLEND are
byte-identical copies of the tested deterministic working outputs.

## Blender and Export Command

```powershell
& "D:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --python scripts\build_stand.py
```

`build_stand.py` saves the BLEND and exports the selected, triangulated
`STAND_PRINT` mesh as binary STL using Blender's `bpy.ops.wm.stl_export` with
`global_scale=1.0`.

## Mesh QA

- STL SHA-256: `45564E74A329E79CA2ECB9BEB63CE78DE9CA942397F3B1185495AEC2E1EA70D7`
- STL bounds: `94.0 x 82.0 x 45.864471 mm`
- triangle count: `4188`
- closed connected components: `1`
- manifold/non-manifold edges: manifold, `0` non-manifold edges
- degenerate triangles: `0`
- Blender scene connected components: `1`; Blender non-manifold edges: `0`
- rendered interface preview: `1200 x 900`, nonblank, visually compared with
  the preserved CW50 preview; the lower interface set and C-ring opening move
  counterclockwise together without disrupting the mating pattern.

## Test Results

All `41` existing mechanical tests passed after regeneration:

- `tests/test_stand_parameters.py`: 9 passed, including the exact CW47.5 and
  CW50-to-CW47.5 angle regression;
- `tests/test_build_contract.py`: 5 passed;
- `tests/test_geometry_build.py`: 13 passed;
- `tests/test_verify_stand.py`: 12 passed;
- `tests/test_render_outputs.py`: 2 passed.

`scripts/verify_stand.py` also completed with `VERIFICATION PASS` on the
exported STL.
