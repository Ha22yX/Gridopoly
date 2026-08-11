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
MOTION_SOURCE_PATH = PROJECT_ROOT / "ui_motion.cpp"
CENTER_LIST_HEADER_PATH = PROJECT_ROOT / "ui_center_list.h"
CENTER_LIST_SOURCE_PATH = PROJECT_ROOT / "ui_center_list.cpp"
MODAL_HEADER_PATH = PROJECT_ROOT / "ui_modal.h"
MODAL_SOURCE_PATH = PROJECT_ROOT / "ui_modal.cpp"
PLAYER_CONSOLE_PATH = PROJECT_ROOT / "PlayerConsole.ino"
LOGIC_TESTS_PATH = PROJECT_ROOT / "logic_tests.cpp"
RADIUS = 192
CENTER = 240
EXPECTED_RECTANGLES = {
    "kNormalListViewport": (104, 137, 272, 166),
    "kNormalListFocus": (104, 195, 272, 50),
    "kNormalListProgress": (168, 320, 144, 8),
    "kNormalListCount": (202, 328, 76, 16),
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
    match = re.search(rf"\b[A-Za-z_][A-Za-z0-9_:<>\s\*&]*\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
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


def assert_modal_contract(renderer_source: str, modal_header: str | None = None,
                          modal_source: str | None = None) -> None:
    if modal_header is None:
        modal_header = MODAL_HEADER_PATH.read_text(encoding="utf-8")
    if modal_source is None:
        modal_source = MODAL_SOURCE_PATH.read_text(encoding="utf-8")

    reject_inactive_blocks(modal_header, "ui_modal.h")
    reject_inactive_blocks(modal_source, "ui_modal.cpp")
    renderer = normalized_code(renderer_source)
    header = normalized_code(modal_header)
    source = normalized_code(modal_source)

    if '#include "ui_modal.h"' not in renderer_source:
        fail("renderer does not include the retained modal component")
    if len(re.findall(r"\bUiModal\s+activeModal\s*\{\s*\}\s*;", renderer)) != 1:
        fail("renderer does not retain exactly one UiModal instance")

    draw_body = function_body(renderer, "drawModal")
    if not re.search(r"\bapplyModal\s*\(\s*state\s*,\s*nowMs\s*,\s*true\s*\)", draw_body):
        fail("drawModal does not create the retained modal component")
    apply_body = function_body(renderer, "applyModal")
    for pattern, message in (
        (r"\buiModalCreate\s*\(\s*activeModal\s*,\s*root\s*,\s*view\s*\)",
         "modal creation is not routed through uiModalCreate"),
        (r"\buiModalUpdate\s*\(\s*activeModal\s*,\s*view\s*\)",
         "dynamic modal progress is not routed through uiModalUpdate"),
        (r"\bstate\.modal\.cancelAllowed\s*&&\s*!\s*state\.modal\.submitting",
         "renderer does not remove voluntary Back after submission"),
        (r"\bappHoldProgressPermille\s*\(",
         "renderer does not feed the authoritative hold progress into UiModal"),
    ):
        if not re.search(pattern, apply_body):
            fail(message)
    for owner in ("rebuild", "uiRendererResetForTest", "uiRendererShowFault"):
        if not re.search(r"\buiModalDestroy\s*\(\s*activeModal\s*\)",
                         function_body(renderer, owner)):
            fail(f"{owner} does not release retained modal ownership")

    for member in ("shade", "panel", "confirm", "holdTrack", "confirmFill",
                   "holdLabel", "back", "countdown", "cashLabel"):
        if not re.search(rf"\blv_obj_t\s*\*\s*{member}\s*=\s*nullptr\s*;", header):
            fail(f"UiModal does not retain {member}")
    for api in ("uiModalCreate", "uiModalUpdate", "uiModalDestroy", "uiModalHoldFillWidth"):
        if not re.search(rf"\b{api}\s*\(", header):
            fail(f"{api} is missing from the modal API")

    required_geometry = (
        (r"\bkConfirm\s*=\s*childRect\s*\(\s*kModalConfirm\s*,\s*kModalRect\s*\)",
         "modal confirm does not derive from kModalConfirm"),
        (r"\bkBack\s*=\s*childRect\s*\(\s*kModalCancel\s*,\s*kModalRect\s*\)",
         "modal Back does not derive from kModalCancel"),
        (r"\bkHoldTrack\s*\{\s*12\s*,\s*46\s*,\s*200\s*,\s*8\s*\}",
         "modal hold track is not the approved 12px-inset 200x8 geometry"),
        (r"\bkHoldLabel\s*\{\s*12\s*,\s*8\s*,\s*200\s*,\s*26\s*\}",
         "modal hold label does not preserve the approved track gap"),
    )
    for pattern, message in required_geometry:
        if not re.search(pattern, source):
            fail(message)

    create_body = function_body(source, "uiModalCreate")
    for pattern, message in (
        (r"\buiBox\s*\(\s*root\s*,\s*UiRect\s*\{\s*0\s*,\s*0\s*,\s*480\s*,\s*480\s*\}",
         "modal shade does not cover the display"),
        (r"\buiBox\s*\(\s*modal\.shade\s*,\s*kModalRect\b",
         "modal panel is not a child of the blocking shade"),
        (r"\buiBox\s*\(\s*modal\.panel\s*,\s*kConfirm\b",
         "modal confirm is not a panel child"),
        (r"\buiBox\s*\(\s*modal\.confirm\s*,\s*kHoldTrack\b",
         "hold track is not clipped by the confirmation control"),
        (r"\buiBox\s*\(\s*modal\.holdTrack\s*,\s*UiRect\s*\{\s*0\s*,\s*0\s*,\s*0\s*,\s*kHoldTrack\.h\s*\}",
         "hold fill is not clipped by the hold track"),
        (r"\buiBindHold\s*\(\s*modal\.confirm\s*\)",
         "modal confirm does not expose hold touch semantics"),
        (r"\blv_obj_set_style_bg_opa\s*\(\s*modal\.shade\s*,\s*kShadeOpacity\b",
         "modal shade opacity is not applied"),
        (r"\blv_obj_add_flag\s*\(\s*modal\.shade\s*,\s*LV_OBJ_FLAG_CLICKABLE\s*\)",
         "modal shade does not block background touch"),
    ):
        if not re.search(pattern, create_body):
            fail(message)
    if not re.search(r"\bkShadeOpacity\s*=\s*184\s*;", source):
        fail("modal shade is not the approved 72 percent opacity")
    for object_name in ("confirm", "holdTrack", "confirmFill"):
        if not re.search(rf"\bmakeNonOverflowing\s*\(\s*modal\.{object_name}\s*\)", create_body):
            fail(f"modal {object_name} does not clip overflow")
    fill_index = create_body.find("modal.confirmFill = uiBox")
    label_index = create_body.find("modal.holdLabel = uiLabel")
    if fill_index < 0 or label_index < 0 or fill_index >= label_index:
        fail("modal hold label is not layered above the progress fill")

    back_body = function_body(source, "createBack")
    if not re.search(r"\buiBindTap\s*\(\s*modal\.back\s*,\s*UiEventKind::Back\s*\)", back_body):
        fail("modal Back does not emit UiEventKind::Back")
    update_back_body = function_body(source, "updateBack")
    if not re.search(r"\bview\.showBack\s*&&\s*!\s*view\.submitting", update_back_body):
        fail("modal Back visibility is not suppressed while submitting")

    helper_body = function_body(source, "uiModalHoldFillWidth")
    if not re.search(r"\bpermille\s*>\s*1000\s*\?\s*1000\s*:\s*permille", helper_body):
        fail("modal hold progress is not clamped to 1000 permille")
    if not re.search(r"\bkHoldTrack\.w\s*\)\s*\*\s*clamped", helper_body):
        fail("modal hold progress does not use the bounded track width")
    update_body = function_body(source, "updateConfirm")
    if not re.search(r"\buiModalHoldFillWidth\s*\(\s*view\.holdPermille\s*\)", update_body):
        fail("modal update bypasses the clamped progress helper")


def assert_carousel_contract(renderer_source: str, carousel_header: str,
                             carousel_source: str) -> None:
    renderer_code = normalized_code(renderer_source)
    carousel_header_code = normalized_code(carousel_header)
    carousel_code = normalized_code(carousel_source)

    if not re.search(r"\blv_obj_t\s*\*items\s*\[\s*5\s*\]", carousel_header_code):
        fail("UiCarousel does not retain exactly five item objects")
    if not re.search(r"\bCarouselPath\s+paths\s*\[\s*5\s*\]", carousel_header_code):
        fail("UiCarousel does not retain one wrap-safe path per item")
    if re.search(r"\blv_obj_t\s*\*labels\s*\[", carousel_header_code):
        fail("UiCarousel retains child labels instead of flattened item canvases")
    if re.search(r"\b(?:new|malloc|calloc|realloc|vector|list|deque)\b", carousel_code):
        fail("ui_carousel.cpp uses heap-backed storage")
    if not re.search(r"\bUiRect\s+kCarouselRect\s*\{\s*48\s*,\s*300\s*,\s*384\s*,\s*74\s*\}",
                     carousel_code):
        fail("carousel container does not occupy the approved y=300..374 clipping band")
    if not re.search(r"\bkCarouselRefreshPeriodMs\s*=\s*16\s*;", carousel_code):
        fail("retained Home does not prepare a bounded 16ms display refresh cadence")

    selection_body = function_body(carousel_code, "uiCarouselSetSelection")
    required_selection = (
        (r"\blv_obj_get_x\s*\(", "carousel does not restart x from the current visual value"),
        (r"\bcarousel\.currentZoom\s*\[",
         "carousel does not restart zoom from the current visual value"),
        (r"\bcarousel\.currentOpacity\s*\[",
         "carousel does not restart opacity from the current visual value"),
        (r"\buiCarouselPose\s*\(", "carousel selection does not use uiCarouselPose targets"),
    )
    for pattern, message in required_selection:
        if not re.search(pattern, selection_body):
            fail(message)
    if not re.search(
        r"\bconst\s+uint8_t\s+previousSelected\s*=\s*carousel\.selected\s*;",
        selection_body,
    ) or not re.search(
        r"\bif\s*\(\s*previousSelected\s*!=\s*selected\s*&&\s*\(\s*"
        r"index\s*==\s*previousSelected\s*\|\|\s*index\s*==\s*selected\s*\)\s*\)",
        selection_body,
    ):
        fail("carousel selection invalidates more than the old and new selected cards")
    static_style_body = function_body(carousel_code, "configureItemStaticStyle")
    selection_style_body = function_body(carousel_code, "applyItemSelectionStyle")
    for token in ("lv_obj_set_style_radius", "lv_obj_set_style_outline_width",
                  "lv_obj_set_style_outline_pad"):
        if token not in static_style_body:
            fail("carousel create-only style is missing static geometry or outline setup")
    if any(token in selection_style_body for token in ("outline", "radius", "pad")):
        fail("carousel selection path rewrites create-only outline/radius/pad styles")
    for token in ("lv_obj_set_style_bg_color", "lv_obj_set_style_border_width"):
        if token not in selection_style_body:
            fail("carousel selection style does not retain guarded background/border updates")
    if "configureItemStaticStyle(" in selection_body:
        fail("carousel selection reconfigures static item styles")
    if not re.search(r"\bapplyItemSelectionStyle\s*\(", selection_body):
        fail("carousel selection does not update only old/new selection visuals")

    if re.search(r"\b(?:animateProperty|setAnimatedX|setAnimatedZoom|setAnimatedOpacity)\b",
                 carousel_code):
        fail("carousel retains independent property animation callbacks")
    if re.search(r"\b(?:iconFont|labelFont)\s*\(", carousel_code):
        fail("carousel restores hard single-font zoom thresholds")
    progress_body = function_body(carousel_code, "setCarouselTransitionProgress")
    if not re.search(r"\bapplyCarouselVisual\s*\(", progress_body):
        fail("carousel progress does not apply every synchronized visual pose")
    if not re.search(r"\btransitionVisual\s*\(", progress_body):
        fail("carousel master progress does not evaluate wrap-safe piecewise poses")
    path_body = function_body(carousel_code, "carouselPath")
    for token in ("WrapLeftToRight", "WrapRightToLeft", "Linear"):
        if token not in path_body:
            fail(f"carousel path selection omits {token}")
    piecewise_body = function_body(carousel_code, "transitionVisual")
    for token, message in (
        ("kWrapSwapProgress", "carousel wrap does not define an invisible side swap"),
        ("leftExit", "carousel wrap does not exit beyond the left clip edge"),
        ("rightExit", "carousel wrap does not exit beyond the right clip edge"),
        ("WrapLeftToRight", "carousel wrap pose omits forward cycling"),
        ("carousel.startOpacity[index], 0",
         "carousel wrap does not fade fully out before changing sides"),
        ("0, carousel.targetOpacity[index]",
         "carousel wrap does not fade back in after changing sides"),
    ):
        if token not in piecewise_body:
            fail(message)
    if not re.search(
        r"\bcarousel\.paths\s*\[\s*index\s*\]\s*=\s*carouselPath\s*\(\s*"
        r"carousel\.startX\s*\[\s*index\s*\]\s*,\s*targetX\s*,\s*direction\s*\)",
        selection_body,
    ):
        fail("carousel retarget does not choose wrap from the current visual X")
    visual_body = function_body(carousel_code, "applyCarouselVisual")
    for pattern, message in (
        (r"\blv_obj_set_pos\s*\(", "carousel visual update does not move the item"),
        (r"\blv_obj_set_size\s*\(", "carousel visual update does not apply zoom as retained geometry"),
        (r"\blv_obj_set_style_bg_opa\s*\(", "carousel visual update does not apply panel opacity"),
        (r"\blv_obj_set_style_text_opa\s*\(", "carousel visual update does not apply text opacity"),
    ):
        if not re.search(pattern, visual_body):
            fail(message)
    if re.search(r"\blv_obj_set_style_(?:transform_zoom|opa)\s*\(", progress_body + visual_body):
        fail("carousel progress still creates software transform or subtree-opacity layers")
    transition_body = function_body(carousel_code, "startCarouselTransition")
    if re.search(r"\blv_refr_now\s*\(", transition_body):
        fail("carousel performs an unsafe synchronous refresh from the locked application task")
    required_timeline = (
        (r"\blv_anim_del\s*\(\s*&carousel\s*,\s*setCarouselTransitionProgress\s*\)",
         "carousel does not cancel the prior master timeline"),
        (r"\blv_anim_set_var\s*\([^,]+,\s*&carousel\s*\)",
         "carousel master timeline is not owned by its retained carousel"),
        (r"\blv_anim_set_exec_cb\s*\([^,]+,\s*setCarouselTransitionProgress\s*\)",
         "carousel master timeline has no synchronized progress callback"),
        (r"\blv_anim_set_values\s*\([^,]+,\s*0\s*,\s*1000\s*\)",
         "carousel master timeline is not the required 0..1000 progress source"),
        (r"\blv_anim_set_time\s*\([^,]+,\s*220\s*\)",
         "carousel master timeline is not exactly 220ms"),
        (r"\blv_anim_set_path_cb\s*\([^,]+,\s*lv_anim_path_ease_out\s*\)",
         "carousel master timeline does not use ease-out motion"),
        (r"\blv_anim_set_early_apply\s*\(\s*&animation\s*,\s*false\s*\)",
         "carousel master timeline rewrites every retained item before its first timer tick"),
        (r"\blv_timer_ready\s*\(\s*display->refr_timer\s*\)",
         "carousel master timeline does not safely schedule its first physical refresh"),
    )
    for pattern, message in required_timeline:
        if not re.search(pattern, transition_body):
            fail(message)
    if len(re.findall(r"\blv_anim_set_var\s*\(", carousel_code)) != 1:
        fail("carousel creates more than one LVGL animation timeline")

    swipe_body = function_body(carousel_code, "carouselSwipeCallback")
    if not re.search(r"\bLV_EVENT_RELEASED\b", swipe_body):
        fail("Home swipe is not dispatched on release")
    if not re.search(r"\blv_indev_reset\s*\(", swipe_body):
        fail("Home swipe does not cancel LVGL's follow-up click")
    if not re.search(r"\bnextTarget\b", swipe_body) or not re.search(r"\bpreviousTarget\b", swipe_body):
        fail("Home swipe must expose one previous and one next focus branch")

    if len(re.findall(r"\buiCarouselCreate\s*\(", renderer_code)) != 1:
        fail("Home must allocate exactly one carousel scene")
    draw_home_body = function_body(renderer_code, "drawHome")
    if not re.search(r"\buiCarouselCreate\s*\(", draw_home_body):
        fail("drawHome() does not create the retained carousel")
    if not re.search(
        r"\bconst\s+HomePhase\s+visualPhase\s*=\s*"
        r"appPresentedHomePhase\s*\(\s*state\s*\)\s*;.*"
        r"\buiCarouselActions\s*\(\s*visualPhase\s*,\s*actions\s*\).*"
        r"\buiCarouselCreate\s*\(\s*homeCarousel\s*,\s*root\s*,\s*actions\s*,\s*"
        r"actionCount\s*,\s*state\.focus\s*\)",
        draw_home_body,
        re.DOTALL,
    ):
        fail("drawHome() does not rebuild count and order from the presented Home phase")
    create_body = function_body(carousel_code, "uiCarouselCreate")
    if not re.search(r"\bconfigureItemStaticStyle\s*\(", create_body) or not re.search(
            r"\bapplyItemSelectionStyle\s*\(", create_body):
        fail("carousel creation does not initialize static and selection styles separately")
    if not re.search(
        r"\blv_timer_set_period\s*\(\s*display->refr_timer\s*,\s*"
        r"kCarouselRefreshPeriodMs\s*\)",
        create_body,
    ):
        fail("retained Home does not prepare its display cadence before user input")
    destroy_body = function_body(carousel_code, "uiCarouselDestroy")
    if not re.search(
        r"\blv_timer_set_period\s*\(\s*display->refr_timer\s*,\s*"
        r"LV_DISP_DEF_REFR_PERIOD\s*\)",
        destroy_body,
    ):
        fail("carousel destruction does not restore the default display cadence")
    if not re.search(r"\blv_obj_create\s*\(\s*carousel\.container\s*\)", create_body):
        fail("carousel items are not retained childless draw objects")
    if re.search(r"\buiLabel\s*\(\s*item\s*,", create_body):
        fail("carousel still retains child labels inside animated item geometry")
    if not re.search(
        r"\blv_obj_add_event_cb\s*\(\s*item\s*,\s*drawCarouselItem\s*,\s*LV_EVENT_DRAW_POST",
        create_body,
    ):
        fail("carousel item does not custom-draw its icon and label without child layers")
    draw_body = function_body(carousel_code, "drawCarouselItem")
    if len(re.findall(r"\bdrawCrossfadedLabel\s*\(", draw_body)) < 2:
        fail("carousel item draw path does not crossfade both icon and label")
    crossfade_body = function_body(carousel_code, "drawCrossfadedLabel")
    if len(re.findall(r"\blv_draw_label\s*\(", crossfade_body)) < 2:
        fail("carousel text crossfade does not blend adjacent retained font sizes")
    if not re.search(r"\blv_obj_add_event_cb\s*\(\s*carousel\.container\s*,\s*carouselSwipeCallback",
                     create_body):
        fail("Home swipe is not bound to the full retained carousel container")
    if re.search(r"\buiBindTap\s*\(\s*item\s*", create_body):
        fail("Home carousel still binds gestures to individual item objects")
    if not re.search(r"\blv_obj_clear_flag\s*\(\s*item\s*,\s*LV_OBJ_FLAG_CLICKABLE\s*\)",
                     create_body) or not re.search(
                         r"\blv_obj_add_flag\s*\(\s*item\s*,\s*LV_OBJ_FLAG_EVENT_BUBBLE\s*\)",
                         create_body):
        fail("Home carousel item hit paths do not reach the container gesture handler")
    if re.search(r"\bdrawHomeWheel\b", renderer_code):
        fail("legacy bounce-only Home wheel remains active")

    if not re.search(r"\bkTurnDots\s*\[\s*5\s*\]", renderer_code):
        fail("Home does not retain five fixed turn-dot positions")
    expected_dots = ((200, 114), (218, 114), (236, 114), (254, 114), (272, 114))
    for x, y in expected_dots:
        if not re.search(rf"\bUiRect\s*\{{\s*{x}\s*,\s*{y}\s*,\s*8\s*,\s*8\s*\}}",
                         renderer_code):
            fail("Home turn dots are not fixed and center-stable")
    dots_body = function_body(renderer_code, "drawTurnDots")
    for pattern, message in (
        (r"\bturnsUntilYou\s*>\s*5\s*\?\s*5\s*:\s*turnsUntilYou",
         "turn dots do not clamp the fill count to five"),
        (r"\bindex\s*<\s*filled\s*\?\s*kGreen\s*:\s*kBg",
         "turn dots do not render filled and hollow states"),
        (r"\bLV_OPA_TRANSP\b", "turn dots do not make unfilled dots hollow"),
    ):
        if not re.search(pattern, dots_body):
            fail(message)
    if not re.search(r"\bdrawOuterRing\s*\(\s*kGreen\s*\)", draw_home_body):
        fail("My-Turn Home ring is not signal green #52DCB7")
    if not re.search(r"\bdrawTurnDots\s*\(\s*state\.turnsUntilYou\s*\)", draw_home_body):
        fail("Home does not render turn distance dots from turnsUntilYou")
    if re.search(r"PLAYERS AHEAD|YOUR TURN IS CLOSE", renderer_source):
        fail("Home retains a duplicate sentence-form turn-distance label")
    if not re.search(r"\bkDiceYellow\s*=\s*0xF2C453\b", carousel_code):
        fail("Dice no longer retains its yellow priority accent")
    if "准备好旋钮与触摸操作" in renderer_source:
        fail("persistent Home operation hint remains in the renderer")

    approved_bands = (
        (r"\bkHomeTitle\s*\{[^}]*\b66\s*,[^}]*\b44\s*\}", "Home title is not y=66..110"),
        (r"\bkHomeArtwork\s*\{[^}]*\b130\s*,[^}]*\b144\s*\}", "Home artwork is not y=130..274"),
        (r"\bkHomeCashLabel\s*\{[^}]*\b144\s*,[^}]*\b22\s*\}", "Home cash label is not y=144..166"),
        (r"\bkHomeCashAmount\s*\{[^}]*\b170\s*,[^}]*\b48\s*\}", "Home cash amount is not y=170..218"),
        (r"\bkHomeTileNumber\s*\{[^}]*\b224\s*,[^}]*\b18\s*\}", "Home tile number is not y=224..242"),
        (r"\bkHomeLocation\s*\{[^}]*\b244\s*,[^}]*\b24\s*\}", "Home location is not y=244..268"),
        (r"\bkHomeDivider\s*\{[^}]*\b282\s*,[^}]*\b1\s*\}", "Home divider is not at y=282"),
    )
    for pattern, message in approved_bands:
        if not re.search(pattern, renderer_code):
            fail(message)


def assert_carousel_motion_contract(motion_source: str) -> None:
    pose_body = function_body(normalized_code(motion_source), "uiCarouselPose")
    expected = {
        "-2": (64, 179, 15),
        "-1": (152, 213, 87),
        "0": (240, 256, 255),
        "1": (328, 213, 87),
        "2": (416, 179, 15),
    }
    for slot, (center_x, zoom, opacity) in expected.items():
        pattern = (
            rf"\bcase\s+{re.escape(slot)}\s*:\s*return\s+CarouselPose\s*\{{\s*"
            rf"{center_x}\s*,\s*{zoom}\s*,\s*{opacity}\s*\}}"
        )
        if not re.search(pattern, pose_body):
            fail(f"carousel slot {slot} does not preserve 256/213/179 zoom hierarchy")


def assert_carousel_perf_contract(player_console_source: str) -> None:
    console_code = normalized_code(player_console_source)
    required_constants = {
        "kCarouselPerfTransitionMs": 220,
        "kCarouselPerfMinFrames": 6,
        "kCarouselPerfMinFps": 24,
        "kCarouselPerfMaxGapMs": 42,
        "kPerfMaxFirstFrameMs": 80,
        "kCarouselPerfObserveMs": 320,
        "kCarouselPerfRetargetMs": 80,
        "kCenterListPerfTransitionMs": 200,
        "kCenterListPerfRetargetMs": 160,
        "kCenterListPerfMinSteadyIntervals": 10,
        "kCenterListPerfColdMaxGapMs": 60,
        "kPerfSceneQuietMs": 250,
        "kPerfSceneTimeoutMs": 2500,
    }
    for name, value in required_constants.items():
        if not re.search(rf"\b{name}\s*=\s*{value}\s*;", console_code):
            fail(f"live carousel performance fixture requires {name}={value}")

    gate_body = function_body(console_code, "carouselPerfMeetsGate")
    for token, message in (
        ("kCarouselPerfMinFrames", "carousel performance gate does not require enough rendered frames"),
        ("kCarouselPerfMinFps", "carousel performance gate does not enforce 24 FPS"),
        ("kCarouselPerfMaxGapMs", "carousel performance gate does not enforce the 42ms gap"),
        ("kPerfMaxFirstFrameMs", "carousel performance gate does not enforce 80ms first-frame feedback"),
        ("kCarouselPerfTransitionMs", "carousel performance gate does not span the 220ms transition"),
    ):
        if token not in gate_body:
            fail(message)

    monitor_body = function_body(console_code, "carouselPerfMonitor")
    if not re.search(r"\+\+\s*carouselPerfProbe\.frames\b", monitor_body):
        fail("display monitor callback does not count completed LVGL refresh cycles")
    if not re.search(r"\bmillis\s*\(\s*\)", monitor_body):
        fail("display monitor callback does not timestamp completed refresh cycles")
    if not re.search(r"\bcarouselPerfProbe\.maxGapMs\b", monitor_body):
        fail("display monitor callback does not retain the largest adjacent refresh gap")
    if not re.search(r"\bcarouselPerfProbe\.frames\s*==\s*0\b", monitor_body) or \
            "carouselPerfProbe.firstFrameMs" not in monitor_body:
        fail("display monitor callback conflates first-frame feedback with adjacent refresh gaps")

    center_gate_body = function_body(console_code, "centerListPerfMeetsGate")
    for token, message in (
        ("kCenterListPerfMinSteadyIntervals",
         "center-list performance gate does not require ten steady intervals"),
        ("kCarouselPerfMinFps",
         "center-list performance gate does not enforce 24 steady FPS"),
        ("kPerfMaxFirstFrameMs",
         "center-list performance gate does not enforce 80ms first-frame feedback"),
        ("kCenterListPerfRequiredCoverageMs",
         "center-list performance gate does not span the chained scroll fixture"),
    ):
        if token not in center_gate_body:
            fail(message)

    if not re.search(
        r"~CarouselPerfMonitorGuard\s*\(\s*\)\s*\{\s*restore\s*\(\s*\)\s*;\s*\}",
        console_code,
    ):
        fail("carousel monitor fixture lacks scope-exit restoration")
    monitor_install_body = function_body(console_code, "install")
    for pattern, message in (
        (r"\bcarouselPerfProbe\.previousMonitor\s*=\s*display->driver->monitor_cb\s*;",
         "carousel monitor guard does not preserve the prior callback"),
        (r"\bcarouselPerfProbe\.armed\s*=\s*true\s*;",
         "carousel monitor guard does not arm measurement during installation"),
        (r"\bdisplay->driver->monitor_cb\s*=\s*carouselPerfMonitor\s*;",
         "carousel monitor guard does not install the probe callback"),
    ):
        if not re.search(pattern, monitor_install_body):
            fail(message)
    monitor_restore_body = function_body(console_code, "restore")
    for pattern, message in (
        (r"\bcarouselPerfProbe\.armed\s*=\s*false\s*;",
         "carousel monitor guard cleanup leaves measurement armed"),
        (r"\bdisplay->driver->monitor_cb\s*=\s*carouselPerfProbe\.previousMonitor\s*;",
         "carousel monitor guard cleanup does not restore the prior callback"),
    ):
        if not re.search(pattern, monitor_restore_body):
            fail(message)

    if re.search(r"\bkPerfSceneSettleMs\b", console_code):
        fail("performance fixture still substitutes a fixed delay for scene stability")
    settle_monitor_body = function_body(console_code, "sceneSettleMonitor")
    for token, message in (
        ("sceneSettleProbe.frames", "scene settle monitor does not count completed refreshes"),
        ("sceneSettleProbe.lastFrameMs", "scene settle monitor does not timestamp its last refresh"),
        ("sceneSettleProbe.previousMonitor", "scene settle monitor does not preserve callback chaining"),
    ):
        if token not in settle_monitor_body:
            fail(message)
    settle_body = function_body(console_code, "waitForStablePerfScene")
    for pattern, message in (
        (r"\blv_anim_count_running\s*\(\s*\)",
         "scene settle barrier does not wait for preparation animations to finish"),
        (r"\bkPerfSceneQuietMs\b",
         "scene settle barrier does not require a post-refresh quiet window"),
        (r"\bkPerfSceneTimeoutMs\b",
         "scene settle barrier is not bounded by a timeout"),
        (r"\bsceneSettleProbe\.frames\b",
         "scene settle barrier can pass before a preparation frame completes"),
        (r"\bdisplay->driver->monitor_cb\s*=\s*sceneSettleProbe\.previousMonitor\s*;",
         "scene settle barrier does not restore the preparation monitor hook"),
    ):
        if not re.search(pattern, settle_body):
            fail(message)

    prepare_body = function_body(console_code, "preparePerfScenario")
    required_prepare = (
        (r"\bdisplay->driver->monitor_cb\s*=\s*sceneSettleMonitor\s*;",
         "scene preparation does not install its refresh monitor while unarmed"),
        (r"\buiRendererRender\s*\(",
         "scene preparation does not render the target state while unarmed"),
        (r"\blv_timer_ready\s*\(\s*display->refr_timer\s*\)",
         "scene preparation does not force its pending refresh to run"),
        (r"\bwaitForStablePerfScene\s*\(",
         "scene preparation does not wait for completed stable refreshes"),
    )
    for pattern, message in required_prepare:
        if not re.search(pattern, prepare_body):
            fail(message)
    install_settle = prepare_body.find("display->driver->monitor_cb = sceneSettleMonitor;")
    prepare_render = prepare_body.find("uiRendererRender(")
    prepare_unlock = prepare_body.find("lvgl_port_unlock();", prepare_render)
    stable_wait = prepare_body.find("waitForStablePerfScene(", prepare_unlock)
    if min(install_settle, prepare_render, prepare_unlock, stable_wait) < 0 or not (
            install_settle < prepare_render < prepare_unlock < stable_wait):
        fail("scene preparation is not monitored before render and settled after unlocking")

    fixture_body = function_body(console_code, "runCarouselPerfFixture")
    required_fixture = (
        (r"\blv_disp_get_default\s*\(\s*\)",
         "live carousel performance fixture does not use the initialized physical display"),
        (r"\bCarouselPerfMonitorGuard\s+monitorGuard\s*;",
         "live carousel performance fixture does not own a scope cleanup guard"),
        (r"\bmonitorGuard\.install\s*\(\s*display\s*\)",
         "live carousel performance fixture does not install through its cleanup guard"),
        (r"\bmonitorGuard\.restore\s*\(\s*\)",
         "live carousel performance fixture does not restore under the final LVGL lock"),
        (r"\bappHandleInput\s*\(",
         "live carousel performance fixture does not use the production focus path"),
        (r"\bappHandleUiEvent\s*\(",
         "live carousel performance fixture does not exercise the touch-swipe UiEvent path"),
        (r"\bUiEventKind::ListNext\b",
         "live carousel performance fixture does not dispatch the touch-swipe focus step"),
        (r"\buiRendererResetTestStats\s*\(\s*\)",
         "live performance fixture does not reset renderer path evidence before input"),
        (r"\buiRendererGetTestStats\s*\(\s*\)",
         "live performance fixture does not capture incremental/rebuild evidence"),
        (r"\bdelay\s*\(\s*kCarouselPerfRetargetMs\s*\)",
         "live carousel performance fixture does not retarget during the visible animation"),
        (r"\buiRendererRender\s*\(",
         "live carousel performance fixture does not start the retained Home transition"),
        (r"\bdelay\s*\(\s*kCarouselPerfObserveMs\s*\)",
         "live carousel performance fixture is not bounded to its observation window"),
    )
    for pattern, message in required_fixture:
        if not re.search(pattern, fixture_body):
            fail(message)
    first_lock = fixture_body.find("lvgl_port_lock(")
    prepare = fixture_body.find("preparePerfScenario(")
    install = fixture_body.find("monitorGuard.install(display)")
    rotate_input = fixture_body.find("appHandleInput(", install)
    swipe_input = fixture_body.find("appHandleUiEvent(", install)
    input_positions = [position for position in (rotate_input, swipe_input) if position >= 0]
    if not input_positions:
        fail("live carousel fixture does not dispatch a measured user input")
    first_input = min(input_positions)
    render = fixture_body.find("uiRendererRender(")
    first_unlock = fixture_body.find("lvgl_port_unlock();", render)
    wait = fixture_body.find("delay(kCarouselPerfObserveMs);", first_unlock)
    second_lock = fixture_body.find("lvgl_port_lock(", first_unlock)
    restore = fixture_body.find("monitorGuard.restore()", second_lock)
    if min(prepare, first_lock, install, first_input, render, first_unlock,
           wait, second_lock, restore) < 0 or not (
            prepare < first_lock < install < first_input < render < first_unlock <
            wait < second_lock < restore):
        fail("live carousel fixture does not mutate under lock, run unlocked, then restore under lock")
    if "preparePerfScenario(" in fixture_body[install:]:
        fail("live carousel fixture rebuilds its target scene after measurement is armed")

    setup_body = function_body(console_code, "setup")
    component_logic = setup_body.find("runLvglComponentTests(testOutput)")
    restart = setup_body.find("esp_restart()", component_logic)
    pure_logic = setup_body.find("runPureLogicTests(testOutput)", restart)
    board = setup_body.find("Board *board")
    display_init = setup_body.find("lvgl_port_init(")
    renderer_init = setup_body.find("uiRendererBegin(")
    initial_render = setup_body.find("uiRendererRender(app")
    perf = setup_body.find("runCarouselPerfFixture(")
    if min(component_logic, restart, pure_logic, board, display_init, renderer_init,
           initial_render, perf) < 0:
        fail("setup does not contain both pre-display logic and post-display performance phases")
    if not (component_logic < restart < pure_logic < board < display_init < renderer_init <
            initial_render < perf):
        fail("fake fixtures, clean reboot, pure logic, live Home, and performance phases are misordered")
    if setup_body.count("runLvglComponentTests(testOutput)") != 1:
        fail("fake-display component fixtures must run exactly once before the clean reboot")
    if not re.search(r"\bRTC_NOINIT_ATTR\s+SelfTestCheckpoint\b", console_code):
        fail("component result is not carried across the clean self-test reboot in RTC memory")
    if not re.search(r"\besp_reset_reason\s*\(\s*\)\s*==\s*ESP_RST_SW\b", setup_body):
        fail("self-test checkpoint is not restricted to the intentional software reboot")
    if len(re.findall(r"\blv_is_initialized\s*\(\s*\)", setup_body)) < 2:
        fail("pure logic phase does not prove that LVGL stays uninitialized")
    if not re.search(
        r"\bconst\s+bool\s+passed\s*=\s*purePassed\s*&&\s*componentPassed\s*&&\s*"
        r"livePerfPassed\s*;",
        setup_body,
    ):
        fail("SELFTEST PASS is not gated by pure, component, and the complete live matrix")
    for marker in (
        "CAROUSEL PERF WAIT_FWD",
        "CAROUSEL PERF WAIT_WRAP_REV",
        "CAROUSEL PERF MYTURN_5",
        "CAROUSEL PERF RETARGET",
        "CAROUSEL PERF SWIPE_EVENT",
        "LIST PERF ASSETS_SCROLL_COLD",
        "LIST PERF ASSETS_SCROLL_WARM",
    ):
        if marker not in player_console_source:
            fail(f"self-test is missing auditable scenario marker: {marker}")
    if "coverage_ms=%u" not in player_console_source:
        fail("live performance evidence does not print full transition coverage")
    if "first_frame_ms=%u" not in player_console_source:
        fail("live performance evidence does not print first-frame feedback separately")
    if "incremental=%u rebuild=%u" not in player_console_source:
        fail("live performance evidence does not print renderer path counters")
    if not re.search(r"\bincrementalOnly\b[^;]*result\.incrementalRenders\s*>\s*0[^;]*"
                     r"result\.rebuildRenders\s*==\s*0", fixture_body, re.DOTALL):
        fail("live performance gate does not reject renderer rebuilds during measured input")


def assert_component_fixture_contract(logic_tests_source: str,
                                      renderer_source: str) -> None:
    logic_code = normalized_code(logic_tests_source)
    renderer_code = normalized_code(renderer_source)
    for function_name in (
        "runModalComponentTests", "runCarouselComponentTests",
        "runCenterListComponentTests", "runHomeRendererTests",
    ):
        body = function_body(logic_code, function_name)
        for pattern, message in (
            (r"\bpreviousDisplay\s*=\s*lv_disp_get_default\s*\(\s*\)",
             f"{function_name} does not retain the prior default display"),
            (r"\blv_disp_set_default\s*\(\s*display\s*\)",
             f"{function_name} does not isolate objects on its fake display"),
            (r"\blv_disp_set_default\s*\(\s*previousDisplay\s*\)",
             f"{function_name} does not restore the prior default display"),
        ):
            if not re.search(pattern, body):
                fail(message)

    home_body = function_body(logic_code, "runHomeRendererTests")
    if not re.search(r"\buiRendererResetForTest\s*\(\s*\)", home_body):
        fail("Home renderer fixture leaves retained renderer ownership dangling")
    reset_body = function_body(renderer_code, "uiRendererResetForTest")
    for pattern, message in (
        (r"\buiCarouselDestroy\s*\(\s*homeCarousel\s*\)",
         "renderer fixture reset does not release the retained carousel"),
        (r"\buiCenterListDestroy\s*\(\s*centerList\s*\)",
         "renderer fixture reset does not release the retained center list"),
        (r"\buiModalDestroy\s*\(\s*activeModal\s*\)",
         "renderer fixture reset does not release the retained modal"),
        (r"\broot\s*=\s*nullptr\s*;", "renderer fixture reset leaves a dangling root"),
        (r"\buiSetEventSink\s*\(\s*nullptr\s*\)",
         "renderer fixture reset leaves the test event sink installed"),
    ):
        if not re.search(pattern, reset_body):
            fail(message)

    pure_body = function_body(logic_code, "runPureLogicTests")
    if not re.search(r"\brunStateLogicTests\s*\(", pure_body):
        fail("pre-display pure test phase does not run the state suite")
    component_body = function_body(logic_code, "runLvglComponentTests")
    for function_name in (
        "runModalComponentTests", "runCenterListComponentTests",
        "runCarouselComponentTests", "runHomeRendererTests",
    ):
        if not re.search(rf"\b{function_name}\s*\(", component_body):
            fail(f"post-init component phase omits {function_name}")
    for evidence in (
        "My Turn Dice-to-Assets selection updates two dynamic cards without static restyle",
        "center-list focus updates only previous/new rows without duplicate content writes",
        "center-list refreshes changed text and uses a graphical selected indicator",
        "My Turn focus change uses retained incremental renderer path",
        "first-row boundary pulse cancels active track travel and owns track y alone",
        "focus plus forced-payment modal rebuilds instead of swallowing the overlay",
        "focus plus money and toast changes rebuilds every visible field",
    ):
        if evidence not in logic_tests_source:
            fail(f"component tests omit incremental invalidation evidence: {evidence}")


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
    for api in ("uiCenterListCreate", "uiCenterListUpdate", "uiCenterListDestroy",
                "uiCenterListSwipeStep", "uiCenterListBoundaryPulse"):
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
        "kCenterListRefreshPeriodMs": 16,
        "kTrackBoundaryPulsePx": 8,
        "kFooterBoundaryPulsePx": 6,
        "kBoundaryPulseOutMs": 60,
        "kBoundaryPulseReturnMs": 120,
    }
    for name, value in required_constants.items():
        if not re.search(rf"\b{name}\s*=\s*{value}\s*;", center_code):
            fail(f"center list {name} must be exactly {value}")
    center_rectangles = read_rectangles(center_source)
    expected_tag_geometry = {
        "kTaggedTitle": (14, 8, 194, 28),
        "kTagPill": (216, 14, 40, 22),
        "kTagText": (2, 2, 36, 18),
    }
    for name, expected in expected_tag_geometry.items():
        values = center_rectangles.get(name, [])
        if values != [expected]:
            fail(f"center-list ownership tag {name} must be exactly {expected}, got {values}")
    tagged_title = expected_tag_geometry["kTaggedTitle"]
    tag_pill = expected_tag_geometry["kTagPill"]
    if tagged_title[0] + tagged_title[2] > tag_pill[0]:
        fail("center-list ownership tag overlaps the activity title")
    if tag_pill[0] + tag_pill[2] > 272 or tag_pill[1] + tag_pill[3] > required_constants["kRowHeight"]:
        fail("center-list ownership tag escapes its clipped row")
    if not re.search(r"\bconst\s+char\s*\*\s*tag\s*=\s*\"\"\s*;", center_header):
        fail("UiListItemView does not expose an optional fixed ownership tag")
    if not re.search(
        r"\bstatic_assert\s*\([^;]*kNormalListViewport\.w\s*==\s*kNormalListFocus\.w"
        r"[^;]*kNormalListViewport\.h\s*==\s*3\s*\*\s*kRowHeight",
        center_code,
        re.DOTALL,
    ):
        fail("center list does not compile-check the exact three-row clipped viewport")

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
        (r"\buiBindTap\s*\(\s*list\.footer\s*,\s*UiEventKind::SelectFooter\s*\)",
         "center list footer does not emit semantic footer selection"),
        (r"\buiBox\s*\(\s*row\s*,\s*kTagPill\b",
         "center list does not create the fixed ownership-tag pill"),
    )
    for pattern, message in required_create:
        if not re.search(pattern, create_body, re.DOTALL):
            fail(message)
    if not re.search(
        r"\blv_timer_set_period\s*\(\s*display->refr_timer\s*,\s*"
        r"kCenterListRefreshPeriodMs\s*\)",
        create_body,
    ):
        fail("center list does not prepare a bounded 16ms display refresh cadence")
    destroy_body = function_body(center_code, "destroyListObjects")
    if not re.search(
        r"\blv_timer_set_period\s*\(\s*display->refr_timer\s*,\s*"
        r"LV_DISP_DEF_REFR_PERIOD\s*\)",
        destroy_body,
    ):
        fail("center-list destruction does not restore the default display cadence")

    animate_body = function_body(center_code, "animateTrack")
    if not re.search(r"\blv_obj_get_y\s*\(", animate_body):
        fail("center-list travel does not restart from the current track y")
    if not re.search(r"\blv_anim_set_time\s*\([^,]+,\s*200\s*\)", animate_body):
        fail("center-list track travel is not exactly 200ms")
    if not re.search(r"\blv_anim_set_path_cb\s*\([^,]+,\s*lv_anim_path_ease_out\s*\)",
                     animate_body):
        fail("center-list track travel does not use ease-out")
    if not re.search(r"\blv_timer_ready\s*\(\s*display->refr_timer\s*\)", animate_body):
        fail("center-list track travel does not schedule its first physical refresh")

    update_body = function_body(center_code, "uiCenterListUpdate")
    if not re.search(r"\buiCenterListTrackY\s*\(", update_body):
        fail("center-list update does not use the pure center-lock target helper")
    if not re.search(r"\buiListProgressPermille\s*\(", update_body):
        fail("center-list update does not use selected-row progress")
    if re.search(r"\b(?:lv_bar_create|lv_slider_create|LV_SCROLLBAR_MODE)\b", center_code):
        fail("center list creates a forbidden scrollbar")
    label_guard_body = function_body(center_code, "setLabelTextIfChanged")
    if not re.search(r"\bstrcmp\s*\(", label_guard_body) or not re.search(
            r"\blv_label_get_text\s*\(", label_guard_body):
        fail("center-list labels are not guarded against identical text rewrites")
    flag_guard_body = function_body(center_code, "setClickableIfChanged")
    if not re.search(r"\blv_obj_has_flag\s*\(", flag_guard_body):
        fail("center-list clickable flags are not guarded against identical rewrites")
    content_body = function_body(center_code, "updateRowContent")
    if "setLabelTextIfChanged(" not in content_body or "setClickableIfChanged(" not in content_body:
        fail("center-list row content does not use text and flag change guards")
    if "updateTag(" not in content_body:
        fail("center-list row content does not update the optional ownership tag")
    selection_row_body = function_body(center_code, "applyRowSelectionStyle")
    if "lv_label_set_text(" in selection_row_body or "setLabelTextIfChanged(" in selection_row_body:
        fail("center-list selection path rewrites row text")
    if "styleRow(" in update_body:
        fail("center-list update still restyles every row through the legacy combined path")
    if update_body.count("applyRowSelectionStyle(") != 2:
        fail("center-list update must dynamically style only previous and new focus rows")
    if not re.search(r"\bconst\s+uint8_t\s+previousSelected\s*=\s*list\.selected\s*;",
                     update_body):
        fail("center-list update does not retain the previous focus row")

    pulse_body = function_body(center_code, "uiCenterListBoundaryPulse")
    for pattern, message in (
        (r"\bcancelBoundaryPulse\s*\(\s*list\s*\)",
         "boundary pulse does not cancel an in-flight pulse before restarting"),
        (r"\bkTrackBoundaryPulsePx\b", "first-row boundary pulse is not +8px"),
        (r"\bkFooterBoundaryPulsePx\b", "footer boundary pulse is not +6px"),
        (r"\bkBoundaryPulseOutMs\b", "boundary pulse outward phase is not 60ms"),
        (r"\blv_anim_set_ready_cb\s*\([^,]+,\s*returnBoundaryPulse\s*\)",
         "boundary pulse does not return to rest"),
    ):
        if not re.search(pattern, pulse_body):
            fail(message)
    if not re.search(r"\blv_anim_set_values\s*\([^,]+,\s*currentY\s*,\s*"
                     r"static_cast<int16_t>\(list\.pulseRestY\s*\+\s*offset\)\s*\)",
                     pulse_body):
        fail("boundary pulse target is not anchored to its fixed rest position")
    capture_y = pulse_body.find("const int16_t currentY = lv_obj_get_y(target);")
    cancel_travel = pulse_body.find("lv_anim_del(list.track, setTrackY);")
    pulse_values = pulse_body.find("lv_anim_set_values(", cancel_travel)
    if min(capture_y, cancel_travel, pulse_values) < 0 or not (
            capture_y < cancel_travel < pulse_values):
        fail("first-row boundary pulse does not capture visual y then cancel track travel")
    if not re.search(
        r"\bif\s*\(\s*firstRow\s*&&\s*list\.track\s*!=\s*nullptr\s*\)\s*"
        r"lv_anim_del\s*\(\s*list\.track\s*,\s*setTrackY\s*\)\s*;",
        pulse_body,
    ):
        fail("footer boundary pulse must not cancel unrelated track travel")
    return_body = function_body(center_code, "startBoundaryReturn")
    if not re.search(r"\bkBoundaryPulseReturnMs\b", return_body) or not re.search(
            r"\blv_anim_path_ease_out\b", return_body):
        fail("boundary pulse does not return over 120ms with ease-out")
    if not re.search(r"\binterruptedPulse\s*==\s*list\.footer", update_body) or not re.search(
            r"\bstartBoundaryReturn\s*\(\s*list\s*\)", update_body):
        fail("normal list focus movement does not cancel boundary feedback")

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
    dispatch_body = function_body(center_code, "uiCenterListDispatchSwipe")
    reset_position = dispatch_body.find("lv_indev_reset(")
    emit_position = dispatch_body.find("lv_event_send(")
    if reset_position < 0 or emit_position < 0 or reset_position > emit_position:
        fail("center-list swipe does not cancel LVGL's follow-up row click before emitting a step")
    if len(re.findall(r"\blv_event_send\s*\(", dispatch_body)) != 2:
        fail("center-list release must expose exactly one previous/next event branch")

    for function_name, count_token in (("drawDemoLab", "kDemoListCount"),):
        body = function_body(renderer_code, function_name)
        if re.search(r"\bdrawListRow\s*\(", body):
            fail(f"{function_name} still renders legacy root rows")
        if not re.search(rf"\bUiListItemView\s+items\s*\[\s*{count_token}\s*\]", body):
            fail(f"{function_name} does not build the complete retained item view")
        if not re.search(rf"\buiCenterListCreate\s*\([^;]*\b{count_token}\b", body, re.DOTALL):
            fail(f"{function_name} does not create one retained center list")

    assets_body = function_body(renderer_code, "drawAssets")
    if re.search(r"\bdrawListRow\s*\(", assets_body):
        fail("drawAssets still renders legacy root rows")
    if not re.search(r"\bUiListItemView\s+items\s*\[\s*kSyncedAssetCapacity\s*\]", assets_body):
        fail("drawAssets does not allocate the complete authority asset view")
    if not re.search(r"\bcount\s*=\s*appVisibleAssetCount\s*\(", assets_body):
        fail("drawAssets does not derive its row count from the authority projection")
    if not re.search(r"\buiCenterListCreate\s*\([^;]*\bcount\b", assets_body, re.DOTALL):
        fail("drawAssets does not create one dynamic retained center list")

    asset_detail_body = function_body(renderer_code, "drawAssetDetail")
    for pattern, message in (
        (r"\bconstexpr\s+UiRect\s+fourActionRects\s*\[\s*4\s*\]\s*=\s*\{\s*"
         r"kAssetDetailAction0\s*,\s*kAssetDetailAction1\s*,\s*"
         r"kAssetDetailAction2\s*,\s*kAssetDetailAction3",
         "Asset detail does not retain the four-action balanced layout"),
        (r"\buint8_t\s+visibleSlots\s*\[\s*4\s*\]",
         "Asset detail does not retain semantic action indexes while adapting layout"),
        (r"\bconst\s+AssetDetailAction\s+action\s*=\s*"
         r"appAssetDetailActionAt\s*\(\s*state\s*,\s*index\s*\)",
         "Asset detail does not resolve actions by their semantic slot"),
        (r"\bif\s*\(\s*appAssetDetailActionVisible\s*\([^)]*\)\s*\)\s*"
         r"visibleSlots\s*\[\s*visibleCount\+\+\s*\]\s*=\s*index",
         "Asset detail does not collect only visible semantic actions"),
        (r"\bif\s*\(\s*visibleCount\s*==\s*1\s*\).*kAssetDetailSingleAction",
         "Asset detail has no centered single-action layout"),
        (r"\belse\s+if\s*\(\s*visibleCount\s*==\s*2\s*\).*"
         r"kAssetDetailPairLeft.*kAssetDetailPairRight",
         "Asset detail has no balanced two-action layout"),
        (r"\belse\s+if\s*\(\s*visibleCount\s*==\s*3\s*\).*"
         r"kAssetDetailTripleTopLeft.*kAssetDetailTripleTopRight.*"
         r"kAssetDetailTripleBottom",
         "Asset detail has no balanced three-action layout"),
    ):
        if not re.search(pattern, asset_detail_body, re.DOTALL):
            fail(message)

    players_body = function_body(renderer_code, "drawPlayers")
    if re.search(r"\bdrawListRow\s*\(", players_body):
        fail("drawPlayers still renders legacy root rows")
    if not re.search(r"\bUiListItemView\s+items\s*\[\s*kMaxPlayerCount\s*\]", players_body):
        fail("drawPlayers does not allocate the bounded six-player retained view")
    if not re.search(r"\buiCenterListCreate\s*\([^;]*\bstate\.playerCount\b",
                     players_body, re.DOTALL):
        fail("drawPlayers does not size the retained list from the authority snapshot")

    if not re.search(r"\bmakeClickable\s*\(\s*footer\s*,\s*TouchAction::Footer\s*\)",
                     renderer_code):
        fail("ordinary fixed footer does not emit semantic footer selection")
    draw_debt_body = function_body(renderer_code, "drawDebt")
    if not re.search(r"\bdrawFooter\s*\(", draw_debt_body):
        fail("ordinary Debt page does not render its fixed footer")

    render_body = function_body(renderer_code, "uiRendererRender")
    modal_guard_body = function_body(renderer_code, "modalVisualStateUnchanged")
    for pattern, message in (
        (r"\bstate\.kind\s*!=\s*rendered\.kind\b",
         "renderer modal guard omits modal kind"),
        (r"\bstate\.submitting\s*==\s*rendered\.submitting\b",
         "renderer modal guard omits submitting state"),
    ):
        if not re.search(pattern, modal_guard_body):
            fail(message)
    visible_guard_body = function_body(renderer_code, "samePageVisibleStateUnchanged")
    for pattern, message in (
        (r"\bmodalVisualStateUnchanged\s*\(", "renderer visible-state guard omits modal state"),
        (r"\bstate\.money\s*==\s*rendered\.money\b", "renderer visible-state guard omits money"),
        (r"\bstate\.position\s*==\s*rendered\.position\b", "renderer visible-state guard omits position"),
        (r"\btextStateUnchanged\s*\(\s*state\.toast\s*,\s*rendered\.toast\s*\)",
         "renderer visible-state guard omits toast text"),
        (r"\bstate\.toastUntilMs\s*==\s*rendered\.toastUntilMs\b",
         "renderer visible-state guard omits toast deadline"),
        (r"\bstate\.tradeAmount\s*==\s*rendered\.tradeAmount\b",
         "renderer visible-state guard omits editable values"),
    ):
        if not re.search(pattern, visible_guard_body):
            fail(message)
    same_page_match = re.search(
        r"\bif\s*\(\s*hasRenderedState\s*&&\s*state\.page\s*==\s*previousPage[\s\S]{0,500}?\)\s*\{",
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

    offer_body = function_body(renderer_code, "drawTradeOffer")
    for pattern, message in (
        (r"\bconst\s+UiRect\s+receiveRect\s*\{\s*82\s*,\s*140\s*,\s*146\s*,\s*122\s*\}",
         "Trade offer receive card moved outside its approved round-safe column"),
        (r"\bconst\s+UiRect\s+giveRect\s*\{\s*252\s*,\s*140\s*,\s*146\s*,\s*122\s*\}",
         "Trade offer give card moved outside its approved round-safe column"),
        (r"\bdrawDecisionButton\s*\(\s*UiRect\s*\{\s*82\s*,\s*300\s*,\s*96\s*,\s*46\s*\}",
         "Trade accept action moved outside its approved bottom row"),
        (r"\bdrawDecisionButton\s*\(\s*UiRect\s*\{\s*192\s*,\s*300\s*,\s*96\s*,\s*46\s*\}",
         "Trade counter action moved outside its approved bottom row"),
        (r"\bdrawDecisionButton\s*\(\s*UiRect\s*\{\s*302\s*,\s*300\s*,\s*96\s*,\s*46\s*\}",
         "Trade reject action moved outside its approved bottom row"),
        (r"\bdrawFooter\s*\(",
         "Trade offer page omits its fixed Back footer"),
    ):
        if not re.search(pattern, offer_body):
            fail(message)
    if re.search(r"\bremainingMs\b|\bexpiresAtMs\s*>\s*nowMs\b", offer_body):
        fail("Trade offer renders a stale countdown without a retained time update")


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

    progress_bottom = rectangles["kNormalListProgress"][1] + rectangles["kNormalListProgress"][3]
    count_y = rectangles["kNormalListCount"][1]
    count_bottom = count_y + rectangles["kNormalListCount"][3]
    footer_y = rectangles["kNormalFooter"][1]
    if count_y < progress_bottom or count_bottom > footer_y:
        fail("list count must stay below progress and above the fixed footer")

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
        r"appPresentedHomePhase\s*\(\s*state\s*\)\s*==\s*"
        r"appPresentedHomePhase\s*\(\s*previousRenderedState\s*\)\s*;",
        render_body,
    ):
        fail("renderer does not compare the current and rendered presented Home phases")
    if not re.search(
        r"\bconst\s+bool\s+inlineEditUnchanged\s*=\s*"
        r"state\.inlineEditField\s*==\s*previousInlineEditField\s*;",
        render_body,
    ):
        fail("renderer does not compare the current and rendered inline editor")
    if not re.search(
        r"\bconst\s+bool\s+visibleStateUnchanged\s*=\s*"
        r"samePageVisibleStateUnchanged\s*\(\s*state\s*,\s*previousRenderedState\s*\)\s*;",
        render_body,
    ):
        fail("renderer does not compare all visible state before incremental refresh")
    same_page_match = re.search(
        r"\bif\s*\(\s*hasRenderedState\s*&&\s*state\.page\s*==\s*previousPage\s*&&\s*"
        r"\(\s*state\.focus\s*!=\s*previousFocus\s*\|\|\s*boundaryPulseChanged\s*\)\s*&&\s*"
        r"homeActionsUnchanged\s*&&\s*"
        r"inlineEditUnchanged\s*&&\s*visibleStateUnchanged\s*\)\s*\{",
        render_body,
    )
    if not same_page_match:
        fail("same-page focus-update branch is not guarded by the rendered Home phase")
    same_page_condition = same_page_match.group(0)
    if "homeActionsUnchanged" not in same_page_condition:
        fail("same-page focus-update branch is not guarded by the rendered Home phase")
    if "inlineEditUnchanged" not in same_page_condition:
        fail("same-page focus-update branch is not guarded by the rendered inline editor")
    if "visibleStateUnchanged" not in same_page_condition:
        fail("same-page focus-update branch can swallow other visible state changes")
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
        r"\bpreviousInlineEditField\s*=\s*state\.inlineEditField\s*;",
        render_body,
        re.DOTALL,
    ):
        fail("rebuild does not retain the inline editor for focus-only updates")
    if len(re.findall(r"\bpreviousRenderedState\s*=\s*state\s*;", render_body)) < 2:
        fail("renderer does not retain visible state after both incremental and rebuild paths")

    if not re.search(r"\+\+\s*rendererTestStats\.incrementalRenders\b", same_page_body):
        fail("renderer self-test evidence does not count retained incremental renders")
    rebuild_counter = render_body.find("++rendererTestStats.rebuildRenders;")
    rebuild_call = render_body.find("rebuild(state, nowMs);")
    if rebuild_counter < 0 or rebuild_call < 0 or rebuild_counter > rebuild_call:
        fail("renderer self-test evidence does not count rebuild renders")

    assert_modal_contract(renderer_source)


def expect_rejected(name: str, layout_source: str, renderer_source: str,
                    carousel_header: str, carousel_source: str) -> None:
    try:
        verify_contract(layout_source, renderer_source, carousel_header, carousel_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def expect_perf_rejected(name: str, player_console_source: str) -> None:
    try:
        assert_carousel_perf_contract(player_console_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def expect_motion_rejected(name: str, motion_source: str) -> None:
    try:
        assert_carousel_motion_contract(motion_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def expect_component_rejected(name: str, logic_tests_source: str,
                              renderer_source: str) -> None:
    try:
        assert_component_fixture_contract(logic_tests_source, renderer_source)
    except LayoutError:
        return
    raise AssertionError(f"{name} fixture unexpectedly passed")


def run_self_test(layout_source: str, renderer_source: str, carousel_header: str,
                  carousel_source: str, motion_source: str, center_header: str,
                  center_source: str, player_console_source: str,
                  logic_tests_source: str) -> None:
    verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                    center_header, center_source)
    assert_carousel_motion_contract(motion_source)
    assert_carousel_perf_contract(player_console_source)
    assert_component_fixture_contract(logic_tests_source, renderer_source)
    inactive_duplicate = layout_source + "\n#if 0\nconstexpr UiRect kModalRect{70, 88, 340, 304};\n#endif\n"
    expect_rejected("inactive duplicate", inactive_duplicate, renderer_source,
                    carousel_header, carousel_source)
    unused_modal = renderer_source.replace("applyModal(state, nowMs, true);", "", 1)
    expect_rejected("unused modal rectangle", layout_source, unused_modal,
                    carousel_header, carousel_source)
    modal_body = function_body(code_only(renderer_source), "drawModal")
    dummy_modal = "void drawModal(const AppState &state, uint32_t nowMs) {" + modal_body + "}"
    noncompliant_live_modal = renderer_source.replace(
        "applyModal(state, nowMs, true);", "applyModal(state, nowMs, false);", 1
    )
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
    modal_header = MODAL_HEADER_PATH.read_text(encoding="utf-8")
    modal_source = MODAL_SOURCE_PATH.read_text(encoding="utf-8")
    modal_mutations = (
        ("modal shade opacity changed", modal_header,
         modal_source.replace("kShadeOpacity = 184", "kShadeOpacity = 160", 1)),
        ("modal track inset changed", modal_header,
         modal_source.replace("kHoldTrack{12, 46, 200, 8}",
                              "kHoldTrack{8, 46, 208, 8}", 1)),
        ("modal fill escaped track", modal_header,
         modal_source.replace("uiBox(modal.holdTrack, UiRect{0, 0, 0, kHoldTrack.h}",
                              "uiBox(modal.confirm, UiRect{0, 0, 0, kHoldTrack.h}", 1)),
        ("modal confirm overflow clipping removed", modal_header,
         modal_source.replace("makeNonOverflowing(modal.confirm);", "", 1)),
        ("modal hold touch binding removed", modal_header,
         modal_source.replace("uiBindHold(modal.confirm);", "", 1)),
        ("modal Back event binding removed", modal_header,
         modal_source.replace("uiBindTap(modal.back, UiEventKind::Back);", "", 1)),
        ("submitted modal Back guard removed", modal_header,
         modal_source.replace("view.showBack && !view.submitting", "view.showBack", 1)),
        ("modal progress clamp removed", modal_header,
         modal_source.replace("permille > 1000 ? 1000 : permille", "permille", 1)),
    )
    for name, mutated_header, mutated_source in modal_mutations:
        try:
            assert_modal_contract(renderer_source, mutated_header, mutated_source)
        except LayoutError:
            continue
        raise AssertionError(f"{name} fixture unexpectedly passed")
    expect_rejected("retained Home selection removed", layout_source,
                    renderer_source.replace("uiCarouselSetSelection(homeCarousel, state.focus, true);", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("retained Home recreated", layout_source,
                    renderer_source.replace("uiCarouselSetSelection(homeCarousel, state.focus, true);",
                                            "drawHome(state, nowMs);", 1),
                    carousel_header, carousel_source)
    expect_rejected("Home phase guard removed", layout_source,
                    renderer_source.replace("homeActionsUnchanged &&", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("presented Home phase comparison bypassed", layout_source,
                    renderer_source.replace(
                        "appPresentedHomePhase(state) == appPresentedHomePhase(previousRenderedState)",
                        "true", 1),
                    carousel_header, carousel_source)
    expect_rejected("inline editor guard removed", layout_source,
                    renderer_source.replace("inlineEditUnchanged", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("visible state guard bypassed", layout_source,
                    renderer_source.replace(
                        "inlineEditUnchanged && visibleStateUnchanged",
                        "inlineEditUnchanged && true", 1),
                    carousel_header, carousel_source)
    expect_rejected("modal submitting guard removed", layout_source,
                    renderer_source.replace(
                        "state.submitting == rendered.submitting &&", "", 1),
                    carousel_header, carousel_source)
    expect_rejected("rendered inline editor update removed", layout_source,
                    renderer_source.replace("previousInlineEditField = state.inlineEditField;", "", 1),
                    carousel_header, carousel_source)
    expect_rejected(
        "wrap path storage removed",
        layout_source,
        renderer_source,
        carousel_header.replace("CarouselPath paths[5]{};", "", 1),
        carousel_source,
    )
    carousel_mutations = (
        ("master progress range changed",
         carousel_source.replace("lv_anim_set_values(&animation, 0, 1000);",
                                 "lv_anim_set_values(&animation, 0, 999);", 1)),
        ("per-property animation restored",
         carousel_source + "\nvoid animateProperty() {}\n"),
        ("Home swipe click reset removed",
         carousel_source.replace("lv_indev_reset(input, carousel->container);", "", 1)),
        ("item event path removed",
         carousel_source.replace("lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);", "", 1)),
        ("deferred initial geometry guard removed",
         carousel_source.replace("lv_anim_set_early_apply(&animation, false);", "", 1)),
        ("safe physical refresh scheduling removed",
         carousel_source.replace("lv_timer_ready(display->refr_timer);", "", 1)),
        ("prepared Home refresh cadence removed",
         carousel_source.replace(
             "lv_timer_set_period(display->refr_timer, kCarouselRefreshPeriodMs);", "", 1
         )),
        ("default refresh cadence restore removed",
         carousel_source.replace(
             "lv_timer_set_period(display->refr_timer, LV_DISP_DEF_REFR_PERIOD);", "", 1
         )),
        ("font crossfade restored to one hard font",
         carousel_source.replace("drawCrossfadedLabel(", "drawSingleLabel(", 1)),
    )
    for name, mutated_carousel in carousel_mutations:
        try:
            verify_contract(layout_source, renderer_source, carousel_header, mutated_carousel,
                            center_header, center_source)
        except LayoutError:
            continue
        raise AssertionError(f"{name} fixture unexpectedly passed")
    expect_motion_rejected(
        "neighbor zoom hierarchy changed",
        motion_source.replace("152, 213, 87", "152, 224, 87", 1),
    )
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
         center_source.replace("lv_indev_reset(input, target);", "", 1)),
        ("first-row boundary distance changed",
         center_source.replace("kTrackBoundaryPulsePx = 8;",
                               "kTrackBoundaryPulsePx = 7;", 1)),
        ("footer boundary duration changed",
         center_source.replace("kBoundaryPulseOutMs = 60;",
                               "kBoundaryPulseOutMs = 61;", 1)),
        ("boundary restart cancellation removed",
         center_source.replace("cancelBoundaryPulse(list);", "", 1)),
        ("boundary fixed-rest target removed",
         center_source.replace("list.pulseRestY + offset", "currentY + offset", 1)),
        ("boundary track-travel takeover removed",
         center_source.replace(
             "if (firstRow && list.track != nullptr) lv_anim_del(list.track, setTrackY);",
             "", 1
         )),
        ("center-list refresh cadence removed",
         center_source.replace(
             "lv_timer_set_period(display->refr_timer, kCenterListRefreshPeriodMs);", "", 1
         )),
        ("center-list first refresh scheduling removed",
         center_source.replace("lv_timer_ready(display->refr_timer);", "", 1)),
        ("center-list default cadence restore removed",
         center_source.replace(
             "lv_timer_set_period(display->refr_timer, LV_DISP_DEF_REFR_PERIOD);", "", 1
         )),
    )
    for name, mutated_center in center_mutations:
        try:
            verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                            center_header, mutated_center)
        except LayoutError:
            continue
        raise AssertionError(f"{name} fixture unexpectedly passed")
    legacy_assets = renderer_source.replace(
        "uiCenterListCreate(centerList, root, items, count",
        "drawListRow(108, items[0].title, items[0].meta, true, TouchAction::Asset0); "
        "uiCenterListCreate(centerList, root, items, count",
        1,
    )
    try:
        verify_contract(layout_source, legacy_assets, carousel_header, carousel_source,
                        center_header, center_source)
    except LayoutError:
        pass
    else:
        raise AssertionError("legacy eight-asset root-row fixture unexpectedly passed")
    perf_mutations = (
        ("display monitor hook removed",
         player_console_source.replace(
             "display->driver->monitor_cb = carouselPerfMonitor;", "", 1
         )),
        ("minimum carousel FPS reduced",
         player_console_source.replace("kCarouselPerfMinFps = 24;",
                                       "kCarouselPerfMinFps = 23;", 1)),
        ("logic result removed from final gate",
         player_console_source.replace(
             "purePassed && componentPassed && livePerfPassed",
             "componentPassed && livePerfPassed", 1
         )),
        ("reverse wrap evidence removed",
         player_console_source.replace("CAROUSEL PERF WAIT_WRAP_REV", "CAROUSEL PERF WAIT_REV", 1)),
        ("scene settle monitor removed",
         player_console_source.replace(
             "display->driver->monitor_cb = sceneSettleMonitor;", "", 1
         )),
        ("scene settle quiet window removed",
         player_console_source.replace("nowMs - lastFrameMs >= kPerfSceneQuietMs", "true", 1)),
        ("clean checkpoint reboot removed",
         player_console_source.replace("esp_restart();", "", 1)),
        ("carousel monitor scope cleanup removed",
         player_console_source.replace(
             "~CarouselPerfMonitorGuard() { restore(); }",
             "~CarouselPerfMonitorGuard() = default;", 1
         )),
    )
    for name, mutated_console in perf_mutations:
        expect_perf_rejected(name, mutated_console)
    component_mutations = (
        ("fake display restore removed",
         logic_tests_source.replace("lv_disp_set_default(previousDisplay);", "", 1),
         renderer_source),
        ("renderer fixture ownership reset removed",
         logic_tests_source.replace("\n    uiRendererResetForTest();\n", "\n", 1),
         renderer_source),
        ("renderer event sink reset removed",
         logic_tests_source,
         renderer_source.replace("uiSetEventSink(nullptr);", "", 1)),
    )
    for name, mutated_logic, mutated_renderer in component_mutations:
        expect_component_rejected(name, mutated_logic, mutated_renderer)
    print(
        "UI LAYOUT NEGATIVE TEST PASS: inactive bypasses, retained Home, center-list, "
        "and live-display performance mutations rejected"
    )


def main() -> int:
    try:
        layout_source = LAYOUT_PATH.read_text(encoding="utf-8")
        renderer_source = RENDERER_PATH.read_text(encoding="utf-8")
        if not CAROUSEL_HEADER_PATH.exists() or not CAROUSEL_SOURCE_PATH.exists():
            fail("retained ui_carousel component files are missing")
        if not CENTER_LIST_HEADER_PATH.exists() or not CENTER_LIST_SOURCE_PATH.exists():
            fail("retained ui_center_list component files are missing")
        if not MODAL_HEADER_PATH.exists() or not MODAL_SOURCE_PATH.exists():
            fail("retained ui_modal component files are missing")
        carousel_header = CAROUSEL_HEADER_PATH.read_text(encoding="utf-8")
        carousel_source = CAROUSEL_SOURCE_PATH.read_text(encoding="utf-8")
        motion_source = MOTION_SOURCE_PATH.read_text(encoding="utf-8")
        center_header = CENTER_LIST_HEADER_PATH.read_text(encoding="utf-8")
        center_source = CENTER_LIST_SOURCE_PATH.read_text(encoding="utf-8")
        player_console_source = PLAYER_CONSOLE_PATH.read_text(encoding="utf-8")
        logic_tests_source = LOGIC_TESTS_PATH.read_text(encoding="utf-8")
        if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
            run_self_test(layout_source, renderer_source, carousel_header, carousel_source,
                          motion_source, center_header, center_source, player_console_source,
                          logic_tests_source)
            return 0
        if len(sys.argv) != 1:
            print("usage: verify-ui-layout.py [--self-test]", file=sys.stderr)
            return 2
        verify_contract(layout_source, renderer_source, carousel_header, carousel_source,
                        center_header, center_source)
        assert_carousel_motion_contract(motion_source)
        assert_carousel_perf_contract(player_console_source)
        assert_component_fixture_contract(logic_tests_source, renderer_source)
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
