"""Protocol and material regression tests; generated test pixels are not artwork."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

import numpy as np
from PIL import Image

TOOLS = Path(__file__).resolve().parents[2] / "Assets/GridCity/Avatars/V2/tools"
sys.path.insert(0, str(TOOLS))
from avatar_v2 import (HEADER, SKINS, clean_source, component_bytes, decode_component,
    encode_samples, largest_component, make_material, normalize_face, quantize,
    register_box, source_over, tint, register_hair_interface, audit_hair_interface)
from build_avatar_v2 import acceptance_fingerprint, acceptance_matches


class AvatarV2Tests(unittest.TestCase):
    def component(self):
        rgba = np.zeros((320, 320, 4), dtype=np.uint8)
        rgba[67:218, 94:226] = (178, 178, 178, 255)
        rgba[140, 130:133] = (255, 255, 255, 255)
        mask = np.zeros((320, 320), dtype=np.uint8)
        mask[67:218, 94:226] = 255
        mask[140, 130:133] = 0
        return Image.fromarray(rgba), Image.fromarray(mask)

    def test_gavc5_roundtrip_including_material_and_crop(self):
        rgba, mask = self.component()
        payload, metadata = component_bytes("face", 7, rgba, mask)
        decoded, header = decode_component(payload)
        self.assertEqual(payload[:8], b"GAVC\x02\x01\x07\x02")
        np.testing.assert_array_equal(decoded[:, :, :4], np.array(rgba)[10:310, 50:270])
        np.testing.assert_array_equal(decoded[:, :, 4], np.array(mask)[10:310, 50:270])
        self.assertEqual(metadata["decodedBytes"], header["width"] * header["height"] * 5)

    def test_reject_corrupt_crc_truncated_payload_and_zero_run(self):
        rgba, mask = self.component()
        payload, _ = component_bytes("face", 1, rgba, mask)
        broken = bytearray(payload)
        broken[-1] ^= 1
        for bad in (bytes(broken), payload[:-1], payload[:12]):
            with self.assertRaises(ValueError):
                decode_component(bad)
        broken = bytearray(payload)
        broken[32:34] = b"\0\0"
        with self.assertRaisesRegex(ValueError, "run length"):
            decode_component(bytes(broken))

    def test_rle_literal_and_repeat_boundary(self):
        raw = b"\x01\x02\x03\x04\x05" * 32767 + b"\x05\x04\x03\x02\x01" * 3
        encoded = encode_samples(raw)
        self.assertEqual(struct.unpack_from("<H", encoded)[0], 0xFFFF)
        self.assertEqual(struct.unpack_from("<H", encoded, 7)[0], 0x8003)
        with self.assertRaises(ValueError):
            encode_samples(b"1234")

    def test_bright_skin_edge_is_tinted_but_eye_white_is_preserved(self):
        rgba = np.zeros((320, 320, 4), dtype=np.uint8)
        rgba[70, 160] = (245, 244, 242, 128)  # old saturation rule wrongly left this white
        rgba[140, 130] = (255, 255, 255, 255)  # eye white
        rgba[140, 160] = (25, 25, 25, 255)  # ink
        neutral, mask, audit = make_material(Image.fromarray(rgba), "face", {"protectRegions": [{"box": [120, 135, 140, 145], "mode": "eye"}]})
        self.assertEqual(mask.getpixel((160, 70)), 255)
        self.assertEqual(mask.getpixel((130, 140)), 0)
        self.assertEqual(mask.getpixel((160, 140)), 0)
        dark = np.array(tint(neutral, mask, SKINS["ebony"], "face"))
        self.assertLess(int(dark[70, 160, 0]), 130)
        self.assertEqual(tuple(dark[140, 130]), (255, 255, 255, 255))
        np.testing.assert_array_equal(dark[:, :, 3], rgba[:, :, 3])
        self.assertFalse(audit["reviewed"])

    def test_shared_neck_uses_same_integer_skin_material(self):
        image = Image.new("RGBA", (320, 320), (178, 178, 178, 123))
        mask = Image.new("L", (320, 320), 255)
        for name, rgb in SKINS.items():
            face = tint(image, mask, rgb, "face")
            neck = tint(image, mask, rgb, "neck")
            self.assertEqual(face.tobytes(), neck.tobytes())
            self.assertEqual(face.getpixel((0, 0)), (*rgb, 123))

    def test_exact_shared_face_alpha_keeps_interior_colour(self):
        template = Image.new("RGBA", (320, 320))
        source = Image.new("RGBA", (320, 320))
        for y in range(67, 218):
            for x in range(94, 226):
                template.putpixel((x, y), (180, 160, 140, 255))
            for x in range(84, 236):
                source.putpixel((x, y), (30, 140, 210, 255))
        result, record = normalize_face(source, template)
        self.assertEqual(result.getchannel("A").tobytes(), template.getchannel("A").tobytes())
        self.assertEqual(result.getpixel((160, 140)), (30, 140, 210, 255))
        self.assertLess(record["maximumRowScale"], 1)

    def test_isolated_alpha_noise_removed_without_changing_original(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "source.png"
            image = Image.new("RGBA", (60, 60))
            for y in range(20, 40):
                for x in range(20, 40):
                    image.putpixel((x, y), (100, 120, 140, 255))
            image.putpixel((2, 2), (255, 0, 255, 100))
            image.putpixel((19, 20), (100, 120, 140, 12))
            image.save(p)
            before = p.read_bytes()
            clean, audit = clean_source(p)
            self.assertEqual(clean.getpixel((2, 2)), (0, 0, 0, 0))
            self.assertEqual(clean.getpixel((19, 20))[3], 12)
            self.assertEqual(audit["mainComponentBounds"], [20, 20, 40, 40])
            self.assertEqual(audit["discardedNonzeroAlphaPixels"], 1)
            self.assertEqual(before, p.read_bytes())

    def test_source_over_matches_integer_edge_example(self):
        dst = np.array([[[20, 40, 60, 128]]], dtype=np.uint8)
        src = np.array([[[200, 100, 50, 128]]], dtype=np.uint8)
        self.assertEqual(tuple(source_over(dst, src)[0, 0]), (140, 80, 53, 192))
        self.assertEqual(source_over(dst, np.zeros_like(dst)).tobytes(), dst.tobytes())

    def test_quantization_zeroes_hidden_colour_and_preserves_gray_material(self):
        rgba = Image.new("RGBA", (320, 320), (177, 177, 177, 3))
        mask = Image.new("L", (320, 320), 255)
        result, mask = quantize(rgba, mask)
        self.assertEqual(result.getpixel((0, 0)), (0, 0, 0, 0))
        self.assertEqual(mask.getpixel((0, 0)), 0)

    def test_visual_acceptance_is_bound_to_source_samples_and_registration(self):
        records = [{"file": "face/f1.gavc", "fileSha256": "sample-a", "sourceSha256": "source-a", "skinMaterial": {"maskSha256": "mask-a"}}]
        config = {"registration": {"x": 1}}
        fingerprint = acceptance_fingerprint(config, records)
        accepted = {**fingerprint, "approved": True}
        self.assertTrue(acceptance_matches(fingerprint, accepted))
        self.assertFalse(acceptance_matches(fingerprint, {**accepted, "approved": False}))
        self.assertFalse(acceptance_matches(acceptance_fingerprint({"registration": {"x": 2}}, records), accepted))
        records[0]["sourceSha256"] = "source-b"
        self.assertFalse(acceptance_matches(acceptance_fingerprint(config, records), accepted))

    def test_hair_inner_fit_preserves_long_outer_edges_and_y_positions(self):
        rgba = np.zeros((320, 320, 4), dtype=np.uint8)
        rgba[90:230, 90:106] = (110, 120, 130, 255)
        rgba[90:230, 215:231] = (110, 120, 130, 255)
        image = Image.fromarray(rgba)
        config = {"headCoreContour": [[90, 110, 210], [230, 110, 210]], "strengthAnchors": [[90, 0], [110, 1], [180, 1], [200, 0]], "overlapPixels": 2, "auditRows": [110, 181]}
        fit, evidence = register_hair_interface(image, config)
        self.assertEqual(fit.getchannel("A").getbbox(), image.getchannel("A").getbbox())
        self.assertEqual(np.array(fit)[200:].tobytes(), rgba[200:].tobytes())
        face = np.zeros_like(rgba)
        face[90:230, 110:211] = (180, 180, 180, 255)
        audit, _ = audit_hair_interface(fit, Image.fromarray(face), config)
        self.assertEqual(audit["failureRows"], 0)
        self.assertEqual(audit["actualTransparentSlitPixels"], 0)

    def test_hair_interior_gap_uses_head_core_not_ears(self):
        hair = np.zeros((320, 320, 4), dtype=np.uint8)
        hair[100:180, 90:106] = (100, 110, 120, 255)
        hair[100:180, 215:231] = (100, 110, 120, 255)
        face = np.zeros_like(hair)
        face[100:180, 94:226] = (180, 180, 180, 255)  # ear-inclusive alpha hides the opening mismatch
        audit, _ = audit_hair_interface(Image.fromarray(hair), Image.fromarray(face), {"headCoreContour": [[100, 110, 210], [180, 110, 210]], "auditRows": [120, 121]})
        self.assertEqual(audit["failureRows"], 1)
        self.assertEqual(audit["actualTransparentSlitPixels"], 0)
        self.assertEqual(audit["rows"][0]["leftGapPixels"], 4)

    def test_hair_overlapping_cheek_too_deeply_also_fails(self):
        hair = np.zeros((320, 320, 4), dtype=np.uint8)
        hair[100:180, 90:124] = (100, 110, 120, 255)
        hair[100:180, 197:231] = (100, 110, 120, 255)
        audit, _ = audit_hair_interface(Image.fromarray(hair), Image.new("RGBA", (320, 320)), {"headCoreContour": [[100, 110, 210], [180, 110, 210]], "auditRows": [120, 121]})
        self.assertEqual(audit["failureRows"], 1)
        self.assertEqual(audit["rows"][0]["leftGapPixels"], 0)
        self.assertEqual(audit["rows"][0]["leftOverlapPixels"], 14)

    def test_invalid_vertical_registration_rejected(self):
        image = Image.new("RGBA", (320, 320))
        with self.assertRaises(ValueError):
            register_box(image, [0, 0, 320, 320], [94, 67, 226, 218], [[100, 150], [90, 160]])


if __name__ == "__main__":
    unittest.main()
