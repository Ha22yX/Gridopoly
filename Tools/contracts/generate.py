"""Generate reproducible contract artifacts and verify them without rewriting files."""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from Tools.contracts.canonical_json import canonical_bytes
from Tools.contracts.contract_model import load_json_value, validate_contract


_GENERATED_ROOT = Path("GameData/generated")
_SCHEMA_ROOT = Path("GameData/schemas")
_META_SCHEMA = "contract-meta-v1.json"


def generate_artifacts(root: Path, output_root: Path) -> None:
    """Validate each contract schema and emit its canonical JSON artifact."""

    schema_root = root / _SCHEMA_ROOT
    if not schema_root.exists():
        return
    for schema_path in sorted(schema_root.glob("*-v1.json"), key=lambda path: path.as_posix()):
        if schema_path.name == _META_SCHEMA:
            continue
        value = load_json_value(schema_path)
        validate_contract(value)
        target = output_root / "contracts" / schema_path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(canonical_bytes(value))


def check_artifacts(root: Path) -> list[Path]:
    """Return generated-relative paths whose tracked bytes differ from fresh output."""

    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary_root = Path(temporary_directory)
        generate_artifacts(root, temporary_root)
        actual_root = root / _GENERATED_ROOT
        return _different_paths(actual_root, temporary_root)


def _different_paths(actual_root: Path, expected_root: Path) -> list[Path]:
    actual_files = _relative_files(actual_root)
    expected_files = _relative_files(expected_root)
    paths = sorted(actual_files | expected_files, key=lambda path: path.as_posix())
    return [
        path
        for path in paths
        if path not in actual_files
        or path not in expected_files
        or (actual_root / path).read_bytes() != (expected_root / path).read_bytes()
    ]


def _relative_files(root: Path) -> set[Path]:
    if not root.exists():
        return set()
    return {path.relative_to(root) for path in root.rglob("*") if path.is_file()}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args(argv)
    root = arguments.root.resolve()

    if arguments.check:
        differences = check_artifacts(root)
        domain_differences = _check_domain_event_artifacts(root)
        for path in differences:
            print(path.as_posix())
        for path in domain_differences:
            print(path.as_posix())
        return 1 if differences or domain_differences else 0

    generate_artifacts(root, root / _GENERATED_ROOT)
    _generate_domain_event_artifacts(root)
    return 0


def _domain_event_outputs(root: Path) -> dict[str, bytes]:
    schema_path = root / _SCHEMA_ROOT / "domain-events-v1.json"
    if not schema_path.exists():
        return {}
    from Tools.contracts.domain_events import generated_outputs

    return generated_outputs(load_json_value(schema_path))


def _check_domain_event_artifacts(root: Path) -> list[Path]:
    return [
        Path(relative)
        for relative, expected in _domain_event_outputs(root).items()
        if not (root / relative).is_file() or (root / relative).read_bytes() != expected
    ]


def _generate_domain_event_artifacts(root: Path) -> None:
    for relative, data in _domain_event_outputs(root).items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


if __name__ == "__main__":
    raise SystemExit(main())
