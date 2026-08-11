#!/usr/bin/env python3
"""Convert Grid City artwork into HTTP-served RGB565 player-console assets."""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image


PROJECT = Path(__file__).resolve().parents[3]
ASSET_ROOT = PROJECT / "Assets" / "GridCity" / "StreetV3"
ASSET_DIR = ASSET_ROOT / "device"
REMOTE_DIR = ASSET_ROOT / "device-rgb565"
MANIFEST = ASSET_ROOT / "manifests" / "grid-city-street-assets-v3.json"
OUTPUT_DIR = PROJECT / "Firmware" / "PlayerConsole" / "src" / "assets"
EMBEDDED_MANIFEST = OUTPUT_DIR / "grid_city_tile_assets.manifest.json"
EMBEDDED_SIZE = 128


def rgb565_bytes(image: Image.Image) -> bytes:
    result = bytearray()
    for red, green, blue in image.convert("RGB").get_flattened_data():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        result.extend((value & 0xFF, value >> 8))
    return bytes(result)


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assets = manifest["assets"]
    if len(assets) != 36:
        raise SystemExit(f"expected 36 tile assets, found {len(assets)}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    REMOTE_DIR.mkdir(parents=True, exist_ok=True)
    embedded_assets = []
    total_bytes = 0
    expected_files = set()

    for asset in assets:
        key = asset["key"]
        source_path = (
            PROJECT / asset["source"]
            if asset.get("source")
            else ASSET_DIR / f"{key}.png"
        )
        if not source_path.exists():
            raise SystemExit(f"missing tile source image: {source_path}")
        with Image.open(source_path) as original:
            if original.width != original.height:
                raise SystemExit(f"tile source must be square: {source_path}")
            image = original.convert("RGB").resize(
                (EMBEDDED_SIZE, EMBEDDED_SIZE), Image.Resampling.LANCZOS
            )

        data = rgb565_bytes(image)
        total_bytes += len(data)
        remote_name = f"{key}.rgb565"
        expected_files.add(remote_name)
        (REMOTE_DIR / remote_name).write_bytes(data)
        embedded_assets.append(
            {
                "key": key,
                "tile_id": asset["tile_id"],
                "name": asset["name"],
                "kind": asset["kind"],
                "path": f"/assets/tiles/{remote_name}",
                "width": EMBEDDED_SIZE,
                "height": EMBEDDED_SIZE,
                "format": "LVGL_TRUE_COLOR_RGB565_LE",
                "bytes": len(data),
            }
        )

    for stale in REMOTE_DIR.glob("*.rgb565"):
        if stale.name not in expected_files:
            stale.unlink()
    EMBEDDED_MANIFEST.write_text(
        json.dumps(
            {
                "schema_version": 2,
                "catalog_id": manifest["catalog_id"],
                "delivery": "http-rgb565-psram-cache",
                "asset_count": len(embedded_assets),
                "embedded_size": EMBEDDED_SIZE,
                "embedded_bytes": total_bytes,
                "assets": embedded_assets,
            },
            ensure_ascii=True,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"generated {len(embedded_assets)} remote tile assets at {EMBEDDED_SIZE}x{EMBEDDED_SIZE}")
    print(f"server RGB565 bytes: {total_bytes}; firmware RGB565 bytes: 0")


if __name__ == "__main__":
    main()
