from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "src" / "fonts"


def required_characters() -> set[str]:
    characters: set[str] = set()
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in {".h", ".hpp", ".c", ".cpp", ".ino"}:
            continue
        if FONT_DIR in path.parents or path.name == "sample_assets.c":
            continue
        characters.update(
            character
            for character in path.read_text(encoding="utf-8")
            if ord(character) > 0x7E
        )
    return characters


def main() -> None:
    glyph_path = FONT_DIR / "ui_glyphs.txt"
    if not glyph_path.exists():
        raise SystemExit("UI glyph manifest is missing; run generate-ui-fonts.py")
    available = set(glyph_path.read_text(encoding="utf-8").rstrip("\n"))
    required = required_characters()
    missing = sorted(required - available, key=ord)
    if missing:
        raise SystemExit(
            "UI font is missing characters: " + "".join(missing) +
            "; rerun tools/generate-ui-fonts.py"
        )
    for size in (14, 16):
        if not (FONT_DIR / f"ui_font_{size}.c").exists():
            raise SystemExit(f"ui_font_{size}.c is missing")
    print(f"UI GLYPH CHECK PASS: {len(required)} non-ASCII characters covered")


if __name__ == "__main__":
    main()
