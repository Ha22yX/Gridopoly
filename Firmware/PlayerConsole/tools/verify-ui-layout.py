#!/usr/bin/env python3
"""Guard the round-screen focus animation against pre-layout coordinates."""

from pathlib import Path
import re
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = PROJECT_ROOT / "ui_renderer.cpp"


def fail(message: str) -> None:
    print(f"UI LAYOUT CHECK FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


source = SOURCE_PATH.read_text(encoding="utf-8")

focus_match = re.search(
    r"void\s+animateFocusEntry\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
    source,
    re.DOTALL,
)
if not focus_match:
    fail("animateFocusEntry() was not found")

focus_body = focus_match.group("body")
layout_refresh = focus_body.find("lv_obj_update_layout(obj);")
coordinate_read = focus_body.find("lv_obj_get_x(obj)")
if layout_refresh < 0:
    fail("animateFocusEntry() does not refresh LVGL layout")
if coordinate_read < 0:
    fail("animateFocusEntry() does not read the target x coordinate")
if layout_refresh > coordinate_read:
    fail("animateFocusEntry() reads x before refreshing LVGL layout")

positions_match = re.search(
    r"const\s+int16_t\s+xs\[3\]\s*=\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}",
    source,
)
if not positions_match:
    fail("home-wheel button positions were not found")

positions = [int(value) for value in positions_match.groups()]
button_width = 66
centers = [position + button_width // 2 for position in positions]
if centers[1] != 240:
    fail(f"selected home-wheel button is centered at x={centers[1]}, expected 240")
if 240 - centers[0] != centers[2] - 240:
    fail(f"home-wheel side buttons are not symmetric: centers={centers}")

print(
    "UI LAYOUT CHECK PASS: "
    f"layout refresh precedes coordinate read; button centers={centers}"
)
