import importlib.util
import copy
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = ROOT / "scripts"
MODEL_PATH = ROOT / "outputs" / "models" / "round_display_test_stand.blend"
STL_PATH = ROOT / "outputs" / "models" / "round_display_test_stand.stl"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))


def load_verifier():
    path = SCRIPT_DIR / "verify_stand.py"
    if not path.exists():
        raise AssertionError(f"required verifier is missing: {path}")
    spec = importlib.util.spec_from_file_location("verify_stand", path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load verifier: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class StandVerificationTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(MODEL_PATH.exists(), f"missing BLEND model: {MODEL_PATH}")
        self.assertTrue(STL_PATH.exists(), f"missing STL model: {STL_PATH}")
        self.verifier = load_verifier()
        self.params = self.verifier.StandParameters()
        self.stand = bpy.data.objects.get("STAND_PRINT")
        self.assertIsNotNone(self.stand)

    def test_stl_bounds_remain_in_millimeters(self):
        bounds = self.verifier.read_binary_stl_bounds(STL_PATH)
        dimensions = bounds["dimensions_mm"]
        self.assertAlmostEqual(dimensions[0], 94.0, delta=0.1)
        self.assertAlmostEqual(dimensions[1], 82.0, delta=0.1)
        self.assertGreater(dimensions[2], 40.0)
        self.assertLess(dimensions[2], 55.0)
        self.assertGreater(bounds["triangle_count"], 100)
        self.assertEqual(bounds["mesh_component_count"], 1)
        self.assertEqual(bounds["non_manifold_edge_count"], 0)
        self.assertEqual(bounds["degenerate_triangle_count"], 0)

    def test_stl_parser_detects_distinct_collinear_triangle(self):
        vertices = (
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            (2.0, 0.0, 0.0),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "collinear.stl"
            with path.open("wb") as handle:
                handle.write(b"collinear triangle test".ljust(80, b"\0"))
                handle.write(struct.pack("<I", 1))
                coordinates = tuple(
                    coordinate for vertex in vertices for coordinate in vertex
                )
                handle.write(
                    struct.pack("<12fH", 0.0, 0.0, 0.0, *coordinates, 0)
                )
            topology = self.verifier.read_binary_stl_bounds(path)
            self.assertEqual(topology["degenerate_triangle_count"], 1)

    def test_print_mesh_is_one_closed_component(self):
        self.assertEqual(self.verifier.mesh_component_count(self.stand), 1)
        self.assertEqual(self.verifier.non_manifold_edge_count(self.stand), 0)

    def test_blend_components_must_share_edges_not_only_one_vertex(self):
        vertices = (
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 1.0),
            (-1.0, 0.0, 0.0),
            (0.0, -1.0, 0.0),
            (0.0, 0.0, -1.0),
        )
        faces = (
            (0, 2, 1),
            (0, 1, 3),
            (1, 2, 3),
            (2, 0, 3),
            (0, 4, 5),
            (0, 6, 4),
            (4, 6, 5),
            (5, 6, 0),
        )
        mesh = bpy.data.meshes.new("POINT_TOUCH_TEST_MESH")
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new("POINT_TOUCH_TEST", mesh)
        bpy.context.scene.collection.objects.link(obj)
        try:
            self.assertEqual(self.verifier.mesh_component_count(obj), 2)
        finally:
            bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.meshes.remove(mesh)

    def test_stl_components_must_share_edges_not_only_one_vertex(self):
        vertices = (
            (0.0, 0.0, 0.0),
            (1.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 1.0),
            (-1.0, 0.0, 0.0),
            (0.0, -1.0, 0.0),
            (0.0, 0.0, -1.0),
        )
        faces = (
            (0, 2, 1),
            (0, 1, 3),
            (1, 2, 3),
            (2, 0, 3),
            (0, 4, 5),
            (0, 6, 4),
            (4, 6, 5),
            (5, 6, 0),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "point_touch.stl"
            with path.open("wb") as handle:
                handle.write(b"point-touch topology test".ljust(80, b"\0"))
                handle.write(struct.pack("<I", len(faces)))
                for face in faces:
                    coordinates = tuple(
                        coordinate
                        for vertex_index in face
                        for coordinate in vertices[vertex_index]
                    )
                    handle.write(struct.pack("<12fH", 0.0, 0.0, 0.0, *coordinates, 0))
            topology = self.verifier.read_binary_stl_bounds(path)
            self.assertEqual(topology["mesh_component_count"], 2)

    def test_report_records_all_mating_features(self):
        report = self.verifier.collect_report(self.params, self.stand, STL_PATH)
        self.assertEqual(len(report["male_pins"]["local_centers_mm"]), 3)
        self.assertEqual(len(report["female_holes"]["local_centers_mm"]), 2)
        self.assertEqual(report["male_pins"]["pcd_mm"], 18.0)
        self.assertEqual(report["female_holes"]["pcd_mm"], 20.0)
        self.assertEqual(
            report["female_holes"].get("diameters_mm"), [3.1, 4.1]
        )
        self.assertEqual(report["fpc_groove"]["width_mm"], 9.0)
        self.assertEqual(
            report.get("c_ring"),
            {
                "inner_diameter_mm": 34.5,
                "outer_diameter_mm": 40.0,
                "height_mm": 7.6,
                "opening_width_mm": 9.0,
            },
        )
        self.assertEqual(
            report.get("display_orientation"),
            {
                "mechanical_rotation_clockwise_deg": 47.5,
                "six_oclock_pin_index": 3,
                "six_oclock_pin_local_mm": [
                    1.9479565254429252,
                    -8.7866640640794,
                ],
                "nominal_firmware_rotation_deg": 0.0,
            },
        )
        self.assertGreaterEqual(report["module_clearance_mm"], 3.0)

    def test_report_measures_mating_geometry_from_the_mesh(self):
        report = self.verifier.collect_report(self.params, self.stand, STL_PATH)
        measured = report["measured_geometry"]

        self.assertAlmostEqual(measured["mating_angle_deg"], 35.0, delta=0.15)
        self.assertEqual(len(measured["male_pins"]), 3)
        for pin in measured["male_pins"]:
            self.assertAlmostEqual(pin["diameter_mm"], 2.9, delta=0.12)
            self.assertAlmostEqual(pin["height_mm"], 3.0, delta=0.12)

        self.assertEqual(len(measured["female_holes"]), 2)
        for hole, expected_diameter in zip(
            measured["female_holes"], (3.1, 4.1)
        ):
            self.assertAlmostEqual(
                hole["diameter_u_mm"], expected_diameter, delta=0.12
            )
            self.assertAlmostEqual(
                hole["diameter_v_mm"], expected_diameter, delta=0.12
            )
            self.assertAlmostEqual(hole["depth_mm"], 4.0, delta=0.15)

        groove = measured["fpc_groove"]
        self.assertAlmostEqual(groove["pad_depth_mm"], 2.0, delta=0.15)
        self.assertAlmostEqual(groove["wedge_depth_mm"], 2.0, delta=0.15)
        self.assertAlmostEqual(groove["pad_width_mm"], 9.0, delta=0.15)
        self.assertAlmostEqual(groove["wedge_width_mm"], 9.0, delta=0.15)

        ring = measured.get("c_ring")
        self.assertIsNotNone(ring)
        self.assertAlmostEqual(ring["inner_diameter_mm"], 34.5, delta=0.12)
        self.assertAlmostEqual(ring["outer_diameter_mm"], 40.0, delta=0.12)
        self.assertAlmostEqual(ring["height_mm"], 7.6, delta=0.12)
        self.assertAlmostEqual(ring["opening_width_mm"], 9.0, delta=0.12)
        opening_samples = ring["opening_width_samples_mm"]
        self.assertEqual(len(opening_samples), 3)
        for sample, expected_height in zip(opening_samples, (0.05, 3.8, 7.55)):
            self.assertAlmostEqual(
                sample["normal_mm"], expected_height, delta=0.01
            )
            self.assertAlmostEqual(sample["width_mm"], 9.0, delta=0.12)

    def test_report_measures_exported_stl_not_scene_object(self):
        baseline = self.verifier.collect_report(
            self.params, self.stand, STL_PATH
        )["measured_geometry"]
        moved = self.stand.copy()
        moved.data = self.stand.data
        moved.location.x += 100.0
        bpy.context.scene.collection.objects.link(moved)
        try:
            measured = self.verifier.collect_report(
                self.params, moved, STL_PATH
            )["measured_geometry"]
        finally:
            bpy.data.objects.remove(moved, do_unlink=True)
        self.assertEqual(measured, baseline)

    def test_mesh_measurements_do_not_come_from_declared_properties(self):
        original_pin = self.stand["male_pin_diameter_mm"]
        original_ring = self.stand["c_ring_inner_diameter_mm"]
        baseline = self.verifier.collect_report(
            self.params, self.stand, STL_PATH
        )["measured_geometry"]
        try:
            self.stand["male_pin_diameter_mm"] = 99.0
            self.stand["c_ring_inner_diameter_mm"] = 99.0
            measured = self.verifier.collect_report(
                self.params, self.stand, STL_PATH
            )["measured_geometry"]
        finally:
            self.stand["male_pin_diameter_mm"] = original_pin
            self.stand["c_ring_inner_diameter_mm"] = original_ring
        self.assertEqual(measured, baseline)

    def test_validation_rejects_blocked_c_ring_opening_sample(self):
        report = self.verifier.collect_report(self.params, self.stand, STL_PATH)
        samples = report["measured_geometry"]["c_ring"][
            "opening_width_samples_mm"
        ]
        samples[-1]["width_mm"] = 0.0
        errors = self.verifier.validate_report(report)
        self.assertTrue(
            any("opening width" in error for error in errors),
            errors,
        )

    def test_validation_rejects_tampered_declared_dimensions(self):
        report = self.verifier.collect_report(self.params, self.stand, STL_PATH)
        cases = (
            (("mating_angle_deg",), 0.0),
            (("mating_center_mm",), [100.0, 100.0, 100.0]),
            (("male_pins", "diameter_mm"), 99.0),
            (("male_pins", "height_mm"), 0.0),
            (("female_holes", "diameters_mm"), [99.0, 99.0]),
            (("female_holes", "depth_mm"), 0.0),
            (("fpc_groove", "depth_mm"), 0.0),
            (("c_ring", "inner_diameter_mm"), 99.0),
            (("c_ring", "height_mm"), 0.0),
            (("display_orientation", "six_oclock_pin_index"), 1),
        )
        for path, bad_value in cases:
            with self.subTest(path=path):
                tampered = copy.deepcopy(report)
                target = tampered
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = bad_value
                self.assertTrue(self.verifier.validate_report(tampered))

    def test_report_passes_all_invariants(self):
        report = self.verifier.collect_report(self.params, self.stand, STL_PATH)
        self.assertEqual(self.verifier.validate_report(report), [])
        json.dumps(report)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(
        StandVerificationTests
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
