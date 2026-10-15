# Virtual geometry rework feasibility — digital simulation only; not validated on a physical board

Original file: Gridopoly_PCB1_20260927_204456.zip. Only the simulated **top-layer copper** was modified; the bottom copper, through-hole barrels and pad annuli were left unchanged. No modified Gerber or EDA design was generated.

## Simulation result

Removing short sections from the three incorrect J3.2 thermal spokes separates BAT_PACK and GND. The following connections remain intact:

- J3.2 → C2.1, C19.1, U1.2, U1.3, U10.2, U10.3.
- J3.1 → C2.2, U1.4, U10.1, U10.4.
- SYS_RAW remains separate.

All 62 named electrical networks in the ZIP's FlyingProbeTesting.json (including separately named unused pins) have no shorts, opens or missing pad contact after the virtual removal, using actual plated-drill coordinates for layer connections.

## Virtual removal rectangles

Coordinates are millimetres in the original Gerber frame. Top/component view with USB-C below: negative Y points down from the board's top edge. These are analytical regions, not a drill template.

| Spoke | Rectangle centre X,Y | Width × height | Bounds Xmin,Ymin,Xmax,Ymax |
|---|---|---|---|
| Upper | 41.00068, -4.07416 | 0.40 × 0.10 | 40.80068, -4.12416, 41.20068, -4.02416 |
| Lower | 41.00068, -5.92836 | 0.40 × 0.10 | 40.80068, -5.97836, 41.20068, -5.87836 |
| Right | 41.92778, -5.00126 | 0.10 × 0.40 | 41.87778, -5.20126, 41.97778, -4.80126 |

Each removes 0.0254 mm² of actual top copper, producing a 0.10 mm virtual gap across the 0.254 mm spoke. All removal rectangles stay 0.077 mm outside the outer radius of the PTH copper pad and 0.4771 mm outside the drilled hole radius.

## Physical limitations that matter

**The lower rectangle projects onto the BAT trace on the bottom side.** Simulated bottom-copper overlap is 0.035268 mm². The virtual check preserves the bottom layer; a real cut through the PCB, drilling, or cutting the backside at that location would risk opening the battery path. Surface-only access and depth control are essential. The 0.077 mm pad-edge margin is small.

A clear photograph of the actual J3 is required to assess access. The installed connector may cover the top copper and may have to be removed for professional rework. No actual board has been reworked by this analysis. Do not infer from the simulation that soldering, accessibility, isolation, or post-repair charging and USB operation are validated. Previously damaged components remain possible.
