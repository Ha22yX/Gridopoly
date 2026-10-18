from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from avatar_component_release import decode_rle, verify_existing, verify_runtime


def fixture(runtime: Path, large: bool = False) -> None:
    source = runtime / "components-v2"
    entries = []
    for kind_id, kind in ((1, "face"), (2, "outfit"), (3, "hair"), (4, "neck")):
        for preset in range(1, 2 if kind == "neck" else 11):
            width, height = (220, 100) if large else (4, 1)
            raw = bytes((178, 178, 178, 128, 255)) * (width * height)
            encoded = struct.pack("<H", width * height) + raw
            header = struct.pack("<4sBBBBHHHHHHIII", b"GAVC", 2, kind_id, preset, 2,
                                 220, 300, 0, 0, width, height, len(raw), len(encoded),
                                 zlib.crc32(raw) & 0xFFFFFFFF)
            data = header + encoded
            name = f"{kind}/{kind[0]}{preset}.gavc"
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            entries.append({"file": name, "kind": kind, "kindId": kind_id, "presetId": preset,
                            "x": 0, "y": 0, "width": width, "height": height,
                            "decodedBytes": len(raw), "encodedBytes": len(encoded),
                            "fileBytes": len(data), "fileSha256": hashlib.sha256(data).hexdigest(),
                            "rgbaSha256": hashlib.sha256(raw).hexdigest()})
    manifest = {"schema": 2, "entryCount": 31, "entries": entries,
                "totalFileBytes": sum(e["fileBytes"] for e in entries)}
    (runtime / "avatar-components-v2.json").write_text(json.dumps(manifest), encoding="utf-8")


def rejected(call, reason: str) -> None:
    try:
        call()
    except ValueError as error:
        assert reason in str(error), str(error)
    else:
        raise AssertionError(f"expected rejection: {reason}")


def main() -> None:
    script = Path(__file__).with_name("stage-avatar-components.py")
    with tempfile.TemporaryDirectory(prefix="gridopoly-avatar-v2-release-") as directory:
        runtime = Path(directory) / "runtime"
        fixture(runtime)
        manifest_bytes, _, files = verify_runtime(runtime, 2)
        target = Path(directory) / "components-v2"
        target.mkdir()
        args = [sys.executable, "-B", str(script), str(target), "--catalog-version", "2",
                "--runtime-root", str(runtime)]
        subprocess.run(args, check=True, capture_output=True)
        assert len(list(target.rglob("*.gavc"))) == 31
        verify_existing(target, manifest_bytes, files)
        subprocess.run(args + ["--verify-existing"], check=True, capture_output=True)
        assert subprocess.run(args, capture_output=True).returncode != 0
        staged_neck = target / "neck/n1.gavc"
        staged_neck.write_bytes(b"changed")
        rejected(lambda: verify_existing(target, manifest_bytes, files), "immutable release differs")
        assert subprocess.run(args + ["--verify-existing"], capture_output=True).returncode != 0
        assert staged_neck.read_bytes() == b"changed"

        name = runtime / "components-v2/neck/n1.gavc"
        original = name.read_bytes()
        manifest_path = runtime / "avatar-components-v2.json"
        for offset, reason in ((4, "header identity"), (7, "header identity"),
                               (20, "header bounds/length"), (38, "CRC")):
            data = bytearray(original)
            data[offset] ^= 0x01
            name.write_bytes(data)
            manifest = json.loads(manifest_bytes)
            manifest["entries"][-1]["fileSha256"] = hashlib.sha256(data).hexdigest()
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            rejected(lambda: verify_runtime(runtime, 2), reason)
        name.write_bytes(original)
        manifest_path.write_bytes(manifest_bytes)
        name.unlink()
        rejected(lambda: verify_runtime(runtime, 2), "source file set")
        name.write_bytes(original)
        name.write_bytes(b"x" * (128 * 1024 + 1))
        rejected(lambda: verify_runtime(runtime, 2), "component file limit/type")
        name.write_bytes(original)
        manifest = json.loads(manifest_bytes)
        manifest["entries"][-1]["file"] = "../outside.gavc"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        rejected(lambda: verify_runtime(runtime, 2), "manifest schema/count/file identities")
        fixture(runtime, large=True)
        rejected(lambda: verify_runtime(runtime, 2), "complete library cache bound")
        sample = bytes((178, 178, 178, 255, 255))
        assert decode_rle(b"\x03\x80" + sample, 15, 5) == sample * 3
        for data in (b"\x00\x00", b"\x04\x80" + sample, b"\x03\x80" + sample[:-1]):
            rejected(lambda data=data: decode_rle(data, 15, 5),
                     "truncated" if len(data) == 6 else "zero token or decoded overflow")
    print("AVATAR V2 RELEASE TEST PASS: immutable staging, schema/mask CRC, bounds and 31-file budget")


if __name__ == "__main__":
    main()
