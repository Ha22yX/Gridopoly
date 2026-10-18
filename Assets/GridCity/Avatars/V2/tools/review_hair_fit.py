"""Capture and compare only H06/H08/H10 fits, without replacing source artwork."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from avatar_v2 import ROOT, compose, write_json
from review_avatar_v2 import load_components


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("before", "after"))
    args = parser.parse_args()
    report = json.loads((ROOT / "runtime/avatar-components-v2.json").read_text(encoding="utf-8"))
    components = load_components(ROOT, report)
    directory = ROOT / "review/hair-fit"
    directory.mkdir(parents=True, exist_ok=True)
    if args.phase == "before":
        baseline = directory / "before-components.json"
        if baseline.exists():
            raise SystemExit("Refusing to overwrite the before baseline")
        write_json(baseline, {e["file"]: e["fileSha256"] for e in report["entries"]})
    for h in (6, 8, 10):
        for f in (1, 7, 10):
            for skin in ("warm", "ebony"):
                compose(components, hair=h, face=f, outfit=1, skin=skin).save(directory / f"{args.phase}-h{h:02d}-f{f:02d}-{skin}.png")
    if args.phase == "before":
        return
    baseline = json.loads((directory / "before-components.json").read_text(encoding="utf-8"))
    changed = [e["file"] for e in report["entries"] if baseline.get(e["file"]) != e["fileSha256"]]
    unexpected = sorted(set(changed) - {"hair/h6.gavc", "hair/h8.gavc", "hair/h10.gavc"})
    write_json(directory / "component-changes.json", {"changed": changed, "unexpected": unexpected, "other28ComponentsUnchanged": not unexpected})
    if unexpected:
        raise SystemExit(f"Unexpected component changes: {unexpected}")
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 16)
    for h in (6, 8, 10):
        sheet = Image.new("RGB", (880, 930), "#111c2b")
        draw = ImageDraw.Draw(sheet)
        for row, f in enumerate((1, 7, 10)):
            for column, skin in enumerate(("warm", "ebony")):
                for side, phase in enumerate(("before", "after")):
                    x, y = column * 440 + side * 220, row * 310
                    image = Image.open(directory / f"{phase}-h{h:02d}-f{f:02d}-{skin}.png").convert("RGBA")
                    image = image.crop((75, 30, 245, 250)).resize((204, 264), Image.Resampling.NEAREST)
                    sheet.paste(image, (x + 8, y + 4), image)
                    draw.text((x + 8, y + 270), f"H{h:02d} F{f:02d} {skin}", font=font, fill="#deebfa")
                    draw.text((x + 8, y + 290), phase.upper(), font=font, fill="#a6e3c4" if phase == "after" else "#b5b2bb")
        sheet.save(directory / f"h{h:02d}-comparison.png")
    print(json.dumps({"changed": changed, "other28ComponentsUnchanged": True}))


if __name__ == "__main__":
    main()
