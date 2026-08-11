# ESP32-S3 Round Display Test Stand

One-piece, 35-degree desktop stand for the Viewe/SpotPear ESP32-S3
2.1-inch round push-knob display. This version is a simple test fixture: it
uses printed locating pins and does not use screws or retain the display for
carrying.

## Main dimensions

- Display reference diameter: 80.13 mm
- Stand footprint: 94 x 82 mm
- Minimum base thickness: 4 mm
- Display angle: 35 degrees above the desktop
- Printed alignment pins: 3 x 2.9 mm diameter, on an 18 mm pitch circle
- Module locating posts: left 3.0 mm, right 4.0 mm diameter
- Module-post holes: left 3.1 mm, right 4.1 mm diameter, on a 20 mm pitch circle
- C-ring: 34.5 mm ID, 40.0 mm OD, 7.6 mm high
- C-ring lower opening: 9.0 mm wide, continuous with the FPC groove
- FPC groove: 9 mm wide and 2 mm deep
- Conservative display-to-desktop clearance: 5.02 mm
- Worst-case clearance above the 4 mm base top: 1.02 mm

The three printed pins enter the module's M4 threaded holes only for
alignment. The two holes receive the module's existing left 3 mm and right
4 mm locating posts. Each hole has 0.1 mm specified diametral clearance.
The integrated C-ring surrounds the module's 34 mm raised rear interface with
0.25 mm nominal radial clearance.

## Display orientation

The complete mating pattern is rotated clockwise by 47.5 degrees. This is a
2.5-degree counterclockwise rotation from the prior CW50 model. The three M4
alignment pins, two locator holes, FPC groove, and C-ring opening rotate as one
rigid mating interface. Firmware content is rendered at the panel's native
zero-degree orientation; confirm final visual trim on the physical printed
fixture.

## Generated files

The delivered `ScreenStand` folder contains these artifacts. They are ignored
by Git because the scripts recreate them; run the commands below after a clean
checkout.

- `outputs/models/round_display_test_stand.stl`: printable stand only
- `outputs/models/Gridopoly_Player_Console_Stand_M4_Pin_Down.stl`: named printable delivery with M4 pin 3 at six o'clock
- `outputs/models/Gridopoly_Player_Console_Stand_CW50.stl`: prior named printable CW50 delivery
- `outputs/models/Gridopoly_Player_Console_Stand_CW47_5.stl`: named printable CW47.5 delivery
- `outputs/models/round_display_test_stand.blend`: stand and reference assembly
- `outputs/models/Gridopoly_Player_Console_Stand_CW50.blend`: prior named CW50 source assembly
- `outputs/models/Gridopoly_Player_Console_Stand_CW47_5.blend`: named CW47.5 source assembly
- `outputs/renders/stand_print.png`: printable-part preview
- `outputs/renders/stand_assembled.png`: assembled reference preview
- `outputs/reports/dimensions.txt`: human-readable verification report
- `outputs/reports/dimensions.json`: machine-readable verification report

## Recommended FDM settings

- Print scale: 100 percent
- Orientation: flat base on the build plate
- Nozzle: 0.4 mm
- Layer height: 0.2 mm
- Walls: 4
- Infill: 20-25 percent
- Material: PLA or PETG
- Supports: not required by the model

Remove any first-layer flare around the base. Test the C-ring, two module posts,
and three printed pins gently before seating the display fully. The C-ring's
0.25 mm radial clearance can print tight on an uncalibrated machine. Do not
force, twist, or carry the display by this test stand; the small printed pins
can break under side load.

## Rebuild and verify

Run these commands from this directory with Blender 5.2 installed at the path
shown below:

```powershell
& "D:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --python scripts\build_stand.py
& "D:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background outputs\models\round_display_test_stand.blend --python scripts\verify_stand.py
& "D:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background outputs\models\round_display_test_stand.blend --python scripts\render_stand.py
```

The verifier loads the exported STL back into Blender and measures the main
dimensions, pin and hole patterns, C-ring opening at bottom/middle/top, and
desktop clearance from that delivered mesh. It also checks both BLEND and STL
topology, including collinear zero-area triangles.
