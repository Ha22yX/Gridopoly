# ESP32-S3 Round Display Table Stand Design

## Goal

Create a simple, one-piece FDM-printable tabletop stand for the Viewe/SpotPear
ESP32-S3 2.1-inch round rotary display. The display face is fixed at 35 degrees
above the desktop. The debug adapter remains external. This first version is a
test fit and uses printed alignment features instead of screws.

## Confirmed Module Interface

- Display outer diameter: 80.13 mm.
- Rear mounting interface diameter: 34 mm.
- Three M4 female threaded holes are equally spaced at 120 degrees on an
  18 mm pitch circle. The upper M4 hole defines the orientation axis.
- The existing left male locating post is 3 mm in diameter; the right post is
  4 mm in diameter. Both are 3.5 mm high.
- The locating-post centers lie at opposite ends of a 20 mm diameter circle,
  perpendicular to the orientation axis.
- The FPC exits downward between the two lower M4 holes.
- Dimensions not required by the mating interface, including minor rear-shell
  contours, are treated as reference-only geometry in the Blender assembly.

## Stand Geometry

- Construction: one-piece wedge with a flat rectangular footprint, a 35-degree
  mating face, and an internal slicer-controlled infill volume.
- Base footprint: 94 mm wide, 82 mm deep, and 4 mm minimum bottom thickness.
- Base corner radius: 5 mm.
- Coordinate system: the base center is `(0, 0, 0)`, X is left-right, negative
  Y is the user-facing front, and Z is upward.
- Mating plane center: `(0, 3, 28)` mm. Its local vertical axis rises toward
  positive Y at 35 degrees above the desktop. Even if the full 80.13 mm outer
  diameter is conservatively extended to the mating plane, it remains 5.02 mm
  above the desktop and 1.02 mm above the 4 mm base top.
- Mating pad: 40 mm diameter and 2 mm proud of the wedge slope. It supports the
  module's 34 mm rear interface without touching the rotary outer ring.
- Wedge body: 54 mm wide, extending from Y = -18 mm to Y = 32 mm. Its upper
  face is parallel to the mating plane and its lower face merges into the 4 mm
  base.
- The wedge is broad enough to resist normal knob rotation and push forces, but
  contains no enclosure for the external debug adapter.

## Test-Fit Interface

- No screws, inserts, or screw clearance holes are used.
- Three printed male alignment pins are placed on the 18 mm pitch circle:
  - shaft diameter: 2.9 mm;
  - height above mating face: 3.0 mm;
  - 0.5 mm tapered lead-in at the tip;
  - small root fillet for FDM strength.
- Two female clearance holes receive the module's existing locating posts:
  - left diameter: 3.1 mm at local X = -10 mm;
  - right diameter: 4.1 mm at local X = +10 mm;
  - depth from the mating face: 4.0 mm;
  - centers at opposite ends of the 20 mm diameter circle.
- This is a gravity fit with 0.1 mm diametral clearance on both locating posts.
  It is not a snap fit and is not intended for carrying or permanent
  installation.

## FPC Routing

- A 9 mm wide and 2 mm deep open groove runs from the center of the mating
  interface toward the front/lower edge. A 3 mm ramp joins the pad-depth and
  wedge-depth portions so the FPC does not cross a sharp 2 mm step.
- The groove opens at the lower edge of the mating pad and continues down the
  exposed wedge surface, so the FPC can leave without entering an enclosed
  tunnel or making a sharp fold.
- The external adapter board is not modeled as an enclosure component.

## FDM Requirements

- Target materials: PLA or PETG.
- Nominal process: 0.4 mm nozzle, 0.2 mm layer height, four walls, 20-25 percent
  infill.
- All mating dimensions are modeled at final size; slicer scaling remains 100%.
- The receiving-hole clearances follow the confirmed test dimensions; actual
  printer calibration still determines physical fit.
- The design should print flat on its bottom face without required supports.

## Blender Deliverables

- A parameterized Blender scene in millimeters containing:
  - `STAND_PRINT`: the printable one-piece stand;
  - `MODULE_REFERENCE`: a non-exported reference envelope;
  - separate reference markers for the M4 pattern, locating posts, and FPC path.
- An STL containing only `STAND_PRINT`.
- Preview renders showing the stand alone and the display reference assembled.
- A dimension report recording bounding dimensions and all mating coordinates.

## Verification

- The three printed pins are centered on an 18 mm pitch circle and spaced 120
  degrees apart.
- The left 3.1 mm and right 4.1 mm holes are centered on a 20 mm diameter
  circle and symmetric about the upper-M4 orientation axis.
- The FPC slot does not intersect any printed alignment pin.
- The display reference sits at 35 degrees and clears the desktop by at least
  3 mm.
- The exported STL is manifold, contains one connected printable component,
  and uses millimeter-scale dimensions.

## Safety and Limitations

- The threaded-hole depth is not used because the test stand has no screws.
- Printed pins can break if the module is twisted or pulled sideways. The stand
  is for desk testing only.
- Final production fit must be validated with a short physical test print before
  adding retention features or reducing clearances.
