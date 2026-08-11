#!/usr/bin/env python3
"""Regression test for the exact 36 PNG + 36 RGB565 deployment payload."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[3]
TOOLS = PROJECT / "Server" / "RaspberryPi" / "tools"
STAGER = TOOLS / "stage-web-tile-assets.py"
MANIFEST = (
    PROJECT
    / "Assets"
    / "GridCity"
    / "StreetV3"
    / "manifests"
    / "grid-city-street-assets-v3.json"
)
DEVICE = PROJECT / "Assets" / "GridCity" / "StreetV3" / "device"
RGB565 = PROJECT / "Assets" / "GridCity" / "StreetV3" / "device-rgb565"
CORNER_KEYS = {
    "corner-central-launch",
    "corner-civic-hold",
    "corner-free-plaza",
    "corner-hold-order",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    if not STAGER.is_file():
        raise AssertionError(f"missing deployment stager: {STAGER}")

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assets = manifest["assets"]
    assert len(assets) == 36
    assert {asset["key"] for asset in assets if asset.get("source")} == CORNER_KEYS

    with tempfile.TemporaryDirectory(prefix="gridopoly-web-tiles-") as temporary:
        output = Path(temporary) / "tiles"
        completed = subprocess.run(
            [sys.executable, str(STAGER), str(output)],
            cwd=PROJECT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stdout)

        expected_names = {
            *(f'{asset["key"]}.png' for asset in assets),
            *(f'{asset["key"]}.rgb565' for asset in assets),
        }
        actual_names = {path.name for path in output.iterdir() if path.is_file()}
        assert actual_names == expected_names, (
            f"deployment set mismatch: missing={sorted(expected_names - actual_names)} "
            f"extra={sorted(actual_names - expected_names)}"
        )

        for asset in assets:
            key = asset["key"]
            png_truth = PROJECT / asset["source"] if asset.get("source") else DEVICE / f"{key}.png"
            assert digest(output / f"{key}.png") == digest(png_truth), key
            rgb_truth = RGB565 / f"{key}.rgb565"
            assert (output / f"{key}.rgb565").stat().st_size == 32768, key
            assert digest(output / f"{key}.rgb565") == digest(rgb_truth), key

    print("GRIDOPOLY_WEB_TILE_STAGING_TESTS_PASS png=36 rgb565=36 corners=4")


if __name__ == "__main__":
    main()
