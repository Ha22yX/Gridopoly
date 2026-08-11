# Round Display Test Stand Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, verify, render, and export a one-piece 35-degree FDM tabletop test stand for the ESP32-S3 2.1-inch round display.

**Architecture:** Keep all dimensions and mating-coordinate calculations in a Blender-independent parameter module. A Blender builder consumes those values to create and boolean-union the printable stand, while a separate verifier checks object geometry, mating coordinates, mesh manifoldness, and the exported STL bounds. Rendering and artifact generation remain repeatable Blender scripts.

**Tech Stack:** Blender 5.2 Python API (`bpy`, `bmesh`, `mathutils`), Python `unittest`, binary STL inspection, Blender MCP for live inspection.

## Global Constraints

- The display face is fixed at 35 degrees above the desktop.
- The stand is one FDM-printable component with a 94 x 82 mm footprint and a 4 mm minimum base.
- The debug adapter remains external.
- Three printed male pins are 2.9 mm diameter and 3.0 mm tall on an 18 mm pitch circle at 120-degree intervals.
- The left/right receiving holes are 3.1/4.1 mm diameter and 4.0 mm deep at opposite ends of a 20 mm diameter circle.
- No screw holes, inserts, snap fits, or retention hardware are allowed.
- The FPC uses a 9 mm wide, 2 mm deep open groove toward the front/lower edge.
- The exported STL must contain one connected manifold component at millimeter-scale dimensions.

---

## File Map

- `scripts/stand_parameters.py`: immutable dimensions, local/world coordinate transforms, and mating-pattern coordinates.
- `scripts/build_stand.py`: Blender mesh construction, booleans, reference assembly, materials, collections, BLEND save, and STL export.
- `scripts/verify_stand.py`: Blender scene checks, manifold checks, STL-bound checks, and JSON/text reports.
- `scripts/render_stand.py`: deterministic cameras, lighting, and preview rendering.
- `tests/test_stand_parameters.py`: Blender-independent unit tests for dimensions and coordinate geometry.
- `README.md`: build, print, fit, and artifact instructions.
- `outputs/models/round_display_test_stand.blend`: editable Blender scene.
- `outputs/models/round_display_test_stand.stl`: slicer-ready stand only.
- `outputs/renders/stand_assembled.png`: display-reference assembly view.
- `outputs/renders/stand_print.png`: printable stand view.
- `outputs/reports/dimensions.json`: machine-readable verification report.
- `outputs/reports/dimensions.txt`: readable dimension summary.

---

### Task 1: Parameter Model and Coordinate Tests

**Files:**
- Create: `scripts/stand_parameters.py`
- Create: `tests/test_stand_parameters.py`

**Interfaces:**
- Produces: `StandParameters`, `local_to_world(x_mm, v_mm, normal_mm)`, `m4_pin_local_positions()`, and `locator_hole_local_positions()`.
- Consumed by: `build_stand.py` and `verify_stand.py`.

- [ ] **Step 1: Write failing geometry tests**

```python
def test_m4_pattern_uses_18_mm_pitch_circle_and_120_degree_spacing():
    points = m4_pin_local_positions()
    assert all(math.isclose(math.hypot(x, v), 9.0, abs_tol=1e-9) for x, v in points)
    distances = sorted(math.dist(points[i], points[(i + 1) % 3]) for i in range(3))
    assert all(math.isclose(distance, 15.5884572681, abs_tol=1e-6) for distance in distances)

def test_locator_holes_are_opposite_on_20_mm_circle():
    assert locator_hole_local_positions() == ((-10.0, 0.0), (10.0, 0.0))

def test_mating_center_gives_conservative_clearance():
    vertical_radius = 80.13 / 2 * math.sin(math.radians(35.0))
    assert math.isclose(28.0 - vertical_radius, 5.015, abs_tol=0.01)
```

- [ ] **Step 2: Run tests and verify the missing module failure**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests/test_stand_parameters.py
```

Expected: non-zero exit because `scripts.stand_parameters` does not exist.

- [ ] **Step 3: Implement exact parameter and transform functions**

```python
@dataclass(frozen=True)
class StandParameters:
    angle_deg: float = 35.0
    base_width_mm: float = 94.0
    base_depth_mm: float = 82.0
    base_thickness_mm: float = 4.0
    mating_center_mm: tuple[float, float, float] = (0.0, 3.0, 28.0)
    m4_pcd_mm: float = 18.0
    printed_pin_diameter_mm: float = 2.9
    printed_pin_height_mm: float = 3.0
    locator_pcd_mm: float = 20.0
    locator_post_diameters_mm: tuple[float, float] = (3.0, 4.0)
    locator_hole_diameters_mm: tuple[float, float] = (3.1, 4.1)
    locator_hole_depth_mm: float = 4.0
```

Define local U as world X, local V as `(0, cos(35 deg), sin(35 deg))`, and the outward mating normal as `(0, -sin(35 deg), cos(35 deg))`. Return M4 coordinates `((0, 9), (-7.794228634, -4.5), (7.794228634, -4.5))` and locator coordinates `((-10, 0), (10, 0))`.

- [ ] **Step 4: Run all parameter tests**

Expected: all tests pass and process exits zero.

- [ ] **Step 5: Commit the parameter model**

```powershell
git add scripts/stand_parameters.py tests/test_stand_parameters.py
git commit -m "feat: define test stand geometry parameters"
```

---

### Task 2: Printable Blender Geometry

**Files:**
- Create: `scripts/build_stand.py`
- Modify: `scripts/stand_parameters.py`

**Interfaces:**
- Consumes: all `StandParameters` dimensions and local/world transforms.
- Produces: Blender objects `STAND_PRINT`, `MODULE_REFERENCE`, `M4_PATTERN_REFERENCE`, `LOCATOR_REFERENCE`, and `FPC_PATH_REFERENCE`.

- [ ] **Step 1: Add a build-contract test mode**

Add `--dry-run` handling that imports parameters, calculates all primitive transforms, and prints a JSON contract without editing a Blender scene. Assert the contract includes exactly three male pins, two female cutters, a 35-degree mating plane, and a 9 mm groove.

- [ ] **Step 2: Run dry-run before implementation**

Expected: non-zero exit because `build_stand.py` does not exist.

- [ ] **Step 3: Build named solid primitives**

Implement these exact interfaces:

- `create_rounded_base(params: StandParameters) -> bpy.types.Object` creates the
  beveled 94 x 82 x 4 mm bottom solid and returns the applied mesh object.
- `create_wedge(params: StandParameters) -> bpy.types.Object` creates the
  54 mm wide triangular-prism support whose top follows the mating plane.
- `create_mating_pad(params: StandParameters) -> bpy.types.Object` creates the
  40 x 2 mm oriented circular contact pad with its front face at the mating
  center.
- `create_alignment_pin(name: str, point: Vector, normal: Vector, params:
  StandParameters) -> bpy.types.Object` creates one 2.5 mm shaft and 0.5 mm
  tapered tip aligned to `normal`, unions both parts, and returns the result.
- `create_cylindrical_cutter(name: str, point: Vector, normal: Vector,
  diameter_mm: float, depth_mm: float) -> bpy.types.Object` places a cutter with
  its front face 0.1 mm beyond the mating plane and its axis along `normal`.
- `apply_boolean(target: bpy.types.Object, operand: bpy.types.Object,
  operation: str) -> None` applies Blender's exact Boolean solver, verifies the
  modifier result, and removes the operand object.

Use exact booleans. Overlap the wedge into the base by 0.5 mm and the pad into the wedge by 1.0 mm so the final union has one connected volume. Build each 2.9 mm pin from a 2.5 mm cylinder plus a 0.5 mm tapered lead-in, then union it into the mating pad.

- [ ] **Step 4: Cut locator holes and FPC groove**

Cut 3.1 mm and 4.1 mm diameter cylinders 4.0 mm into the mating face at local coordinates `(-10, 0)` and `(10, 0)`, respectively. Cut a 9 x 30 x 2 mm oriented box from local V = 2 mm to local V = -28 mm so the groove opens at the lower wedge edge without intersecting the lower M4-pattern pins.

- [ ] **Step 5: Create non-exported module references**

Create a translucent 80.13 mm outer envelope, a 34 mm rear interface, left 3 x 3.5 mm and right 4 x 3.5 mm module posts, and three M4-axis markers in a `REFERENCE` collection. Mark all reference objects with `hide_render = False` but never select them for STL export.

- [ ] **Step 6: Join solids and save/export**

Boolean-union the base, wedge, mating pad, and pins into `STAND_PRINT`; apply transforms; select only `STAND_PRINT`; save the scene to `outputs/models/round_display_test_stand.blend`; export binary STL to `outputs/models/round_display_test_stand.stl` with global scale 1.0.

- [ ] **Step 7: Run the builder in Blender**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python scripts/build_stand.py
```

Expected: exit zero and both model files exist.

- [ ] **Step 8: Commit printable geometry**

```powershell
git add scripts/build_stand.py scripts/stand_parameters.py
git commit -m "feat: build one-piece round display stand"
```

---

### Task 3: Geometry and STL Verification

**Files:**
- Create: `scripts/verify_stand.py`
- Create: `outputs/reports/.gitkeep`

**Interfaces:**
- Consumes: the saved BLEND scene, exported STL, and `StandParameters`.
- Produces: `outputs/reports/dimensions.json` and `outputs/reports/dimensions.txt`; exits non-zero on any failed invariant.

- [ ] **Step 1: Write verification assertions before running them**

```python
assert stand_dimensions.x <= 94.1
assert stand_dimensions.y <= 82.1
assert mesh_component_count(stand) == 1
assert non_manifold_edge_count(stand) == 0
assert all(abs(radial_distance(p) - 9.0) < 0.01 for p in pin_centers)
assert math.dist(locator_centers[0], locator_centers[1]) == pytest.approx(20.0, abs=0.01)
assert minimum_module_clearance_mm >= 3.0
```

Implement equivalent explicit checks without a pytest dependency.

- [ ] **Step 2: Run verification before the verifier exists**

Expected: non-zero exit because `verify_stand.py` does not exist.

- [ ] **Step 3: Implement scene and mesh checks**

Load the BLEND file, confirm required objects and custom parameter properties, use `bmesh` to count connected components and non-manifold edges, and compare all mating coordinates against `stand_parameters.py`.

- [ ] **Step 4: Implement binary STL bounds parsing**

Read the 80-byte header, triangle count, and 50-byte triangle records using `struct`. Calculate raw STL bounds and assert the largest dimensions remain in the expected millimeter range rather than meters or micrometers.

- [ ] **Step 5: Write reports and run verification**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs/models/round_display_test_stand.blend --python scripts/verify_stand.py
```

Expected: `VERIFICATION PASS`, zero non-manifold edges, one component, five mating features at the specified coordinates, and STL dimensions near the scene dimensions.

- [ ] **Step 6: Commit verification tooling**

```powershell
git add scripts/verify_stand.py outputs/reports/.gitkeep
git commit -m "test: verify stand geometry and STL output"
```

---

### Task 4: Preview Renders and User Documentation

**Files:**
- Create: `scripts/render_stand.py`
- Create: `README.md`
- Create: `outputs/renders/.gitkeep`

**Interfaces:**
- Consumes: the verified BLEND scene.
- Produces: two 1200 x 900 PNG previews and user-facing print instructions.

- [ ] **Step 1: Create deterministic render setup**

Use Blender EEVEE, a neutral world, one area key light, one area fill light, and an orthographic camera. Render `stand_print.png` with references hidden, then render `stand_assembled.png` with `MODULE_REFERENCE` visible.

- [ ] **Step 2: Run rendering**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs/models/round_display_test_stand.blend --python scripts/render_stand.py
```

Expected: two nonblank 1200 x 900 PNG files.

- [ ] **Step 3: Inspect renders and revise only if geometry is obscured**

Check that the three male pins, two locator holes, FPC groove, 35-degree module pose, and base footprint are visually distinguishable. Keep the camera fixed after a successful inspection.

- [ ] **Step 4: Document print and test-fit procedure**

Document 100% model scale, 0.2 mm layers, four walls, 20-25% infill, PLA/PETG, flat-bottom orientation, no required supports, and the warning that printed pins are for desk testing only.

- [ ] **Step 5: Run the full pipeline again**

Run parameter tests, build, verify, and render from an empty Blender scene. Confirm all commands exit zero and all six deliverables exist.

- [ ] **Step 6: Commit documentation and render tooling**

```powershell
git add scripts/render_stand.py README.md outputs/renders/.gitkeep
git commit -m "docs: add stand previews and print guidance"
```

---

### Task 5: Final Blender MCP Inspection and Delivery

**Files:**
- Modify only if verification identifies a concrete geometry defect.

**Interfaces:**
- Consumes: final BLEND file and renders.
- Produces: live Blender scene ready for the user and final delivery summary.

- [ ] **Step 1: Open the final BLEND file in the running Blender instance**

Use Blender MCP to load `outputs/models/round_display_test_stand.blend` and retrieve scene information.

- [ ] **Step 2: Capture an MCP viewport screenshot**

Confirm `STAND_PRINT` and `MODULE_REFERENCE` are correctly framed and that no default Cube, Camera, or Light from the original startup scene remains unintentionally.

- [ ] **Step 3: Compare MCP object data with the verification report**

Check stand dimensions, reference pose, object names, and material assignments against `dimensions.json`.

- [ ] **Step 4: Confirm repository state and artifacts**

Run `git status --short --branch` and list model, render, and report files with sizes. The worktree must be clean except for intentionally ignored generated artifacts.

- [ ] **Step 5: Deliver clickable paths**

Provide direct paths to the BLEND, STL, both PNG previews, and dimensions report, and state any remaining requirement for a physical test print.
