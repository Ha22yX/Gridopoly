#!/usr/bin/env python3
"""Build the exact manifest-defined web tile deployment directory."""

from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[3]
ASSET_ROOT = PROJECT / "Assets" / "GridCity" / "StreetV3"
DEVICE = ASSET_ROOT / "device"
RGB565 = ASSET_ROOT / "device-rgb565"
MANIFEST = ASSET_ROOT / "manifests" / "grid-city-street-assets-v3.json"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
SAFE_KEY = re.compile(r"^[a-z0-9-]+$")


def fail(message: str) -> None:
    raise SystemExit(f"GRIDOPOLY_WEB_TILE_STAGING_FAIL: {message}")


def project_path(relative: str) -> Path:
    path = (PROJECT / relative).resolve()
    project = PROJECT.resolve()
    if path != project and project not in path.parents:
        fail(f"manifest source escapes project: {relative}")
    return path


def png_source(asset: dict[str, object]) -> Path:
    source = asset.get("source")
    return project_path(str(source)) if source else DEVICE / f'{asset["key"]}.png'


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: stage-web-tile-assets.py OUTPUT_DIRECTORY")

    output = Path(sys.argv[1]).resolve()
    if output == Path(output.anchor) or output == PROJECT.resolve():
        fail(f"unsafe output directory: {output}")
    if output.exists() and any(output.iterdir()):
        fail(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assets = manifest.get("assets", [])
    if len(assets) != 36:
        fail(f"expected 36 manifest assets, found {len(assets)}")

    keys = [str(asset.get("key", "")) for asset in assets]
    if len(set(keys)) != len(keys):
        fail("manifest contains duplicate keys")
    for key in keys:
        if not SAFE_KEY.fullmatch(key):
            fail(f"unsafe manifest key: {key}")

    expected: set[str] = set()
    for asset in assets:
        key = str(asset["key"])
        png = png_source(asset)
        rgb565 = RGB565 / f"{key}.rgb565"
        signature = b""
        if png.is_file():
            with png.open("rb") as handle:
                signature = handle.read(8)
        if signature != PNG_SIGNATURE:
            fail(f"missing or malformed PNG truth: {png}")
        if not rgb565.is_file() or rgb565.stat().st_size != 32768:
            fail(f"missing or malformed RGB565 truth: {rgb565}")

        png_name = f"{key}.png"
        rgb565_name = f"{key}.rgb565"
        shutil.copyfile(png, output / png_name)
        shutil.copyfile(rgb565, output / rgb565_name)
        expected.update((png_name, rgb565_name))

    actual = {path.name for path in output.iterdir() if path.is_file()}
    if actual != expected or len(actual) != 72:
        fail(
            f"staged set is not exact: missing={sorted(expected - actual)} "
            f"extra={sorted(actual - expected)}"
        )

    print("GRIDOPOLY_WEB_TILE_STAGING_PASS png=36 rgb565=36 files=72")


if __name__ == "__main__":
    main()
