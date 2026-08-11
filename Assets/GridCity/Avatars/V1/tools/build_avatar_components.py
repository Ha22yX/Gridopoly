from __future__ import annotations

import hashlib
import json
import shutil
import struct
import zlib
from pathlib import Path

from PIL import Image

from build_avatar_prototype import (
    DEVICE_OFFSETS,
    DEVICE_SIZE,
    FACE,
    HAIR,
    OUTFIT,
    REGISTRATION,
    load_layer,
    lock_face_neck_interface,
    offset_device_layer,
    register_layer,
)


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime"
TARGET = RUNTIME / "components-v1"
MANIFEST = RUNTIME / "avatar-components-v1.json"
PREVIEW_BOX = (50, 10, 270, 310)
CANVAS = (220, 300)
HEADER_SIZE = 32
KINDS = {"face": 1, "outfit": 2, "hair": 3}


def encode_pixels(raw: bytes) -> bytes:
    if len(raw) % 4:
        raise ValueError("RGBA data is not pixel aligned")
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


def registered_neutral(kind: str, preset: str, source: Path) -> Image.Image:
    image = load_layer(source)
    if kind == "face":
        image = lock_face_neck_interface(image)
    image = register_layer(image, REGISTRATION[kind][preset])
    image = image.resize(DEVICE_SIZE, Image.Resampling.LANCZOS)
    return offset_device_layer(image, DEVICE_OFFSETS[kind][preset])


def component_bytes(kind: str, preset_number: int, image: Image.Image) -> tuple[bytes, dict]:
    preview = image.crop(PREVIEW_BOX)
    alpha_box = preview.getchannel("A").getbbox()
    if alpha_box is None:
        raise ValueError(f"empty component {kind} {preset_number}")
    x0, y0, x1, y1 = alpha_box
    cropped = preview.crop(alpha_box)
    raw = cropped.tobytes()
    encoded = encode_pixels(raw)
    crc = zlib.crc32(raw) & 0xFFFFFFFF
    header = struct.pack(
        "<4sBBBBHHHHHHIII",
        b"GAVC",
        1,
        KINDS[kind],
        preset_number,
        1,
        CANVAS[0],
        CANVAS[1],
        x0,
        y0,
        x1 - x0,
        y1 - y0,
        len(raw),
        len(encoded),
        crc,
    )
    if len(header) != HEADER_SIZE:
        raise AssertionError(f"unexpected GAVC header size {len(header)}")
    payload = header + encoded
    return payload, {
        "kind": kind,
        "kindId": KINDS[kind],
        "presetId": preset_number,
        "x": x0,
        "y": y0,
        "width": x1 - x0,
        "height": y1 - y0,
        "decodedBytes": len(raw),
        "encodedBytes": len(encoded),
        "crc32": f"{crc:08x}",
        "rgbaSha256": hashlib.sha256(raw).hexdigest(),
        "fileBytes": len(payload),
        "fileSha256": hashlib.sha256(payload).hexdigest(),
    }


def sources() -> list[tuple[str, int, str, Path]]:
    output: list[tuple[str, int, str, Path]] = []
    for kind, mapping in (("face", FACE), ("outfit", OUTFIT), ("hair", HAIR)):
        for preset_number, (preset, path) in enumerate(mapping.items(), 1):
            output.append((kind, preset_number, preset, path))
    return output


def main() -> None:
    stage = RUNTIME / ".components-v1-stage"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    entries = []
    for kind, preset_number, preset, source in sources():
        image = registered_neutral(kind, preset, source)
        payload, record = component_bytes(kind, preset_number, image)
        prefix = kind[0]
        relative = Path(kind) / f"{prefix}{preset_number}.gavc"
        path = stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        record["file"] = relative.as_posix()
        record["source"] = source.relative_to(ROOT).as_posix()
        record["sourceSha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        entries.append(record)

    if len(entries) != 30:
        raise ValueError(f"expected 30 avatar components, got {len(entries)}")
    largest = {
        kind: max(entry["fileBytes"] for entry in entries if entry["kind"] == kind)
        for kind in KINDS
    }
    largest_decoded = {
        kind: max(entry["decodedBytes"] for entry in entries if entry["kind"] == kind)
        for kind in KINDS
    }
    maximum_working_set = sum(largest.values())
    maximum_decoded_set = sum(largest_decoded.values())
    rgba_canvas_bytes = CANVAS[0] * CANVAS[1] * 4
    rgb565_preview_bytes = CANVAS[0] * CANVAS[1] * 2
    straightforward_client_peak = (
        maximum_working_set
        + max(largest_decoded.values())
        + rgba_canvas_bytes
        + rgb565_preview_bytes
    )
    summary = {
        "schema": 1,
        "format": "GAVC",
        "headerBytes": HEADER_SIZE,
        "canvas": {"width": CANVAS[0], "height": CANVAS[1]},
        "previewCropFromDevice": {"x": 50, "y": 10, "width": 220, "height": 300},
        "compositionOrder": ["face", "outfit", "hair"],
        "tintAlgorithm": "gridopoly-avatar-fixed-v1",
        "entryCount": len(entries),
        "totalFileBytes": sum(entry["fileBytes"] for entry in entries),
        "largestFileBytesByKind": largest,
        "largestDecodedBytesByKind": largest_decoded,
        "maximumThreeComponentCacheBytes": maximum_working_set,
        "maximumThreeComponentDecodedBytes": maximum_decoded_set,
        "rgbaCanvasBytes": rgba_canvas_bytes,
        "rgb565PreviewBytes": rgb565_preview_bytes,
        "straightforwardClientPeakBytes": straightforward_client_peak,
        "entries": entries,
    }
    manifest_bytes = (json.dumps(summary, indent=2) + "\n").encode("utf-8")
    (stage / "manifest.json").write_bytes(manifest_bytes)
    if TARGET.exists():
        shutil.rmtree(TARGET)
    stage.replace(TARGET)
    MANIFEST.write_bytes(manifest_bytes)
    print(
        f"PASS: 30 GAVC components, {summary['totalFileBytes']} bytes, "
        f"worst three-layer cache {maximum_working_set} bytes, "
        f"manifest sha256 {hashlib.sha256(manifest_bytes).hexdigest()}"
    )


if __name__ == "__main__":
    main()
