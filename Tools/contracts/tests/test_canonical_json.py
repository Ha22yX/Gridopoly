from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from Tools.contracts.canonical_json import canonical_bytes


class CanonicalJsonTests(unittest.TestCase):
    def test_canonical_bytes_follow_the_design_record_golden_vector(self) -> None:
        value = {
            "name": "\u57ce\u5e02\"A\\B",
            "note": "cafe\u0301\n",
            "value": -2,
        }

        self.assertEqual(
            canonical_bytes(value),
            bytes.fromhex(
                "7b226e616d65223a22e59f8ee5b8825c22415c5c42222c226e6f7465223a"
                "22636166c3a95c6e222c2276616c7565223a2d327d0a"
            ),
        )
        self.assertEqual(
            __import__("hashlib").sha256(canonical_bytes(value)).hexdigest(),
            "5ccf52d2720ec7f78aa71ed7b7490d9664ef85445a9ea5e7458518a4539042a0",
        )

    def test_normalizes_strings_sorts_nfc_utf8_keys_and_preserves_arrays(self) -> None:
        decomposed = {"z": ["b", "a"], "cafe\u0301": "e\u0301"}
        precomposed = {"z": ["b", "a"], "caf\u00e9": "\u00e9"}

        self.assertEqual(canonical_bytes(decomposed), canonical_bytes(precomposed))
        self.assertEqual(canonical_bytes(precomposed), b'{"caf\xc3\xa9":"\xc3\xa9","z":["b","a"]}\n')

    def test_rejects_invalid_canonical_json_values(self) -> None:
        cases = [
            {"e\u0301": 1, "\u00e9": 2},
            {"value": 1.5},
            {"value": float("nan")},
            {"value": "\ud800"},
        ]

        for value in cases:
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                canonical_bytes(value)

    def test_generator_check_reports_drift_without_rewriting_outputs(self) -> None:
        repository_root = Path(__file__).resolve().parents[3]
        generator = repository_root / "Tools" / "contracts" / "generate.py"
        contract = {
            "contract_schema": 1,
            "contract_name": "example-contract",
            "contract_version": 1,
            "namespace": "gridopoly.example",
            "definitions": [
                {
                    "id": 1,
                    "name": "example_record",
                    "fields": [
                        {
                            "id": 1,
                            "name": "seat_id",
                            "type": "U8",
                            "cardinality": "ONE",
                            "required": True,
                            "range": {"min": 0, "max": 255},
                            "default": 0,
                        }
                    ],
                }
            ],
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            schema_dir = root / "GameData" / "schemas"
            generated_dir = root / "GameData" / "generated" / "contracts"
            schema_dir.mkdir(parents=True)
            generated_dir.mkdir(parents=True)
            (schema_dir / "example-v1.json").write_text(json.dumps(contract), encoding="utf-8")
            stale = generated_dir / "stale.json"
            stale.write_bytes(b"must remain unchanged")

            result = subprocess.run(
                [sys.executable, str(generator), "--root", str(root), "--check"],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "contracts/example-v1.json\ncontracts/stale.json\n")
            self.assertEqual(stale.read_bytes(), b"must remain unchanged")

    def test_generator_check_reports_same_path_byte_drift_without_rewriting(self) -> None:
        repository_root = Path(__file__).resolve().parents[3]
        generator = repository_root / "Tools" / "contracts" / "generate.py"
        contract = {
            "contract_schema": 1, "contract_name": "drift-contract", "contract_version": 1,
            "namespace": "gridopoly.example",
            "definitions": [{"id": 1, "name": "root", "fields": [{
                "id": 1, "name": "value", "type": "U8", "cardinality": "ONE",
                "required": True, "range": {"min": 0, "max": 255}, "default": 0,
            }]}],
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            schema_dir = root / "GameData" / "schemas"
            schema_dir.mkdir(parents=True)
            (schema_dir / "drift-v1.json").write_text(json.dumps(contract), encoding="utf-8")
            subprocess.run([sys.executable, str(generator), "--root", str(root)], check=True)
            stale = root / "GameData" / "generated" / "contracts" / "drift-v1.json"
            stale.write_bytes(b"same-path-stale")

            result = subprocess.run(
                [sys.executable, str(generator), "--root", str(root), "--check"],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "contracts/drift-v1.json\n")
            self.assertEqual(stale.read_bytes(), b"same-path-stale")


if __name__ == "__main__":
    unittest.main()
