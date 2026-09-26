#!/usr/bin/env python3
"""Collect the three verified build candidate manifests into a portable local release."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil

from batch_flash import FULL_SET, PROFILES, load_release
from flash_layout import validate_images


def prepare(candidates: list[Path], destination: Path) -> Path:
    profiles = {}
    buffers = {}
    for candidate in candidates:
        value = json.loads(candidate.read_text(encoding="utf-8-sig"))
        name = value.get("profile")
        if value.get("schemaVersion") != 1 or name not in PROFILES or name in profiles:
            raise ValueError("provide exactly one schemaVersion 1 manifest for each profile")
        if value.get("normalFirmware") is not True:
            raise ValueError(f"{name}: an explicitly marked normal firmware is required")
        source_manifest = Path(value["sourceManifest"])
        if not source_manifest.is_absolute():
            source_manifest = candidate.resolve().parent / source_manifest
        source_bytes = source_manifest.read_bytes()
        if hashlib.sha256(source_bytes).hexdigest() != value["sourceDigest"].lower():
            raise ValueError(f"{name}: source manifest SHA mismatch")
        json.loads(source_bytes)  # Copy the hash manifest, never its referenced source files.
        images = []
        for segment in value["segments"]:
            source = Path(segment["path"])
            if not source.is_absolute():
                source = candidate.resolve().parent / source
            content = source.read_bytes()
            if hashlib.sha256(content).hexdigest() != segment["sha256"].lower():
                raise ValueError(f"{name}: candidate binary SHA mismatch")
            images.append((segment["offset"], content))
        validate_images(images)
        profiles[name] = value
        buffers[name] = (source_bytes, dict(images))
    if set(profiles) != PROFILES:
        raise ValueError("tile-long, tile-corner and player candidates are all required")
    destination.mkdir(parents=True, exist_ok=False)
    for name, profile in profiles.items():
        (destination / name).mkdir()
        source_bytes, images = buffers[name]
        source_relative = Path(name) / "source-manifest.json"
        (destination / source_relative).write_bytes(source_bytes)
        profile["sourceManifest"] = source_relative.as_posix()
        for segment in profile["segments"]:
            relative = Path(name) / f"{segment['offset']:08x}.bin"
            (destination / relative).write_bytes(images[segment["offset"]])
            segment["path"] = relative.as_posix()
    manifest = destination / "release.json"
    manifest.write_text(json.dumps({"schemaVersion": 1, "profiles": profiles}, indent=2) + "\n",
                        encoding="utf-8")
    load_release(manifest)
    with (destination / "inventory-template.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["label", "port", "mac", "profile"])
        for name, count in FULL_SET.items():
            for number in range(1, count + 1):
                writer.writerow([f"{name}-{number:02}", "", "", name])
    for filename in ("batch_flash.py", "flash_worker.py", "flash_layout.py", "README.md"):
        shutil.copyfile(Path(__file__).with_name(filename), destination / filename)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(prepare(args.candidate, args.output))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
