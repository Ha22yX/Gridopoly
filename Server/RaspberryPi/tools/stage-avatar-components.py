from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from avatar_component_release import runtime_path, verify_existing, verify_runtime


def main() -> None:
    parser = argparse.ArgumentParser(description="Stage or verify one immutable avatar release.")
    parser.add_argument("target", type=Path)
    parser.add_argument("--catalog-version", type=int, choices=(1, 2), default=1)
    parser.add_argument("--runtime-root")
    parser.add_argument("--verify-existing", action="store_true")
    args = parser.parse_args()
    target = args.target.absolute()
    if not args.verify_existing and (target.is_symlink() or not target.is_dir() or any(target.iterdir())):
        raise ValueError("target must be an existing empty directory")
    manifest_bytes, _, files = verify_runtime(runtime_path(args.catalog_version, args.runtime_root),
                                             args.catalog_version)
    if not args.verify_existing:
        for name, data in files.items():
            destination = target / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (target / "manifest.json").write_bytes(manifest_bytes)
    verify_existing(target, manifest_bytes, files)
    print(f"AVATAR COMPONENT STAGE PASS: catalog={args.catalog_version} "
          f"gavc={len(files)} files={len(files) + 1} "
          f"manifest-sha256={hashlib.sha256(manifest_bytes).hexdigest()}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"AVATAR COMPONENT STAGE FAIL: {error}") from error
