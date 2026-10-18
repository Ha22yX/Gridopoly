"""Validate immutable avatar catalogs before staging or installing them."""
from __future__ import annotations

import hashlib
import json
import struct
import zlib
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
MAX_COMPONENT_BYTES = 128 * 1024
MAX_LIBRARY_BYTES = 2304 * 1024 - 2 * 220 * 300 * 2


def runtime_path(version: int, override: str | None) -> Path:
    return Path(override).resolve() if override else (
        REPOSITORY / f"Assets/GridCity/Avatars/V{version}/runtime"
    )


def decode_rle(payload: bytes, expected: int, sample_bytes: int) -> bytes:
    output = bytearray()
    offset = 0
    while offset < len(payload) and len(output) < expected:
        if offset + 2 > len(payload):
            raise ValueError("truncated token")
        token = struct.unpack_from("<H", payload, offset)[0]
        offset += 2
        count = token & 0x7FFF
        if not count or count > (expected - len(output)) // sample_bytes:
            raise ValueError("zero token or decoded overflow")
        size = sample_bytes if token & 0x8000 else count * sample_bytes
        if offset + size > len(payload):
            raise ValueError("truncated sample")
        data = payload[offset:offset + size]
        output.extend(data * count if token & 0x8000 else data)
        offset += size
    if offset != len(payload) or len(output) != expected:
        raise ValueError("encoded or decoded length mismatch")
    return bytes(output)


def verify_runtime(runtime: Path, version: int) -> tuple[bytes, dict, dict[str, bytes]]:
    if version not in (1, 2):
        raise ValueError("unsupported catalog version")
    manifest_bytes = (runtime / f"avatar-components-v{version}.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    source = runtime / f"components-v{version}"
    expected = {
        f"{kind}/{kind[0]}{preset}.gavc": (kind_id, preset)
        for kind_id, kind in ((1, "face"), (2, "outfit"), (3, "hair"))
        for preset in range(1, 11)
    }
    if version == 2:
        expected["neck/n1.gavc"] = (4, 1)
    entries = manifest.get("entries", [])
    if (manifest.get("schema") != version or manifest.get("entryCount") != len(expected)
            or len(entries) != len(expected)
            or {entry["file"] for entry in entries} != set(expected)):
        raise ValueError("manifest schema/count/file identities")
    if {p.relative_to(source).as_posix() for p in source.rglob("*.gavc")} != set(expected):
        raise ValueError("source file set")
    embedded = source / "manifest.json"
    if embedded.exists() and embedded.read_bytes() != manifest_bytes:
        raise ValueError("embedded manifest differs from release manifest")
    samples = 5 if version == 2 else 4
    files: dict[str, bytes] = {}
    for entry in entries:
        name = entry["file"]
        path = source / name
        if path.is_symlink() or not 32 < path.stat().st_size <= MAX_COMPONENT_BYTES:
            raise ValueError(f"component file limit/type {name}")
        data = path.read_bytes()
        if len(data) != entry["fileBytes"] or hashlib.sha256(data).hexdigest() != entry["fileSha256"]:
            raise ValueError(f"file hash {name}")
        fields = struct.unpack("<4sBBBBHHHHHHIII", data[:32])
        magic, schema, kind, preset, encoding, cw, ch, x, y, width, height, decoded, encoded, crc = fields
        if (magic != b"GAVC" or schema != version or encoding != version or (cw, ch) != (220, 300)
                or (kind, preset) != expected[name]
                or (kind, preset) != (entry["kindId"], entry["presetId"])):
            raise ValueError(f"header identity {name}")
        if (not width or not height or x + width > cw or y + height > ch
                or decoded != width * height * samples or encoded != len(data) - 32):
            raise ValueError(f"header bounds/length {name}")
        for key, actual in (("x", x), ("y", y), ("width", width), ("height", height),
                            ("decodedBytes", decoded), ("encodedBytes", encoded)):
            if entry.get(key) != actual:
                raise ValueError(f"manifest geometry {name}: {key}")
        raw = decode_rle(data[32:], decoded, samples)
        if zlib.crc32(raw) & 0xFFFFFFFF != crc:
            raise ValueError(f"CRC {name}")
        if hashlib.sha256(raw).hexdigest() != entry["rgbaSha256"]:
            raise ValueError(f"sample hash {name}")
        files[name] = data
    total = sum(map(len, files.values()))
    if total != manifest.get("totalFileBytes") or total > MAX_LIBRARY_BYTES:
        raise ValueError(f"complete library cache bound {total}")
    if version == 1:
        maximum = sum(max(e["fileBytes"] for e in entries if e["kind"] == kind)
                      for kind in ("hair", "face", "outfit"))
        decoded_by_kind = {kind: max(e["decodedBytes"] for e in entries if e["kind"] == kind)
                           for kind in ("hair", "face", "outfit")}
        peak = maximum + max(decoded_by_kind.values()) + 220 * 300 * 6
        if (maximum != manifest.get("maximumThreeComponentCacheBytes") or maximum > 256 * 1024
                or decoded_by_kind != manifest.get("largestDecodedBytesByKind")
                or sum(decoded_by_kind.values()) != manifest.get("maximumThreeComponentDecodedBytes")
                or manifest.get("rgbaCanvasBytes") != 220 * 300 * 4
                or manifest.get("rgb565PreviewBytes") != 220 * 300 * 2
                or manifest.get("straightforwardClientPeakBytes") != peak):
            raise ValueError("legacy cache/transient memory bound")
    return manifest_bytes, manifest, files


def verify_existing(target: Path, manifest_bytes: bytes, files: dict[str, bytes]) -> None:
    expected = set(files) | {"manifest.json"}
    actual = {p.relative_to(target).as_posix() for p in target.rglob("*") if p.is_file()}
    if target.is_symlink() or actual != expected:
        raise ValueError("existing release file set/type differs")
    for name, data in {**files, "manifest.json": manifest_bytes}.items():
        path = target / name
        if path.is_symlink() or path.read_bytes() != data:
            raise ValueError(f"immutable release differs: {name}")
