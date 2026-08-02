from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

from ui_glyph_source import GlyphSourceError, generated_font_codepoints, required_characters


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "src" / "fonts"


class GlyphVerificationError(ValueError):
    pass


def coverage_errors(root: Path, font_dir: Path) -> list[str]:
    glyph_path = font_dir / "ui_glyphs.txt"
    if not glyph_path.exists():
        return ["UI glyph manifest is missing; run generate-ui-fonts.py"]

    required = required_characters(root, font_dir)
    available = set(glyph_path.read_text(encoding="utf-8").rstrip("\n"))
    errors: list[str] = []
    missing_manifest = sorted(required - available, key=ord)
    if missing_manifest:
        errors.append("UI glyph manifest is missing characters: " + "".join(missing_manifest))

    for size in (14, 16):
        font_path = font_dir / f"ui_font_{size}.c"
        if not font_path.exists():
            errors.append(f"ui_font_{size}.c is missing")
            continue
        covered = {chr(codepoint) for codepoint in generated_font_codepoints(font_path)}
        missing_font = sorted(required - covered, key=ord)
        if missing_font:
            errors.append(f"ui_font_{size}.c CMap is missing characters: " + "".join(missing_font))
    return errors


def verify_coverage(root: Path, font_dir: Path) -> int:
    errors = coverage_errors(root, font_dir)
    if errors:
        raise GlyphVerificationError("; ".join(errors) + "; rerun tools/generate-ui-fonts.py")
    return len(required_characters(root, font_dir))


def write_sparse_font(path: Path, characters: str) -> None:
    range_start = min(ord(character) for character in characters)
    offsets = ", ".join(f"0x{ord(character) - range_start:x}" for character in characters)
    path.write_text(
        "static const uint16_t unicode_list_1[] = { " + offsets + " };\n"
        "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n"
        "    {\n"
        f"        .range_start = {range_start}, .range_length = 256, .glyph_id_start = 1,\n"
        f"        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = {len(characters)}, "
        ".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n"
        "    }\n"
        "};\n",
        encoding="utf-8",
    )


def run_self_test() -> None:
    with tempfile.TemporaryDirectory() as temporary_directory:
        root = Path(temporary_directory)
        font_dir = root / "src" / "fonts"
        font_dir.mkdir(parents=True)
        fixture = root / "fixture.cpp"
        fixture.write_bytes(
            b"const char quote = '\"';\n"
            + b'const char *escaped = "\\xE5\\x85\\x89\\xE6\\xA0\\x85\\xE5\\x85\\xAC\\xE5\\xAF\\x93";\n'
            + 'const char *raw = "原始";\n'.encode("utf-8")
            + b'// const char *line_comment = "\\xE5\\x81\\x87";\n'
            + b'/* const char *block_comment = "\\xE4\\xBC\\xAA"; */\n'
        )
        (root / "unrelated.py").write_bytes(b'value = "\\xE6\\x97\\xa0"\n')
        (root / "notes.txt").write_bytes(b'"\\xE6\\x97\\xa0"\n')

        discovered = required_characters(root, font_dir)
        assert set("光栅公寓原始") <= discovered
        assert "假" not in discovered
        assert "伪" not in discovered
        assert "无" not in discovered

        malformed = root / "malformed.cpp"
        malformed.write_bytes(b'const char *bad = "\\xE5\\x";\n')
        try:
            required_characters(root, font_dir)
        except GlyphSourceError as error:
            message = str(error)
            assert "malformed.cpp" in message and "byte offset" in message
        else:
            raise AssertionError("malformed escaped byte fixture unexpectedly passed")
        malformed.unlink()

        invalid = root / "invalid.cpp"
        invalid.write_bytes(b'const char *bad = "\\xFF";\n')
        try:
            required_characters(root, font_dir)
        except GlyphSourceError as error:
            message = str(error)
            assert "invalid.cpp" in message and "byte offset" in message
        else:
            raise AssertionError("invalid UTF-8 escaped byte fixture unexpectedly passed")
        invalid.unlink()
        fixture.unlink()
        (root / "unrelated.py").unlink()
        (root / "notes.txt").unlink()

        (root / "coverage.cpp").write_text('const char *value = "甲";\n', encoding="utf-8")
        (font_dir / "ui_glyphs.txt").write_text("甲\n", encoding="utf-8")
        write_sparse_font(font_dir / "ui_font_14.c", "甲")
        write_sparse_font(font_dir / "ui_font_16.c", "乙")
        assert generated_font_codepoints(font_dir / "ui_font_14.c") == {ord("甲")}
        errors = coverage_errors(root, font_dir)
        assert errors == ["ui_font_16.c CMap is missing characters: 甲"]

    print("UI GLYPH SELF-TEST PASS: escaped UTF-8, source filtering, malformed bytes, and CMaps")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            run_self_test()
            return 0
        count = verify_coverage(ROOT, FONT_DIR)
    except (GlyphSourceError, GlyphVerificationError, ValueError) as error:
        print(f"UI GLYPH CHECK FAIL: {error}")
        return 1
    print(f"UI GLYPH CHECK PASS: {count} non-ASCII characters covered in manifest and both CMaps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
