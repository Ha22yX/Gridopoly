import importlib.util
import math
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "scripts" / "stand_parameters.py"


def load_parameters_module():
    if not MODULE_PATH.exists():
        raise AssertionError(f"required parameter module is missing: {MODULE_PATH}")

    spec = importlib.util.spec_from_file_location("stand_parameters", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load parameter module: {MODULE_PATH}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class StandParameterTests(unittest.TestCase):
    def setUp(self):
        self.module = load_parameters_module()
        self.params = self.module.StandParameters()

    def test_confirmed_dimensions_are_preserved(self):
        self.assertEqual(self.params.angle_deg, 35.0)
        self.assertEqual(self.params.base_width_mm, 94.0)
        self.assertEqual(self.params.base_depth_mm, 82.0)
        self.assertEqual(self.params.m4_pcd_mm, 18.0)
        self.assertEqual(self.params.printed_pin_diameter_mm, 2.9)
        self.assertEqual(self.params.printed_pin_height_mm, 3.0)
        self.assertEqual(self.params.locator_pcd_mm, 20.0)
        self.assertEqual(
            getattr(self.params, "locator_post_diameters_mm", None),
            (3.0, 4.0),
        )
        self.assertEqual(
            getattr(self.params, "locator_hole_diameters_mm", None),
            (3.1, 4.1),
        )
        self.assertEqual(self.params.locator_hole_depth_mm, 4.0)
        self.assertEqual(self.params.fpc_groove_width_mm, 9.0)
        self.assertEqual(self.params.fpc_groove_depth_mm, 2.0)
        self.assertEqual(
            getattr(self.params, "module_rear_interface_depth_mm", None), 7.6
        )
        self.assertEqual(
            getattr(self.params, "c_ring_inner_diameter_mm", None), 34.5
        )
        self.assertEqual(
            getattr(self.params, "c_ring_outer_diameter_mm", None), 40.0
        )
        self.assertEqual(getattr(self.params, "c_ring_height_mm", None), 7.6)
        self.assertEqual(
            getattr(self.params, "display_six_oclock_pin_index", None), 3
        )
        self.assertEqual(
            getattr(self.params, "display_rotation_nominal_deg", None), 0.0
        )
        self.assertEqual(
            self.params.mating_pattern_rotation_clockwise_deg, 47.5
        )

    def test_m4_pattern_uses_18_mm_pitch_circle_and_120_degree_spacing(self):
        points = self.module.m4_pin_local_positions(self.params)
        self.assertEqual(len(points), 3)

        for x_mm, v_mm in points:
            self.assertTrue(
                math.isclose(math.hypot(x_mm, v_mm), 9.0, abs_tol=1e-9)
            )

        pair_distances = [
            math.dist(points[index], points[(index + 1) % 3])
            for index in range(3)
        ]
        for distance_mm in pair_distances:
            self.assertTrue(
                math.isclose(distance_mm, 15.5884572681, abs_tol=1e-6)
            )

    def test_rotated_m4_and_locator_positions_match_cw47_5_rigid_pattern(self):
        pins = self.module.m4_pin_local_positions(self.params)
        expected_pins = (
            (6.635496031291117, 6.080311868540943),
            (-8.583452556734045, 2.706352195538458),
            (1.9479565254429286, -8.7866640640794),
        )
        for actual, expected in zip(pins, expected_pins):
            self.assertTrue(math.dist(actual, expected) < 1e-9)

        points = self.module.locator_hole_local_positions(self.params)
        expected = (
            (-6.755902076156602, 7.37277336810124),
            (6.755902076156602, -7.37277336810124),
        )
        for actual, wanted in zip(points, expected):
            self.assertTrue(math.dist(actual, wanted) < 1e-9)
        self.assertTrue(math.isclose(math.dist(*points), 20.0, abs_tol=1e-9))

    def test_complete_mating_pattern_rotates_counterclockwise_by_2_5_degrees_from_cw50(self):
        pins = self.module.m4_pin_local_positions(self.params)
        pin_three = pins[self.params.display_six_oclock_pin_index - 1]
        self.assertTrue(math.isclose(pin_three[0], 1.9479565254429286, abs_tol=1e-9))
        self.assertTrue(math.isclose(pin_three[1], -8.7866640640794, abs_tol=1e-9))
        groove_center = self.module.rotate_mating_pattern_local(0.0, -19.0, self.params)
        self.assertTrue(math.isclose(groove_center[0], -14.008269399392444, abs_tol=1e-9))
        self.assertTrue(math.isclose(groove_center[1], -12.836213944697549, abs_tol=1e-9))

    def test_local_basis_is_orthonormal_and_matches_35_degree_plane(self):
        u_axis, v_axis, normal = self.module.local_basis(self.params)

        def dot(left, right):
            return sum(a * b for a, b in zip(left, right))

        for axis in (u_axis, v_axis, normal):
            self.assertTrue(math.isclose(dot(axis, axis), 1.0, abs_tol=1e-9))

        self.assertTrue(math.isclose(dot(u_axis, v_axis), 0.0, abs_tol=1e-9))
        self.assertTrue(math.isclose(dot(u_axis, normal), 0.0, abs_tol=1e-9))
        self.assertTrue(math.isclose(dot(v_axis, normal), 0.0, abs_tol=1e-9))
        self.assertTrue(
            math.isclose(v_axis[2], math.sin(math.radians(35.0)), abs_tol=1e-9)
        )

    def test_local_to_world_places_upper_pin_on_positive_local_vertical(self):
        center = self.params.mating_center_mm
        point = self.module.local_to_world(0.0, 9.0, 0.0, self.params)
        _, v_axis, _ = self.module.local_basis(self.params)
        expected = tuple(center[index] + 9.0 * v_axis[index] for index in range(3))

        for actual, wanted in zip(point, expected):
            self.assertTrue(math.isclose(actual, wanted, abs_tol=1e-9))

    def test_mating_center_gives_at_least_three_mm_conservative_clearance(self):
        clearance_mm = self.module.conservative_module_clearance_mm(self.params)
        self.assertGreaterEqual(clearance_mm, 3.0)
        self.assertTrue(math.isclose(clearance_mm, 5.015, abs_tol=0.01))

    def test_worst_case_outer_diameter_clears_top_of_base(self):
        desktop_clearance_mm = self.module.conservative_module_clearance_mm(
            self.params
        )
        clearance_above_base_mm = (
            desktop_clearance_mm - self.params.base_thickness_mm
        )
        self.assertGreaterEqual(clearance_above_base_mm, 1.0)

    def test_fpc_groove_fits_between_lower_alignment_pins(self):
        points = self.module.m4_pin_local_positions(self.params)
        unrotated_points = [
            self.module.rotate_local_clockwise(
                x_mm,
                v_mm,
                -self.params.mating_pattern_rotation_clockwise_deg,
            )
            for x_mm, v_mm in points
        ]
        lower_points = [point for point in unrotated_points if point[1] < 0.0]
        groove_half_width = self.params.fpc_groove_width_mm / 2.0
        pin_radius = self.params.printed_pin_diameter_mm / 2.0

        for x_mm, _ in lower_points:
            self.assertGreater(abs(x_mm) - pin_radius, groove_half_width)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(StandParameterTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
