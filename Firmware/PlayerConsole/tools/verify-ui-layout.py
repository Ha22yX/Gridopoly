#!/usr/bin/env python3
"""Verify the compiled-source contracts for the 480px round player console."""

from pathlib import Path
import re
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LAYOUT_PATH = PROJECT_ROOT / "ui_layout.h"
RENDERER_PATH = PROJECT_ROOT / "ui_renderer.cpp"
RADIUS = 192
CENTER = 240
EXPECTED_RECTANGLES = {
    "kNormalListViewport": (78, 128, 324, 184),
    "kNormalListFocus": (104, 195, 272, 50),
    "kNormalListProgress": (168, 320, 144, 8),
    "kNormalListCount": (202, 104, 76, 24),
    "kNormalFooter": (152, 352, 176, 56),
    "kModalRect": (104, 106, 272, 268),
    "kModalConfirm": (128, 256, 224, 64),
    "kModalCancel": (152, 328, 176, 40),
}


class LayoutError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise LayoutError(message)


def code_only(source: str) -> str:
    """Remove comments and literals while retaining source offsets and braces."""
    result: list[str] = []
    index = 0
    state = "code"
    quote = ""
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
            quote = char
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
                if char == quote:
                    state = "code"
        else:
            result.append(char)
            index += 1
    return "".join(result)


def splice_translation_lines(source: str) -> str:
    """Apply C/C++ translation-phase backslash-newline splicing."""
    return re.sub(r"\\(?:\r\n|\r|\n)", "", source)


def normalized_code(source: str) -> str:
    return code_only(splice_translation_lines(source))


def normalized_preprocessor_code(source: str) -> str:
    # Comments and literals are already blank, so only real %: digraphs remain.
    return normalized_code(source).replace("%:", "#")


def block_after(source: str, match: re.Match) -> str:
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


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\bvoid\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    if not match:
        fail(f"{name}() was not found")
    return block_after(source, match)


def reject_inactive_blocks(source: str, path: str) -> None:
    directive = re.search(
        r"(?m)^[ \t]*#[ \t]*(?:if|ifdef|ifndef|elif|else|endif)\b",
        normalized_preprocessor_code(source),
    )
    if directive:
        fail(f"{path} contains conditional preprocessing: {directive.group(0).strip()}")


def read_rectangles(source: str) -> dict[str, list[tuple[int, int, int, int]]]:
    rectangles: dict[str, list[tuple[int, int, int, int]]] = {}
    pattern = re.compile(
        r"\bconstexpr\s+UiRect\s+(k[A-Za-z0-9_]+)\s*\{\s*"
        r"(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*;"
    )
    for match in pattern.finditer(source):
        rectangles.setdefault(match.group(1), []).append(
            tuple(int(match.group(index)) for index in range(2, 6))
        )
    return rectangles


def assert_inside_circle(name: str, rect: tuple[int, int, int, int]) -> None:
    x, y, width, height = rect
    for corner_x in (x, x + width):
        for corner_y in (y, y + height):
            if (corner_x - CENTER) ** 2 + (corner_y - CENTER) ** 2 > RADIUS ** 2:
                fail(f"{name} corner ({corner_x}, {corner_y}) is outside the {RADIUS}px safe circle")


def assert_modal_contract(renderer: str) -> None:
    modal_body = function_body(renderer, "drawModal")
    required_calls = (
        (r"\buiBox\s*\(\s*root\s*,\s*kModalRect\b", "kModalRect is not used for the modal"),
        (r"\buiBox\s*\(\s*modal\s*,\s*modalChildRect\s*\(\s*kModalConfirm\s*\)",
         "kModalConfirm is not used for the confirmation control"),
        (r"\buiLabel\s*\(\s*modal\s*,\s*state\.modal\.title\s*,\s*modalChildRect\s*\(\s*kModalTitle\s*\)",
         "modal title is not placed from its bounded child rectangle"),
        (r"\buiLabel\s*\(\s*modal\s*,\s*amount\s*,\s*modalChildRect\s*\(\s*kModalAmount\s*\)",
         "modal amount is not placed from its bounded child rectangle"),
        (r"\buiLabel\s*\(\s*modal\s*,\s*detail\s*,\s*modalChildRect\s*\(\s*kModalDetail\s*\)",
         "modal detail is not placed from its bounded child rectangle"),
        (r"\buiLabel\s*\(\s*hold\s*,[^;]*\bchildRect\s*\(\s*kModalConfirmLabel\s*,\s*kModalConfirm\s*\)",
         "modal confirmation label is not bounded by kModalConfirm"),
        (r"\bkModalConfirm\.w\s*\*\s*progress\s*/\s*1000", "modal progress does not use confirm width"),
        (r"\bUiRect\s*\{\s*0\s*,\s*static_cast\s*<\s*int16_t\s*>\s*\(\s*kModalConfirm\.h\s*-\s*6\s*\)[^}]*\b6\s*\}",
         "modal progress overlaps the confirmation label"),
    )
    for pattern, message in required_calls:
        if not re.search(pattern, modal_body):
            fail(message)

    if re.search(r"\bif\s*\(\s*state\.modal\.cancelAllowed\b", modal_body):
        cancel_match = re.search(
            r"\buiBox\s*\(\s*modal\s*,\s*modalChildRect\s*\(\s*kModalCancel\s*\)",
            modal_body,
        )
        if not cancel_match:
            fail("visible modal cancel path does not use kModalCancel")

    legacy_geometries = (
        r"\b(?:box|uiBox)\s*\(\s*root\s*,\s*70\s*,\s*88\s*,\s*340\s*,\s*304\b",
        r"\b(?:box|uiBox)\s*\(\s*modal\s*,\s*34\s*,\s*196\s*,\s*272\s*,\s*72\b",
    )
    for pattern in legacy_geometries:
        if re.search(pattern, modal_body):
            fail("drawModal() retains legacy unsafe modal geometry")


def verify_contract(layout_source: str, renderer_source: str) -> None:
    reject_inactive_blocks(layout_source, "ui_layout.h")
    reject_inactive_blocks(renderer_source, "ui_renderer.cpp")
    layout_code = normalized_code(layout_source)
    renderer_code = normalized_code(renderer_source)
    draw_modal_definitions = re.findall(r"\bvoid\s+drawModal\s*\([^)]*\)\s*\{", renderer_code)
    if len(draw_modal_definitions) != 1:
        fail(f"ui_renderer.cpp requires exactly one drawModal() definition, found {len(draw_modal_definitions)}")
    rectangle_definitions = read_rectangles(layout_code)

    rectangles: dict[str, tuple[int, int, int, int]] = {}
    for name, expected in EXPECTED_RECTANGLES.items():
        definitions = rectangle_definitions.get(name, [])
        if len(definitions) != 1:
            fail(f"{name} requires exactly one active rectangle definition")
        if definitions[0] != expected:
            fail(f"{name} is {definitions[0]}, expected {expected}")
        rectangles[name] = definitions[0]

    for name in ("kNormalFooter", "kModalRect", "kModalConfirm", "kModalCancel"):
        assert_inside_circle(name, rectangles[name])

    for name in EXPECTED_RECTANGLES:
        if not re.search(rf"\bstatic_assert\s*\([^;]*\b{re.escape(name)}\b", layout_code, re.DOTALL):
            fail(f"{name} is missing a compiled static_assert contract")

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

    focus_body = function_body(renderer_code, "animateFocusEntry")
    layout_refresh = focus_body.find("lv_obj_update_layout(obj);")
    coordinate_read = focus_body.find("lv_obj_get_x(obj)")
    if layout_refresh < 0 or coordinate_read < 0 or layout_refresh > coordinate_read:
        fail("animateFocusEntry() does not refresh layout before reading x")

    positions_match = re.search(
        r"\bconst\s+int16_t\s+xs\s*\[\s*3\s*\]\s*=\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*;",
        renderer_code,
    )
    if not positions_match:
        fail("home-wheel button positions were not found")
    positions = [int(value) for value in positions_match.groups()]
    centers = [position + 33 for position in positions]
    if centers[1] != CENTER or CENTER - centers[0] != centers[2] - CENTER:
        fail(f"home-wheel buttons are not centered and symmetric: centers={centers}")

    toast_body = function_body(renderer_code, "drawToast")
    if re.search(r"\bnowMs\s*>=\s*state\.toastUntilMs\b", toast_body):
        fail("drawToast() uses a wrap-unsafe toast deadline comparison")
    if not re.search(
        r"static_cast\s*<\s*int32_t\s*>\s*\(\s*nowMs\s*-\s*state\.toastUntilMs\s*\)\s*>=\s*0",
        toast_body,
    ):
        fail("drawToast() does not use a signed elapsed-time deadline comparison")

    render_body = function_body(renderer_code, "uiRendererRender")
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

    assert_modal_contract(renderer_code)


def expect_rejected(name: str, layout_source: str, renderer_source: str) -> None:
    try:
        verify_contract(layout_source, renderer_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def run_self_test(layout_source: str, renderer_source: str) -> None:
    verify_contract(layout_source, renderer_source)
    inactive_duplicate = layout_source + "\n#if 0\nconstexpr UiRect kModalRect{70, 88, 340, 304};\n#endif\n"
    expect_rejected("inactive duplicate", inactive_duplicate, renderer_source)
    unused_modal = renderer_source.replace("uiBox(root, kModalRect", "uiBox(root, kNormalFooter", 1)
    expect_rejected("unused modal rectangle", layout_source, unused_modal)
    modal_body = function_body(code_only(renderer_source), "drawModal")
    dummy_modal = "void drawModal(const AppState &state, uint32_t nowMs) {" + modal_body + "}"
    noncompliant_live_modal = renderer_source.replace("uiBox(root, kModalRect", "uiBox(root, kNormalFooter", 1)
    expect_rejected("#if false dummy modal", layout_source,
                    "#if false\n" + dummy_modal + "\n#endif\n" + noncompliant_live_modal)
    expect_rejected("#if 0x0 dummy modal", layout_source,
                    "#if 0x0\n" + dummy_modal + "\n#endif\n" + noncompliant_live_modal)
    expect_rejected("%:if false inactive renderer", layout_source,
                    "%:if false\n" + renderer_source + "\n%:endif\n")
    expect_rejected("line-spliced #if false inactive renderer", layout_source,
                    "#\\\nif false\n" + renderer_source + "\n#\\\nendif\n")
    print("UI LAYOUT NEGATIVE TEST PASS: conditional spelling, inactive duplicates, and dummy modal bypasses rejected")


def main() -> int:
    layout_source = LAYOUT_PATH.read_text(encoding="utf-8")
    renderer_source = RENDERER_PATH.read_text(encoding="utf-8")
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        run_self_test(layout_source, renderer_source)
        return 0
    if len(sys.argv) != 1:
        print("usage: verify-ui-layout.py [--self-test]", file=sys.stderr)
        return 2
    try:
        verify_contract(layout_source, renderer_source)
    except LayoutError as error:
        print(f"UI LAYOUT CHECK FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "UI LAYOUT CHECK PASS: compiled rectangles, safe modal geometry, inactive-source rejection, "
        "focus layout ordering, wrap-safe toast, and retained same-page focus updates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
