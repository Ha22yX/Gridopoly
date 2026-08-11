from __future__ import annotations

import hashlib
import json
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Assets/GridCity/Avatars/V1/runtime/components-v1"
MANIFEST = ROOT / "Assets/GridCity/Avatars/V1/runtime/avatar-components-v1.json"


def fail(message: str) -> None:
    raise SystemExit(f"AVATAR COMPONENT STAGE FAIL: {message}")


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: stage-avatar-components.py <empty-target-directory>")
    target = Path(sys.argv[1]).resolve()
    if not target.is_dir() or any(target.iterdir()):
        fail("target must be an existing empty directory")
    manifest_bytes = MANIFEST.read_bytes()
    manifest = json.loads(manifest_bytes)
    entries = manifest.get("entries", [])
    if manifest.get("schema") != 1 or len(entries) != 30:
        fail("manifest schema/count")
    expected = {entry["file"] for entry in entries}
    actual = {path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.gavc")}
    if actual != expected:
        fail("source file set")
    for entry in entries:
        source = SOURCE / entry["file"]
        data = source.read_bytes()
        if len(data) != entry["fileBytes"] or hashlib.sha256(data).hexdigest() != entry["fileSha256"]:
            fail(f"source hash {entry['file']}")
        destination = target / entry["file"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    (target / "manifest.json").write_bytes(manifest_bytes)
    staged = {path.relative_to(target).as_posix() for path in target.rglob("*.gavc")}
    all_files = [path for path in target.rglob("*") if path.is_file()]
    if staged != expected or len(all_files) != 31:
        fail("staged file set")
    print(
        f"AVATAR COMPONENT STAGE PASS: gavc=30 files=31 "
        f"manifest-sha256={hashlib.sha256(manifest_bytes).hexdigest()}"
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        fail(str(error))
