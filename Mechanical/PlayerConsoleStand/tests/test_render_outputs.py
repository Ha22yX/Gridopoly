import importlib.util
import sys
import unittest
from pathlib import Path

import bpy


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = ROOT / "scripts"
RENDERER_PATH = SCRIPT_DIR / "render_stand.py"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))


def load_renderer():
    spec = importlib.util.spec_from_file_location("render_stand", RENDERER_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot load renderer: {RENDERER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RenderOutputTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(
            RENDERER_PATH.exists(), f"required renderer is missing: {RENDERER_PATH}"
        )
        if not hasattr(self.__class__, "renderer"):
            self.__class__.renderer = load_renderer()
            self.__class__.paths = self.renderer.render_previews(ROOT)

    def test_two_preview_files_are_created(self):
        self.assertEqual(set(self.paths), {"assembled", "print"})
        for path in self.paths.values():
            self.assertTrue(path.exists(), f"missing render: {path}")
            self.assertGreater(path.stat().st_size, 50_000)

    def test_previews_are_1200_by_900_and_nonblank(self):
        for label, path in self.paths.items():
            image = bpy.data.images.load(str(path), check_existing=False)
            try:
                self.assertEqual(tuple(image.size), (1200, 900))
                pixels = list(image.pixels[::4000])
                self.assertGreater(max(pixels) - min(pixels), 0.08, label)
            finally:
                bpy.data.images.remove(image)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RenderOutputTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
