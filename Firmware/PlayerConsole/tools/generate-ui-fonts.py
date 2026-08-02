from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "src" / "fonts"
GLYPH_FILE = FONT_DIR / "ui_glyphs.txt"
DEFAULT_FONT = Path(r"C:\Windows\Fonts\Noto Sans SC (TrueType).otf")
DEFAULT_NPX = Path(r"D:\Program Files\nodejs\npx.cmd")


def source_characters() -> str:
    characters: set[str] = set()
    extensions = {".h", ".hpp", ".c", ".cpp", ".ino"}
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in extensions:
            continue
        if FONT_DIR in path.parents or path.name == "sample_assets.c":
            continue
        text = path.read_text(encoding="utf-8")
        characters.update(character for character in text if ord(character) > 0x7E)
    return "".join(sorted(characters, key=ord))


def generate(size: int, symbols: str, font_path: Path, npx_path: Path) -> None:
    output = FONT_DIR / f"ui_font_{size}.c"
    command = [
        str(npx_path),
        "--yes",
        "lv_font_conv",
        "--size",
        str(size),
        "--bpp",
        "4",
        "--format",
        "lvgl",
        "--font",
        str(font_path),
        "-r",
        "0x20-0x7E",
        "--symbols",
        symbols,
        "--no-kerning",
        "--lv-include",
        "lvgl.h",
        "--lv-font-name",
        f"ui_font_{size}",
        "-o",
        str(output),
    ]
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT)
    parser.add_argument("--npx", type=Path, default=DEFAULT_NPX)
    args = parser.parse_args()
    if not args.font.exists():
        raise FileNotFoundError(args.font)
    if not args.npx.exists():
        raise FileNotFoundError(args.npx)

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    symbols = source_characters()
    GLYPH_FILE.write_text(symbols + "\n", encoding="utf-8")
    for size in (14, 16):
        generate(size, symbols, args.font, args.npx)
    print(f"Generated 14px and 16px fonts with {len(symbols)} non-ASCII glyphs")


if __name__ == "__main__":
    main()
