#!/usr/bin/env python3
"""Tests for the exact, immutable development/v1 deployment set."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("development_stager", Path(__file__).with_name("stage-development-assets.py"))
stager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stager)


class DevelopmentStagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected = stager.payload()
        cls.manifest = json.loads(stager.MANIFEST.read_text(encoding="utf-8"))

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="gridopoly-development-staging-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def invalid_manifest(self, mutate):
        manifest = copy.deepcopy(self.manifest)
        mutate(manifest)
        path = self.root / "manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(ValueError):
            stager.payload(manifest_path=path)

    def test_exact_published_set(self):
        self.assertEqual(len(self.expected), 330)
        for directory in ("tile", "player", "web"):
            self.assertEqual(sum(name.startswith(directory + "/") for name in self.expected), 110)
        self.assertFalse(any("-l0." in name for name in self.expected))
        self.assertTrue(all(len(data) == 51200 for name, data in self.expected.items() if name.startswith("tile/")))
        self.assertTrue(all(len(data) == 32768 for name, data in self.expected.items() if name.startswith("player/")))

    def test_atomic_stage_and_identical_reinstall(self):
        target = self.root / "tiles/development/v1"
        stager.stage(target, self.expected)
        stager.verify_existing(target, self.expected)
        times = {p: p.stat().st_mtime_ns for p in target.rglob("*")}
        stager.stage(target, self.expected)
        self.assertEqual(times, {p: p.stat().st_mtime_ns for p in target.rglob("*")})
        self.assertEqual(list(target.parent.glob(".development-v1-*")), [])

    def test_existing_changed_bytes_rejected_without_overwrite(self):
        target = self.root / "v1"
        stager.stage(target, self.expected)
        path = target / "tile/a1-rivet-row-l1.rgb565"
        data = bytearray(path.read_bytes()); data[55] ^= 1; path.write_bytes(data)
        with self.assertRaises(ValueError): stager.stage(target, self.expected)
        self.assertEqual(path.read_bytes(), data)

    def test_partial_existing_set_rejected(self):
        target = self.root / "v1"; target.mkdir()
        with self.assertRaises(ValueError): stager.stage(target, self.expected)
        self.assertEqual(list(target.iterdir()), [])

    def test_extra_file_rejected(self):
        target = self.root / "v1"; stager.stage(target, self.expected)
        (target / "unexpected").write_bytes(b"keep")
        with self.assertRaises(ValueError): stager.stage(target, self.expected)
        self.assertEqual((target / "unexpected").read_bytes(), b"keep")

    def test_extra_empty_directory_rejected(self):
        target = self.root / "v1"; stager.stage(target, self.expected)
        (target / "unpublished").mkdir()
        with self.assertRaises(ValueError): stager.verify_existing(target, self.expected)

    def test_duplicate_entry_rejected(self):
        self.invalid_manifest(lambda m: m["assets"].append(copy.deepcopy(m["assets"][1])))

    def test_missing_entry_rejected(self):
        self.invalid_manifest(lambda m: m["assets"].pop())

    def test_unpublished_key_rejected(self):
        self.invalid_manifest(lambda m: m["assets"][1].update(key="a1-new-key"))

    def test_unsafe_path_rejected(self):
        self.invalid_manifest(lambda m: m["assets"][1]["files"]["tile_rgb565"].update(path="../../secret"))

    def test_immutable_v1_hash_change_rejected(self):
        self.invalid_manifest(lambda m: m["assets"][1]["files"]["tile_rgb565"].update(sha256="0" * 64))

    def test_incorrect_rgb_size_rejected(self):
        self.invalid_manifest(lambda m: m["assets"][1]["files"]["tile_rgb565"].update(bytes=51201))

    def test_editable_status_is_not_payload_identity(self):
        manifest = copy.deepcopy(self.manifest); manifest["runtime_integrated"] = True
        path = self.root / "manifest.json"; path.write_text(json.dumps(manifest), encoding="utf-8")
        self.assertEqual(stager.payload(manifest_path=path), self.expected)

    def test_corrupt_and_truncated_source_rejected(self):
        source = self.root / "source"
        # Only the first selected source is needed: payload must fail there.
        entry = self.manifest["assets"][1]["files"]["tile_rgb565"]
        path = source / entry["path"]; path.parent.mkdir(parents=True)
        data = bytearray((stager.ASSET_ROOT / entry["path"]).read_bytes())
        for changed in (bytes(data[:-1]), bytes([data[0] ^ 1]) + bytes(data[1:])):
            path.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "source length/SHA mismatch"):
                stager.payload(asset_root=source)

    def test_missing_source_rejected(self):
        with self.assertRaises(ValueError): stager.payload(asset_root=self.root / "missing")

    def test_png_incomplete_or_changed_rejected(self):
        data = self.expected["web/a1-rivet-row-l1.png"]
        for invalid in (data[:8], data[:-1], data + b"trailing", b"x" + data[1:], data[:40] + bytes([data[40] ^ 1]) + data[41:]):
            with self.assertRaises(ValueError): stager.validate_png(invalid)

    def test_png_wrong_dimensions_rejected(self):
        data = bytearray(self.expected["web/a1-rivet-row-l1.png"])
        struct.pack_into(">I", data, 16, 128)
        struct.pack_into(">I", data, 29, zlib.crc32(data[12:29]))
        with self.assertRaisesRegex(ValueError, "160x160"): stager.validate_png(bytes(data))

    def test_installer_checks_immutability_before_binary(self):
        installer = (stager.PROJECT / "Server/RaspberryPi/deploy/install.sh").read_text(encoding="utf-8")
        self.assertLess(installer.index('--verify-existing "$TILE_TARGET/development/v1"'),
                        installer.index('install -m 0755 "$1"'))

    @unittest.skipUnless(os.name == "posix", "POSIX deployed filesystem symlink contract")
    def test_symlink_destination_and_entries_rejected(self):
        target = self.root / "real"; stager.stage(target, self.expected)
        link = self.root / "link"; link.symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError): stager.stage(link, self.expected)
        file = target / "tile/a1-rivet-row-l1.rgb565"
        file.unlink(); file.symlink_to(stager.ASSET_ROOT / "rgb565-tile/a1-rivet-row-l1.rgb565")
        with self.assertRaises(ValueError): stager.verify_existing(target, self.expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
