"""Validate a finished catalog independently of its exporter."""
from __future__ import annotations
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]

def validate():
    manifest = json.loads((ROOT / "manifests/development-v1.json").read_text(encoding="utf-8"))
    base = ROOT.parent / "StreetV3"
    base_manifest = json.loads((base / "manifests/grid-city-street-assets-v3.json").read_text(encoding="utf-8"))
    properties = {r["key"]: r["tile_id"] for r in base_manifest["assets"] if r["kind"] == "property"}
    assert len(properties) == 22
    expected = {(key, level) for key in properties for level in range(6)}
    assets = manifest["assets"]
    assert len(assets) == 132
    assert {(r["key"], r["level"]) for r in assets} == expected
    assert manifest["state_count"] == 132 and manifest["new_state_count"] == 110
    assert manifest["missing"] == [] and manifest["runtime_integrated"] is False
    assert manifest["status"] == "complete-assets-not-deployed"
    source_hashes = set()
    file_count = 0
    for asset in assets:
        key, level = asset["key"], asset["level"]
        assert asset["tile_id"] == properties[key]
        assert asset["house_count"] == (level if level < 5 else 0)
        assert asset["hotel_count"] == int(level == 5)
        for kind, item in asset["files"].items():
            path = (ROOT / item["path"]).resolve()
            assert path.is_relative_to(ROOT.resolve()), (key, kind, "path escaped catalog")
            data = path.read_bytes()
            assert len(data) == item["bytes"], (key, level, kind, "size")
            assert hashlib.sha256(data).hexdigest() == item["sha256"], (key, level, kind, "hash")
            file_count += 1
        master = ROOT / asset["files"]["master"]["path"]
        digest = hashlib.sha256(master.read_bytes()).hexdigest()
        assert digest not in source_hashes, (key, level, "duplicate state image")
        source_hashes.add(digest)
        with Image.open(master) as image:
            image.verify()
        if level == 0:
            assert master.read_bytes() == (base / "source" / f"{key}.png").read_bytes(), (key, "original changed")
        for kind, raw_kind, size in [("tile_png", "tile_rgb565", 160), ("player_png", "player_rgb565", 128)]:
            with Image.open(ROOT / asset["files"][kind]["path"]) as image:
                assert image.size == (size, size), (key, level, kind)
                pixels = list(image.convert("RGB").get_flattened_data())
            raw = (ROOT / asset["files"][raw_kind]["path"]).read_bytes()
            assert len(raw) == size * size * 2
            # Decode every little-endian pixel and check RGB quantization bounds.
            for (word,), (r, g, b) in zip(struct.iter_unpack("<H", raw), pixels):
                assert (word >> 11) == r // 8
                assert ((word >> 5) & 63) == g // 4
                assert (word & 31) == b // 8
    result = dict(properties=22, states=132, files=file_count, unique_masters=len(source_hashes),
                  original_states_unchanged=22, rgb565_pixels_verified=132*(160*160+128*128),
                  runtime_integrated=False)
    print(json.dumps(result, ensure_ascii=False))
    return result

if __name__ == "__main__":
    validate()
