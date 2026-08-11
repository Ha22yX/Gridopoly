"""Gridopoly Canonical JSON v1, as fixed by the design record."""

from __future__ import annotations

import unicodedata
from collections.abc import Mapping, Sequence


class CanonicalJsonError(ValueError):
    """Raised when a value has no Canonical JSON v1 representation."""


def canonical_bytes(value: object) -> bytes:
    """Encode *value* as NFC, UTF-8, sorted Canonical JSON v1 plus one LF."""

    return (_encode_value(value) + "\n").encode("utf-8")


def _encode_value(value: object) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        raise CanonicalJsonError("Canonical JSON permits integers, not floats")
    if isinstance(value, str):
        return _encode_string(_normalize_string(value))
    if isinstance(value, Mapping):
        return _encode_object(value)
    if isinstance(value, Sequence) and not isinstance(value, (bytes, bytearray, memoryview)):
        return "[" + ",".join(_encode_value(item) for item in value) + "]"
    raise CanonicalJsonError(f"unsupported Canonical JSON value {type(value).__name__}")


def _encode_object(value: Mapping[object, object]) -> str:
    normalized: dict[str, object] = {}
    for key, item in value.items():
        if not isinstance(key, str):
            raise CanonicalJsonError("object keys must be strings")
        normalized_key = _normalize_string(key)
        if normalized_key in normalized:
            raise CanonicalJsonError("duplicate object key after NFC normalization")
        normalized[normalized_key] = item
    ordered_keys = sorted(normalized, key=lambda key: key.encode("utf-8"))
    return "{" + ",".join(
        _encode_string(key) + ":" + _encode_value(normalized[key]) for key in ordered_keys
    ) + "}"


def _normalize_string(value: str) -> str:
    if any(0xD800 <= ord(character) <= 0xDFFF for character in value):
        raise CanonicalJsonError("strings must contain only Unicode scalar values")
    return unicodedata.normalize("NFC", value)


def _encode_string(value: str) -> str:
    escaped: list[str] = ['"']
    for character in value:
        codepoint = ord(character)
        if character == '"':
            escaped.append('\\"')
        elif character == "\\":
            escaped.append("\\\\")
        elif character == "\b":
            escaped.append("\\b")
        elif character == "\t":
            escaped.append("\\t")
        elif character == "\n":
            escaped.append("\\n")
        elif character == "\f":
            escaped.append("\\f")
        elif character == "\r":
            escaped.append("\\r")
        elif codepoint <= 0x1F:
            escaped.append(f"\\u{codepoint:04x}")
        else:
            escaped.append(character)
    escaped.append('"')
    return "".join(escaped)
