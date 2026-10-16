#!/usr/bin/env python3
"""Validate and atomically stage the immutable v1 L1..L5 development payload."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import tempfile
import zlib
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[3]
ASSET_ROOT = PROJECT / "Assets/GridCity/DevelopmentV1"
MANIFEST = ASSET_ROOT / "manifests/development-v1.json"
# Pin only the 330 published files, not editable report/status metadata or L0.
# Different bytes require a new URL version and matching client catalog.
V1_PAYLOAD_SHA256 = "5a546ea2b2b9ad6256d243d076bf6efd94b3d969bff99ac9543c753f250dd458"
SAFE_KEY = re.compile(r"[a-z][a-z0-9-]+\Z")
FORMATS = (("tile", "tile_rgb565", "rgb565", 51200),
           ("player", "player_rgb565", "rgb565", 32768),
           ("web", "tile_png", "png", None))


def validate_png(data: bytes) -> None:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    offset = 8
    first = True
    has_idat = False
    while offset + 12 <= len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 8 + size
        if end + 4 > len(data):
            raise ValueError("truncated PNG chunk")
        if zlib.crc32(data[offset + 4:end]) != struct.unpack_from(">I", data, end)[0]:
            raise ValueError("invalid PNG chunk CRC")
        if first and (kind != b"IHDR" or size != 13 or
                      struct.unpack_from(">II", data, offset + 8) != (160, 160)):
            raise ValueError("expected 160x160 PNG")
        first = False
        has_idat |= kind == b"IDAT"
        offset = end + 4
        if kind == b"IEND":
            if size or offset != len(data) or not has_idat:
                raise ValueError("invalid PNG end")
            return
    raise ValueError("incomplete PNG")


def payload(asset_root: Path = ASSET_ROOT, manifest_path: Path = MANIFEST) -> dict[str, bytes]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1 or manifest.get("catalog_id") != "grid-city-development-v1":
        raise ValueError("unsupported development catalog")
    rows = []
    seen = set()
    keys = set()
    tile_ids = {}
    for asset in manifest.get("assets", []):
        key, level = asset.get("key", ""), asset.get("level")
        if not SAFE_KEY.fullmatch(key) or type(level) is not int or not 0 <= level <= 5:
            raise ValueError("invalid key or level")
        if (key, level) in seen:
            raise ValueError("duplicate key/level")
        seen.add((key, level))
        keys.add(key)
        tile = asset.get("tile_id")
        if not isinstance(tile, str) or not re.fullmatch(r"[A-Z][1-4]", tile):
            raise ValueError("invalid tile_id")
        if key in tile_ids and tile_ids[key] != tile:
            raise ValueError("inconsistent tile_id")
        tile_ids[key] = tile
        if level == 0:
            continue
        for kind, field, extension, size in FORMATS:
            file = asset["files"][field]
            if type(file.get("bytes")) is not int or file["bytes"] <= 0:
                raise ValueError("invalid file length")
            if size is not None and file["bytes"] != size:
                raise ValueError("invalid RGB565 length")
            rows.append([f"{kind}/{key}-l{level}.{extension}", file["path"], file["bytes"], file["sha256"]])
    if len(keys) != 22 or len(set(tile_ids.values())) != 22 or seen != {(key, level) for key in keys for level in range(6)}:
        raise ValueError("expected exactly 22 properties x 6 levels")
    fingerprint = hashlib.sha256(json.dumps(sorted(rows), separators=(",", ":")).encode()).hexdigest()
    if fingerprint != V1_PAYLOAD_SHA256:
        raise ValueError("immutable v1 payload changed; publish a new catalog/URL version")
    result = {}
    root = asset_root.resolve()
    for target, relative, size, digest in rows:
        source = root / relative
        resolved = source.resolve()
        if root not in resolved.parents or source.is_symlink() or not source.is_file():
            raise ValueError(f"invalid source path: {relative}")
        data = source.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"source length/SHA mismatch: {relative}")
        if target.startswith("web/"):
            validate_png(data)
        result[target] = data
    return result


def verify_existing(output: Path, expected: dict[str, bytes]) -> None:
    if output.is_symlink() or not output.is_dir():
        raise ValueError("existing v1 is not a real directory")
    paths = list(output.rglob("*"))
    if any(path.is_symlink() or not (path.is_file() or path.is_dir()) for path in paths):
        raise ValueError("unexpected link or non-regular entry in v1")
    actual = {path.relative_to(output).as_posix() for path in paths if path.is_file()}
    directories = {path.relative_to(output).as_posix() for path in paths if path.is_dir()}
    if actual != set(expected) or directories != {"tile", "player", "web"}:
        raise ValueError("existing immutable v1 file set differs")
    for relative, data in expected.items():
        if (output / relative).read_bytes() != data:
            raise ValueError(f"refusing to overwrite immutable v1: {relative}")


def stage(output: Path, expected: dict[str, bytes]) -> None:
    if output.is_symlink():
        raise ValueError("refusing symlink destination")
    output = output.absolute()
    if output.exists():
        verify_existing(output, expected)
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".development-v1-", dir=output.parent))
    try:
        temporary.chmod(0o755)
        for relative, data in expected.items():
            target = temporary / relative
            target.parent.mkdir(mode=0o755, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o644)
        verify_existing(temporary, expected)
        # Rename the complete directory only; never expose a partial live set.
        temporary.rename(output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="the development/v1 directory")
    parser.add_argument("--verify-existing", action="store_true")
    args = parser.parse_args()
    try:
        expected = payload()
        if args.verify_existing:
            verify_existing(args.output, expected)
        else:
            stage(args.output, expected)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f"GRIDOPOLY_DEVELOPMENT_STAGING_FAIL: {exc}\n")
    print("GRIDOPOLY_DEVELOPMENT_STAGING_PASS version=v1 levels=1..5 tile=110 player=110 web=110 files=330")


if __name__ == "__main__":
    main()
