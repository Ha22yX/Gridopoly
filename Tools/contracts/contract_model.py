"""Validated, immutable representation of a versioned Gridopoly contract."""

from __future__ import annotations

import json
import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping, cast


WIRE_TYPES = frozenset(
    {
        "U8",
        "I8",
        "U16",
        "I16",
        "U32",
        "I32",
        "U64",
        "I64",
        "BOOL",
        "BYTES",
        "UTF8",
        "RECORD",
        "RECORD_LIST",
        "U8_LIST",
        "U16_LIST",
        "U32_LIST",
    }
)

_INTEGER_LIMITS = {
    "U8": (0, 2**8 - 1),
    "I8": (-(2**7), 2**7 - 1),
    "U16": (0, 2**16 - 1),
    "I16": (-(2**15), 2**15 - 1),
    "U32": (0, 2**32 - 1),
    "I32": (-(2**31), 2**31 - 1),
    "U64": (0, 2**64 - 1),
    "I64": (-(2**63), 2**63 - 1),
}
_LIST_TYPES = {"U8_LIST", "U16_LIST", "U32_LIST", "RECORD_LIST"}
_MAX_TYPES = {"BYTES", "UTF8", *_LIST_TYPES}
_TOP_LEVEL_KEYS = {
    "contract_schema",
    "contract_name",
    "contract_version",
    "namespace",
    "definitions",
}
_DEFINITION_KEYS = {
    "id", "name", "fields", "stable_name", "critical", "terminal_role", "visibility", "description", "kind",
}
_FIELD_KEYS = {
    "id",
    "name",
    "type",
    "cardinality",
    "required",
    "default",
    "range",
    "max",
    "enum",
    "record",
    "stable_name",
    "critical",
    "private",
    "visibility",
    "sort",
    "description",
}
_TYPE_SPECIFIC_PROPERTIES = {
    "U8": frozenset({"range", "enum"}),
    "I8": frozenset({"range", "enum"}),
    "U16": frozenset({"range", "enum"}),
    "I16": frozenset({"range", "enum"}),
    "U32": frozenset({"range", "enum"}),
    "I32": frozenset({"range", "enum"}),
    "U64": frozenset({"range", "enum"}),
    "I64": frozenset({"range", "enum"}),
    "BOOL": frozenset(),
    "BYTES": frozenset({"max"}),
    "UTF8": frozenset({"max"}),
    "RECORD": frozenset({"record"}),
    "RECORD_LIST": frozenset({"record", "max"}),
    "U8_LIST": frozenset({"max"}),
    "U16_LIST": frozenset({"max"}),
    "U32_LIST": frozenset({"max"}),
}
NORMATIVE_EXTENSIONS = {
    "source_loader_entry": "Tools.contracts.contract_model.load_json_value/load_contract",
    "semantic_entry": "Tools.contracts.contract_model.validate_contract",
    "rules": [
        {"id": "source-duplicate-keys", "boundary": "source_loader"},
        {"id": "unicode-scalar-values", "boundary": "semantic"},
        {"id": "integer-only-numerics", "boundary": "semantic"},
        {"id": "ascending-unique-ids", "boundary": "semantic"},
        {"id": "unique-definition-and-field-names", "boundary": "semantic"},
        {"id": "unique-enum-names-and-values", "boundary": "semantic"},
        {"id": "nfc-identities", "boundary": "semantic"},
        {"id": "wire-range-bounds", "boundary": "semantic"},
        {"id": "enum-and-default-bounds", "boundary": "semantic"},
        {"id": "bytes-hex-and-max", "boundary": "semantic"},
        {"id": "utf8-byte-max", "boundary": "semantic"},
        {"id": "list-count-and-elements", "boundary": "semantic"},
        {"id": "record-target-and-acyclic", "boundary": "semantic"},
        {"id": "record-default-completion", "boundary": "semantic"},
    ],
}


class ContractError(ValueError):
    """Raised when JSON cannot represent a stable contract."""


@dataclass(frozen=True)
class ContractField:
    id: int
    name: str
    wire_type: str
    cardinality: str
    required: bool
    metadata: Mapping[str, object]


@dataclass(frozen=True)
class ContractDefinition:
    id: int
    name: str
    fields: tuple[ContractField, ...]
    metadata: Mapping[str, object]


@dataclass(frozen=True)
class ContractDocument:
    contract_schema: int
    contract_name: str
    contract_version: int
    namespace: str
    definitions: tuple[ContractDefinition, ...]


def load_contract(path: Path) -> ContractDocument:
    """Load and validate a UTF-8 JSON contract from *path*."""

    return validate_contract(load_json_value(path))


def load_json_value(path: Path) -> object:
    """Load a UTF-8 JSON value without losing exact or NFC-colliding keys."""

    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_duplicate_aware_object)
    except (OSError, UnicodeError, json.JSONDecodeError, ContractError) as error:
        raise ContractError(f"cannot load contract {path}: {error}") from error


def validate_contract(value: object) -> ContractDocument:
    """Validate an untrusted JSON value and return its immutable model."""

    _ensure_json_tree(value)
    document = _as_dict(value, "contract")
    _require_exact_keys(document, _TOP_LEVEL_KEYS, "top-level")
    contract_schema = _positive_int(document["contract_schema"], "contract_schema")
    contract_name = _nonempty_string(document["contract_name"], "contract_name")
    contract_version = _positive_int(document["contract_version"], "contract_version")
    namespace = _nonempty_string(document["namespace"], "namespace")
    definitions_value = _as_list(document["definitions"], "definitions")
    if not definitions_value:
        raise ContractError("definitions must not be empty")

    definitions = tuple(_validate_definition(item, index) for index, item in enumerate(definitions_value))
    _require_unique_and_ordered(
        definitions,
        "definition",
        lambda definition: definition.id,
        lambda definition: definition.name,
    )
    _validate_record_graph(definitions)
    _validate_record_defaults(definitions)
    return ContractDocument(
        contract_schema=contract_schema,
        contract_name=contract_name,
        contract_version=contract_version,
        namespace=namespace,
        definitions=definitions,
    )


def _validate_definition(value: object, index: int) -> ContractDefinition:
    definition = _as_dict(value, f"definitions[{index}]")
    unknown = sorted(set(definition) - _DEFINITION_KEYS)
    if unknown:
        raise ContractError(f"definitions[{index}]: unknown key {unknown[0]}")
    for key in ("id", "name", "fields"):
        if key not in definition:
            raise ContractError(f"definitions[{index}]: missing key {key}")
    definition_id = _positive_int(definition["id"], f"definitions[{index}].id")
    name = _nfc_identifier(definition["name"], f"definitions[{index}].name")
    fields_value = _as_list(definition["fields"], f"definitions[{index}].fields")
    if not fields_value:
        raise ContractError(f"definitions[{index}].fields must not be empty")
    _validate_definition_metadata(definition, index)
    fields = tuple(_validate_field(item, index, field_index) for field_index, item in enumerate(fields_value))
    _require_unique_and_ordered(fields, "field", lambda field: field.id, lambda field: field.name)
    metadata = cast(
        Mapping[str, object],
        _freeze({key: item for key, item in definition.items() if key not in {"id", "name", "fields"}}),
    )
    return ContractDefinition(definition_id, name, fields, metadata)


def _validate_field(value: object, definition_index: int, field_index: int) -> ContractField:
    context = f"definitions[{definition_index}].fields[{field_index}]"
    field = _as_dict(value, context)
    unknown = sorted(set(field) - _FIELD_KEYS)
    if unknown:
        raise ContractError(f"{context}: unknown key {unknown[0]}")
    for key in ("id", "name", "type", "cardinality", "required"):
        if key not in field:
            if key == "cardinality":
                raise ContractError(f"{context}: {field.get('type', 'field')} requires cardinality")
            raise ContractError(f"{context}: missing key {key}")
    field_id = _positive_int(field["id"], f"{context}.id")
    name = _nfc_identifier(field["name"], f"{context}.name")
    wire_type = _nonempty_string(field["type"], f"{context}.type")
    if wire_type not in WIRE_TYPES:
        raise ContractError(f"{context}.type: unsupported wire type {wire_type}")
    irrelevant = (set(field) & {"range", "max", "enum", "record"}) - _TYPE_SPECIFIC_PROPERTIES[wire_type]
    if irrelevant:
        raise ContractError(f"{context}: irrelevant property {sorted(irrelevant)[0]} for {wire_type}")
    cardinality = _nonempty_string(field["cardinality"], f"{context}.cardinality")
    if cardinality not in {"ONE", "OPTIONAL", "MANY"}:
        raise ContractError(f"{context}.cardinality: invalid cardinality {cardinality}")
    if not isinstance(field["required"], bool):
        raise ContractError(f"{context}.required must be boolean")

    numeric_range: tuple[int, int] | None = None
    if wire_type in _INTEGER_LIMITS:
        numeric_range = _validate_integer_range(field, context, wire_type)
    if wire_type in _MAX_TYPES:
        _validate_maximum(field, context)
    if wire_type in {"RECORD", "RECORD_LIST"}:
        _nfc_identifier(field.get("record"), f"{context}.record")
    _validate_field_metadata(field, context)
    enum_values = _validate_enum(field.get("enum"), context, wire_type, numeric_range)
    if "default" in field:
        _validate_default(field["default"], field, context, wire_type, enum_values)

    metadata = cast(
        Mapping[str, object],
        _freeze({key: item for key, item in field.items() if key not in {"id", "name", "type", "cardinality", "required"}}),
    )
    return ContractField(field_id, name, wire_type, cardinality, field["required"], metadata)


def _validate_definition_metadata(definition: Mapping[str, object], index: int) -> None:
    context = f"definitions[{index}]"
    if "stable_name" in definition:
        _nfc_identifier(definition["stable_name"], f"{context}.stable_name")
    for key in ("terminal_role", "visibility", "description", "kind"):
        if key in definition:
            _nonempty_string(definition[key], f"{context}.{key}")
    if "critical" in definition and not isinstance(definition["critical"], bool):
        raise ContractError(f"{context}.critical must be boolean")


def _validate_field_metadata(field: Mapping[str, object], context: str) -> None:
    if "stable_name" in field:
        _nfc_identifier(field["stable_name"], f"{context}.stable_name")
    for key in ("visibility", "sort", "description"):
        if key in field:
            _nonempty_string(field[key], f"{context}.{key}")
    for key in ("critical", "private"):
        if key in field and not isinstance(field[key], bool):
            raise ContractError(f"{context}.{key} must be boolean")


def _validate_record_graph(definitions: tuple[ContractDefinition, ...]) -> None:
    by_name = {definition.name: definition for definition in definitions}
    edges: dict[str, set[str]] = {definition.name: set() for definition in definitions}
    for definition in definitions:
        for field in definition.fields:
            if field.wire_type not in {"RECORD", "RECORD_LIST"}:
                continue
            target = field.metadata["record"]
            if target not in by_name:
                raise ContractError(f"field {field.name} refers to unknown record {target}")
            edges[definition.name].add(target)

    active: set[str] = set()
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in active:
            raise ContractError(f"record reference cycle at {name}")
        if name in visited:
            return
        active.add(name)
        for target in edges[name]:
            visit(target)
        active.remove(name)
        visited.add(name)

    for definition in definitions:
        visit(definition.name)


def _validate_record_defaults(definitions: tuple[ContractDefinition, ...]) -> None:
    by_name = {definition.name: definition for definition in definitions}
    for definition in definitions:
        for field in definition.fields:
            if field.wire_type not in {"RECORD", "RECORD_LIST"}:
                continue
            record_name = field.metadata["record"]
            target = by_name.get(record_name)
            if target is None:
                raise ContractError(f"field {field.name} refers to unknown record {record_name}")
            if "default" not in field.metadata:
                continue
            default = field.metadata["default"]
            if field.wire_type == "RECORD":
                _validate_record_instance(default, target, by_name, f"field {field.name}.default")
            else:
                for item_index, item in enumerate(default):
                    _validate_record_instance(item, target, by_name, f"field {field.name}.default[{item_index}]")


def _validate_record_instance(
    value: object,
    definition: ContractDefinition,
    definitions: Mapping[str, ContractDefinition],
    context: str,
) -> None:
    if not isinstance(value, Mapping):
        raise ContractError(f"{context} must be a record object")
    fields = {field.name: field for field in definition.fields}
    unknown = sorted(set(value) - set(fields))
    if unknown:
        raise ContractError(f"{context} contains unknown field {unknown[0]}")
    for name, field in fields.items():
        if name not in value and field.required and "default" not in field.metadata:
            raise ContractError(f"{context} missing required field {name}")
    for name, item in value.items():
        field = fields[name]
        _validate_default(item, field.metadata, f"{context}.{name}", field.wire_type, _metadata_enum_values(field.metadata))
        if field.wire_type == "RECORD":
            target = definitions.get(field.metadata["record"])
            if target is None:
                raise ContractError(f"{context}.{name} refers to an unknown record")
            _validate_record_instance(item, target, definitions, f"{context}.{name}")
        elif field.wire_type == "RECORD_LIST":
            target = definitions.get(field.metadata["record"])
            if target is None:
                raise ContractError(f"{context}.{name} refers to an unknown record")
            for item_index, nested_item in enumerate(item):
                _validate_record_instance(nested_item, target, definitions, f"{context}.{name}[{item_index}]")


def _metadata_enum_values(metadata: Mapping[str, object]) -> frozenset[int] | None:
    members = metadata.get("enum")
    if members is None:
        return None
    return frozenset(member["value"] for member in members)


def _validate_integer_range(field: Mapping[str, object], context: str, wire_type: str) -> tuple[int, int]:
    if "range" not in field:
        raise ContractError(f"{context}: {wire_type} requires range")
    range_value = _as_dict(field["range"], f"{context}.range")
    _require_exact_keys(range_value, {"min", "max"}, f"{context}.range")
    minimum = _int(range_value["min"], f"{context}.range.min")
    maximum = _int(range_value["max"], f"{context}.range.max")
    type_minimum, type_maximum = _INTEGER_LIMITS[wire_type]
    if minimum > maximum or minimum < type_minimum or maximum > type_maximum:
        raise ContractError(f"{context}.range is outside {wire_type} limits")
    return minimum, maximum


def _validate_maximum(field: Mapping[str, object], context: str) -> None:
    if "max" not in field:
        raise ContractError(f"{context}: {field['type']} requires max")
    if _positive_int(field["max"], f"{context}.max") < 1:
        raise ContractError(f"{context}.max must be positive")


def _validate_enum(
    value: object | None,
    context: str,
    wire_type: str,
    numeric_range: tuple[int, int] | None,
) -> frozenset[int] | None:
    if value is None:
        return None
    if wire_type not in _INTEGER_LIMITS or numeric_range is None:
        raise ContractError(f"{context}.enum is only valid for integer wire types")
    enum = _as_list(value, f"{context}.enum")
    if not enum:
        raise ContractError(f"{context}.enum must not be empty")
    names: set[str] = set()
    values: set[int] = set()
    for index, item in enumerate(enum):
        if not isinstance(item, dict) or set(item) != {"name", "value"}:
            raise ContractError(f"{context}.enum[{index}] requires an explicit enum value")
        name = _nfc_identifier(item["name"], f"{context}.enum[{index}].name")
        enum_value = _int(item["value"], f"{context}.enum[{index}].value")
        if not numeric_range[0] <= enum_value <= numeric_range[1]:
            raise ContractError(f"{context}.enum[{index}].value is outside range")
        if name in names or enum_value in values:
            raise ContractError(f"{context}.enum[{index}] duplicates an enum name or value")
        names.add(name)
        values.add(enum_value)
    return frozenset(values)


def _validate_default(
    default: object,
    field: Mapping[str, object],
    context: str,
    wire_type: str,
    enum_values: frozenset[int] | None,
) -> None:
    if wire_type in _INTEGER_LIMITS:
        default_value = _int(default, f"{context}.default")
        range_value = _as_dict(field["range"], f"{context}.range")
        if not range_value["min"] <= default_value <= range_value["max"]:
            raise ContractError(f"{context}.default is outside range")
        if enum_values is not None and default_value not in enum_values:
            raise ContractError(f"{context}.default is not present in enum")
    elif wire_type == "BOOL" and not isinstance(default, bool):
        raise ContractError(f"{context}.default must be boolean")
    elif wire_type == "UTF8":
        if not isinstance(default, str) or not _is_unicode_scalar_string(default) or len(default.encode("utf-8")) > field["max"]:
            raise ContractError(f"{context}.default exceeds UTF8 max")
    elif wire_type == "BYTES":
        if not isinstance(default, str) or len(default) % 2 or re.fullmatch(r"[0-9a-f]*", default) is None or len(default) // 2 > field["max"]:
            raise ContractError(f"{context}.default exceeds BYTES max")
    elif wire_type == "RECORD":
        if not isinstance(default, Mapping):
            raise ContractError(f"{context}.default must be a record object")
    elif wire_type == "RECORD_LIST":
        if not isinstance(default, (list, tuple)) or len(default) > field["max"] or any(not isinstance(item, Mapping) for item in default):
            raise ContractError(f"{context}.default exceeds list max")
    elif wire_type in {"U8_LIST", "U16_LIST", "U32_LIST"}:
        item_type = wire_type.removesuffix("_LIST")
        item_minimum, item_maximum = _INTEGER_LIMITS[item_type]
        if (
            not isinstance(default, (list, tuple))
            or len(default) > field["max"]
            or any(isinstance(item, bool) or not isinstance(item, int) or not item_minimum <= item <= item_maximum for item in default)
        ):
            raise ContractError(f"{context}.default exceeds list max")


def _require_unique_and_ordered(items: tuple[Any, ...], noun: str, id_of: Any, name_of: Any) -> None:
    ids: set[int] = set()
    names: set[str] = set()
    previous_id = 0
    for item in items:
        item_id = id_of(item)
        item_name = name_of(item)
        if item_id in ids:
            raise ContractError(f"duplicate {noun} id {item_id}")
        if item_name in names:
            raise ContractError(f"duplicate {noun} name {item_name}")
        if item_id <= previous_id:
            plural = "definitions" if noun == "definition" else f"{noun}s"
            raise ContractError(f"{plural} must be ordered by id")
        ids.add(item_id)
        names.add(item_name)
        previous_id = item_id


def _require_exact_keys(value: Mapping[str, object], expected: set[str], context: str) -> None:
    missing = sorted(expected - set(value))
    if missing:
        raise ContractError(f"missing {context} key {missing[0]}")
    unknown = sorted(set(value) - expected)
    if unknown:
        raise ContractError(f"unknown {context} key {unknown[0]}")


def _ensure_json_tree(value: object) -> None:
    if isinstance(value, float):
        raise ContractError("floats are not allowed")
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise ContractError("object keys must be strings")
            if not _is_unicode_scalar_string(key):
                raise ContractError("strings must contain only Unicode scalar values")
            _ensure_json_tree(child)
    elif isinstance(value, list):
        for child in value:
            _ensure_json_tree(child)
    elif isinstance(value, str) and not _is_unicode_scalar_string(value):
        raise ContractError("strings must contain only Unicode scalar values")
    elif value is None or isinstance(value, (bool, int, str)):
        return
    else:
        raise ContractError(f"value must use JSON-native types, not {type(value).__name__}")


def _is_unicode_scalar_string(value: str) -> bool:
    return not any(0xD800 <= ord(character) <= 0xDFFF for character in value)


def _duplicate_aware_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if not _is_unicode_scalar_string(key):
            raise ContractError("JSON object key is not a Unicode scalar string")
        normalized = unicodedata.normalize("NFC", key)
        if normalized in result:
            raise ContractError(f"duplicate JSON object key after NFC normalization: {normalized}")
        result[normalized] = value
    return result


def _freeze(value: object) -> object:
    if isinstance(value, dict):
        return MappingProxyType({key: _freeze(child) for key, child in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(child) for child in value)
    return value


def _as_dict(value: object, context: str) -> Mapping[str, object]:
    if not isinstance(value, Mapping):
        raise ContractError(f"{context} must be an object")
    return value


def _as_list(value: object, context: str) -> list[object]:
    if not isinstance(value, list):
        raise ContractError(f"{context} must be an array")
    return value


def _int(value: object, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{context} must be an integer")
    return value


def _positive_int(value: object, context: str) -> int:
    result = _int(value, context)
    if result < 1:
        raise ContractError(f"{context} must be positive")
    return result


def _nonempty_string(value: object, context: str) -> str:
    if not isinstance(value, str) or not value:
        raise ContractError(f"{context} must be a non-empty string")
    return value


def _nfc_identifier(value: object, context: str) -> str:
    result = _nonempty_string(value, context)
    if unicodedata.normalize("NFC", result) != result:
        raise ContractError(f"{context} must be NFC normalized")
    return result
