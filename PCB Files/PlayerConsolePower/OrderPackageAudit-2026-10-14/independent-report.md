# Independent order-file check — 2026-10-14 (America/New_York)

Source: `C:/Users/kicof/Downloads/Gridopoly_PCB1_20260927_204456.zip`
SHA-256: `2344f34c45307a879c9a3274bddd53681152dcf42d9fb47637cb690d554405b6`

## Confirmed finding

The submitted Gerber itself shorts J3 pin 2 / BAT_PACK to GND. It is not merely a manufacturing-process hypothesis. With board top viewed from the component side and USB-C at the bottom, J3 is near the top centre: pin 1 at (38.99916, -5.00126) mm is GND, pin 2 at (41.00068, -5.00126) mm is BAT_PACK. Coordinate signs follow the Gerber origin; physical board distances from top edge are about 5 mm.

The source ZIP's own FlyingProbeTesting.json explicitly declares J3_1=GND, J3_2=BAT_PACK. Yet the top copper puts J3_2, C2.2 and U1.4 in the same continuous polygon. Full through-hole connectivity joins J3_1 into this same component through the bottom ground plane. C3.1 / SYS_RAW stays in a distinct component, consistent with the user's high SYS_RAW resistance measurement.

## Exact raw proof

GTL aperture `%ADD11C,0.254*%` is selected in the copper-fill output section. The following dark copper strokes appear at source lines 4944–4947:

```
G01X4100068Y-500126D02*
G01X4100068Y-394462D01*
G01X4100068Y-500126D02*
G01X4100068Y-605790D01*
```

They are 0.254 mm wide spokes from pin 2 upward and downward into the surrounding ground copper. Source lines 5176–5177 add a right-hand spoke:

```
G01X4100068Y-500126D02*
G01X4205732Y-500126D01*
```

These three dark thermal spokes are in `Copper Start/End`, rather than solder mask or silkscreen. The copper is already shorted before assembling any component.

## Independent method

Gerbonara parses each entire top/bottom file; all 2,230 top primitives and 465 bottom primitives are dark. Shapely unions the primitives without gap-closing tolerance (arcs approximated to 1 µm). Cross-layer joins come from 170 numeric X/Y entries in the ZIP's two plated drill files, rather than a CAD/live-via snapshot. No unplated drill or SMD pad is used for a cross-layer join. J3 through pads join both layers; SMD nets use only their copper side. This independently reproduces the parent's full-net check.

## RC5 comparison

Frozen `Layout-2026-09-27-RC5/NativeExports/PlayerPower-R1-RC5-Gerber.zip` has the thermal spokes on pin 1/GND and clearance around pin 2/BAT_PACK. BAT and GND are distinct connected components there.

The major top-copper geometric changes between the user file and RC5 are at J3: two local difference groups of approximately 0.1961 mm² each. Minor differences elsewhere total 0.00654 mm² in top copper and 0.00294 mm² in bottom copper; these are not evidence of another hard short. Current DRC0 on the rebuilt/current file does not establish the earlier ordered Gerber was safe.

The pattern is specifically consistent with a pin-polarity change followed by export while old ground copper thermals remained. Historical notes independently record that issue and a later RC5 copper rebuild. We can confirm file geometry; we cannot establish who uploaded which file when, factory process, or whether components have since suffered damage.

## Artifacts

- `independent-connectivity.json`: direct pad/ground/through-hole connectivity evidence.
- `geometry-difference.json`: Gerber-to-RC5 copper geometry differences.
- `independent-j3-comparison.png`: side-by-side top copper with drilled holes.
- `full-independent.py`, `rawcheck.py`, `difference.py`: scripts, absolute source paths; no changes to source ZIP, EDA or repository.

Do not energize affected boards until rework and resistance verification. No physical rework has been performed or validated by this analysis.
