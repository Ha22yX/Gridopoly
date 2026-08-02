from __future__ import annotations

import re
from pathlib import Path


SOURCE_EXTENSIONS = {".h", ".hpp", ".c", ".cpp", ".ino"}
HEX_DIGITS = frozenset(b"0123456789abcdefABCDEF")
NUMBER_PATTERN = r"(?:0[xX][0-9A-Fa-f]+|[0-9]+)"


class GlyphSourceError(ValueError):
    pass


def required_characters(root: Path, font_dir: Path) -> set[str]:
    characters: set[str] = set()
    resolved_font_dir = font_dir.resolve()
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        if resolved_font_dir in path.resolve().parents or path.name == "sample_assets.c":
            continue
        _collect_source_characters(path, path.read_bytes(), characters)
    return characters


def generated_font_codepoints(font_path: Path) -> set[int]:
    source = font_path.read_text(encoding="utf-8")
    unicode_lists = {
        match.group("name"): _parse_number_list(match.group("body"), font_path)
        for match in re.finditer(
            r"static const uint16_t (?P<name>unicode_list_\d+)\[\]\s*=\s*\{(?P<body>.*?)\};",
            source,
            re.DOTALL,
        )
    }
    cmaps_match = re.search(
        r"static const lv_font_fmt_txt_cmap_t cmaps\[\]\s*=\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if cmaps_match is None:
        raise GlyphSourceError(f"{font_path}: LVGL cmaps array is missing")

    codepoints: set[int] = set()
    cmap_matches = list(re.finditer(r"\{(?P<body>.*?)\}", cmaps_match.group("body"), re.DOTALL))
    if not cmap_matches:
        raise GlyphSourceError(f"{font_path}: LVGL cmaps array has no entries")
    for cmap_match in cmap_matches:
        cmap = cmap_match.group("body")
        range_start = _cmap_number(cmap, "range_start", font_path)
        range_length = _cmap_number(cmap, "range_length", font_path)
        unicode_list_match = re.search(r"\.unicode_list\s*=\s*(\w+)", cmap)
        if unicode_list_match is None:
            raise GlyphSourceError(f"{font_path}: LVGL CMap unicode_list is missing")
        unicode_list = unicode_list_match.group(1)
        if unicode_list == "NULL":
            codepoints.update(range(range_start, range_start + range_length))
            continue

        list_length = _cmap_number(cmap, "list_length", font_path)
        offsets = unicode_lists.get(unicode_list)
        if offsets is None:
            raise GlyphSourceError(f"{font_path}: LVGL CMap references missing {unicode_list}")
        if len(offsets) != list_length:
            raise GlyphSourceError(
                f"{font_path}: LVGL CMap {unicode_list} has {len(offsets)} entries, expected {list_length}"
            )
        if any(offset < 0 or offset >= range_length for offset in offsets):
            raise GlyphSourceError(f"{font_path}: LVGL CMap {unicode_list} contains an out-of-range offset")
        codepoints.update(range_start + offset for offset in offsets)
    return codepoints


def _collect_source_characters(path: Path, source: bytes, characters: set[str]) -> None:
    index = 0
    while index < len(source):
        if source.startswith(b"//", index):
            newline = source.find(b"\n", index + 2)
            index = len(source) if newline == -1 else newline + 1
        elif source.startswith(b"/*", index):
            end = source.find(b"*/", index + 2)
            index = len(source) if end == -1 else end + 2
        elif source.startswith(b'R"', index):
            index = _collect_raw_string(path, source, index, characters)
        elif source[index] == ord('"'):
            index = _collect_ordinary_string(path, source, index, characters)
        elif source[index] == ord("'"):
            index = _skip_character_literal(path, source, index)
        else:
            index += 1


def _collect_raw_string(path: Path, source: bytes, start: int, characters: set[str]) -> int:
    delimiter_end = source.find(b"(", start + 2)
    if delimiter_end == -1:
        raise _source_error(path, start, "malformed raw string literal")
    delimiter = source[start + 2:delimiter_end]
    terminator = b")" + delimiter + b'"'
    content_end = source.find(terminator, delimiter_end + 1)
    if content_end == -1:
        raise _source_error(path, start, "unterminated raw string literal")
    _collect_raw_characters(path, source, delimiter_end + 1, content_end, characters)
    return content_end + len(terminator)


def _collect_ordinary_string(path: Path, source: bytes, start: int, characters: set[str]) -> int:
    index = start + 1
    raw_start = index
    while index < len(source):
        current = source[index]
        if current == ord('"'):
            _collect_raw_characters(path, source, raw_start, index, characters)
            return index + 1
        if current != ord("\\"):
            index += 1
            continue
        if index + 1 >= len(source):
            raise _source_error(path, index, "unterminated string literal")
        if source[index + 1] != ord("x"):
            index += 2
            continue

        _collect_raw_characters(path, source, raw_start, index, characters)
        run_start = index
        escaped_bytes = bytearray()
        while source.startswith(b"\\x", index):
            if index + 3 >= len(source) or source[index + 2] not in HEX_DIGITS or source[index + 3] not in HEX_DIGITS:
                raise _source_error(path, index, "malformed \\xNN byte escape")
            escaped_bytes.append(int(source[index + 2:index + 4], 16))
            index += 4
        try:
            decoded = escaped_bytes.decode("utf-8", errors="strict")
        except UnicodeDecodeError as error:
            raise _source_error(path, run_start + error.start * 4, "invalid UTF-8 byte escape run") from error
        characters.update(character for character in decoded if ord(character) > 0x7E)
        raw_start = index
    raise _source_error(path, start, "unterminated string literal")


def _skip_character_literal(path: Path, source: bytes, start: int) -> int:
    index = start + 1
    while index < len(source):
        if source[index] == ord("'"):
            return index + 1
        if source[index] == ord("\\"):
            index += 2
        else:
            index += 1
    raise _source_error(path, start, "unterminated character literal")


def _collect_raw_characters(path: Path, source: bytes, start: int, end: int, characters: set[str]) -> None:
    try:
        decoded = source[start:end].decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise _source_error(path, start + error.start, "source text is not valid UTF-8") from error
    characters.update(character for character in decoded if ord(character) > 0x7E)


def _cmap_number(cmap: str, field: str, font_path: Path) -> int:
    match = re.search(rf"\.{field}\s*=\s*({NUMBER_PATTERN})", cmap)
    if match is None:
        raise GlyphSourceError(f"{font_path}: LVGL CMap {field} is missing")
    return int(match.group(1), 0)


def _parse_number_list(body: str, font_path: Path) -> list[int]:
    values = re.findall(NUMBER_PATTERN, body)
    if not values:
        raise GlyphSourceError(f"{font_path}: LVGL unicode list is empty")
    return [int(value, 0) for value in values]


def _source_error(path: Path, offset: int, reason: str) -> GlyphSourceError:
    return GlyphSourceError(f"{path}: byte offset {offset}: {reason}")
