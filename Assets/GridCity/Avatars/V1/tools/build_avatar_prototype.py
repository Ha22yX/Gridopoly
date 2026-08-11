from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
LAYERS = ROOT / "source" / "layers"
DEVICE = ROOT / "device"
REVIEW = ROOT / "review"
SOURCE_SIZE = (1254, 1254)
DEVICE_SIZE = (320, 320)

HAIR = {
    "H01": LAYERS / "hair" / "hair-h01-angular-crop-v3.png",
    "H02": LAYERS / "hair" / "hair-h02-asymmetric-bob.png",
    "H03": LAYERS / "hair" / "hair-h03-textured-quiff.png",
    "H04": LAYERS / "hair" / "hair-h04-curly-fade.png",
    "H05": LAYERS / "hair" / "hair-h05-classic-side-part-v2.png",
    "H06": LAYERS / "hair" / "hair-h06-short-coils-v2.png",
    "H07": LAYERS / "hair" / "hair-h07-shoulder-waves.png",
    "H08": LAYERS / "hair" / "hair-h08-braided-crown-v2.png",
    "H09": LAYERS / "hair" / "hair-h09-high-bun.png",
    "H10": LAYERS / "hair" / "hair-h10-long-straight.png",
}
FACE = {
    "F01": LAYERS / "face" / "face-f01-angular-oval.png",
    "F02": LAYERS / "face" / "face-f02-soft-square-v2.png",
    "F03": LAYERS / "face" / "face-f03-round-youthful.png",
    "F04": LAYERS / "face" / "face-f04-heart-taper.png",
    "F05": LAYERS / "face" / "face-f05-long-rectangle.png",
    "F06": LAYERS / "face" / "face-f06-diamond-angular.png",
    "F07": LAYERS / "face" / "face-f07-broad-strong.png",
    "F08": LAYERS / "face" / "face-f08-soft-triangle.png",
    "F09": LAYERS / "face" / "face-f09-narrow-refined.png",
    "F10": LAYERS / "face" / "face-f10-mature-sculpted.png",
}
OUTFIT = {
    "O01": LAYERS / "outfit" / "outfit-o01-utility-bomber-v2.png",
    "O02": LAYERS / "outfit" / "outfit-o02-operations-jacket.png",
    "O03": LAYERS / "outfit" / "outfit-o03-streetline-hoodie.png",
    "O04": LAYERS / "outfit" / "outfit-o04-metro-varsity.png",
    "O05": LAYERS / "outfit" / "outfit-o05-civic-blazer.png",
    "O06": LAYERS / "outfit" / "outfit-o06-workshop-vest.png",
    "O07": LAYERS / "outfit" / "outfit-o07-signal-knit.png",
    "O08": LAYERS / "outfit" / "outfit-o08-denim-commuter.png",
    "O09": LAYERS / "outfit" / "outfit-o09-rainline-shell.png",
    "O10": LAYERS / "outfit" / "outfit-o10-gala-tailored.png",
}

HAIR_COLORS = {
    "graphite": (104, 116, 124),
    "copper": (176, 83, 43),
    "raven": (40, 48, 58),
    "espresso": (80, 55, 47),
    "chestnut": (142, 90, 60),
    "golden": (209, 164, 79),
    "platinum": (216, 204, 176),
    "city-teal": (45, 132, 138),
    "ash-brown": (112, 92, 82),
    "auburn": (139, 54, 42),
    "mahogany": (104, 42, 44),
    "honey-blonde": (200, 151, 78),
    "strawberry-blonde": (213, 139, 92),
    "silver": (166, 178, 188),
    "snow-white": (235, 238, 234),
    "burgundy": (117, 37, 63),
    "rose-pink": (194, 85, 119),
    "violet": (111, 78, 160),
    "cobalt-blue": (55, 93, 168),
    "emerald": (48, 125, 91),
}

SKIN_COLORS = {
    "porcelain": (239, 202, 173),
    "fair": (232, 181, 139),
    "warm": (224, 158, 100),
    "golden": (202, 141, 82),
    "olive": (173, 128, 83),
    "bronze": (161, 96, 62),
    "deep": (137, 83, 56),
    "ebony": (91, 53, 42),
}

# AI outputs share a canvas but not a true skeleton. These fixed transforms align
# their eye line, chin, neckline and shoulder line to one registration system.
REGISTRATION = {
    "hair": {hair_id: (0.82, 0, -30) for hair_id in HAIR},
    "face": {face_id: (0.82, 0, -30) for face_id in FACE},
    "outfit": {
        **{outfit_id: (0.82, 0, 135) for outfit_id in OUTFIT},
        "O02": (0.86, 0, 315),
    },
}

# Final visual calibration supplied from the 320 x 320 interactive prototype.
# Keep these values in device pixels so the runtime layers match the review UI exactly.
DEVICE_OFFSETS = {
    "hair": {
        "H01": (0, -11),
        "H02": (0, -24),
        "H03": (-2, -28),
        "H04": (0, -20),
        "H05": (0, -33),
        "H06": (0, -33),
        "H07": (0, 2),
        "H08": (0, -15),
        "H09": (0, -24),
        "H10": (-1, -10),
    },
    "face": {
        **{face_id: (0, 0) for face_id in FACE},
        "F10": (0, 1),
    },
    "outfit": {
        **{outfit_id: (0, 31) for outfit_id in OUTFIT},
        "O02": (0, 33),
        "O07": (0, 48),
        "O08": (0, 52),
        "O09": (0, 48),
    },
}


def load_layer(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    if image.size != SOURCE_SIZE:
        raise ValueError(f"Unexpected layer size: {path} is {image.size}")
    return image


def lock_face_neck_interface(image: Image.Image) -> Image.Image:
    """Keep every face's outfit-facing neck geometry identical to F01."""
    if image is None:
        raise ValueError("Face image is required")
    reference = load_layer(FACE["F01"])
    if image.tobytes() == reference.tobytes():
        return image

    result = image.copy()
    transition_start = 936
    locked_start = 960
    for y in range(transition_start, SOURCE_SIZE[1]):
        source_row = result.crop((0, y, SOURCE_SIZE[0], y + 1))
        reference_row = reference.crop((0, y, SOURCE_SIZE[0], y + 1))
        amount = min(1.0, (y - transition_start) / (locked_start - transition_start))
        result.paste(Image.blend(source_row, reference_row, amount), (0, y))
    return result


def register_layer(image: Image.Image, registration: tuple[float, int, int]) -> Image.Image:
    scale, offset_x, offset_y = registration
    width = round(image.width * scale)
    height = round(image.height * scale)
    resized = image.resize((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", SOURCE_SIZE, (0, 0, 0, 0))
    x = (SOURCE_SIZE[0] - width) // 2 + offset_x
    y = (SOURCE_SIZE[1] - height) // 2 + offset_y
    canvas.alpha_composite(resized, (x, y))
    return canvas


def offset_device_layer(image: Image.Image, offset: tuple[int, int]) -> Image.Image:
    canvas = Image.new("RGBA", DEVICE_SIZE, (0, 0, 0, 0))
    canvas.alpha_composite(image, offset)
    return canvas


def compose(hair_id: str, face_id: str, outfit_id: str) -> Image.Image:
    canvas = Image.new("RGBA", SOURCE_SIZE, (0, 0, 0, 0))
    layers = (
        register_layer(lock_face_neck_interface(load_layer(FACE[face_id])), REGISTRATION["face"][face_id]),
        register_layer(load_layer(OUTFIT[outfit_id]), REGISTRATION["outfit"][outfit_id]),
        register_layer(load_layer(HAIR[hair_id]), REGISTRATION["hair"][hair_id]),
    )
    for layer in layers:
        canvas.alpha_composite(layer)
    return canvas


def compose_device(hair_id: str, hair_color: str, face_id: str, skin: str, outfit_id: str) -> Image.Image:
    canvas = Image.new("RGBA", DEVICE_SIZE, (0, 0, 0, 0))
    paths = (
        DEVICE / "layers" / "face" / f"{face_id.lower()}-{skin}.png",
        DEVICE / "layers" / "outfit" / f"{outfit_id.lower()}.png",
        DEVICE / "layers" / "hair" / f"{hair_id.lower()}-{hair_color}.png",
    )
    for path in paths:
        image = Image.open(path).convert("RGBA")
        if image.size != DEVICE_SIZE:
            raise ValueError(f"Unexpected device layer size: {path} is {image.size}")
        canvas.alpha_composite(image)
    return canvas


def tint_hair(image: Image.Image, target: tuple[int, int, int]) -> Image.Image:
    result = image.copy()
    pixels = result.load()
    for y in range(result.height):
        for x in range(result.width):
            red, green, blue, alpha = pixels[x, y]
            if alpha == 0:
                continue
            luminance = (red * 54 + green * 183 + blue * 19) / 65280.0
            if luminance < 0.10:
                continue
            strength = 0.35 + luminance * 1.05
            pixels[x, y] = (
                min(255, round(target[0] * strength)),
                min(255, round(target[1] * strength)),
                min(255, round(target[2] * strength)),
                alpha,
            )
    return result


def tint_skin(image: Image.Image, target: tuple[int, int, int]) -> Image.Image:
    result = image.copy()
    pixels = result.load()
    source_skin_luminance = 178.0
    for y in range(result.height):
        for x in range(result.width):
            red, green, blue, alpha = pixels[x, y]
            if alpha == 0:
                continue
            maximum = max(red, green, blue)
            minimum = min(red, green, blue)
            saturation = (maximum - minimum) / maximum if maximum else 0.0
            luminance = (red * 54 + green * 183 + blue * 19) / 256.0
            # Warm, sufficiently saturated mid/high-value pixels are skin planes.
            # This explicitly excludes eye whites, dark irises, ink, brows and lashes.
            is_skin = (
                luminance >= 92
                and saturation >= 0.20
                and red >= green * 1.04
                and green >= blue * 0.92
            )
            if not is_skin:
                continue
            ratio = max(0.42, min(1.35, luminance / source_skin_luminance))
            pixels[x, y] = (
                min(255, round(target[0] * ratio)),
                min(255, round(target[1] * ratio)),
                min(255, round(target[2] * ratio)),
                alpha,
            )
    return result


def font(size: int) -> ImageFont.ImageFont:
    candidates = (
        Path("C:/Windows/Fonts/seguisb.ttf"),
        Path("C:/Windows/Fonts/arialbd.ttf"),
    )
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def main() -> None:
    DEVICE.mkdir(parents=True, exist_ok=True)
    REVIEW.mkdir(parents=True, exist_ok=True)

    hair_device = DEVICE / "layers" / "hair"
    face_device = DEVICE / "layers" / "face"
    outfit_device = DEVICE / "layers" / "outfit"
    hair_device.mkdir(parents=True, exist_ok=True)
    face_device.mkdir(parents=True, exist_ok=True)
    outfit_device.mkdir(parents=True, exist_ok=True)

    for hair_id, path in HAIR.items():
        source = load_layer(path)
        for color_id, color in HAIR_COLORS.items():
            tinted = register_layer(tint_hair(source, color), REGISTRATION["hair"][hair_id])
            tinted = tinted.resize(DEVICE_SIZE, Image.Resampling.LANCZOS)
            tinted = offset_device_layer(tinted, DEVICE_OFFSETS["hair"][hair_id])
            tinted.save(hair_device / f"{hair_id.lower()}-{color_id}.png", optimize=True)

    for face_id, path in FACE.items():
        source = lock_face_neck_interface(load_layer(path))
        for color_id, color in SKIN_COLORS.items():
            tinted = register_layer(tint_skin(source, color), REGISTRATION["face"][face_id])
            tinted = tinted.resize(DEVICE_SIZE, Image.Resampling.LANCZOS)
            tinted = offset_device_layer(tinted, DEVICE_OFFSETS["face"][face_id])
            tinted.save(face_device / f"{face_id.lower()}-{color_id}.png", optimize=True)

    for outfit_id, path in OUTFIT.items():
        outfit = register_layer(load_layer(path), REGISTRATION["outfit"][outfit_id]).resize(
            DEVICE_SIZE, Image.Resampling.LANCZOS
        )
        offset_device_layer(outfit, DEVICE_OFFSETS["outfit"][outfit_id]).save(
            outfit_device / f"{outfit_id.lower()}.png", optimize=True
        )

    # Keep every 10 x 10 x 10 combination live-composable without storing 1,000
    # redundant portraits. These 271 samples cover every pair of structural axes.
    for stale_preview in DEVICE.glob("avatar-*.png"):
        stale_preview.unlink()

    variant_keys = set()
    variant_keys.update((hair_id, face_id, "O01") for hair_id in HAIR for face_id in FACE)
    variant_keys.update(("H01", face_id, outfit_id) for face_id in FACE for outfit_id in OUTFIT)
    variant_keys.update((hair_id, "F01", outfit_id) for hair_id in HAIR for outfit_id in OUTFIT)

    variants = []
    for hair_id, face_id, outfit_id in sorted(variant_keys):
        variant_id = f"{hair_id}-{face_id}-{outfit_id}"
        portrait = compose_device(hair_id, "graphite", face_id, "warm", outfit_id)
        portrait.save(DEVICE / f"avatar-{variant_id.lower()}.png", optimize=True)
        variants.append((variant_id, portrait))

    columns = 5
    rows = (len(variants) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * 312 + 32, 112 + rows * 292), "#071018")
    draw = ImageDraw.Draw(sheet)
    title_font = font(34)
    label_font = font(22)
    draw.text((40, 24), "GRIDOPOLY AVATAR LAYER PROTOTYPE", fill="#ecf7ff", font=title_font)
    draw.text((40, 68), f"{len(variants)} PAIRWISE SAMPLES / ALL 1,000 COMBINATIONS LIVE-COMPOSABLE", fill="#55e6ce", font=label_font)

    for index, (variant_id, portrait) in enumerate(variants):
        col = index % columns
        row = index // columns
        x = 32 + col * 312
        y = 112 + row * 292
        card = Image.new("RGBA", (280, 260), (10, 24, 34, 255))
        thumb = portrait.resize((238, 238), Image.Resampling.LANCZOS)
        card.alpha_composite(thumb, (21, -6))
        card_draw = ImageDraw.Draw(card)
        card_draw.rounded_rectangle((1, 1, 278, 258), 8, outline="#2c6170", width=2)
        card_draw.rectangle((1, 222, 278, 258), fill="#0d202b")
        card_draw.text((14, 228), variant_id, fill="#ecf7ff", font=label_font)
        sheet.paste(card.convert("RGB"), (x, y))

    sheet.save(REVIEW / "avatar-layer-combinations-v1.png", optimize=True)

    face_sheet = Image.new("RGB", (5 * 312 + 32, 112 + 2 * 292), "#071018")
    face_draw = ImageDraw.Draw(face_sheet)
    face_draw.text((40, 24), "GRIDOPOLY FACE PRESETS", fill="#ecf7ff", font=title_font)
    face_draw.text((40, 68), "F01-F10 / IDENTICAL F01 NECK INTERFACE", fill="#55e6ce", font=label_font)
    for index, face_id in enumerate(FACE):
        portrait = compose_device("H01", "graphite", face_id, "warm", "O01")
        card = Image.new("RGBA", (280, 260), (10, 24, 34, 255))
        card.alpha_composite(portrait.resize((238, 238), Image.Resampling.LANCZOS), (21, -6))
        card_draw = ImageDraw.Draw(card)
        card_draw.rounded_rectangle((1, 1, 278, 258), 8, outline="#2c6170", width=2)
        card_draw.rectangle((1, 222, 278, 258), fill="#0d202b")
        card_draw.text((14, 228), face_id, fill="#ecf7ff", font=label_font)
        face_sheet.paste(card.convert("RGB"), (32 + (index % 5) * 312, 112 + (index // 5) * 292))
    face_sheet.save(REVIEW / "avatar-face-presets-v1.png", optimize=True)

    outfit_sheet = Image.new("RGB", (5 * 312 + 32, 112 + 2 * 292), "#071018")
    outfit_draw = ImageDraw.Draw(outfit_sheet)
    outfit_draw.text((40, 24), "GRIDOPOLY OUTFIT PRESETS", fill="#ecf7ff", font=title_font)
    outfit_draw.text((40, 68), "O01-O10 / NECK-SAFE REGISTRATION REVIEW", fill="#55e6ce", font=label_font)
    for index, outfit_id in enumerate(OUTFIT):
        portrait = compose_device("H01", "graphite", "F01", "warm", outfit_id)
        card = Image.new("RGBA", (280, 260), (10, 24, 34, 255))
        card.alpha_composite(portrait.resize((238, 238), Image.Resampling.LANCZOS), (21, -6))
        card_draw = ImageDraw.Draw(card)
        card_draw.rounded_rectangle((1, 1, 278, 258), 8, outline="#2c6170", width=2)
        card_draw.rectangle((1, 222, 278, 258), fill="#0d202b")
        card_draw.text((14, 228), outfit_id, fill="#ecf7ff", font=label_font)
        outfit_sheet.paste(card.convert("RGB"), (32 + (index % 5) * 312, 112 + (index // 5) * 292))
    outfit_sheet.save(REVIEW / "avatar-outfit-presets-v1.png", optimize=True)

    color_samples = tuple(("H03", color_id, "F01", "warm", "O01") for color_id in HAIR_COLORS)
    color_columns = 4
    color_rows = (len(color_samples) + color_columns - 1) // color_columns
    color_sheet = Image.new("RGB", (1280, 116 + color_rows * 292), "#071018")
    color_draw = ImageDraw.Draw(color_sheet)
    color_draw.text((40, 24), "PROGRAMMATIC COLOR TEST", fill="#ecf7ff", font=title_font)
    color_draw.text((40, 68), "Same art layers / no regenerated color variants", fill="#55e6ce", font=label_font)
    for index, sample in enumerate(color_samples):
        portrait = compose_device(*sample)
        card = Image.new("RGBA", (280, 260), (10, 24, 34, 255))
        card.alpha_composite(portrait.resize((238, 238), Image.Resampling.LANCZOS), (21, -6))
        card_draw = ImageDraw.Draw(card)
        card_draw.rounded_rectangle((1, 1, 278, 258), 8, outline="#2c6170", width=2)
        card_draw.rectangle((1, 222, 278, 258), fill="#0d202b")
        card_draw.text((14, 228), " / ".join(sample).upper(), fill="#ecf7ff", font=label_font)
        color_sheet.paste(card.convert("RGB"), (32 + (index % color_columns) * 312, 116 + (index // color_columns) * 292))
    color_sheet.save(REVIEW / "avatar-color-combinations-v1.png", optimize=True)

    skin_samples = tuple(("H01", "graphite", "F05", skin_id, "O01") for skin_id in SKIN_COLORS)
    skin_columns = 4
    skin_rows = (len(skin_samples) + skin_columns - 1) // skin_columns
    skin_sheet = Image.new("RGB", (1280, 116 + skin_rows * 292), "#071018")
    skin_draw = ImageDraw.Draw(skin_sheet)
    skin_draw.text((40, 24), "PROGRAMMATIC SKIN TONE TEST", fill="#ecf7ff", font=title_font)
    skin_draw.text((40, 68), "Eight audited tones / identical face geometry", fill="#55e6ce", font=label_font)
    for index, sample in enumerate(skin_samples):
        portrait = compose_device(*sample)
        card = Image.new("RGBA", (280, 260), (10, 24, 34, 255))
        card.alpha_composite(portrait.resize((238, 238), Image.Resampling.LANCZOS), (21, -6))
        card_draw = ImageDraw.Draw(card)
        card_draw.rounded_rectangle((1, 1, 278, 258), 8, outline="#2c6170", width=2)
        card_draw.rectangle((1, 222, 278, 258), fill="#0d202b")
        card_draw.text((14, 228), sample[3].upper(), fill="#ecf7ff", font=label_font)
        skin_sheet.paste(card.convert("RGB"), (32 + (index % skin_columns) * 312, 116 + (index // skin_columns) * 292))
    skin_sheet.save(REVIEW / "avatar-skin-tones-v1.png", optimize=True)

    manifest = {
        "schema": 1,
        "sourceCanvas": {"width": SOURCE_SIZE[0], "height": SOURCE_SIZE[1]},
        "deviceCanvas": {"width": DEVICE_SIZE[0], "height": DEVICE_SIZE[1]},
        "layerOrder": ["face", "outfit", "hair"],
        "registration": REGISTRATION,
        "deviceOffsets": DEVICE_OFFSETS,
        "hairColors": [{"id": key, "rgb": list(color)} for key, color in HAIR_COLORS.items()],
        "skinColors": [{"id": key, "rgb": list(color)} for key, color in SKIN_COLORS.items()],
        "hair": [{"id": key, "file": str(path.relative_to(ROOT)).replace("\\", "/")} for key, path in HAIR.items()],
        "face": [{"id": key, "file": str(path.relative_to(ROOT)).replace("\\", "/")} for key, path in FACE.items()],
        "outfit": [{"id": key, "file": str(path.relative_to(ROOT)).replace("\\", "/")} for key, path in OUTFIT.items()],
    }
    (ROOT / "avatar-layers.manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
