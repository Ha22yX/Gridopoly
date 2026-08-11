# Round Display Stand C-Ring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a one-piece 34.5 mm ID, 40.0 mm OD, 7.6 mm high C-ring around the display's raised rear interface while preserving the existing 9.0 mm FPC exit and recording the display's mechanical six-o'clock reference.

**Architecture:** Extend the existing immutable parameter model and dry-run build contract, then construct the ring as an oriented annulus with a full-height rectangular cable opening before unioning it into `STAND_PRINT`. Extend the independent mesh verifier to measure the finished ring with ray casts, so declared metadata cannot conceal incorrect boolean geometry.

**Tech Stack:** Blender 5.2 Python API (`bpy`, `mathutils`), Python `unittest`, exact Blender booleans, binary STL topology inspection, Blender MCP for final live-scene inspection.

## Global Constraints

- The module rear interface is 34.0 mm diameter and 7.6 mm high.
- The C-ring is 34.5 mm inner diameter, 40.0 mm outer diameter, and 7.6 mm high along the mating-plane normal.
- The lower C-ring opening is 9.0 mm wide and continuous with the existing FPC groove toward negative local V.
- The C-ring is part of the one-piece stand; no screws, inserts, clips, or separate parts are added.
- Existing left/right locating posts remain 3.0/4.0 mm, and their holes remain 3.1/4.1 mm.
- The 4.0 mm locating feature at local `(10, 0)` is the display firmware's logical six-o'clock direction.
- Mechanical feature positions remain unchanged; firmware records a nominal 90-degree display rotation whose API direction is confirmed on hardware.
- The 35-degree angle, 94 x 82 mm footprint, three printed pins, and existing cable groove remain unchanged.
- The exported BLEND mesh and STL must each be one edge-connected manifold component with no degenerate triangles.

## File Map

- `scripts/stand_parameters.py`: confirmed module, C-ring, and display-orientation parameters.
- `scripts/build_stand.py`: build contract, C-ring boolean geometry, stand metadata, and module reference geometry.
- `scripts/verify_stand.py`: actual ring ray-cast measurements, declared-value validation, and JSON/text reporting.
- `tests/test_stand_parameters.py`: immutable dimension and orientation assertions.
- `tests/test_build_contract.py`: dry-run C-ring and display-orientation contract assertions.
- `tests/test_geometry_build.py`: generated-scene metadata and reference-geometry assertions.
- `tests/test_verify_stand.py`: actual ring dimension/opening and tamper-rejection assertions.
- `README.md`: final dimensions, firmware orientation note, and FDM fit warning.
- `outputs/`: regenerated BLEND, STL, reports, and preview renders.

---

### Task 1: Parameter and Build Contract

**Files:**
- Modify: `scripts/stand_parameters.py`
- Modify: `scripts/build_stand.py`
- Modify: `tests/test_stand_parameters.py`
- Modify: `tests/test_build_contract.py`

**Interfaces:**
- Consumes: existing `StandParameters`, `build_contract(params)`, and local mating coordinates.
- Produces: `module_rear_interface_depth_mm`, `c_ring_inner_diameter_mm`, `c_ring_outer_diameter_mm`, `c_ring_height_mm`, `display_six_oclock_locator_index`, and `display_rotation_nominal_deg`.

- [ ] **Step 1: Write failing parameter and contract assertions**

Add these assertions to `test_confirmed_dimensions_are_preserved`:

```python
self.assertEqual(self.params.module_rear_interface_depth_mm, 7.6)
self.assertEqual(self.params.c_ring_inner_diameter_mm, 34.5)
self.assertEqual(self.params.c_ring_outer_diameter_mm, 40.0)
self.assertEqual(self.params.c_ring_height_mm, 7.6)
self.assertEqual(self.params.display_six_oclock_locator_index, 2)
self.assertEqual(self.params.display_rotation_nominal_deg, 90.0)
```

Add this contract test:

```python
def test_contract_records_c_ring_and_display_orientation(self):
    self.assertEqual(
        self.contract["c_ring"],
        {
            "inner_diameter_mm": 34.5,
            "outer_diameter_mm": 40.0,
            "height_mm": 7.6,
            "opening_width_mm": 9.0,
            "opening_direction": "front_lower",
        },
    )
    self.assertEqual(
        self.contract["display_orientation"],
        {
            "six_oclock_locator_index": 2,
            "six_oclock_local_mm": [10.0, 0.0],
            "nominal_firmware_rotation_deg": 90.0,
        },
    )
```

- [ ] **Step 2: Run the tests and confirm the new fields are missing**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_stand_parameters.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_build_contract.py
```

Expected: both commands exit non-zero because the C-ring parameters and contract keys do not exist.

- [ ] **Step 3: Add the immutable parameters**

Add to `StandParameters`:

```python
module_rear_interface_depth_mm: float = 7.6
c_ring_inner_diameter_mm: float = 34.5
c_ring_outer_diameter_mm: float = 40.0
c_ring_height_mm: float = 7.6
display_six_oclock_locator_index: int = 2
display_rotation_nominal_deg: float = 90.0
```

Replace the hard-coded `boss_depth = 7.6` in `create_reference_assembly` with
`boss_depth = params.module_rear_interface_depth_mm`.

- [ ] **Step 4: Extend the dry-run build contract**

Add these entries to the dictionary returned by `build_contract`:

```python
"c_ring": {
    "inner_diameter_mm": params.c_ring_inner_diameter_mm,
    "outer_diameter_mm": params.c_ring_outer_diameter_mm,
    "height_mm": params.c_ring_height_mm,
    "opening_width_mm": params.fpc_groove_width_mm,
    "opening_direction": "front_lower",
},
"display_orientation": {
    "six_oclock_locator_index": params.display_six_oclock_locator_index,
    "six_oclock_local_mm": list(
        locator_hole_local_positions(params)[
            params.display_six_oclock_locator_index - 1
        ]
    ),
    "nominal_firmware_rotation_deg": params.display_rotation_nominal_deg,
},
```

- [ ] **Step 5: Run both test files and commit**

Expected: 13 existing/new tests pass with zero failures.

```powershell
git add scripts\stand_parameters.py scripts\build_stand.py tests\test_stand_parameters.py tests\test_build_contract.py
git commit -m "feat: define c-ring interface parameters"
```

---

### Task 2: One-Piece C-Ring Geometry

**Files:**
- Modify: `scripts/build_stand.py`
- Modify: `tests/test_geometry_build.py`

**Interfaces:**
- Consumes: C-ring parameters from Task 1, `oriented_cylinder`, `create_oriented_box`, `apply_boolean`, and `world_point`.
- Produces: `create_c_ring(params) -> bpy.types.Object` and ring metadata on `STAND_PRINT`.

- [ ] **Step 1: Write failing generated-scene tests**

Add these assertions to `test_printable_mesh_records_confirmed_interface`:

```python
self.assertEqual(stand.get("c_ring_inner_diameter_mm"), 34.5)
self.assertEqual(stand.get("c_ring_outer_diameter_mm"), 40.0)
self.assertEqual(stand.get("c_ring_height_mm"), 7.6)
self.assertEqual(stand.get("c_ring_opening_width_mm"), 9.0)
self.assertEqual(stand.get("display_six_oclock_locator_index"), 2)
self.assertEqual(stand.get("display_rotation_nominal_deg"), 90.0)
```

Add a test that imports the builder and asserts `hasattr(builder, "create_c_ring")`.

- [ ] **Step 2: Run the geometry tests and confirm failure**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_geometry_build.py
```

Expected: failure because `create_c_ring` and ring metadata are absent.

- [ ] **Step 3: Build the annulus and full-height FPC opening**

Add this function next to `create_mating_pad`:

```python
def create_c_ring(params: StandParameters) -> bpy.types.Object:
    _, _, normal = basis_vectors(params)
    overlap_mm = 0.4
    total_depth = params.c_ring_height_mm + overlap_mm
    center_normal = (params.c_ring_height_mm - overlap_mm) / 2.0
    center = world_point(0.0, 0.0, center_normal, params)

    ring = oriented_cylinder(
        "C_RING_SOLID",
        params.c_ring_outer_diameter_mm / 2.0,
        total_depth,
        center,
        params,
        vertices=128,
    )
    inner = oriented_cylinder(
        "C_RING_INNER_CUTTER",
        params.c_ring_inner_diameter_mm / 2.0,
        total_depth + 0.2,
        center,
        params,
        vertices=128,
    )
    apply_boolean(ring, inner, "DIFFERENCE")

    opening_center_v = (
        params.fpc_groove_v_min_mm + params.fpc_groove_v_max_mm
    ) / 2.0
    opening = create_oriented_box(
        "C_RING_OPENING_CUTTER",
        (
            params.fpc_groove_width_mm,
            params.fpc_groove_v_max_mm - params.fpc_groove_v_min_mm,
            total_depth + 0.2,
        ),
        world_point(0.0, opening_center_v, center_normal, params),
        params,
    )
    apply_boolean(ring, opening, "DIFFERENCE")
    return ring
```

The helper uses a 0.4 mm overlap below the mating face and stops exactly at
normal offset 7.6 mm. The opening cutter spans local V = -28 to +2 mm, so it
removes the lower ring wall without creating an enclosed cable tunnel.

- [ ] **Step 4: Union the ring and store exact metadata**

Immediately after unioning the mating pad in `build_scene`, add:

```python
c_ring = create_c_ring(params)
apply_boolean(stand, c_ring, "UNION")
```

Before creating references, store:

```python
stand["c_ring_inner_diameter_mm"] = params.c_ring_inner_diameter_mm
stand["c_ring_outer_diameter_mm"] = params.c_ring_outer_diameter_mm
stand["c_ring_height_mm"] = params.c_ring_height_mm
stand["c_ring_opening_width_mm"] = params.fpc_groove_width_mm
stand["display_six_oclock_locator_index"] = (
    params.display_six_oclock_locator_index
)
stand["display_rotation_nominal_deg"] = params.display_rotation_nominal_deg
```

- [ ] **Step 5: Run geometry tests and commit**

Expected: all generated-scene tests pass, including the unchanged pin, hole,
groove, material, and footprint assertions.

```powershell
git add scripts\build_stand.py tests\test_geometry_build.py
git commit -m "feat: add integrated c-ring geometry"
```

---

### Task 3: Actual Mesh Measurement and Validation

**Files:**
- Modify: `scripts/verify_stand.py`
- Modify: `tests/test_verify_stand.py`

**Interfaces:**
- Consumes: `raycast_mating_surface`, `mating_basis`, final `STAND_PRINT`, and C-ring parameters.
- Produces: `measure_c_ring(stand, params) -> dict[str, float]`, report key `measured_geometry.c_ring`, and declared report keys `c_ring` and `display_orientation`.

- [ ] **Step 1: Write failing report and mesh-measurement tests**

Add report assertions:

```python
self.assertEqual(
    report["c_ring"],
    {
        "inner_diameter_mm": 34.5,
        "outer_diameter_mm": 40.0,
        "height_mm": 7.6,
        "opening_width_mm": 9.0,
    },
)
self.assertEqual(report["display_orientation"]["six_oclock_locator_index"], 2)
self.assertEqual(report["display_orientation"]["six_oclock_local_mm"], [10.0, 0.0])
self.assertEqual(
    report["display_orientation"]["nominal_firmware_rotation_deg"], 90.0
)
```

Add actual mesh assertions:

```python
ring = report["measured_geometry"]["c_ring"]
self.assertAlmostEqual(ring["inner_diameter_mm"], 34.5, delta=0.12)
self.assertAlmostEqual(ring["outer_diameter_mm"], 40.0, delta=0.12)
self.assertAlmostEqual(ring["height_mm"], 7.6, delta=0.12)
self.assertAlmostEqual(ring["opening_width_mm"], 9.0, delta=0.12)
```

Extend the tamper cases with:

```python
(("c_ring", "inner_diameter_mm"), 99.0),
(("c_ring", "height_mm"), 0.0),
(("display_orientation", "six_oclock_locator_index"), 1),
```

- [ ] **Step 2: Run verifier tests and confirm missing report keys**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs\models\round_display_test_stand.blend --python tests\test_verify_stand.py
```

Expected: non-zero exit because the old generated model and verifier do not
contain C-ring data.

- [ ] **Step 3: Measure the finished ring with ray casts**

Add this function after `measure_fpc_groove`:

```python
def measure_c_ring(
    stand: bpy.types.Object, params: StandParameters
) -> dict[str, float]:
    u_axis, _, normal = mating_basis(params)
    mid_height = params.c_ring_height_mm / 2.0
    wall_center = (
        params.c_ring_inner_diameter_mm + params.c_ring_outer_diameter_mm
    ) / 4.0

    inner_left = raycast_mating_surface(
        stand, (0.0, 0.0, mid_height), -u_axis, params
    )
    inner_right = raycast_mating_surface(
        stand, (0.0, 0.0, mid_height), u_axis, params
    )
    outer_left = raycast_mating_surface(
        stand, (-25.0, 0.0, mid_height), u_axis, params
    )
    outer_right = raycast_mating_surface(
        stand, (25.0, 0.0, mid_height), -u_axis, params
    )
    opening_left = raycast_mating_surface(
        stand, (0.0, -19.0, mid_height), -u_axis, params
    )
    opening_right = raycast_mating_surface(
        stand, (0.0, -19.0, mid_height), u_axis, params
    )
    top = raycast_mating_surface(
        stand, (wall_center, 0.0, 12.0), -normal, params
    )
    return {
        "inner_diameter_mm": round(inner_right[0] - inner_left[0], 6),
        "outer_diameter_mm": round(outer_right[0] - outer_left[0], 6),
        "height_mm": round(top[2], 6),
        "opening_width_mm": round(opening_right[0] - opening_left[0], 6),
    }
```

Add `"c_ring": measure_c_ring(stand, params)` to
`measure_mating_geometry`.

- [ ] **Step 4: Report and validate declared and measured values**

In `collect_report`, add declared `c_ring` values from stand properties and a
`display_orientation` object whose six-o'clock local coordinate comes from
locator index 2. In `validate_report`, compare all four declared ring values
with their parameters at 0.01 mm tolerance, all four measured values at 0.12
mm tolerance, and compare the orientation index, coordinate, and nominal
rotation exactly.

- [ ] **Step 5: Rebuild, run verifier tests, and commit**

Run:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python scripts\build_stand.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs\models\round_display_test_stand.blend --python tests\test_verify_stand.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs\models\round_display_test_stand.blend --python scripts\verify_stand.py
```

Expected: verifier tests pass, `VERIFICATION PASS` is printed, and the report
contains measured 34.5/40.0/7.6/9.0 mm C-ring geometry.

```powershell
git add scripts\verify_stand.py tests\test_verify_stand.py
git commit -m "test: verify c-ring from generated mesh"
```

---

### Task 4: Documentation, Rendering, and Delivery

**Files:**
- Modify: `README.md`
- Regenerate: `outputs/models/round_display_test_stand.blend`
- Regenerate: `outputs/models/round_display_test_stand.stl`
- Regenerate: `outputs/reports/dimensions.json`
- Regenerate: `outputs/reports/dimensions.txt`
- Regenerate: `outputs/renders/stand_print.png`
- Regenerate: `outputs/renders/stand_assembled.png`

**Interfaces:**
- Consumes: verified final scene and all automated tests.
- Produces: slicer-ready and editable desktop deliverables plus an explicit firmware-orientation record.

- [ ] **Step 1: Update the README**

Record the C-ring as 34.5 mm ID, 40.0 mm OD, 7.6 mm height, and 9.0 mm lower
opening. Add that the right 4.0 mm module post is logical six o'clock and the
firmware needs a nominal 90-degree content rotation, with direction confirmed
on the physical display. Warn that 0.25 mm radial ring clearance may print
tight.

- [ ] **Step 2: Run all 34 existing tests plus the new C-ring tests**

Run each command from the project root:

```powershell
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_stand_parameters.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_build_contract.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python tests\test_geometry_build.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs\models\round_display_test_stand.blend --python tests\test_verify_stand.py
& 'D:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background outputs\models\round_display_test_stand.blend --python tests\test_render_outputs.py
```

Expected: every command exits zero. The final count must include every test
reported by these five commands; do not rely on the previous 34-test count.

- [ ] **Step 3: Inspect generated artifacts**

Open both PNG files and confirm the ring is visible, its lower opening aligns
with the FPC groove, no object overlaps are incoherent, and the assembled
module seats inside the ring. Open the final BLEND through Blender MCP and
inspect `STAND_PRINT`, `MODULE_REAR_INTERFACE_REFERENCE`, ring metadata, and
the two locator post references.

- [ ] **Step 4: Commit source and documentation**

```powershell
git diff --check
git add README.md scripts tests docs\superpowers\specs\2026-08-02-round-display-stand-c-ring-design.md
git commit -m "feat: complete integrated c-ring stand"
```

- [ ] **Step 5: Synchronize the verified delivery folder**

Copy `README.md`, `.gitignore`, `docs`, `scripts`, `tests`, and `outputs` to
`C:\Users\Administrator\Desktop\圆形屏幕测试\ScreenStand` without copying
`.git`. Compare SHA-256 hashes for every copied file and require zero
mismatches. Leave the final desktop BLEND open in Blender for inspection.
