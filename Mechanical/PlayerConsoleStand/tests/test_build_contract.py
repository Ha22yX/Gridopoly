import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "build_stand.py"
PARAMETER_PATH = ROOT / "scripts" / "stand_parameters.py"


def load_module(name, path):
    if not path.exists():
        raise AssertionError(f"required module is missing: {path}")

    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load module: {path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class BuildContractTests(unittest.TestCase):
    def setUp(self):
        self.parameters = load_module("stand_parameters", PARAMETER_PATH)
        self.builder = load_module("build_stand", SCRIPT_PATH)
        self.params = self.parameters.StandParameters()
        self.contract = self.builder.build_contract(self.params)

    def test_contract_has_one_printable_stand_and_reference_assembly(self):
        self.assertEqual(self.contract["print_object"], "STAND_PRINT")
        self.assertEqual(self.contract["reference_object"], "MODULE_REFERENCE")
        self.assertEqual(self.contract["mating_angle_deg"], 35.0)

    def test_contract_describes_exact_base_wedge_and_pad(self):
        self.assertEqual(self.contract["base_mm"], [94.0, 82.0, 4.0])
        self.assertEqual(self.contract["base_corner_radius_mm"], 5.0)
        self.assertEqual(self.contract["wedge_width_mm"], 54.0)
        self.assertEqual(self.contract["wedge_y_range_mm"], [-18.0, 32.0])
        self.assertEqual(self.contract["mating_pad_mm"], [40.0, 2.0])

    def test_contract_has_three_male_pins_and_two_female_holes(self):
        self.assertEqual(len(self.contract["male_pins"]), 3)
        self.assertEqual(len(self.contract["female_holes"]), 2)

        for pin in self.contract["male_pins"]:
            self.assertEqual(pin["diameter_mm"], 2.9)
            self.assertEqual(pin["height_mm"], 3.0)

        self.assertEqual(
            [hole["diameter_mm"] for hole in self.contract["female_holes"]],
            [3.1, 4.1],
        )
        self.assertEqual(
            [
                post["diameter_mm"]
                for post in self.contract.get("module_locator_posts", [])
            ],
            [3.0, 4.0],
        )
        for hole in self.contract["female_holes"]:
            self.assertEqual(hole["depth_mm"], 4.0)

    def test_contract_has_open_fpc_groove(self):
        groove = self.contract["fpc_groove"]
        self.assertEqual(groove["width_mm"], 9.0)
        self.assertEqual(groove["depth_mm"], 2.0)
        self.assertEqual(groove["v_range_mm"], [-28.0, 2.0])
        self.assertEqual(groove["rotation_clockwise_deg"], 47.5)
        self.assertEqual(groove["exit"], "front_lower_after_clockwise_rotation")

    def test_contract_records_c_ring_and_display_orientation(self):
        self.assertEqual(
            self.contract.get("c_ring"),
            {
                "inner_diameter_mm": 34.5,
                "outer_diameter_mm": 40.0,
                "height_mm": 7.6,
                "opening_width_mm": 9.0,
                "opening_direction": "clockwise_47.5_from_front_lower",
            },
        )
        self.assertEqual(
            self.contract.get("display_orientation"),
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


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BuildContractTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
