from __future__ import annotations

import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "Assets/GridCity/Avatars/V1/runtime"
COMPONENTS = RUNTIME / "components-v1"
MANIFEST = RUNTIME / "avatar-components-v1.json"


def fail(message: str) -> None:
    raise SystemExit(f"AVATAR COMPONENT CHECK FAIL: {message}")


def decode_rle(payload: bytes, expected: int) -> bytes:
    output = bytearray()
    offset = 0
    while offset < len(payload) and len(output) < expected:
        if offset + 2 > len(payload):
            fail("truncated token")
        token = struct.unpack_from("<H", payload, offset)[0]
        offset += 2
        count = token & 0x7FFF
        if count == 0:
            fail("zero token")
        if token & 0x8000:
            if offset + 4 > len(payload):
                fail("truncated repeat pixel")
            output += payload[offset : offset + 4] * count
            offset += 4
        else:
            size = count * 4
            if offset + size > len(payload):
                fail("truncated literal")
            output += payload[offset : offset + size]
            offset += size
        if len(output) > expected:
            fail("decoded overflow")
    if offset != len(payload) or len(output) != expected:
        fail("encoded or decoded length mismatch")
    return bytes(output)


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("entryCount") != 30:
        fail("manifest schema/count")
    expected_files = {entry["file"] for entry in manifest["entries"]}
    actual_files = {
        path.relative_to(COMPONENTS).as_posix()
        for path in COMPONENTS.rglob("*.gavc")
    }
    if actual_files != expected_files:
        fail(f"file set expected={len(expected_files)} actual={len(actual_files)}")
    for entry in manifest["entries"]:
        data = (COMPONENTS / entry["file"]).read_bytes()
        if len(data) != entry["fileBytes"] or hashlib.sha256(data).hexdigest() != entry["fileSha256"]:
            fail(f"file hash {entry['file']}")
        if len(data) < 32:
            fail(f"short header {entry['file']}")
        fields = struct.unpack("<4sBBBBHHHHHHIII", data[:32])
        magic, schema, kind, preset, encoding, cw, ch, x, y, width, height, decoded, encoded, crc = fields
        if magic != b"GAVC" or schema != 1 or encoding != 1 or cw != 220 or ch != 300:
            fail(f"header constants {entry['file']}")
        if kind != entry["kindId"] or preset != entry["presetId"]:
            fail(f"header identity {entry['file']}")
        if x + width > cw or y + height > ch or decoded != width * height * 4:
            fail(f"header bounds {entry['file']}")
        if encoded != len(data) - 32:
            fail(f"encoded size {entry['file']}")
        raw = decode_rle(data[32:], decoded)
        if zlib.crc32(raw) & 0xFFFFFFFF != crc:
            fail(f"CRC {entry['file']}")
        if hashlib.sha256(raw).hexdigest() != entry["rgbaSha256"]:
            fail(f"RGBA hash {entry['file']}")
    maximum = sum(
        max(entry["fileBytes"] for entry in manifest["entries"] if entry["kind"] == kind)
        for kind in ("hair", "face", "outfit")
    )
    if maximum != manifest["maximumThreeComponentCacheBytes"] or maximum > 256 * 1024:
        fail(f"cache bound {maximum}")
    largest_decoded = {
        kind: max(entry["decodedBytes"] for entry in manifest["entries"] if entry["kind"] == kind)
        for kind in ("hair", "face", "outfit")
    }
    decoded_set = sum(largest_decoded.values())
    peak = maximum + max(largest_decoded.values()) + 220 * 300 * 4 + 220 * 300 * 2
    if manifest.get("largestDecodedBytesByKind") != largest_decoded or \
       manifest.get("maximumThreeComponentDecodedBytes") != decoded_set or \
       manifest.get("rgbaCanvasBytes") != 220 * 300 * 4 or \
       manifest.get("rgb565PreviewBytes") != 220 * 300 * 2 or \
       manifest.get("straightforwardClientPeakBytes") != peak:
        fail("transient memory bound")
    print(
        f"AVATAR COMPONENT CHECK PASS: 30 files, total={manifest['totalFileBytes']}, "
        f"maximum-three={maximum}, peak={peak}, "
        f"manifest-sha256={hashlib.sha256(MANIFEST.read_bytes()).hexdigest()}"
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, struct.error) as error:
        fail(str(error))
