#!/usr/bin/env python3
"""Verify static layout contracts for the 480px round player console."""

from pathlib import Path
import re
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LAYOUT_PATH = PROJECT_ROOT / "ui_layout.h"
RENDERER_PATH = PROJECT_ROOT / "ui_renderer.cpp"
RADIUS = 192
CENTER = 240


def fail(message: str) -> None:
    print(f"UI LAYOUT CHECK FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def code_only(source: str) -> str:
    """Remove comments and literals while retaining source offsets and braces."""
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "code" and char == "/" and next_char == "/":
            result.extend("  ")
            index += 2
            state = "line_comment"
        elif state == "code" and char == "/" and next_char == "*":
            result.extend("  ")
            index += 2
            state = "block_comment"
        elif state == "code" and char in "\"'":
            result.append(" ")
            index += 1
            state = "string"
        elif state == "line_comment":
            result.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                result.extend("  ")
                index += 2
                state = "code"
            else:
                result.append("\n" if char == "\n" else " ")
                index += 1
        elif state == "string":
            if char == "\\":
                result.append(" ")
                index += 1
                if index < len(source):
                    result.append("\n" if source[index] == "\n" else " ")
                    index += 1
            else:
                result.append("\n" if char == "\n" else " ")
                index += 1
                if char in "\"'":
                    state = "code"
        else:
            result.append(char)
            index += 1
    return "".join(result)


def block_after(source: str, match: re.Match[str]) -> str:
    open_brace = source.find("{", match.start(), match.end() + 1)
    if open_brace < 0:
        fail("expected block opening brace was not found")
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    fail("expected block closing brace was not found")


def read_rectangles(source: str) -> dict[str, tuple[int, int, int, int]]:
    rectangles: dict[str, tuple[int, int, int, int]] = {}
    pattern = re.compile(
        r"\bconstexpr\s+UiRect\s+(k[A-Za-z0-9_]+)\s*\{\s*"
        r"(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*;"
    )
    for match in pattern.finditer(source):
        rectangles[match.group(1)] = tuple(int(match.group(index)) for index in range(2, 6))
    return rectangles


def assert_inside_circle(name: str, rect: tuple[int, int, int, int]) -> None:
    x, y, width, height = rect
    for corner_x in (x, x + width):
        for corner_y in (y, y + height):
            if (corner_x - CENTER) ** 2 + (corner_y - CENTER) ** 2 > RADIUS ** 2:
                fail(f"{name} corner ({corner_x}, {corner_y}) is outside the {RADIUS}px safe circle")


layout_source = LAYOUT_PATH.read_text(encoding="utf-8")
renderer_source = code_only(RENDERER_PATH.read_text(encoding="utf-8"))
rectangles = read_rectangles(code_only(layout_source))

required = (
    "kNormalListFocus",
    "kNormalListProgress",
    "kNormalListCount",
    "kNormalFooter",
    "kModalRect",
    "kModalConfirm",
    "kModalCancel",
)
for name in required:
    if name not in rectangles:
        fail(f"{name} rectangle was not found in ui_layout.h")
    assert_inside_circle(name, rectangles[name])

footer_x, footer_y, footer_width, footer_height = rectangles["kNormalFooter"]
if footer_x + footer_width // 2 != CENTER:
    fail(f"footer center is x={footer_x + footer_width // 2}, expected {CENTER}")
if footer_width > 176 or footer_y + footer_height > 408:
    fail("footer exceeds its approved round-safe bounds")

for name in ("kNormalListFocus", "kNormalListProgress", "kNormalListCount", "kNormalFooter"):
    x, _, width, _ = rectangles[name]
    if x + width // 2 != CENTER:
        fail(f"{name} center is x={x + width // 2}, expected {CENTER}")

confirm_y = rectangles["kModalConfirm"][1]
confirm_height = rectangles["kModalConfirm"][3]
cancel_y = rectangles["kModalCancel"][1]
if cancel_y - (confirm_y + confirm_height) < 8:
    fail("modal confirm and cancel controls are less than 8px apart")

focus_match = re.search(r"\bvoid\s+animateFocusEntry\s*\([^)]*\)\s*\{", renderer_source)
if not focus_match:
    fail("animateFocusEntry() was not found")
focus_body = block_after(renderer_source, focus_match)
layout_refresh = focus_body.find("lv_obj_update_layout(obj);")
coordinate_read = focus_body.find("lv_obj_get_x(obj)")
if layout_refresh < 0 or coordinate_read < 0 or layout_refresh > coordinate_read:
    fail("animateFocusEntry() does not refresh layout before reading x")

positions_match = re.search(
    r"\bconst\s+int16_t\s+xs\s*\[\s*3\s*\]\s*=\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*;",
    renderer_source,
)
if not positions_match:
    fail("home-wheel button positions were not found")
positions = [int(value) for value in positions_match.groups()]
centers = [position + 33 for position in positions]
if centers[1] != CENTER or CENTER - centers[0] != centers[2] - CENTER:
    fail(f"home-wheel buttons are not centered and symmetric: centers={centers}")

toast_match = re.search(r"\bvoid\s+drawToast\s*\([^)]*\)\s*\{", renderer_source)
if not toast_match:
    fail("drawToast() was not found")
toast_body = block_after(renderer_source, toast_match)
if re.search(r"\bnowMs\s*>=\s*state\.toastUntilMs\b", toast_body):
    fail("drawToast() uses a wrap-unsafe toast deadline comparison")
if not re.search(
    r"static_cast\s*<\s*int32_t\s*>\s*\(\s*nowMs\s*-\s*state\.toastUntilMs\s*\)\s*>=\s*0",
    toast_body,
):
    fail("drawToast() does not use a signed elapsed-time deadline comparison")

render_match = re.search(r"\bvoid\s+uiRendererRender\s*\([^)]*\)\s*\{", renderer_source)
if not render_match:
    fail("uiRendererRender() was not found")
render_body = block_after(renderer_source, render_match)
same_page_match = re.search(
    r"\bif\s*\(\s*hasRenderedState\s*&&\s*state\.page\s*==\s*previousPage\s*&&\s*"
    r"state\.focus\s*!=\s*previousFocus\s*\)\s*\{",
    render_body,
)
if not same_page_match:
    fail("same-page focus-update branch was not found")
same_page_body = block_after(render_body, same_page_match)
if re.search(r"\blv_obj_clean\s*\(\s*root\s*\)", same_page_body):
    fail("same-page focus update clears the root")
if re.search(r"\brebuild\s*\(", same_page_body):
    fail("same-page focus update enters the root rebuilding path")

print(
    "UI LAYOUT CHECK PASS: round-safe rectangles, centered normal controls, "
    "modal separation, focus layout ordering, wrap-safe toast, and retained same-page focus updates"
)
