#!/usr/bin/env python3
"""Verify the compiled-source contracts for the 480px round player console."""

from pathlib import Path
import re
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LAYOUT_PATH = PROJECT_ROOT / "ui_layout.h"
RENDERER_PATH = PROJECT_ROOT / "ui_renderer.cpp"
CAROUSEL_HEADER_PATH = PROJECT_ROOT / "ui_carousel.h"
CAROUSEL_SOURCE_PATH = PROJECT_ROOT / "ui_carousel.cpp"
CENTER_LIST_HEADER_PATH = PROJECT_ROOT / "ui_center_list.h"
CENTER_LIST_SOURCE_PATH = PROJECT_ROOT / "ui_center_list.cpp"
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


def assert_carousel_contract(renderer_source: str, carousel_header: str,
                             carousel_source: str) -> None:
    renderer_code = normalized_code(renderer_source)
    carousel_header_code = normalized_code(carousel_header)
    carousel_code = normalized_code(carousel_source)

    if not re.search(r"\blv_obj_t\s*\*items\s*\[\s*5\s*\]", carousel_header_code):
        fail("UiCarousel does not retain exactly five item objects")
    if not re.search(r"\blv_obj_t\s*\*labels\s*\[\s*5\s*\]", carousel_header_code):
        fail("UiCarousel does not retain exactly five label objects")
    if re.search(r"\b(?:new|malloc|calloc|realloc|vector|list|deque)\b", carousel_code):
        fail("ui_carousel.cpp uses heap-backed storage")
    if not re.search(r"\bUiRect\s+kCarouselRect\s*\{\s*48\s*,\s*300\s*,\s*384\s*,\s*74\s*\}",
                     carousel_code):
        fail("carousel container does not occupy the approved y=300..374 clipping band")

    selection_body = function_body(carousel_code, "uiCarouselSetSelection")
    required_selection = (
        (r"\blv_obj_get_x\s*\(", "carousel does not restart x from the current visual value"),
        (r"\blv_obj_get_style_transform_zoom\s*\(",
         "carousel does not restart zoom from the current visual value"),
        (r"\blv_obj_get_style_opa\s*\(",
         "carousel does not restart opacity from the current visual value"),
        (r"\buiCarouselPose\s*\(", "carousel selection does not use uiCarouselPose targets"),
    )
    for pattern, message in required_selection:
        if not re.search(pattern, selection_body):
            fail(message)

    animation_body = function_body(carousel_code, "animateProperty")
    if not re.search(r"\blv_anim_del\s*\(\s*object\s*,\s*callback\s*\)", animation_body):
        fail("carousel animation does not delete only the object's prior property animation")
    if not re.search(r"\blv_anim_set_time\s*\([^,]+,\s*220\s*\)", animation_body):
        fail("carousel properties do not animate for exactly 220ms")
    if not re.search(r"\blv_anim_set_path_cb\s*\([^,]+,\s*lv_anim_path_ease_out\s*\)",
                     animation_body):
        fail("carousel properties do not use ease-out motion")
    if len(re.findall(r"\banimateProperty\s*\(", selection_body)) < 3:
        fail("carousel selection does not animate x, zoom, and opacity")

    if len(re.findall(r"\buiCarouselCreate\s*\(", renderer_code)) != 1:
        fail("Home must allocate exactly one carousel scene")
    draw_home_body = function_body(renderer_code, "drawHome")
    if not re.search(r"\buiCarouselCreate\s*\(", draw_home_body):
        fail("drawHome() does not create the retained carousel")
    if not re.search(
        r"\buiCarouselActions\s*\(\s*state\.homePhase\s*,\s*actions\s*\).*"
        r"\buiCarouselCreate\s*\(\s*homeCarousel\s*,\s*root\s*,\s*actions\s*,\s*"
        r"actionCount\s*,\s*state\.focus\s*\)",
        draw_home_body,
        re.DOTALL,
    ):
        fail("drawHome() does not rebuild count and order from the current Home phase")
    create_body = function_body(carousel_code, "uiCarouselCreate")
    if not re.search(
        r"\buiBindTap\s*\(\s*item\s*,\s*UiEventKind::SelectHomeAction\s*,\s*index\s*\)",
        create_body,
    ):
        fail("rebuilt carousel items do not bind their current action indexes")
    if re.search(r"\bdrawHomeWheel\b", renderer_code):
        fail("legacy bounce-only Home wheel remains active")
    if "准备好旋钮与触摸操作" in renderer_source:
        fail("persistent Home operation hint remains in the renderer")

    approved_bands = (
        (r"\bkHomeTitle\s*\{[^}]*\b66\s*,[^}]*\b44\s*\}", "Home title is not y=66..110"),
        (r"\bkHomeCashLabel\s*\{[^}]*\b132\s*,[^}]*\b22\s*\}", "Home cash label is not y=132..154"),
        (r"\bkHomeCashAmount\s*\{[^}]*\b158\s*,[^}]*\b48\s*\}", "Home cash amount is not y=158..206"),
        (r"\bkHomeLocation\s*\{[^}]*\b226\s*,[^}]*\b28\s*\}", "Home location is not y=226..254"),
        (r"\bkHomeDivider\s*\{[^}]*\b270\s*,[^}]*\b1\s*\}", "Home divider is not at y=270"),
    )
    for pattern, message in approved_bands:
        if not re.search(pattern, renderer_code):
            fail(message)


def assert_center_list_contract(renderer_source: str, center_header: str,
                                center_source: str) -> None:
    renderer_code = normalized_code(renderer_source)
    header_code = normalized_code(center_header)
    center_code = normalized_code(center_source)

    retained_members = (
        "viewport", "track", "focusFrame", "progressFill", "countLabel", "footer",
    )
    for member in retained_members:
        if not re.search(rf"\blv_obj_t\s*\*\s*{member}\s*=\s*nullptr\s*;", header_code):
            fail(f"UiCenterList does not retain {member}")
    for member in ("selected", "count"):
        if not re.search(rf"\buint8_t\s+{member}\s*=\s*0\s*;", header_code):
            fail(f"UiCenterList does not retain {member}")
    for api in ("uiCenterListCreate", "uiCenterListUpdate", "uiCenterListSwipeStep"):
        if not re.search(rf"\b{api}\s*\(", header_code):
            fail(f"{api} is missing from the center-list API")

    if re.search(r"\b(?:new|malloc|calloc|realloc)\b|\bstd\s*::\s*(?:vector|list|deque)\b",
                 center_code):
        fail("ui_center_list.cpp uses heap-backed storage")
    required_constants = {
        "kRowHeight": 50,
        "kRowStride": 58,
        "kProgressWidth": 72,
        "kProgressHeight": 4,
        "kSwipeThreshold": 36,
        "kSwipeDominance": 12,
    }
    for name, value in required_constants.items():
        if not re.search(rf"\b{name}\s*=\s*{value}\s*;", center_code):
            fail(f"center list {name} must be exactly {value}")
    if not re.search(
        r"\bstatic_assert\s*\([^;]*kNormalListViewport\.h[^;]*/\s*kRowStride[^;]*<=\s*3",
        center_code,
        re.DOTALL,
    ):
        fail("center list does not compile-check the three-full-row viewport limit")

    create_body = function_body(center_code, "uiCenterListCreate")
    required_create = (
        (r"\buiBox\s*\(\s*parent\s*,\s*kNormalListViewport\b",
         "center list viewport does not use kNormalListViewport"),
        (r"\blv_obj_clear_flag\s*\(\s*list\.viewport\s*,\s*LV_OBJ_FLAG_SCROLLABLE\s*\)",
         "center list viewport remains scrollable"),
        (r"\blv_obj_clear_flag\s*\(\s*list\.viewport\s*,\s*LV_OBJ_FLAG_OVERFLOW_VISIBLE\s*\)",
         "center list viewport does not clip its retained track"),
        (r"\buiBox\s*\(\s*parent\s*,\s*kNormalListFocus\b",
         "center list focus frame does not use kNormalListFocus"),
        (r"\bkNormalListProgress\b[^;]*\bkProgressWidth\b[^;]*\bkProgressHeight\b",
         "center list progress is not derived as centered 72x4 geometry"),
        (r"\buiLabel\s*\([^;]*\bkNormalListCount\b",
         "center list count does not use kNormalListCount"),
        (r"\buiBox\s*\(\s*parent\s*,\s*kNormalFooter\b",
         "center list footer does not use kNormalFooter"),
    )
    for pattern, message in required_create:
        if not re.search(pattern, create_body, re.DOTALL):
            fail(message)

    animate_body = function_body(center_code, "animateTrack")
    if not re.search(r"\blv_obj_get_y\s*\(", animate_body):
        fail("center-list travel does not restart from the current track y")
    if not re.search(r"\blv_anim_set_time\s*\([^,]+,\s*200\s*\)", animate_body):
        fail("center-list track travel is not exactly 200ms")
    if not re.search(r"\blv_anim_set_path_cb\s*\([^,]+,\s*lv_anim_path_ease_out\s*\)",
                     animate_body):
        fail("center-list track travel does not use ease-out")

    update_body = function_body(center_code, "uiCenterListUpdate")
    if not re.search(r"\buiCenterListTrackY\s*\(", update_body):
        fail("center-list update does not use the pure center-lock target helper")
    if not re.search(r"\buiListProgressPermille\s*\(", update_body):
        fail("center-list update does not use selected-row progress")
    if re.search(r"\b(?:lv_bar_create|lv_slider_create|LV_SCROLLBAR_MODE)\b", center_code):
        fail("center list creates a forbidden scrollbar")

    swipe_match = re.search(r"\bint8_t\s+uiCenterListSwipeStep\s*\([^)]*\)\s*\{", center_code)
    if not swipe_match:
        fail("uiCenterListSwipeStep() implementation is missing")
    swipe_body = block_after(center_code, swipe_match)
    if not re.search(r"\babsoluteY\s*<\s*kSwipeThreshold\b", swipe_body):
        fail("center-list swipe does not enforce the 36px threshold")
    if not re.search(r"\babsoluteY\s*<\s*absoluteX\s*\+\s*kSwipeDominance\b", swipe_body):
        fail("center-list swipe does not enforce 12px vertical dominance")
    gesture_body = function_body(center_code, "swipeCallback")
    if not re.search(r"\bcode\s*==\s*LV_EVENT_RELEASED\b", gesture_body):
        fail("center-list swipe is not emitted on release")
    reset_position = gesture_body.find("lv_indev_reset(")
    emit_position = gesture_body.find("lv_event_send(")
    if reset_position < 0 or emit_position < 0 or reset_position > emit_position:
        fail("center-list swipe does not cancel LVGL's follow-up row click before emitting a step")
    if len(re.findall(r"\blv_event_send\s*\(", gesture_body)) != 2:
        fail("center-list release must expose exactly one previous/next event branch")

    for function_name, count_token in (
        ("drawAssets", "kAssetCount"),
        ("drawPlayers", "kPlayerCount"),
        ("drawDemoLab", "kDemoListCount"),
    ):
        body = function_body(renderer_code, function_name)
        if re.search(r"\bdrawListRow\s*\(", body):
            fail(f"{function_name} still renders legacy root rows")
        if not re.search(rf"\bUiListItemView\s+items\s*\[\s*{count_token}\s*\]", body):
            fail(f"{function_name} does not build the complete retained item view")
        if not re.search(rf"\buiCenterListCreate\s*\([^;]*\b{count_token}\b", body, re.DOTALL):
            fail(f"{function_name} does not create one retained center list")

    render_body = function_body(renderer_code, "uiRendererRender")
    same_page_match = re.search(
        r"\bif\s*\(\s*hasRenderedState\s*&&\s*state\.page\s*==\s*previousPage[^)]*\)\s*\{",
        render_body,
    )
    if not same_page_match:
        fail("renderer has no same-page retained update branch")
    same_page_body = block_after(render_body, same_page_match)
    if not re.search(r"\bupdateCenterListFocus\s*\(\s*state\s*\)", same_page_body):
        fail("ordinary list focus changes do not update retained center-list objects")
    draw_trade_body = function_body(renderer_code, "drawTrade")
    draw_field_body = function_body(renderer_code, "drawTradeField")
    if not re.search(r"\breceiverLocked\b[^;]*\bappTradeReceiverLocked\s*\(", draw_trade_body):
        fail("Trade does not query the Player-locked Receiver mode")
    if not re.search(r"\blockedMarkerImage\s*\(\)", draw_field_body):
        fail("locked Trade Receiver does not render the fixed 已锁定 marker image")
    if "\\x" in draw_trade_body:
        fail("Trade introduces escaped UTF-8 instead of raw source text")


def verify_contract(layout_source: str, renderer_source: str, carousel_header: str,
                    carousel_source: str, center_header: str | None = None,
                    center_source: str | None = None) -> None:
    reject_inactive_blocks(layout_source, "ui_layout.h")
    reject_inactive_blocks(renderer_source, "ui_renderer.cpp")
    layout_code = normalized_code(layout_source)
    renderer_code = normalized_code(renderer_source)
    assert_carousel_contract(renderer_source, carousel_header, carousel_source)
    if center_header is None:
        center_header = CENTER_LIST_HEADER_PATH.read_text(encoding="utf-8")
    if center_source is None:
        center_source = CENTER_LIST_SOURCE_PATH.read_text(encoding="utf-8")
    assert_center_list_contract(renderer_source, center_header, center_source)
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

    toast_body = function_body(renderer_code, "drawToast")
    if re.search(r"\bnowMs\s*>=\s*state\.toastUntilMs\b", toast_body):
        fail("drawToast() uses a wrap-unsafe toast deadline comparison")
    if not re.search(
        r"static_cast\s*<\s*int32_t\s*>\s*\(\s*nowMs\s*-\s*state\.toastUntilMs\s*\)\s*>=\s*0",
        toast_body,
    ):
        fail("drawToast() does not use a signed elapsed-time deadline comparison")

    render_body = function_body(renderer_code, "uiRendererRender")
    if not re.search(
        r"\bconst\s+bool\s+homeActionsUnchanged\s*=\s*"
        r"state\.page\s*!=\s*ScreenPage::Home\s*\|\|\s*"
        r"state\.homePhase\s*==\s*previousHomePhase\s*;",
        render_body,
    ):
        fail("renderer does not compare the current and rendered Home phases")
    if not re.search(
        r"\bconst\s+bool\s+inlineEditUnchanged\s*=\s*"
        r"state\.inlineEditField\s*==\s*previousInlineEditField\s*;",
        render_body,
    ):
        fail("renderer does not compare the current and rendered inline editor")
    same_page_match = re.search(
        r"\bif\s*\(\s*hasRenderedState\s*&&\s*state\.page\s*==\s*previousPage\s*&&\s*"
        r"state\.focus\s*!=\s*previousFocus\s*&&\s*homeActionsUnchanged\s*&&\s*"
        r"inlineEditUnchanged\s*\)\s*\{",
        render_body,
    )
    if not same_page_match:
        fail("same-page focus-update branch is not guarded by the rendered Home phase")
    same_page_body = block_after(render_body, same_page_match)
    if re.search(r"\blv_obj_clean\s*\(\s*root\s*\)", same_page_body):
        fail("same-page focus update clears the root")
    if re.search(r"\brebuild\s*\(", same_page_body):
        fail("same-page focus update enters the root rebuilding path")
    if not re.search(r"\bif\s*\(\s*state\.page\s*==\s*ScreenPage::Home\s*\)", same_page_body):
        fail("same-page focus update has no retained Home branch")
    if not re.search(r"\buiCarouselSetSelection\s*\(\s*homeCarousel\s*,\s*state\.focus\s*,\s*true\s*\)",
                     same_page_body):
        fail("same-page Home focus update does not call uiCarouselSetSelection")
    if re.search(r"\b(?:uiCarouselCreate|drawHome|animateFocusEntry)\s*\(", same_page_body):
        fail("same-page Home focus update recreates or bounce-animates the scene")
    if not re.search(
        r"\brebuild\s*\(\s*state\s*,\s*nowMs\s*\)\s*;.*"
        r"\bpreviousHomePhase\s*=\s*state\.homePhase\s*;.*"
        r"\bpreviousInlineEditField\s*=\s*state\.inlineEditField\s*;",
        render_body,
        re.DOTALL,
    ):
        fail("rebuild does not retain Home phase and inline editor for focus-only updates")

    assert_modal_contract(renderer_code)


def expect_rejected(name: str, layout_source: str, renderer_source: str,
                    carousel_header: str, carousel_source: str) -> None:
    try:
        verify_contract(layout_source, renderer_source, carousel_header, carousel_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def run_self_test(layout_source: str, renderer_source: str, carousel_header: str,
                  carousel_source: str, center_header: str, center_source: str) -> None:
    verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                    center_header, center_source)
    inactive_duplicate = layout_source + "\n#if 0\nconstexpr UiRect kModalRect{70, 88, 340, 304};\n#endif\n"
    expect_rejected("inactive duplicate", inactive_duplicate, renderer_source,
                    carousel_header, carousel_source)
    unused_modal = renderer_source.replace("uiBox(root, kModalRect", "uiBox(root, kNormalFooter", 1)
    expect_rejected("unused modal rectangle", layout_source, unused_modal,
                    carousel_header, carousel_source)
    modal_body = function_body(code_only(renderer_source), "drawModal")
    dummy_modal = "void drawModal(const AppState &state, uint32_t nowMs) {" + modal_body + "}"
    noncompliant_live_modal = renderer_source.replace("uiBox(root, kModalRect", "uiBox(root, kNormalFooter", 1)
    expect_rejected("#if false dummy modal", layout_source,
                    "#if false\n" + dummy_modal + "\n#endif\n" + noncompliant_live_modal,
                    carousel_header, carousel_source)
    expect_rejected("#if 0x0 dummy modal", layout_source,
                    "#if 0x0\n" + dummy_modal + "\n#endif\n" + noncompliant_live_modal,
                    carousel_header, carousel_source)
    expect_rejected("%:if false inactive renderer", layout_source,
                    "%:if false\n" + renderer_source + "\n%:endif\n",
                    carousel_header, carousel_source)
    expect_rejected("line-spliced #if false inactive renderer", layout_source,
                    "#\\\nif false\n" + renderer_source + "\n#\\\nendif\n",
                    carousel_header, carousel_source)
    expect_rejected("retained Home selection removed", layout_source,
                    renderer_source.replace("uiCarouselSetSelection(homeCarousel, state.focus, true);", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("retained Home recreated", layout_source,
                    renderer_source.replace("uiCarouselSetSelection(homeCarousel, state.focus, true);",
                                            "drawHome(state);", 1),
                    carousel_header, carousel_source)
    expect_rejected("Home phase guard removed", layout_source,
                    renderer_source.replace(" && homeActionsUnchanged", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("rendered Home phase update removed", layout_source,
                    renderer_source.replace("previousHomePhase = state.homePhase;", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("inline editor guard removed", layout_source,
                    renderer_source.replace(" && inlineEditUnchanged", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("rendered inline editor update removed", layout_source,
                    renderer_source.replace("previousInlineEditField = state.inlineEditField;", "", 1),
                    carousel_header, carousel_source)
    center_mutations = (
        ("viewport clipping removed",
         center_source.replace("lv_obj_clear_flag(list.viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);", "", 1)),
        ("track duration changed", center_source.replace("lv_anim_set_time(&animation, 200);",
                                                        "lv_anim_set_time(&animation, 201);", 1)),
        ("swipe threshold changed", center_source.replace("kSwipeThreshold = 36;",
                                                         "kSwipeThreshold = 35;", 1)),
        ("swipe dominance changed", center_source.replace("kSwipeDominance = 12;",
                                                         "kSwipeDominance = 11;", 1)),
        ("swipe click cancellation removed",
         center_source.replace("lv_indev_reset(input, lv_event_get_target(event));", "", 1)),
    )
    for name, mutated_center in center_mutations:
        try:
            verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                            center_header, mutated_center)
        except LayoutError:
            continue
        raise AssertionError(f"{name} fixture unexpectedly passed")
    legacy_assets = renderer_source.replace(
        "uiCenterListCreate(centerList, root, items, kAssetCount",
        "drawListRow(108, items[0].title, items[0].meta, true, TouchAction::Asset0); "
        "uiCenterListCreate(centerList, root, items, kAssetCount",
        1,
    )
    try:
        verify_contract(layout_source, legacy_assets, carousel_header, carousel_source,
                        center_header, center_source)
    except LayoutError:
        pass
    else:
        raise AssertionError("legacy eight-asset root-row fixture unexpectedly passed")
    print("UI LAYOUT NEGATIVE TEST PASS: inactive bypasses, retained Home, and center-list mutations rejected")


def main() -> int:
    try:
        layout_source = LAYOUT_PATH.read_text(encoding="utf-8")
        renderer_source = RENDERER_PATH.read_text(encoding="utf-8")
        if not CAROUSEL_HEADER_PATH.exists() or not CAROUSEL_SOURCE_PATH.exists():
            fail("retained ui_carousel component files are missing")
        if not CENTER_LIST_HEADER_PATH.exists() or not CENTER_LIST_SOURCE_PATH.exists():
            fail("retained ui_center_list component files are missing")
        carousel_header = CAROUSEL_HEADER_PATH.read_text(encoding="utf-8")
        carousel_source = CAROUSEL_SOURCE_PATH.read_text(encoding="utf-8")
        center_header = CENTER_LIST_HEADER_PATH.read_text(encoding="utf-8")
        center_source = CENTER_LIST_SOURCE_PATH.read_text(encoding="utf-8")
        if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
            run_self_test(layout_source, renderer_source, carousel_header, carousel_source,
                          center_header, center_source)
            return 0
        if len(sys.argv) != 1:
            print("usage: verify-ui-layout.py [--self-test]", file=sys.stderr)
            return 2
        verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                        center_header, center_source)
    except LayoutError as error:
        print(f"UI LAYOUT CHECK FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "UI LAYOUT CHECK PASS: compiled rectangles, safe modal geometry, inactive-source rejection, "
        "focus layout ordering, wrap-safe toast, retained Home, and center-locked lists"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
