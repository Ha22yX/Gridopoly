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

toast_match = re.search(
    r"void\s+drawToast\s*\([^)]*\)\s*\{(?P<body>.*?)\n\}",
    source,
    re.DOTALL,
)
if not toast_match:
    fail("drawToast() was not found")

toast_body = toast_match.group("body")
if re.search(r"nowMs\s*>=\s*state\.toastUntilMs", toast_body):
    fail("drawToast() uses a wrap-unsafe toast deadline comparison")
if not re.search(
    r"static_cast\s*<\s*int32_t\s*>\s*\(\s*nowMs\s*-\s*state\.toastUntilMs\s*\)\s*>=\s*0",
    toast_body,
):
    fail("drawToast() does not use a signed elapsed-time deadline comparison")

print(
    "UI LAYOUT CHECK PASS: "
    f"layout refresh precedes coordinate read; button centers={centers}; toast deadline is wrap safe"
)
