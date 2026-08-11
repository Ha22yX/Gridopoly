from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT.parents[2]
MANIFEST_PATH = ROOT / "manifests" / "grid-city-street-assets-v3.json"
PROCESSED_MANIFEST_PATH = (
    ROOT / "manifests" / "grid-city-street-assets-v3.processed.json"
)
SOURCE_DIR = ROOT / "source"
DEVICE_DIR = ROOT / "device"
REVIEW_DIR = ROOT / "review"
CONTACT_SHEET_PATH = REVIEW_DIR / "asset-contact-sheet.png"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    windows_fonts = Path("C:/Windows/Fonts")
    candidates = (
        [windows_fonts / "arialbd.ttf", windows_fonts / "segoeuib.ttf"]
        if bold
        else [windows_fonts / "arial.ttf", windows_fonts / "segoeui.ttf"]
    )
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


def build_contact_sheet(records: list[dict]) -> None:
    columns = 8
    rows = (len(records) + columns - 1) // columns
    cell_width = 220
    cell_height = 246
    margin = 28
    header_height = 90
    sheet = Image.new(
        "RGB",
        (margin * 2 + columns * cell_width, header_height + margin + rows * cell_height),
        "#111719",
    )
    draw = ImageDraw.Draw(sheet)
    title_font = load_font(28, bold=True)
    label_font = load_font(16, bold=True)
    meta_font = load_font(13)
    draw.text((margin, 22), "GRID CITY STREET ASSETS V3", fill="#F4F7F5", font=title_font)
    draw.text(
        (margin, 58),
        "A2 Copper Lane reference-locked production set | 36 production assets",
        fill="#8EA0A5",
        font=meta_font,
    )

    for index, record in enumerate(records):
        row, column = divmod(index, columns)
        x = margin + column * cell_width
        y = header_height + row * cell_height
        draw.rounded_rectangle(
            (x + 5, y + 5, x + cell_width - 5, y + cell_height - 5),
            radius=6,
            fill="#182124",
            outline="#2D3A3E",
            width=1,
        )
        image = Image.open(record["source_path_abs"]).convert("RGB")
        image = image.resize((184, 184), Image.Resampling.LANCZOS)
        sheet.paste(image, (x + 18, y + 14))
        draw.text((x + 16, y + 202), record["name"], fill="#EDF5F2", font=label_font)
        draw.text(
            (x + 16, y + 225),
            f'{record["tile_id"]} | {record["kind"].replace("_", " ").upper()}',
            fill=record["accent"],
            font=meta_font,
        )

    sheet.save(CONTACT_SHEET_PATH, optimize=True)


def main() -> None:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    source_size = int(manifest["source_size"])
    device_size = int(manifest["device_size"])
    expected_keys = [
        asset["key"] for asset in manifest["assets"] if not asset.get("source")
    ]
    actual_keys = sorted(path.stem for path in SOURCE_DIR.glob("*.png"))

    missing = sorted(set(expected_keys) - set(actual_keys))
    unexpected = sorted(set(actual_keys) - set(expected_keys))
    if missing or unexpected:
        raise RuntimeError(f"Asset catalog mismatch: missing={missing}, unexpected={unexpected}")

    DEVICE_DIR.mkdir(parents=True, exist_ok=True)
    REVIEW_DIR.mkdir(parents=True, exist_ok=True)
    records: list[dict] = []

    for asset in manifest["assets"]:
        explicit_source = asset.get("source")
        source_path = (
            PROJECT / explicit_source
            if explicit_source
            else SOURCE_DIR / f'{asset["key"]}.png'
        )
        device_path = DEVICE_DIR / f'{asset["key"]}.png'
        with Image.open(source_path) as source:
            source_image = source.convert("RGB")
        if source_image.size != (source_size, source_size):
            width, height = source_image.size
            if width != height or width < source_size:
                raise RuntimeError(
                    f"{source_path.name}: cannot normalize {source_image.size} to "
                    f"{source_size}x{source_size} without cropping or upscaling"
                )
            source_image = source_image.resize(
                (source_size, source_size), Image.Resampling.LANCZOS
            )
            temporary_path = source_path.with_suffix(".normalized.png")
            source_image.save(temporary_path, optimize=True)
            temporary_path.replace(source_path)
            print(f"Normalized {source_path.name}: {width}x{height} -> {source_size}x{source_size}")

        converted = source_image.resize(
                (device_size, device_size), Image.Resampling.LANCZOS
        )
        converted.save(device_path, optimize=True)

        group = manifest["groups"].get(asset["group"], {})
        records.append(
            {
                **asset,
                "accent": group.get("color", "#91A1A6"),
                "source": (
                    explicit_source
                    if explicit_source
                    else source_path.relative_to(ROOT).as_posix()
                ),
                "source_dimensions": [source_size, source_size],
                "source_sha256": sha256(source_path),
                "device": device_path.relative_to(ROOT).as_posix(),
                "device_dimensions": [device_size, device_size],
                "device_sha256": sha256(device_path),
                "source_path_abs": str(source_path),
                "device_path_abs": str(device_path),
            }
        )

    build_contact_sheet(records)
    processed = {
        "schema_version": manifest["schema_version"],
        "catalog_id": manifest["catalog_id"],
        "canonical_style_reference": "source/a2-copper-lane.png",
        "asset_count": len(records),
        "contact_sheet": CONTACT_SHEET_PATH.relative_to(ROOT).as_posix(),
        "assets": [
            {
                key: value
                for key, value in record.items()
                if key not in {"accent", "source_path_abs", "device_path_abs"}
            }
            for record in records
        ],
    }
    PROCESSED_MANIFEST_PATH.write_text(
        json.dumps(processed, ensure_ascii=True, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Processed {len(records)} assets")
    print(f"Device assets: {DEVICE_DIR}")
    print(f"Contact sheet: {CONTACT_SHEET_PATH}")


if __name__ == "__main__":
    main()
