from __future__ import annotations

import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "source"
DEVICE_DIR = ROOT / "device"
MANIFEST_DIR = ROOT / "manifests"
FIRMWARE_DIR = ROOT.parents[2] / "Firmware" / "PlayerConsole" / "src" / "assets"

ASSETS = (
    (
        "property_neon_harbor",
        "property_neon_harbor_master.png",
        160,
        "asset_property_neon_harbor",
    ),
    (
        "transit_cloudrail_central",
        "transit_cloudrail_central_master.png",
        160,
        "asset_transit_cloudrail_central",
    ),
    (
        "utility_quantum_grid",
        "utility_quantum_grid_master.png",
        160,
        "asset_utility_quantum_grid",
    ),
    (
        "avatar_p1_lingxi",
        "avatar_p1_lingxi_master.png",
        128,
        "asset_avatar_p1_lingxi",
    ),
)


def rgb565_bytes(image: Image.Image) -> bytes:
    payload = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        payload.append(value & 0xFF)
        payload.append((value >> 8) & 0xFF)
    return bytes(payload)


def write_c_source(entries: list[dict[str, object]]) -> None:
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    header_lines = [
        "#pragma once",
        "",
        "#include <lvgl.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    source_lines = [
        '#include "sample_assets.h"',
        "",
    ]

    for entry in entries:
        symbol = str(entry["symbol"])
        width = int(entry["width"])
        height = int(entry["height"])
        payload = entry["payload"]
        assert isinstance(payload, bytes)
        data_symbol = f"{symbol}_map"
        header_lines.append(f"extern const lv_img_dsc_t {symbol};")
        source_lines.extend(
            [
                f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {data_symbol}[] = {{",
            ]
        )
        for offset in range(0, len(payload), 24):
            chunk = payload[offset : offset + 24]
            source_lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
        source_lines.extend(
            [
                "};",
                "",
                f"const lv_img_dsc_t {symbol} = {{",
                "    .header = {",
                "        .cf = LV_IMG_CF_TRUE_COLOR,",
                "        .always_zero = 0,",
                "        .reserved = 0,",
                f"        .w = {width},",
                f"        .h = {height},",
                "    },",
                f"    .data_size = {len(payload)},",
                f"    .data = {data_symbol},",
                "};",
                "",
            ]
        )

    header_lines.extend(["", "#ifdef __cplusplus", "}", "#endif", ""])
    (FIRMWARE_DIR / "sample_assets.h").write_text(
        "\n".join(header_lines), encoding="ascii"
    )
    (FIRMWARE_DIR / "sample_assets.c").write_text(
        "\n".join(source_lines), encoding="ascii"
    )


def main() -> None:
    DEVICE_DIR.mkdir(parents=True, exist_ok=True)
    MANIFEST_DIR.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, object]] = []
    manifest_assets: list[dict[str, object]] = []

    for asset_id, filename, size, symbol in ASSETS:
        source_path = SOURCE_DIR / filename
        if not source_path.exists():
            raise FileNotFoundError(source_path)
        with Image.open(source_path) as source:
            image = source.convert("RGB").resize((size, size), Image.Resampling.LANCZOS)
        preview_path = DEVICE_DIR / f"{asset_id}_{size}.png"
        image.save(preview_path, format="PNG", optimize=True)
        payload = rgb565_bytes(image)
        entries.append(
            {
                "symbol": symbol,
                "width": size,
                "height": size,
                "payload": payload,
            }
        )
        manifest_assets.append(
            {
                "id": asset_id,
                "source": str(source_path.relative_to(ROOT.parent.parent.parent)),
                "device_preview": str(preview_path.relative_to(ROOT.parent.parent.parent)),
                "width": size,
                "height": size,
                "format": "LVGL_TRUE_COLOR_RGB565_LE",
                "bytes": len(payload),
                "symbol": symbol,
            }
        )

    write_c_source(entries)
    manifest = {
        "version": 1,
        "style": "flat-2d-isometric-cartoon-v1",
        "palette": ["#090E10", "#EDF3F1", "#52DCB7", "#F2C453", "#58A7EB", "#EF7168"],
        "assets": manifest_assets,
        "embedded_bytes": sum(int(item["bytes"]) for item in manifest_assets),
    }
    (MANIFEST_DIR / "sample-assets.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
