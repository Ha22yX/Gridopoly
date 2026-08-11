from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
DEVICE = ROOT / "device" / "layers"
RUNTIME = ROOT / "runtime"
MANIFEST = ROOT / "avatar-layers.manifest.json"
PACK = RUNTIME / "avatar-v1.layers"
PACK_MANIFEST = RUNTIME / "avatar-v1.layers.json"
WIDTH = 320
HEIGHT = 320


def encode_pixels(raw: bytes) -> bytes:
    if len(raw) != WIDTH * HEIGHT * 4:
        raise ValueError(f"unexpected RGBA byte count {len(raw)}")
    pixels = [raw[index : index + 4] for index in range(0, len(raw), 4)]
    encoded = bytearray()
    index = 0
    while index < len(pixels):
        run = 1
        while index + run < len(pixels) and pixels[index + run] == pixels[index] and run < 0x7FFF:
            run += 1
        if run >= 3:
            encoded += struct.pack("<H", 0x8000 | run)
            encoded += pixels[index]
            index += run
            continue
        literal_start = index
        index += run
        while index < len(pixels) and index - literal_start < 0x7FFF:
            following = 1
            while (
                index + following < len(pixels)
                and pixels[index + following] == pixels[index]
                and following < 3
            ):
                following += 1
            if following >= 3:
                break
            index += following
        count = index - literal_start
        encoded += struct.pack("<H", count)
        encoded += b"".join(pixels[literal_start:index])
    return bytes(encoded)


def source_entries(manifest: dict) -> list[tuple[int, int, int, Path]]:
    entries: list[tuple[int, int, int, Path]] = []
    skin_ids = [item["id"] for item in manifest["skinColors"]]
    hair_color_ids = [item["id"] for item in manifest["hairColors"]]
    for preset, face in enumerate(manifest["face"], 1):
        for variant, skin in enumerate(skin_ids, 1):
            entries.append((1, preset, variant, DEVICE / "face" / f"{face['id'].lower()}-{skin}.png"))
    for preset, outfit in enumerate(manifest["outfit"], 1):
        entries.append((2, preset, 0, DEVICE / "outfit" / f"{outfit['id'].lower()}.png"))
    for preset, hair in enumerate(manifest["hair"], 1):
        for variant, color in enumerate(hair_color_ids, 1):
            entries.append((3, preset, variant, DEVICE / "hair" / f"{hair['id'].lower()}-{color}.png"))
    return entries


def main() -> None:
    manifest_bytes = MANIFEST.read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest.get("schema") != 1 or manifest.get("deviceCanvas") != {"width": WIDTH, "height": HEIGHT}:
        raise ValueError("avatar manifest schema/canvas mismatch")
    sources = source_entries(manifest)
    if len(sources) != 290:
        raise ValueError(f"expected 290 runtime layers, got {len(sources)}")

    encoded_layers: list[bytes] = []
    records: list[dict] = []
    for kind, preset, variant, path in sources:
        if not path.is_file():
            raise FileNotFoundError(path)
        image = Image.open(path).convert("RGBA")
        if image.size != (WIDTH, HEIGHT):
            raise ValueError(f"{path} is {image.size}, expected {(WIDTH, HEIGHT)}")
        raw = image.tobytes()
        encoded = encode_pixels(raw)
        encoded_layers.append(encoded)
        records.append(
            {
                "kind": kind,
                "preset": preset,
                "variant": variant,
                "source": path.relative_to(ROOT).as_posix(),
                "rawBytes": len(raw),
                "encodedBytes": len(encoded),
                "rgbaSha256": hashlib.sha256(raw).hexdigest(),
            }
        )

    header_size = 32
    index_size = 16 * len(records)
    data_offset = header_size + index_size
    manifest_hash64 = int.from_bytes(hashlib.sha256(manifest_bytes).digest()[:8], "little")
    output = bytearray(
        struct.pack(
            "<4sHHHHHHIIQ",
            b"GAVL",
            1,
            header_size,
            WIDTH,
            HEIGHT,
            len(records),
            0,
            header_size,
            data_offset,
            manifest_hash64,
        )
    )
    offset = data_offset
    for record, encoded in zip(records, encoded_layers):
        output += struct.pack(
            "<BBBBIII",
            record["kind"],
            record["preset"],
            record["variant"],
            0,
            offset,
            len(encoded),
            WIDTH * HEIGHT * 4,
        )
        offset += len(encoded)
    for encoded in encoded_layers:
        output += encoded

    RUNTIME.mkdir(parents=True, exist_ok=True)
    temporary = PACK.with_suffix(".layers.tmp")
    temporary.write_bytes(output)
    temporary.replace(PACK)
    summary = {
        "schema": 1,
        "sourceManifest": MANIFEST.name,
        "sourceManifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "pack": PACK.name,
        "packSha256": hashlib.sha256(output).hexdigest(),
        "width": WIDTH,
        "height": HEIGHT,
        "entryCount": len(records),
        "rawBytes": len(records) * WIDTH * HEIGHT * 4,
        "packBytes": len(output),
        "entries": records,
    }
    PACK_MANIFEST.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS: avatar runtime pack {len(records)} layers, "
        f"{summary['rawBytes']} raw bytes -> {summary['packBytes']} bytes"
    )


if __name__ == "__main__":
    main()
