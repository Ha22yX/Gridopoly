from __future__ import annotations

import argparse
import hashlib

from avatar_component_release import runtime_path, verify_runtime


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog-version", type=int, choices=(1, 2), default=1)
    parser.add_argument("--runtime-root")
    args = parser.parse_args()
    data, manifest, files = verify_runtime(runtime_path(args.catalog_version, args.runtime_root),
                                          args.catalog_version)
    print(f"AVATAR COMPONENT CHECK PASS: catalog={args.catalog_version} files={len(files)} "
          f"total={manifest['totalFileBytes']} manifest-sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"AVATAR COMPONENT CHECK FAIL: {error}") from error
