"""Deterministic V2 avatar registration, material encoding, and review primitives.

This is an asset compiler, not an art generator. Original artwork is never changed.
Every geometric transform and inferred material mask is recorded for human review.
Dependencies: Python 3.12, Pillow, numpy. Runtime firmware has no such dependency.
"""
from __future__ import annotations

from collections import defaultdict
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
V1 = ROOT.parent / "V1"
CANVAS = (320, 320)
CROP = (50, 10, 270, 310)
PREVIEW = (220, 300)
KINDS = {"face": 1, "outfit": 2, "hair": 3, "neck": 4}
ORDER = ("neck", "face", "outfit", "hair")
HEADER = struct.Struct("<4sBBBBHHHHHHIII")
SINGLE_BUDGET = 128 * 1024
TOTAL_BUDGET = 2_095_296
FACE_NAMES = ["Angular Oval", "Soft Square", "Round Youthful", "Heart Taper", "Long Rectangle", "Diamond Angular", "Broad Strong", "Soft Triangle", "Narrow Refined", "Mature Sculpted"]
OUTFIT_NAMES = ["Utility Bomber", "Operations Jacket", "Streetline Hoodie", "Metro Varsity", "Civic Blazer", "Workshop Vest", "Signal Knit", "Denim Commuter", "Rainline Shell", "Gala Tailored"]
HAIR_NAMES = ["Angular Crop", "Asymmetric Bob", "Textured Quiff", "Curly Fade", "Classic Side Part", "Short Coils", "Shoulder Waves", "Braided Crown", "High Bun", "Long Straight"]
SKINS = {"porcelain": (239, 202, 173), "fair": (232, 181, 139), "warm": (224, 158, 100), "golden": (202, 141, 82), "olive": (173, 128, 83), "bronze": (161, 96, 62), "deep": (137, 83, 56), "ebony": (91, 53, 42)}
HAIR_COLORS = {"graphite": (104, 116, 124), "copper": (176, 83, 43), "raven": (40, 48, 58), "espresso": (80, 55, 47), "chestnut": (142, 90, 60), "golden": (209, 164, 79), "platinum": (216, 204, 176), "city-teal": (45, 132, 138), "ash-brown": (112, 92, 82), "auburn": (139, 54, 42), "mahogany": (104, 42, 44), "honey-blonde": (200, 151, 78), "strawberry-blonde": (213, 139, 92), "silver": (166, 178, 188), "snow-white": (235, 238, 234), "burgundy": (117, 37, 63), "rose-pink": (194, 85, 119), "violet": (111, 78, 160), "cobalt-blue": (55, 93, 168), "emerald": (48, 125, 91)}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def round_up(numerator, denominator):
    return (numerator + denominator // 2) // denominator


def encode_samples(raw: bytes, stride: int = 5) -> bytes:
    if not raw or len(raw) % stride:
        raise ValueError("sample data is empty or not pixel aligned")
    pixels = [raw[i:i + stride] for i in range(0, len(raw), stride)]
    result = bytearray()
    i = 0
    while i < len(pixels):
        run = 1
        while i + run < len(pixels) and run < 0x7FFF and pixels[i + run] == pixels[i]:
            run += 1
        if run >= 3:
            result += struct.pack("<H", 0x8000 | run) + pixels[i]
            i += run
        else:
            start = i
            i += run
            while i < len(pixels) and i - start < 0x7FFF:
                if i + 2 < len(pixels) and pixels[i] == pixels[i + 1] == pixels[i + 2]:
                    break
                i += 1
            result += struct.pack("<H", i - start) + b"".join(pixels[start:i])
    return bytes(result)


def decode_component(payload: bytes) -> tuple[np.ndarray, dict]:
    if len(payload) < HEADER.size:
        raise ValueError("truncated header")
    magic, schema, kind, preset, encoding, cw, ch, x, y, w, h, decoded, encoded, crc = HEADER.unpack_from(payload)
    if magic != b"GAVC" or schema != 2 or encoding != 2 or kind not in KINDS.values():
        raise ValueError("unsupported component header")
    if not 1 <= preset <= (1 if kind == 4 else 10):
        raise ValueError("invalid preset")
    if (cw, ch) != PREVIEW or not w or not h or x + w > cw or y + h > ch:
        raise ValueError("invalid dimensions")
    if decoded != w * h * 5 or encoded != len(payload) - HEADER.size:
        raise ValueError("invalid byte count")
    raw = bytearray()
    offset = HEADER.size
    while offset < len(payload):
        if offset + 2 > len(payload):
            raise ValueError("truncated token")
        token, = struct.unpack_from("<H", payload, offset)
        offset += 2
        count = token & 0x7FFF
        if not count or len(raw) + count * 5 > decoded:
            raise ValueError("invalid run length")
        size = 5 if token & 0x8000 else count * 5
        if offset + size > len(payload):
            raise ValueError("truncated sample data")
        part = payload[offset:offset + size]
        raw += part * count if token & 0x8000 else part
        offset += size
    if len(raw) != decoded or zlib.crc32(raw) & 0xFFFFFFFF != crc:
        raise ValueError("decoded length or CRC mismatch")
    image = np.zeros((ch, cw, 5), dtype=np.uint8)
    image[y:y + h, x:x + w] = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 5)
    return image, {"kindId": kind, "presetId": preset, "x": x, "y": y, "width": w, "height": h}


def component_bytes(kind: str, preset: int, rgba: Image.Image, mask: Image.Image) -> tuple[bytes, dict]:
    samples = np.dstack((np.array(rgba.crop(CROP)), np.array(mask.crop(CROP))))
    box = Image.fromarray(samples[:, :, 3]).getbbox()
    if box is None:
        raise ValueError("empty component")
    x, y, right, bottom = box
    raw = samples[y:bottom, x:right].tobytes()
    encoded = encode_samples(raw)
    crc = zlib.crc32(raw) & 0xFFFFFFFF
    header = HEADER.pack(b"GAVC", 2, KINDS[kind], preset, 2, *PREVIEW, x, y, right - x, bottom - y, len(raw), len(encoded), crc)
    payload = header + encoded
    decoded, _ = decode_component(payload)
    if not np.array_equal(decoded, samples):
        raise AssertionError("GAVC roundtrip changed material samples")
    return payload, {"kind": kind, "kindId": KINDS[kind], "presetId": preset, "x": x, "y": y, "width": right - x, "height": bottom - y, "decodedBytes": len(raw), "encodedBytes": len(encoded), "crc32": f"{crc:08x}", "sampleSha256": digest(raw), "fileBytes": len(payload), "fileSha256": digest(payload)}


def largest_component(binary: np.ndarray) -> np.ndarray:
    """Row-run union-find: deterministic, without an optional image library."""
    parent: list[int] = []
    runs: list[tuple[int, int, int, int]] = []
    previous = []

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for y, row in enumerate(binary):
        transitions = np.diff(np.pad(row.astype(np.int8), (1, 1)))
        starts = np.flatnonzero(transitions == 1)
        ends = np.flatnonzero(transitions == -1)
        current = []
        for left, right in zip(starts, ends):
            label = len(parent)
            parent.append(label)
            for pleft, pright, plabel in previous:
                if pright >= left and pleft <= right:
                    parent[find(plabel)] = find(label)
            current.append((int(left), int(right), label))
            runs.append((y, int(left), int(right), label))
        previous = current
    if not runs:
        raise ValueError("no opaque artwork above alpha threshold")
    sizes = defaultdict(int)
    for y, left, right, label in runs:
        sizes[find(label)] += right - left
    selected = max(sizes, key=lambda key: (sizes[key], -key))
    result = np.zeros(binary.shape, dtype=np.uint8)
    for y, left, right, label in runs:
        if find(label) == selected:
            result[y, left:right] = 255
    return result


def clean_source(path: Path, threshold: int = 32) -> tuple[Image.Image, dict]:
    original = Image.open(path).convert("RGBA")
    array = np.array(original)
    main = largest_component(array[:, :, 3] >= threshold)
    bounds = Image.fromarray(main).getbbox()
    # Keep local antialias fringe, discard disconnected noise, never invent RGB.
    dilation = np.array(Image.fromarray(main).filter(ImageFilter.MaxFilter(7))) > 0
    remove = (~dilation) | (array[:, :, 3] < 4)
    removed = int(np.count_nonzero((array[:, :, 3] > 0) & remove))
    array[remove] = 0
    return Image.fromarray(array), {"sourceSize": list(original.size), "alphaThreshold": threshold, "mainComponentBounds": list(bounds), "discardedNonzeroAlphaPixels": removed, "cleanup": "largest 8-connected alpha>=threshold component; retain 3px adjacent antialias; alpha<4 cleared"}


def register_box(image: Image.Image, source_box, target_box, y_anchors=None) -> Image.Image:
    sx0, sy0, sx1, sy1 = source_box
    tx0, ty0, tx1, ty1 = target_box
    if not (0 <= tx0 < tx1 <= 320 and 0 <= ty0 < ty1 <= 320):
        raise ValueError("target box outside device canvas")
    result = Image.new("RGBA", CANVAS)
    anchors = [(sy0, ty0)] + [tuple(v) for v in (y_anchors or [])] + [(sy1, ty1)]
    if any(a[0] >= b[0] or a[1] >= b[1] for a, b in zip(anchors, anchors[1:])):
        raise ValueError("vertical landmarks must be strictly increasing")
    for (source_top, target_top), (source_bottom, target_bottom) in zip(anchors, anchors[1:]):
        strip = image.crop((sx0, source_top, sx1, source_bottom))
        strip = strip.resize((tx1 - tx0, target_bottom - target_top), Image.Resampling.LANCZOS)
        result.paste(strip, (tx0, target_top))
    return result


def normalize_face(image: Image.Image, template: Image.Image) -> tuple[Image.Image, dict]:
    """Map each artwork row to F01's exact silhouette; facial RGB remains source art."""
    source = np.array(image)
    alpha = np.array(template.getchannel("A"))
    result = np.zeros_like(source)
    ratios = []
    for y in range(320):
        src = np.flatnonzero(source[y, :, 3] >= 16)
        dst = np.flatnonzero(alpha[y] >= 16)
        if not len(dst):
            continue
        if not len(src):
            raise ValueError(f"face has no material for common silhouette row {y}")
        sx0, sx1 = int(src[0]), int(src[-1]) + 1
        dx0, dx1 = int(dst[0]), int(dst[-1]) + 1
        # One pixel of adjacent RGB supplies the antialias boundary without a white matte.
        sx0, sx1 = max(0, sx0 - 2), min(320, sx1 + 2)
        dx0, dx1 = max(0, dx0 - 2), min(320, dx1 + 2)
        strip = image.crop((sx0, y, sx1, y + 1)).resize((dx1 - dx0, 1), Image.Resampling.BICUBIC)
        result[y:y + 1, dx0:dx1] = np.array(strip)
        ratios.append((dx1 - dx0) / (sx1 - sx0))
    result[:, :, 3] = alpha
    # A nonzero target alpha must have actual source colour after row registration.
    result[alpha == 0] = 0
    if ratios and (min(ratios) < 0.45 or max(ratios) > 2.2):
        raise ValueError(f"face registration exceeds bounded row resampling: {min(ratios):.3f}..{max(ratios):.3f}")
    return Image.fromarray(result), {"method": "F01 common alpha; per-row RGB resample", "minimumRowScale": min(ratios), "maximumRowScale": max(ratios), "alphaSha256": digest(alpha.tobytes())}


# The ear-inclusive alpha bounding box is deliberately NOT this contour. These
# head-core landmarks were measured from F01's dark scalp/temple/jaw outline.
# Values remain explicit, reviewable device coordinates and may be overridden.
HEAD_CORE_CONTOUR = [[90, 115, 204], [100, 112, 207], [110, 110, 209],
    [120, 109, 210], [130, 110, 209], [140, 110, 209], [150, 111, 208],
    [160, 112, 207], [170, 114, 205], [180, 117, 202]]


def interpolate_anchors(anchors, y):
    if not anchors or any(a[0] >= b[0] for a, b in zip(anchors, anchors[1:])):
        raise ValueError("hair interface anchors must have strictly increasing y")
    if y <= anchors[0][0]:
        return tuple(float(v) for v in anchors[0][1:])
    if y >= anchors[-1][0]:
        return tuple(float(v) for v in anchors[-1][1:])
    for first, second in zip(anchors, anchors[1:]):
        if first[0] <= y <= second[0]:
            ratio = (y - first[0]) / (second[0] - first[0])
            return tuple(a + (b - a) * ratio for a, b in zip(first[1:], second[1:]))
    raise AssertionError("unreachable anchor interval")


def hair_row_edges(alpha, y, threshold=128, center_x=160):
    occupied = np.flatnonzero(alpha[y] >= threshold)
    left, right = occupied[occupied < center_x], occupied[occupied >= center_x]
    return {"y": y, "leftOuter": int(left[0]) if len(left) else None,
        "leftInner": int(left[-1]) if len(left) else None,
        "rightInner": int(right[0]) if len(right) else None,
        "rightOuter": int(right[-1]) if len(right) else None,
        "centerCovered": bool(alpha[y, center_x] >= threshold)}


def register_hair_interface(image: Image.Image, config: dict) -> tuple[Image.Image, dict]:
    """Map existing side-lock artwork to the shared head core, not ear bounds.

    Keep all y positions and the centre line. A long style can preserve its outer
    silhouette/length while moving only its inner side edges. Compact styles may
    additionally provide explicit outer anchors; the top does not move vertically.
    No absent side lock is synthesized and no face/neck/outfit pixels are changed.
    """
    contour = config.get("headCoreContour", HEAD_CORE_CONTOUR)
    strengths = config["strengthAnchors"]
    outer_anchors = config.get("outerAnchors")
    overlap = float(config.get("overlapPixels", 2))
    max_shift = float(config.get("maximumInnerShift", 24))
    result = image.copy()
    alpha = np.array(image.getchannel("A"))
    evidence = []
    for y in range(320):
        strength, = interpolate_anchors(strengths, y)
        if not 0 <= strength <= 1:
            raise ValueError("hair interface strength must be within 0..1")
        if strength == 0:
            continue
        row = hair_row_edges(alpha, y)
        if row["leftOuter"] is None or row["rightOuter"] is None:
            continue
        left_core, right_core = interpolate_anchors(contour, y)
        new_li = row["leftInner"]
        new_ri = row["rightInner"]
        if not row["centerCovered"]:
            left_goal, right_goal = left_core + overlap, right_core - overlap
            if abs(left_goal - new_li) > max_shift or abs(right_goal - new_ri) > max_shift:
                raise ValueError(f"hair interior shift exceeds {max_shift}px at row {y}")
            new_li = round(new_li + (left_goal - new_li) * strength)
            new_ri = round(new_ri + (right_goal - new_ri) * strength)
        new_lo, new_ro = row["leftOuter"], row["rightOuter"]
        if outer_anchors:
            goal_lo, goal_ro = interpolate_anchors(outer_anchors, y)
            new_lo = round(new_lo + (goal_lo - new_lo) * strength)
            new_ro = round(new_ro + (goal_ro - new_ro) * strength)
        if not 0 <= new_lo <= new_li < 160 <= new_ri <= new_ro < 320:
            raise ValueError(f"invalid hair edge ordering at row {y}")
        if row["centerCovered"]:
            # Compact top width may change while the midpoint and entire y axis stay fixed.
            ranges = [(max(0, row["leftOuter"] - 2), 160, max(0, new_lo - 2), 160),
                (160, min(320, row["rightOuter"] + 3), 160, min(320, new_ro + 3))]
        else:
            ranges = [(max(0, row["leftOuter"] - 2), min(160, row["leftInner"] + 3), max(0, new_lo - 2), min(160, new_li + 3)),
                (max(160, row["rightInner"] - 2), min(320, row["rightOuter"] + 3), max(160, new_ri - 2), min(320, new_ro + 3))]
        result.paste((0, 0, 0, 0), (0, y, 320, y + 1))
        for sx0, sx1, tx0, tx1 in ranges:
            if sx1 <= sx0 or tx1 <= tx0:
                raise ValueError("degenerate hair side-lock resample")
            scale = (tx1 - tx0) / (sx1 - sx0)
            if not config.get("minimumRowScale", .35) <= scale <= config.get("maximumRowScale", 3.5):
                raise ValueError(f"hair side-lock row scale outside bounds at y={y}: {scale:.3f}")
            strip = image.crop((sx0, y, sx1, y + 1)).resize((tx1 - tx0, 1), Image.Resampling.BICUBIC)
            result.paste(strip, (tx0, y))
        evidence.append({**row, "strength": strength, "targetLeftInner": new_li,
            "targetRightInner": new_ri, "targetLeftOuter": new_lo, "targetRightOuter": new_ro})
    return result, {"method": "independent side-lock inner edges registered to F01 head core; center/y preserved",
        "config": config, "rows": evidence, "inputAlphaBounds": image.getchannel("A").getbbox(),
        "outputAlphaBounds": result.getchannel("A").getbbox()}


def audit_hair_interface(hair: Image.Image, face: Image.Image, config: dict):
    """Report head-core gaps, actual transparent slits, and excessive cheek overlap."""
    ha = np.array(hair.getchannel("A")) >= 128
    fa = np.array(face.getchannel("A")) >= 128
    contour = config.get("headCoreContour", HEAD_CORE_CONTOUR)
    rows = config.get("auditRows", [110, 151])
    minimum_overlap = int(config.get("minimumOverlap", 1))
    maximum_overlap = int(config.get("maximumOverlap", 5))
    result = []
    debug = np.zeros((320, 320, 4), dtype=np.uint8)
    debug[fa] = (170, 115, 70, 220)
    debug[ha] = (60, 145, 210, 220)
    for y in range(*rows):
        row = hair_row_edges(ha.astype(np.uint8) * 255, y)
        left, right = (round(v) for v in interpolate_anchors(contour, y))
        debug[y, left] = debug[y, right] = (70, 255, 160, 255)
        if row["centerCovered"]:
            result.append({**row, "leftCore": left, "rightCore": right, "status": "cap covers center"})
            continue
        if row["leftInner"] is None or row["rightInner"] is None:
            result.append({**row, "leftCore": left, "rightCore": right, "status": "missing side lock"})
            continue
        li, ri = row["leftInner"], row["rightInner"]
        left_overlap, right_overlap = li - left + 1, right - ri + 1
        left_gap, right_gap = max(0, -left_overlap), max(0, -right_overlap)
        actual_slits = 0
        for x0, x1 in ((li + 1, left), (right + 1, ri)):
            if x1 > x0:
                actual_slits += int((~(ha[y, x0:x1] | fa[y, x0:x1])).sum())
                debug[y, x0:x1] = (255, 35, 65, 255)
        if left_overlap > maximum_overlap:
            debug[y, left:li + 1] = (255, 210, 35, 255)
        if right_overlap > maximum_overlap:
            debug[y, ri:right + 1] = (255, 210, 35, 255)
        valid = minimum_overlap <= left_overlap <= maximum_overlap and minimum_overlap <= right_overlap <= maximum_overlap
        result.append({**row, "leftCore": left, "rightCore": right, "leftGapPixels": left_gap,
            "rightGapPixels": right_gap, "leftOverlapPixels": left_overlap, "rightOverlapPixels": right_overlap,
            "actualTransparentSlitPixels": actual_slits, "status": "passed" if valid else "opening mismatch"})
    failures = [row for row in result if row["status"] in ("opening mismatch", "missing side lock")]
    return {"config": config, "checkedRows": len(result), "failureRows": len(failures),
        "missingSideLockRows": sum(r["status"] == "missing side lock" for r in result),
        "actualTransparentSlitPixels": sum(r.get("actualTransparentSlitPixels", 0) for r in result),
        "rows": result}, Image.fromarray(debug)


def neckline_measure(image: Image.Image, center_x: int = 160, threshold: int = 128) -> dict:
    alpha = np.array(image.getchannel("A"))
    rows = []
    for y in range(320):
        left = np.flatnonzero(alpha[y, :center_x] >= threshold)
        right = np.flatnonzero(alpha[y, center_x + 1:] >= threshold)
        if len(left) and len(right) and alpha[y, center_x] < threshold:
            rows.append((y, int(left[-1]), int(right[0] + center_x + 1)))
    if not rows:
        raise ValueError("no centered transparent neckline was found")
    top = rows[0][0]
    contiguous = [rows[0]]
    for row in rows[1:]:
        if row[0] != contiguous[-1][0] + 1:
            break
        contiguous.append(row)
    return {"topY": top, "bottomY": contiguous[-1][0] + 1, "rows": [list(row) for row in contiguous]}


def normalize_neckline(image: Image.Image, target: dict) -> tuple[Image.Image, dict]:
    """Register authored collar edges to a common opening, preserving collar art.

    This is an optional geometric warp, not a semantic proof of outfit quality.
    A generated jacket must already face front before this operation.
    """
    measured = neckline_measure(image)
    source_top, source_bottom = measured["topY"], measured["bottomY"]
    target_top, target_bottom = target["topY"], target["bottomY"]
    if not 0 <= target_top < target_bottom < 320:
        raise ValueError("invalid target neckline")
    box = image.getchannel("A").getbbox()
    y0 = min(box[1], source_top)
    # Preserve an authored tall hood above the collar; translate it with the collar top.
    top_target = max(0, y0 + target_top - source_top)
    anchors = [(y0, top_target)]
    if source_top > y0 and target_top > top_target:
        anchors.append((source_top, target_top))
    anchors += [(source_bottom, target_bottom), (320, 320)]
    vertical = Image.new("RGBA", CANVAS)
    for (sy0, ty0), (sy1, ty1) in zip(anchors, anchors[1:]):
        if sy1 > sy0 and ty1 > ty0:
            vertical.paste(image.crop((0, sy0, 320, sy1)).resize((320, ty1 - ty0), Image.Resampling.BICUBIC), (0, ty0))
    result = vertical.copy()
    rows = {r[0]: r for r in neckline_measure(vertical)["rows"]}
    target_rows = target.get("rows")
    for y in range(target_top, target_bottom):
        if y not in rows:
            continue
        _, left, right = rows[y]
        fraction = (y - target_top) / max(1, target_bottom - target_top - 1)
        if target_rows:
            row = min(target_rows, key=lambda r: abs(r[0] - y))
            desired_left, desired_right = row[1], row[2]
        else:
            half = max(1, round((target["rightX"] - target["leftX"]) / 2 * (1 - fraction ** 2)))
            desired_left, desired_right = 160 - half, 160 + half
        # Left and right artwork remain on their own side of the transparent opening.
        result.paste((0, 0, 0, 0), (0, y, 320, y + 1))
        if left >= 0 and desired_left >= 0:
            strip = vertical.crop((0, y, left + 1, y + 1)).resize((desired_left + 1, 1), Image.Resampling.BICUBIC)
            result.paste(strip, (0, y))
        if right < 320 and desired_right < 320:
            strip = vertical.crop((right, y, 320, y + 1)).resize((320 - desired_right, 1), Image.Resampling.BICUBIC)
            result.paste(strip, (desired_right, y))
    return result, {"source": measured, "target": target, "result": neckline_measure(result)}


def make_material(image: Image.Image, kind: str, config: dict, root: Path = ROOT) -> tuple[Image.Image, Image.Image, dict]:
    a = np.array(image).astype(np.int32)
    weight = round_up(54 * a[:, :, 0] + 183 * a[:, :, 1] + 19 * a[:, :, 2], 256)
    mask = np.zeros((320, 320), dtype=np.uint8)
    if kind not in ("face", "neck"):
        return image, Image.fromarray(mask), {"mode": "none", "reviewed": True}
    if config.get("mask"):
        path = (root / config["mask"]).resolve()
        mask_image = Image.open(path).convert("L")
        if mask_image.size != CANVAS:
            raise ValueError("explicit skin mask must be registered 320x320")
        mask = np.array(mask_image).copy()
        mode = "explicit registered mask"
    else:
        # Unlike V1, bright/desaturated skin is included. Only ink and explicit
        # facial feature ROIs are protected. The inferred mask remains review-required.
        mask[(a[:, :, 3] > 0) & (weight > config.get("inkLumaMax", 52))] = 255
        for region in config.get("protectRegions", []):
            x0, y0, x1, y1 = region["box"]
            part = a[y0:y1, x0:x1, :3]
            local_luma = weight[y0:y1, x0:x1]
            if region.get("mode", "all") == "eye":
                neutral = part.max(axis=2) - part.min(axis=2) < 34
                protected = (neutral & (local_luma >= 145)) | (local_luma < 105) | (part[:, :, 2] > part[:, :, 0] + 12)
                mask[y0:y1, x0:x1][protected] = 0
            else:
                mask[y0:y1, x0:x1] = 0
        mode = "inferred geometry and eye ROI; manual audit required"
    mask[a[:, :, 3] == 0] = 0
    # Explicit shade is R=G=B on every positive material sample. Neutral shade
    # reference 178 matches the firmware/server integer tint contract.
    shade = np.clip(round_up(weight * 178, config.get("sourceLumaReference", 178)), config.get("minShade", 40), config.get("maxShade", 239))
    tinted = mask > 0
    for channel in range(3):
        a[:, :, channel][tinted] = shade[tinted]
    a[a[:, :, 3] == 0] = 0
    return Image.fromarray(a.astype(np.uint8)), Image.fromarray(mask), {"mode": mode, "reviewed": bool(config.get("reviewed", False)), "tintedPixels": int(np.count_nonzero(mask)), "protectedOpaquePixels": int(np.count_nonzero((mask == 0) & (a[:, :, 3] > 0))), "sourceLumaReference": config.get("sourceLumaReference", 178), "shadeReference": 178, "maskSha256": digest(mask.tobytes()), "protectRegions": config.get("protectRegions", [])}


def quantize(image: Image.Image, mask: Image.Image, rgb_step=8, alpha_step=17):
    a = np.array(image).astype(np.uint16)
    if rgb_step < 1 or alpha_step < 1:
        raise ValueError("quantization steps must be positive")
    a[:, :, :3] = np.minimum(255, round_up(a[:, :, :3], rgb_step) * rgb_step)
    a[:, :, 3] = np.minimum(255, round_up(a[:, :, 3], alpha_step) * alpha_step)
    a[a[:, :, 3] == 0] = 0
    material = np.array(mask).copy()
    material[a[:, :, 3] == 0] = 0
    return Image.fromarray(a.astype(np.uint8)), Image.fromarray(material)


def tint(image: Image.Image, mask: Image.Image, target, kind: str) -> Image.Image:
    a = np.array(image).astype(np.int64)
    if kind in ("face", "neck"):
        m = np.array(mask).astype(np.int64)
        shade = a[:, :, 0].copy()
        for channel, color in enumerate(target):
            value = np.minimum(255, round_up(color * shade, 178))
            a[:, :, channel] = round_up(value * m + a[:, :, channel] * (255 - m), 255)
    elif kind == "hair":
        weight = 54 * a[:, :, 0] + 183 * a[:, :, 1] + 19 * a[:, :, 2]
        active = (a[:, :, 3] > 0) & (weight >= 6528)
        numerator = 7 * 65280 + 21 * weight
        for channel, color in enumerate(target):
            a[:, :, channel][active] = np.minimum(255, round_up(color * numerator[active], 1305600))
    return Image.fromarray(a.astype(np.uint8))


def source_over(destination: np.ndarray, source: np.ndarray) -> np.ndarray:
    """Byte-for-byte integer contract shared with AvatarComponentMath on device."""
    d, s = destination.astype(np.int64), source.astype(np.int64)
    sa, da = s[:, :, 3], d[:, :, 3]
    inv = 255 - sa
    out_alpha = sa + round_up(da * inv, 255)
    result = np.zeros_like(d)
    result[:, :, 3] = out_alpha
    safe_alpha = np.maximum(1, out_alpha)
    for c in range(3):
        premultiplied = s[:, :, c] * sa + round_up(d[:, :, c] * da * inv, 255)
        result[:, :, c] = np.minimum(255, round_up(premultiplied, safe_alpha))
    # The C++ routine returns immediately when source alpha is zero.
    result[sa == 0] = d[sa == 0]
    return result.astype(np.uint8)


def compose(components, face=1, hair=1, outfit=1, skin="warm", hair_color="graphite") -> Image.Image:
    result = np.zeros((320, 320, 4), dtype=np.uint8)
    ids = {"face": face, "hair": hair, "outfit": outfit, "neck": 1}
    for kind in ORDER:
        image, mask = components[(kind, ids[kind])]
        color = HAIR_COLORS[hair_color] if kind == "hair" else SKINS[skin]
        layer = tint(image, mask, color, kind)
        result = source_over(result, np.array(layer))
    return Image.fromarray(result)


def legacy_hair(preset: int) -> tuple[Image.Image, Path]:
    directory = str(V1 / "tools")
    if directory not in sys.path:
        sys.path.insert(0, directory)
    from build_avatar_components import registered_neutral
    from build_avatar_prototype import HAIR
    key = f"H{preset:02d}"
    return registered_neutral("hair", key, HAIR[key]), HAIR[key]


def default_manifest() -> dict:
    entries = []
    for kind, names in (("face", FACE_NAMES), ("outfit", OUTFIT_NAMES), ("hair", HAIR_NAMES), ("neck", ["Shared Neck"])):
        for preset, name in enumerate(names, 1):
            key = f"{kind[0]}{preset:02d}" if kind != "neck" else "n1"
            entry = {"kind": kind, "presetId": preset, "name": name, "source": f"source/{kind}/{key}.png", "registration": {"reviewed": False}}
            if kind == "hair" and preset not in (6, 8, 10):
                entry["legacyHair"] = f"H{preset:02d}"
                entry.pop("source")
                entry["registration"] = {"method": "legacy-v1-approved-registration", "reviewed": True}
            elif kind == "hair":
                fits = {
                    6: ([99, 42, 221, 158], [[100, 0], [110, 1], [151, 1], [158, 0]], [110, 151]),
                    8: ([96, 45, 224, 176], [[96, 0], [112, 1], [166, 1], [176, 0]], [112, 166]),
                    10: ([84, 39, 236, 247], [[95, 0], [112, 1], [181, 1], [212, 0]], [112, 181]),
                }
                box, strengths, rows = fits[preset]
                if preset == 8:
                    entry["source"] = "source/hair/h08-fit2.png"
                entry["registration"].update({"method": "alpha-box", "targetBox": box,
                    "innerFit": {"strengthAnchors": strengths, "overlapPixels": 2,
                        "maximumInnerShift": 24, "auditRows": rows, "minimumOverlap": 1, "maximumOverlap": 5}})
            elif kind == "face":
                entry["registration"].update({"method": "common-face", "targetBox": [94, 67, 226, 218]})
                entry["skinMaterial"] = {"reviewed": False, "sourceLumaReference": 178, "protectRegions": [{"box": [118, 132, 151, 151], "mode": "eye"}, {"box": [169, 132, 202, 151], "mode": "eye"}]}
                if preset == 1:
                    entry["registration"]["yAnchors"] = [[600, 140]]
            elif kind == "neck":
                entry["registration"].update({"method": "alpha-box", "targetBox": [122, 193, 198, 255]})
                entry["skinMaterial"] = {"reviewed": False, "sourceLumaReference": 178}
            else:
                entry["registration"].update({"method": "neckline", "targetBox": [35, 206, 285, 320], "neckline": {"topY": 210, "bottomY": 242, "leftX": 124, "rightX": 196}})
            entries.append(entry)
    return {"schema": 2, "artRelease": "avatar-v2-draft", "status": "draft-unreviewed", "faceTemplate": 1, "quantization": {"rgbStep": 8, "alphaStep": 17}, "components": entries}
