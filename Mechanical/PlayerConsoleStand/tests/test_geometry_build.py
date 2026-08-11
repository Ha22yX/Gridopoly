import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = ROOT / "scripts"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))


def load_builder():
    path = SCRIPT_DIR / "build_stand.py"
    spec = importlib.util.spec_from_file_location("build_stand", path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load builder: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class GeometryBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.builder = load_builder()
        cls.params = cls.builder.StandParameters()

    def setUp(self):
        self.assertTrue(
            hasattr(self.builder, "build_scene"),
            "build_stand.build_scene must create the Blender geometry",
        )
        if not hasattr(self.__class__, "artifacts"):
            self.__class__.artifacts = self.builder.build_scene(self.params)
            bpy.context.view_layer.update()

    def groove_floor_normal_offset(self, v_mm):
        stand = bpy.data.objects["STAND_PRINT"]
        _, _, normal = self.builder.basis_vectors(self.params)
        origin = self.builder.pattern_world_point(
            0.0, v_mm, 6.0, self.params
        )
        hit, location, _, _ = stand.ray_cast(origin, -normal)
        self.assertTrue(hit, f"no stand surface found at local V={v_mm} mm")
        mating_center = Vector(self.params.mating_center_mm)
        return (location - mating_center).dot(normal)

    def test_scene_contains_one_printable_mesh(self):
        self.assertIn("PRINT", bpy.data.collections)
        print_meshes = [
            obj
            for obj in bpy.data.collections["PRINT"].objects
            if obj.type == "MESH"
        ]
        self.assertEqual([obj.name for obj in print_meshes], ["STAND_PRINT"])

    def test_printable_mesh_matches_base_footprint(self):
        stand = bpy.data.objects["STAND_PRINT"]
        self.assertAlmostEqual(stand.dimensions.x, 94.0, delta=0.1)
        self.assertAlmostEqual(stand.dimensions.y, 82.0, delta=0.1)
        self.assertGreater(stand.dimensions.z, 40.0)
        self.assertLess(stand.dimensions.z, 55.0)

        world_z = [
            (stand.matrix_world @ Vector(corner)).z for corner in stand.bound_box
        ]
        self.assertAlmostEqual(min(world_z), 0.0, delta=0.05)

    def test_printable_mesh_records_confirmed_interface(self):
        stand = bpy.data.objects["STAND_PRINT"]
        self.assertEqual(stand["mating_angle_deg"], 35.0)
        self.assertEqual(stand["male_pin_count"], 3)
        self.assertEqual(stand["male_pin_diameter_mm"], 2.9)
        self.assertEqual(stand["female_hole_count"], 2)
        self.assertEqual(
            stand.get("female_hole_diameters_mm_json"), "[3.1, 4.1]"
        )
        self.assertEqual(stand["fpc_groove_width_mm"], 9.0)
        self.assertEqual(stand.get("c_ring_inner_diameter_mm"), 34.5)
        self.assertEqual(stand.get("c_ring_outer_diameter_mm"), 40.0)
        self.assertEqual(stand.get("c_ring_height_mm"), 7.6)
        self.assertEqual(stand.get("c_ring_opening_width_mm"), 9.0)
        self.assertEqual(stand.get("display_six_oclock_pin_index"), 3)
        self.assertEqual(
            stand.get("mating_pattern_rotation_clockwise_deg"), 47.5
        )
        self.assertEqual(stand.get("display_rotation_nominal_deg"), 0.0)
        self.assertEqual(stand["uses_screws"], False)

    def test_c_ring_wall_surrounds_module_interface(self):
        stand = bpy.data.objects["STAND_PRINT"]
        u_axis, _, _ = self.builder.basis_vectors(self.params)
        origin = self.builder.world_point(
            0.0, 0.0, self.params.c_ring_height_mm / 2.0, self.params
        )
        hit, location, _, _ = stand.ray_cast(origin, u_axis)
        self.assertTrue(hit, "C-ring inner wall is missing from generated mesh")
        mating_center = Vector(self.params.mating_center_mm)
        inner_radius = (location - mating_center).dot(u_axis)
        self.assertAlmostEqual(inner_radius, 17.25, delta=0.08)

    def test_module_reference_uses_three_and_four_mm_locator_posts(self):
        posts = [
            bpy.data.objects[f"MODULE_LOCATOR_POST_{index}_REFERENCE"]
            for index in range(1, 3)
        ]
        self.assertAlmostEqual(posts[0].dimensions.x, 3.0, delta=0.01)
        self.assertAlmostEqual(posts[1].dimensions.x, 4.0, delta=0.01)

    def test_fpc_groove_is_two_mm_deep_through_mating_pad(self):
        self.assertAlmostEqual(
            self.groove_floor_normal_offset(-10.0),
            -2.0,
            delta=0.15,
        )

    def test_fpc_groove_is_two_mm_deep_on_exposed_wedge(self):
        wedge_surface_normal_offset = -self.params.mating_pad_proud_mm
        expected_floor = (
            wedge_surface_normal_offset - self.params.fpc_groove_depth_mm
        )
        self.assertAlmostEqual(
            self.groove_floor_normal_offset(-24.0),
            expected_floor,
            delta=0.15,
        )

    def test_fpc_groove_floor_uses_a_ramped_pad_to_wedge_transition(self):
        transition_midpoint = self.groove_floor_normal_offset(-18.5)
        self.assertAlmostEqual(transition_midpoint, -3.0, delta=0.2)

    def test_failed_stl_triangulation_removes_temporary_export_copy(self):
        original_root = self.builder.ROOT_DIR
        original_triangulate = self.builder.bmesh.ops.triangulate

        def fail_triangulation(*args, **kwargs):
            raise RuntimeError("forced triangulation failure")

        with tempfile.TemporaryDirectory() as directory:
            try:
                self.builder.ROOT_DIR = Path(directory)
                self.builder.bmesh.ops.triangulate = fail_triangulation
                with self.assertRaisesRegex(
                    RuntimeError, "forced triangulation failure"
                ):
                    self.builder.save_and_export(self.artifacts["stand"])
            finally:
                self.builder.ROOT_DIR = original_root
                self.builder.bmesh.ops.triangulate = original_triangulate

        leaked = bpy.data.objects.get("STAND_STL_EXPORT")
        try:
            self.assertIsNone(leaked)
        finally:
            if leaked is not None:
                leaked_mesh = leaked.data
                bpy.data.objects.remove(leaked, do_unlink=True)
                if leaked_mesh.users == 0:
                    bpy.data.meshes.remove(leaked_mesh)

    def test_reference_collection_contains_module_and_mating_markers(self):
        self.assertIn("REFERENCE", bpy.data.collections)
        names = {obj.name for obj in bpy.data.collections["REFERENCE"].objects}
        self.assertIn("MODULE_REFERENCE", names)
        self.assertIn("M4_PATTERN_REFERENCE", names)
        self.assertIn("LOCATOR_REFERENCE", names)
        self.assertIn("FPC_PATH_REFERENCE", names)
        self.assertTrue(
            {f"M4_AXIS_{index}_REFERENCE" for index in range(1, 4)} <= names
        )
        self.assertTrue(
            {f"LOCATOR_AXIS_{index}_REFERENCE" for index in range(1, 3)}
            <= names
        )

    def test_reference_axes_are_placed_at_the_confirmed_mating_centers(self):
        expected_patterns = (
            ("M4_AXIS", self.builder.m4_pin_local_positions(self.params)),
            (
                "LOCATOR_AXIS",
                self.builder.locator_hole_local_positions(self.params),
            ),
        )
        for prefix, positions in expected_patterns:
            for index, (x_mm, v_mm) in enumerate(positions, start=1):
                marker = bpy.data.objects[f"{prefix}_{index}_REFERENCE"]
                expected = self.builder.world_point(x_mm, v_mm, 0.0, self.params)
                self.assertLess((marker.location - expected).length, 0.01)

    def test_no_screw_geometry_exists(self):
        screw_names = [
            obj.name for obj in bpy.data.objects if "SCREW" in obj.name.upper()
        ]
        self.assertEqual(screw_names, [])

    def test_scene_contains_only_design_materials(self):
        self.assertEqual(
            set(bpy.data.materials.keys()),
            {
                "FPC_REFERENCE_MATERIAL",
                "MODULE_REFERENCE_MATERIAL",
                "SCREEN_REFERENCE_MATERIAL",
                "STAND_MATERIAL",
            },
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GeometryBuildTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
