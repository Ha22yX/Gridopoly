from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from collections.abc import Mapping
from pathlib import Path

import Tools.contracts.contract_model as contract_model
from Tools.contracts.contract_model import ContractError, WIRE_TYPES, load_contract, validate_contract


def field(
    field_id: int,
    name: str,
    wire_type: str,
    *,
    enum: list[object] | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {
        "id": field_id,
        "name": name,
        "type": wire_type,
        "cardinality": "ONE",
        "required": True,
        "default": 0,
    }
    if wire_type in {"U8", "I8", "U16", "I16", "U32", "I32", "U64", "I64"}:
        result["range"] = {"min": 0, "max": 255}
    if enum is not None:
        result["enum"] = enum
    return result


def fixture(*, fields: list[dict[str, object]] | None = None) -> dict[str, object]:
    return {
        "contract_schema": 1,
        "contract_name": "example-contract",
        "contract_version": 1,
        "namespace": "gridopoly.example",
        "definitions": [
            {
                "id": 1,
                "name": "example_record",
                "fields": fields or [field(1, "seat_id", "U8")],
            }
        ],
    }


def typed_field(wire_type: str, default: object, *, enum: list[object] | None = None) -> dict[str, object]:
    result: dict[str, object] = {
        "id": 1,
        "name": "value",
        "type": wire_type,
        "cardinality": "ONE",
        "required": True,
        "default": default,
    }
    limits = {
        "U8": (0, 255), "I8": (-128, 127), "U16": (0, 65535), "I16": (-32768, 32767),
        "U32": (0, 4294967295), "I32": (-2147483648, 2147483647),
        "U64": (0, 18446744073709551615), "I64": (-9223372036854775808, 9223372036854775807),
    }
    if wire_type in limits:
        result["range"] = {"min": limits[wire_type][0], "max": limits[wire_type][1]}
    if wire_type in {"BYTES", "UTF8", "RECORD_LIST", "U8_LIST", "U16_LIST", "U32_LIST"}:
        result["max"] = 2
    if wire_type in {"RECORD", "RECORD_LIST"}:
        result["record"] = "example_record"
    if enum is not None:
        result["enum"] = enum
    return result


class ContractMetaValidationTests(unittest.TestCase):
    def test_rejects_duplicate_field_ids(self) -> None:
        doc = fixture(fields=[field(1, "room_id", "U64"), field(1, "seat_id", "U8")])

        with self.assertRaisesRegex(ContractError, "duplicate field id 1"):
            validate_contract(doc)

    def test_rejects_implicit_enum_values(self) -> None:
        doc = fixture(fields=[field(1, "phase", "U8", enum=["LOBBY", "IN_GAME"])])

        with self.assertRaisesRegex(ContractError, "explicit enum value"):
            validate_contract(doc)

    def test_rejects_unknown_or_missing_top_level_keys(self) -> None:
        unknown = fixture()
        unknown["extra"] = True
        missing = fixture()
        del missing["namespace"]

        with self.assertRaisesRegex(ContractError, "unknown top-level key extra"):
            validate_contract(unknown)
        with self.assertRaisesRegex(ContractError, "missing top-level key namespace"):
            validate_contract(missing)

    def test_rejects_floats_anywhere_in_the_document(self) -> None:
        doc = fixture()
        doc["contract_version"] = 1.0

        with self.assertRaisesRegex(ContractError, "floats are not allowed"):
            validate_contract(doc)

    def test_rejects_duplicate_definition_names_and_unordered_definitions(self) -> None:
        duplicate = fixture()
        duplicate["definitions"].append(
            {"id": 2, "name": "example_record", "fields": [field(1, "value", "U8")]}
        )
        unordered = fixture()
        unordered["definitions"].insert(
            0, {"id": 2, "name": "later_record", "fields": [field(1, "value", "U8")]}
        )

        with self.assertRaisesRegex(ContractError, "duplicate definition name example_record"):
            validate_contract(duplicate)
        with self.assertRaisesRegex(ContractError, "definitions must be ordered by id"):
            validate_contract(unordered)

    def test_rejects_missing_type_specific_limits_and_cardinality(self) -> None:
        no_range = fixture()
        del no_range["definitions"][0]["fields"][0]["range"]
        no_max = fixture(fields=[{"id": 1, "name": "label", "type": "UTF8", "cardinality": "ONE", "required": True, "default": ""}])
        no_cardinality = fixture()
        del no_cardinality["definitions"][0]["fields"][0]["cardinality"]

        with self.assertRaisesRegex(ContractError, "requires range"):
            validate_contract(no_range)
        with self.assertRaisesRegex(ContractError, "requires max"):
            validate_contract(no_max)
        with self.assertRaisesRegex(ContractError, "requires cardinality"):
            validate_contract(no_cardinality)

    def test_rejects_defaults_outside_the_declared_contract(self) -> None:
        out_of_range = fixture()
        out_of_range["definitions"][0]["fields"][0]["default"] = 256
        invalid_enum = fixture(fields=[field(1, "phase", "U8", enum=[{"name": "LOBBY", "value": 1}])])
        invalid_enum["definitions"][0]["fields"][0]["default"] = 2

        with self.assertRaisesRegex(ContractError, "default.*outside range"):
            validate_contract(out_of_range)
        with self.assertRaisesRegex(ContractError, "default.*not present in enum"):
            validate_contract(invalid_enum)

    def test_returns_an_immutable_document(self) -> None:
        document = validate_contract(fixture())

        with self.assertRaises(AttributeError):
            document.contract_name = "changed"  # type: ignore[misc]
        with self.assertRaises(TypeError):
            document.definitions[0].fields[0].metadata["range"]["min"] = 1  # type: ignore[index]

    def test_rejects_invalid_utf8_as_a_contract_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "invalid-contract.json"
            path.write_bytes(b"\xff")

            with self.assertRaisesRegex(ContractError, "cannot load contract.*invalid-contract.json"):
                load_contract(path)

    def test_rejects_unrepresentable_enum_and_defaults_for_all_wire_types(self) -> None:
        cases = [
            ("U8", typed_field("U8", 256)),
            ("I8", typed_field("I8", 128)),
            ("U16", typed_field("U16", 65536)),
            ("I16", typed_field("I16", -32769)),
            ("U32", typed_field("U32", 4294967296)),
            ("I32", typed_field("I32", -2147483649)),
            ("U64", typed_field("U64", -1)),
            ("I64", typed_field("I64", 9223372036854775808)),
            ("BOOL", typed_field("BOOL", 1)),
            ("BYTES", typed_field("BYTES", "0")),
            ("UTF8", typed_field("UTF8", "\ud800")),
            ("RECORD", typed_field("RECORD", [])),
            ("RECORD_LIST", typed_field("RECORD_LIST", [[]])),
            ("U8_LIST", typed_field("U8_LIST", [256])),
            ("U16_LIST", typed_field("U16_LIST", [-1])),
            ("U32_LIST", typed_field("U32_LIST", [4294967296])),
        ]
        enum_outside_type = typed_field("U8", 0, enum=[{"name": "IMPOSSIBLE", "value": 256}])

        for wire_type, invalid_field in cases + [("U8 enum", enum_outside_type)]:
            with self.subTest(wire_type=wire_type), self.assertRaises(ContractError):
                validate_contract(fixture(fields=[invalid_field]))

    def test_accepts_representable_defaults_for_all_wire_types(self) -> None:
        valid_defaults = [
            ("U8", 255), ("I8", -128), ("U16", 65535), ("I16", -32768),
            ("U32", 4294967295), ("I32", -2147483648), ("U64", 18446744073709551615),
            ("I64", -9223372036854775808), ("BOOL", True), ("BYTES", "00ff"),
            ("UTF8", "hi"), ("RECORD", {}), ("RECORD_LIST", [{}]),
            ("U8_LIST", [0, 255]), ("U16_LIST", [0, 65535]), ("U32_LIST", [0, 4294967295]),
        ]

        for wire_type, default in valid_defaults:
            with self.subTest(wire_type=wire_type):
                candidate = fixture(fields=[typed_field(wire_type, default)])
                if wire_type in {"RECORD", "RECORD_LIST"}:
                    candidate["definitions"][0]["fields"][0]["record"] = "leaf"
                    candidate["definitions"].append({"id": 2, "name": "leaf", "fields": [typed_field("U8", 0)]})
                validate_contract(candidate)

    def test_validates_nested_record_default_field_values(self) -> None:
        def record_contract(default: object) -> dict[str, object]:
            return {
                "contract_schema": 1,
                "contract_name": "record-contract",
                "contract_version": 1,
                "namespace": "gridopoly.example",
                "definitions": [
                    {"id": 1, "name": "item", "fields": [typed_field("U8", 0)]},
                    {
                        "id": 2,
                        "name": "container",
                        "fields": [
                            {
                                "id": 1, "name": "item", "type": "RECORD", "record": "item",
                                "cardinality": "ONE", "required": True, "default": default,
                            }
                        ],
                    },
                ],
            }

        validate_contract(record_contract({"value": 255}))
        for invalid_default in ({"unknown": 0}, {"value": 256}):
            with self.subTest(default=invalid_default), self.assertRaises(ContractError):
                validate_contract(record_contract(invalid_default))

    def test_meta_schema_structurally_covers_the_model_grammar(self) -> None:
        schema_path = Path(__file__).resolve().parents[3] / "GameData" / "schemas" / "contract-meta-v1.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        field_schema = schema["$defs"]["field"]

        self.assertFalse(field_schema["additionalProperties"])
        self.assertEqual(
            set(field_schema["properties"]),
            {
                "id", "name", "type", "cardinality", "required", "default", "range", "max", "enum",
                "record", "stable_name", "critical", "private", "visibility", "sort", "description",
            },
        )
        self.assertEqual(set(field_schema["properties"]["type"]["enum"]), WIRE_TYPES)
        self.assertIn("allOf", field_schema)
        self.assertIn("stable_name", schema["$defs"]["definition"]["properties"])
        self.assertIn("visibility", schema["$defs"]["definition"]["properties"])

    def test_model_rejects_metadata_that_the_meta_schema_forbids(self) -> None:
        invalid_definition = fixture()
        invalid_definition["definitions"][0]["critical"] = "yes"
        invalid_field = fixture()
        invalid_field["definitions"][0]["fields"][0]["private"] = "no"

        for document in (invalid_definition, invalid_field):
            with self.subTest(document=document), self.assertRaises(ContractError):
                validate_contract(document)

    def test_file_loading_rejects_exact_and_nfc_duplicate_json_keys(self) -> None:
        base = (
            '{"contract_schema":1,"contract_name":"example","contract_version":1,'
            '"namespace":"gridopoly.example","definitions":[{"id":1,"name":"root","fields":'
            '[{"id":1,"name":"value","type":"U8","cardinality":"ONE","required":true,'
            '"range":{"min":0,"max":255},"default":0%s}]}]}'
        )
        documents = [
            base % ',"id":1',
            base % ',"e\\u0301":1,"\\u00e9":2',
        ]
        with tempfile.TemporaryDirectory() as temporary_directory:
            for index, source in enumerate(documents):
                path = Path(temporary_directory) / f"duplicate-{index}.json"
                path.write_text(source, encoding="utf-8")
                with self.subTest(path=path), self.assertRaisesRegex(ContractError, "duplicate JSON object key"):
                    load_contract(path)

    def test_generator_reuses_duplicate_aware_contract_parsing(self) -> None:
        generator = Path(__file__).resolve().parents[1] / "generate.py"
        source = (
            '{"contract_schema":1,"contract_name":"example","contract_version":1,'
            '"namespace":"one","namespace":"two","definitions":[{"id":1,"name":"root",'
            '"fields":[{"id":1,"name":"value","type":"U8","cardinality":"ONE",'
            '"required":true,"range":{"min":0,"max":255},"default":0}]}]}'
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            schema_dir = root / "GameData" / "schemas"
            schema_dir.mkdir(parents=True)
            (schema_dir / "example-v1.json").write_text(source, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(generator), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate JSON object key", result.stderr)

    def test_definition_metadata_is_retained_immutably(self) -> None:
        document = fixture()
        document["definitions"][0].update(
            {
                "stable_name": "ROOT",
                "critical": True,
                "terminal_role": "NONE",
                "visibility": "PUBLIC",
                "description": "root definition",
                "kind": "EVENT",
            }
        )

        definition = validate_contract(document).definitions[0]
        self.assertEqual(dict(definition.metadata), {
            "stable_name": "ROOT", "critical": True, "terminal_role": "NONE",
            "visibility": "PUBLIC", "description": "root definition", "kind": "EVENT",
        })
        with self.assertRaises(TypeError):
            definition.metadata["kind"] = "OTHER"  # type: ignore[index]

    def test_record_defaults_require_members_or_explicit_member_defaults(self) -> None:
        required_member = {"id": 1, "name": "value", "type": "U8", "cardinality": "ONE", "required": True, "range": {"min": 0, "max": 255}}
        defaulted_member = {**required_member, "default": 7}

        def document(member: dict[str, object], wire_type: str, default: object) -> dict[str, object]:
            root_field = {"id": 1, "name": "item", "type": wire_type, "record": "item", "cardinality": "ONE", "required": True, "default": default}
            if wire_type == "RECORD_LIST":
                root_field["max"] = 2
            return {
                "contract_schema": 1, "contract_name": "records", "contract_version": 1, "namespace": "gridopoly.example",
                "definitions": [
                    {"id": 1, "name": "item", "fields": [member]},
                    {"id": 2, "name": "root", "fields": [root_field]},
                ],
            }

        for wire_type, default in (("RECORD", {}), ("RECORD_LIST", [{}])):
            with self.subTest(wire_type=wire_type), self.assertRaisesRegex(ContractError, "missing required field value"):
                validate_contract(document(required_member, wire_type, default))
        validate_contract(document(defaulted_member, "RECORD", {}))
        validate_contract(document(defaulted_member, "RECORD_LIST", [{}]))

    def test_record_graph_rejects_cycles_and_allows_forward_references(self) -> None:
        def record_field(target: str) -> dict[str, object]:
            return {"id": 1, "name": "next", "type": "RECORD", "record": target, "cardinality": "OPTIONAL", "required": False}

        base = {"contract_schema": 1, "contract_name": "graph", "contract_version": 1, "namespace": "gridopoly.example"}
        self_cycle = {**base, "definitions": [{"id": 1, "name": "node", "fields": [record_field("node")]}]}
        mutual_cycle = {**base, "definitions": [{"id": 1, "name": "a", "fields": [record_field("b")]}, {"id": 2, "name": "b", "fields": [record_field("a")]}]}
        forward = {**base, "definitions": [{"id": 1, "name": "root", "fields": [record_field("leaf")]}, {"id": 2, "name": "leaf", "fields": [typed_field("U8", 0)]}]}

        for document in (self_cycle, mutual_cycle):
            with self.subTest(document=document), self.assertRaisesRegex(ContractError, "record reference cycle"):
                validate_contract(document)
        validate_contract(forward)

    def test_identifier_names_must_be_nfc_unique(self) -> None:
        document = fixture(fields=[field(1, "e\u0301", "U8"), field(2, "\u00e9", "U8")])

        with self.assertRaisesRegex(ContractError, "NFC"):
            validate_contract(document)

    def test_meta_schema_declares_normative_extensions_and_model_parity(self) -> None:
        schema_path = Path(__file__).resolve().parents[3] / "GameData" / "schemas" / "contract-meta-v1.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        extensions = schema["x-gridopoly-normative-extensions"]
        self.assertEqual(extensions["source_loader_entry"], "Tools.contracts.contract_model.load_json_value/load_contract")
        self.assertEqual(extensions["semantic_entry"], "Tools.contracts.contract_model.validate_contract")
        self.assertTrue({"ascending-unique-ids", "wire-range-bounds", "record-target-and-acyclic"}.issubset({rule["id"] for rule in extensions["rules"]}))
        variants = schema["$defs"]["field"]["oneOf"]
        self.assertEqual(len(variants), len(WIRE_TYPES))
        self.assertTrue(all("default" in variant["properties"] for variant in variants))
        positive = fixture()
        bool_default = fixture(fields=[typed_field("BOOL", 1)])
        out_of_wire_range = fixture()
        out_of_wire_range["definitions"][0]["fields"][0]["range"]["max"] = 256
        duplicate_definition_id = fixture()
        duplicate_definition_id["definitions"].append({"id": 1, "name": "other", "fields": [typed_field("U8", 0)]})

        validate_contract(positive)
        for invalid in (bool_default, out_of_wire_range, duplicate_definition_id):
            with self.subTest(invalid=invalid), self.assertRaises(ContractError):
                validate_contract(invalid)

    def test_every_wire_type_rejects_a_schema_forbidden_irrelevant_property(self) -> None:
        forbidden = {
            "U8": ("max", 1), "I8": ("max", 1), "U16": ("max", 1), "I16": ("max", 1),
            "U32": ("max", 1), "I32": ("max", 1), "U64": ("max", 1), "I64": ("max", 1),
            "BOOL": ("range", {"min": 0, "max": 1}), "BYTES": ("range", {"min": 0, "max": 1}),
            "UTF8": ("range", {"min": 0, "max": 1}), "RECORD": ("max", 1),
            "RECORD_LIST": ("range", {"min": 0, "max": 1}),
            "U8_LIST": ("range", {"min": 0, "max": 1}),
            "U16_LIST": ("range", {"min": 0, "max": 1}),
            "U32_LIST": ("range", {"min": 0, "max": 1}),
        }
        defaults = {
            "U8": 0, "I8": 0, "U16": 0, "I16": 0, "U32": 0, "I32": 0, "U64": 0, "I64": 0,
            "BOOL": True, "BYTES": "00", "UTF8": "ok", "RECORD": {}, "RECORD_LIST": [{}],
            "U8_LIST": [0], "U16_LIST": [0], "U32_LIST": [0],
        }
        for wire_type, (property_name, property_value) in forbidden.items():
            candidate = fixture(fields=[typed_field(wire_type, defaults[wire_type])])
            if wire_type in {"RECORD", "RECORD_LIST"}:
                candidate["definitions"][0]["fields"][0]["record"] = "leaf"
                candidate["definitions"].append({"id": 2, "name": "leaf", "fields": [typed_field("U8", 0)]})
            candidate["definitions"][0]["fields"][0][property_name] = property_value
            with self.subTest(wire_type=wire_type, property=property_name), self.assertRaisesRegex(ContractError, "irrelevant"):
                validate_contract(candidate)

    def test_normative_extension_registry_matches_schema_and_code(self) -> None:
        schema_path = Path(__file__).resolve().parents[3] / "GameData" / "schemas" / "contract-meta-v1.json"
        schema = json.loads(schema_path.read_text(encoding="utf-8"))

        self.assertEqual(schema["x-gridopoly-normative-extensions"], contract_model.NORMATIVE_EXTENSIONS)
        self.assertEqual(
            {rule["id"] for rule in contract_model.NORMATIVE_EXTENSIONS["rules"]},
            {
                "source-duplicate-keys", "unicode-scalar-values", "integer-only-numerics",
                "ascending-unique-ids", "unique-definition-and-field-names", "unique-enum-names-and-values",
                "nfc-identities", "wire-range-bounds", "enum-and-default-bounds", "bytes-hex-and-max",
                "utf8-byte-max", "list-count-and-elements", "record-target-and-acyclic",
                "record-default-completion",
            },
        )

    def test_differential_corpus_exercises_schema_and_extension_boundaries(self) -> None:
        corpus_path = Path(__file__).resolve().parents[3] / "GameData" / "tests" / "contracts" / "contract-meta-differential-v1.json"
        corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
        extension_ids = {rule["id"] for rule in contract_model.NORMATIVE_EXTENSIONS["rules"]}

        for case in corpus["same_accept"]:
            with self.subTest(case=case["id"]):
                validate_contract(case["document"])
        for case in corpus["same_reject"]:
            with self.subTest(case=case["id"]), self.assertRaises(ContractError):
                validate_contract(case["document"])
        for case in corpus["extension_only_reject"]:
            with self.subTest(case=case["id"]):
                self.assertIn(case["extension"], extension_ids)
                with self.assertRaises(ContractError):
                    validate_contract(case["document"])

    def test_programmatic_validation_accepts_only_json_native_trees(self) -> None:
        class CustomMapping(Mapping[str, object]):
            def __iter__(self):
                return iter(())

            def __len__(self) -> int:
                return 0

            def __getitem__(self, key: str) -> object:
                raise KeyError(key)

        tuple_default = fixture(fields=[typed_field("U8_LIST", [0])])
        tuple_default["definitions"][0]["fields"][0]["default"] = (0,)
        for value in (CustomMapping(), tuple_default):
            with self.subTest(value=type(value).__name__), self.assertRaisesRegex(ContractError, "JSON-native"):
                validate_contract(value)

    def test_identity_metadata_requires_nfc_but_description_remains_free_text(self) -> None:
        document = fixture()
        document["definitions"][0]["description"] = "cafe\u0301"
        document["definitions"][0]["stable_name"] = "ROOT"
        validate_contract(document)
        document["definitions"][0]["stable_name"] = "e\u0301"

        with self.assertRaisesRegex(ContractError, "NFC"):
            validate_contract(document)


if __name__ == "__main__":
    unittest.main()
