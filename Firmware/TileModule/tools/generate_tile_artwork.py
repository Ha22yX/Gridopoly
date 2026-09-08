"""Convert the complete Grid City 160px catalog to flash-resident RGB565 includes."""

from __future__ import annotations

import hashlib
from pathlib import Path

from PIL import Image


PROJECT_ROOT = Path(__file__).resolve().parents[3]
SOURCE_DIR = PROJECT_ROOT / "Assets" / "GridCity" / "StreetV3" / "device"
OUTPUT_DIR = Path(__file__).resolve().parents[1] / "src" / "assets"

ARTWORK = {
    "rivet_row": "a1-rivet-row.png",
    "copper_lane": "a2-copper-lane.png",
    "lantern_avenue": "b1-lantern-avenue.png",
    "tideway_drive": "b2-tideway-drive.png",
    "beacon_boulevard": "b3-beacon-boulevard.png",
    "canvas_street": "c1-canvas-street.png",
    "bloom_terrace": "c2-bloom-terrace.png",
    "aurora_avenue": "c3-aurora-avenue.png",
    "archive_way": "d1-archive-way.png",
    "forum_drive": "d2-forum-drive.png",
    "meridian_avenue": "d3-meridian-avenue.png",
    "pulse_street": "e1-pulse-street.png",
    "prism_boulevard": "e2-prism-boulevard.png",
    "nova_avenue": "e3-nova-avenue.png",
    "sunstep_terrace": "f1-sunstep-terrace.png",
    "helix_way": "f2-helix-way.png",
    "horizon_drive": "f3-horizon-drive.png",
    "canopy_lane": "g1-canopy-lane.png",
    "verdant_avenue": "g2-verdant-avenue.png",
    "summit_boulevard": "g3-summit-boulevard.png",
    "crown_promenade": "h1-crown-promenade.png",
    "grand_meridian": "h2-grand-meridian.png",
    "westline_terminal": "transit-westline-terminal.png",
    "northloop_station": "transit-northloop-station.png",
    "eastgate_terminal": "transit-eastgate-terminal.png",
    "southline_depot": "transit-southline-depot.png",
    "metro_grid": "utility-metro-grid.png",
    "bluewater_works": "utility-bluewater-works.png",
    "chance": "cover-chance.png",
    "community_fund": "cover-community-fund.png",
    "income_tax": "cover-income-tax.png",
    "luxury_tax": "cover-luxury-tax.png",
    "central_launch": "corner-central-launch.png",
    "civic_hold": "corner-civic-hold.png",
    "free_plaza": "corner-free-plaza.png",
    "hold_order": "corner-hold-order.png",
}


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def convert(name: str, filename: str) -> None:
    source = SOURCE_DIR / filename
    data = source.read_bytes()
    image = Image.open(source).convert("RGB")
    if image.size != (160, 160):
        raise ValueError(f"{source} must be 160x160, got {image.size}")

    values = [rgb565(*pixel) for pixel in image.get_flattened_data()]
    lines = [
        f"// Generated from {source.relative_to(PROJECT_ROOT).as_posix()}",
        f"// SHA-256: {hashlib.sha256(data).hexdigest()}",
    ]
    for offset in range(0, len(values), 12):
        chunk = ", ".join(f"0x{value:04X}" for value in values[offset : offset + 12])
        lines.append(f"  {chunk},")
    (OUTPUT_DIR / f"{name}_rgb565.inc").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, filename in ARTWORK.items():
        convert(name, filename)
        print(f"generated {name}_rgb565.inc")


if __name__ == "__main__":
    main()
