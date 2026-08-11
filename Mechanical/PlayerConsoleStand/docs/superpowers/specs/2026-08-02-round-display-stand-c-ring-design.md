# Round Display Stand C-Ring Design

## Goal

Add a simple, one-piece locating ring to the existing 35-degree FDM test
stand. The ring surrounds the display module's raised rear interface so the
module has lateral support in addition to the existing pins and locating
holes.

## Confirmed Module Interface

- The raised rear interface is 34.0 mm in diameter.
- It extends 7.6 mm from the module mounting face to the rear of the main
  display body.
- The existing left and right locating posts remain 3.0 mm and 4.0 mm in
  diameter. Their receiving holes remain 3.1 mm and 4.1 mm.
- The 4.0 mm locating feature defines the display's logical six-o'clock
  direction.

## C-Ring Geometry

- Construction: one-piece C-shaped ring, unioned into `STAND_PRINT` and the
  existing circular mating pad.
- Ring center and axis: identical to the existing mating pad and module rear
  interface.
- Inner diameter: 34.5 mm, giving 0.5 mm diametral clearance around the
  nominal 34.0 mm module interface.
- Outer diameter: 40.0 mm, matching the existing mating pad diameter.
- Radial wall thickness: 2.75 mm.
- Height: 7.6 mm measured outward from the module mounting face along the
  mating-plane normal.
- The ring base overlaps the existing mating pad so the exported mesh remains
  one connected printable solid.
- No screws, inserts, snap hooks, or separate components are added.

## FPC Opening

- The existing 9.0 mm FPC groove continues through the lower side of the ring
  for its full 7.6 mm height.
- The result is a C-shaped ring with its opening centered on local X = 0 and
  directed toward negative local V, the stand's front/lower edge.
- The opening must connect continuously to the current pad and wedge grooves;
  no enclosed cable tunnel or sharp blocking wall is allowed.

## Display Orientation Record

- Mechanical feature geometry is not rotated as part of this change.
- In the current stand coordinate system, the 4.0 mm module locating post and
  4.1 mm receiving hole are at local X = +10 mm, local V = 0.
- That feature is the display firmware's logical six-o'clock direction.
- Display content therefore requires a nominal 90-degree rotation relative to
  the stand's current mechanical vertical axis. The clockwise/counterclockwise
  API value must be confirmed on hardware because display drivers use
  different rotation conventions.

## Verification

- Parameter and build-contract tests must record 34.5 mm inner diameter,
  40.0 mm outer diameter, and 7.6 mm height.
- Mesh measurements must verify both ring diameters and the height from actual
  generated geometry rather than custom properties alone.
- Cross-section checks must confirm that the 9.0 mm lower opening remains clear
  through the full ring height.
- The existing pin, hole, FPC groove, 35-degree angle, base footprint, and
  module-clearance tests must continue to pass.
- The final BLEND mesh and exported STL must each contain one edge-connected,
  manifold printable component with no degenerate triangles.

## Printing Limitation

The ring has 0.25 mm nominal radial clearance. Actual FDM hole shrinkage,
layer texture, and XY calibration can make the fit tighter than the CAD model.
The module must be inserted gently during the first physical test and never
forced into the ring.
