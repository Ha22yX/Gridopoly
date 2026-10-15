#!/usr/bin/env python3
"""Validate and flash an explicit Gridopoly device inventory; default is dry-run.

Release manifests contain only relative files with SHA-256 hashes. Device roles
come from the inventory, never the COM number or discovery order. No erase-all,
network provisioning, or live game mutation is performed by this tool.
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from flash_layout import FLASH_BYTES, validate_images

PROFILES = {"tile-long", "tile-corner", "player"}
FULL_SET = Counter({"tile-long": 28, "tile-corner": 4, "player": 4})
BOARD_SIZES = (16, 32)
RESET_MODES = ("hard-reset", "watchdog-reset")
WORKER_TIMEOUT = 240
RESULT_PREFIX = "GRIDOPOLY_RESULT "


def inventory_counts(board_size: int = 32, player_count: int = 4) -> Counter:
    """The release fixes the physical board size; old releases retain 32 + 4."""
    if type(board_size) is not int or board_size not in BOARD_SIZES:
        raise ValueError("boardSize must be 16 or 32")
    if type(player_count) is not int or not 1 <= player_count <= 4:
        raise ValueError("playerCount must be an integer from 1 to 4")
    return Counter({"tile-long": board_size - 4, "tile-corner": 4, "player": player_count})


def mac_address(value: str) -> str:
    compact = value.replace(":", "").replace("-", "").lower()
    if not re.fullmatch(r"[0-9a-f]{12}", compact) or compact in {"0" * 12, "f" * 12}:
        raise ValueError("a complete factory MAC is required")
    return ":".join(compact[i:i + 2] for i in range(0, 12, 2))


def sha256(path: Path) -> str:
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def load_profile_images(profile: dict) -> list[tuple[int, bytes]]:
    images = []
    for segment in profile["segments"]:
        content = Path(segment["resolvedPath"]).read_bytes()
        if hashlib.sha256(content).hexdigest() != segment["sha256"].lower():
            raise ValueError("release binary SHA-256 changed after validation")
        images.append((segment["offset"], content))
    validate_images(images)
    return images


def load_release(manifest: Path) -> dict:
    root = manifest.resolve().parent
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1 or not isinstance(data.get("profiles"), dict):
        raise ValueError("unsupported release manifest")
    inventory_counts(data.get("boardSize", 32), data.get("playerCount", 4))
    if set(data["profiles"]) != PROFILES:
        raise ValueError("release must include tile-long, tile-corner and player profiles")
    for name, profile in data["profiles"].items():
        if profile.get("profile") != name or profile.get("chip") != "esp32s3":
            raise ValueError(f"{name}: inconsistent profile or unsupported chip")
        if profile.get("normalFirmware") is not True:
            raise ValueError(f"{name}: an explicitly marked normal firmware is required")
        if profile.get("flashSize") != "16MB":
            raise ValueError(f"{name}: expected the validated 16MB flash configuration")
        if profile.get("flashMode") not in {"dio", "dout", "qio", "qout", "keep"}:
            raise ValueError(f"{name}: invalid flash mode")
        if profile.get("flashFrequency") not in {"40m", "80m", "keep"}:
            raise ValueError(f"{name}: invalid flash frequency")
        if not re.fullmatch(r"[0-9a-fA-F]{40}", profile.get("gitRevision", "")):
            raise ValueError(f"{name}: full source Git revision is required")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", profile.get("sourceDigest", "")):
            raise ValueError(f"{name}: source manifest digest is required")
        source_relative = Path(profile["sourceManifest"])
        source_manifest = (root / source_relative).resolve()
        if source_relative.is_absolute() or not source_manifest.is_relative_to(root):
            raise ValueError(f"{name}: source manifest escapes the release directory")
        if sha256(source_manifest) != profile["sourceDigest"].lower():
            raise ValueError(f"{name}: source manifest SHA-256 mismatch")
        segments = profile.get("segments")
        if not isinstance(segments, list) or not segments:
            raise ValueError(f"{name}: missing flash segments")
        ranges = []
        for segment in segments:
            relative = Path(segment["path"])
            path = (root / relative).resolve()
            if relative.is_absolute() or not path.is_relative_to(root):
                raise ValueError(f"{name}: segment escapes the release directory")
            digest = segment.get("sha256", "")
            if not re.fullmatch(r"[0-9a-fA-F]{64}", digest) or sha256(path) != digest.lower():
                raise ValueError(f"{name}: SHA-256 mismatch for {relative}")
            offset = segment.get("offset")
            size = path.stat().st_size
            if type(offset) is not int or offset < 0 or offset % 4096 or size == 0:
                raise ValueError(f"{name}: invalid offset or empty segment")
            # Flash erases complete 4KiB sectors even for a shorter binary.
            end = offset + (size + 4095) // 4096 * 4096
            if end > FLASH_BYTES or any(offset < b and a < end for a, b in ranges):
                raise ValueError(f"{name}: overlapping or oversized flash segments")
            ranges.append((offset, end))
            segment["resolvedPath"] = str(path)
        load_profile_images(profile)
    return data


def load_inventory(path: Path, *, partial: bool = False,
                   board_size: int = 32, player_count: int = 4) -> list[dict]:
    expected = inventory_counts(board_size, player_count)
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if not {"label", "port", "mac", "profile"}.issubset(reader.fieldnames or []):
            raise ValueError("inventory columns must include label, port, mac, profile")
        devices = [{key: (value or "").strip() for key, value in row.items()}
                   for row in reader]
    ports, macs, labels = set(), set(), set()
    for device in devices:
        profile, port, label = device["profile"], device["port"], device["label"]
        device["mac"] = mac_address(device["mac"])
        if profile not in PROFILES or not port or port.startswith("-") or not label:
            raise ValueError("each device needs a label, explicit port and supported profile")
        if port.casefold() in ports or device["mac"] in macs or label in labels:
            raise ValueError("duplicate port, MAC or device label in inventory")
        ports.add(port.casefold())
        macs.add(device["mac"])
        labels.add(label)
    counts = Counter(device["profile"] for device in devices)
    description = f"{expected['tile-long']} long + 4 corner + {player_count} player"
    if not devices or any(counts[p] > expected[p] for p in counts):
        raise ValueError(f"inventory is empty or exceeds the release set ({description})")
    if not partial and counts != expected:
        raise ValueError(f"full inventory must be {description}; use --partial for a batch")
    return devices


def run_worker(python: str, job: dict, *, timeout: int = WORKER_TIMEOUT) -> dict:
    command = [python, "-B", str(Path(__file__).with_name("flash_worker.py"))]
    result = subprocess.run(command, input=json.dumps(job), capture_output=True,
                            text=True, encoding="utf-8", errors="replace",
                            timeout=timeout, check=False)
    output = result.stdout + result.stderr
    if result.returncode:
        raise RuntimeError(f"flash worker exited {result.returncode}: {output[-3000:]}")
    reports = [line[len(RESULT_PREFIX):] for line in result.stdout.splitlines()
               if line.startswith(RESULT_PREFIX)]
    if len(reports) != 1:
        raise ValueError("flash worker did not return one explicit result")
    report = json.loads(reports[0])
    report["output"] = output[-3000:]
    return report


def flash_batch(release: dict, devices: list[dict], python: str, log: Path,
                runner=run_worker, *, after: str = "hard-reset") -> None:
    """Fail the whole preflight before any write; stop on first write failure."""
    if after not in RESET_MODES:
        raise ValueError("unsupported post-flash reset mode")
    log.parent.mkdir(parents=True, exist_ok=True)

    def event(device, status, **details):
        with log.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps({"at": datetime.now(timezone.utc).isoformat(),
                                     **device, "status": status, "requestedAfter": after, **details}) + "\n")

    def operate(device, operation):
        report = runner(python, {"operation": operation, "device": device,
                                "after": after,
                                "profile": release["profiles"][device["profile"]]})
        status = "verified" if operation == "flash" else "preflight_ok"
        reset_mode = after if operation == "flash" else "hard-reset"
        if (report.get("status") != status or report.get("mac") != device["mac"] or
                report.get("profile") != device["profile"] or
                report.get("esptoolVersion") != "5.3.1" or report.get("resetMode") != reset_mode):
            raise ValueError("flash worker result identity/status mismatch")
        return report

    for device in devices:
        try:
            report = operate(device, "preflight")
            event(device, "preflight_ok", resetMode=report["resetMode"], output=report.get("output", ""))
        except Exception as error:
            event(device, "preflight_failed", error=str(error))
            raise
    for device in devices:
        profile = release["profiles"][device["profile"]]
        try:
            report = operate(device, "flash")
            event(device, "verified", gitRevision=profile["gitRevision"],
                  sourceDigest=profile["sourceDigest"],
                  segments=[{k: s[k] for k in ("offset", "sha256")} for s in profile["segments"]],
                  esptoolVersion=report["esptoolVersion"], resetMode=report["resetMode"],
                  output=report.get("output", ""))
            print(f"VERIFIED {device['label']} {device['mac']} {device['profile']}")
        except Exception as error:
            event(device, "failed", error=str(error))
            raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("--partial", action="store_true", help="flash a smaller explicit batch")
    parser.add_argument("--execute", action="store_true", help="actually identify/write/verify hardware")
    parser.add_argument("--python", default=sys.executable, help="Python with esptool==5.3.1 installed")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--after", choices=RESET_MODES, default="hard-reset",
                        help="watchdog-reset: S3 USB-Serial/JTAG boards with BOOT released")
    args = parser.parse_args()
    try:
        release = load_release(args.manifest)
        devices = load_inventory(args.inventory, partial=args.partial,
                                 board_size=release.get("boardSize", 32),
                                 player_count=release.get("playerCount", 4))
        for device in devices:
            print(f"PLAN {device['label']} {device['port']} {device['mac']} {device['profile']} after={args.after}")
        if not args.execute:
            print("DRY RUN: hashes and inventory checked; no serial port opened.")
            return 0
        if args.log is None:
            raise ValueError("--log is required for execution")
        flash_batch(release, devices, args.python, args.log, after=args.after)
        return 0
    except (ValueError, OSError, KeyError, TypeError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
