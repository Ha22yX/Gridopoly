#include "ui_modal.h"

#include <cstring>

#include "src/fonts/ui_fonts.h"
#include "ui_layout.h"
#include "ui_primitives.h"

namespace {

constexpr uint32_t kPanel = 0x101719;
constexpr uint32_t kControl = 0x172224;
constexpr uint32_t kFocused = 0x16302A;
constexpr uint32_t kLine = 0x263234;
constexpr uint32_t kText = 0xEDF3F1;
constexpr uint32_t kMuted = 0x81908C;
constexpr uint32_t kGreen = 0x52DCB7;
constexpr uint32_t kGreenFill = 0x20483E;
constexpr uint32_t kRed = 0xEF7168;
constexpr uint32_t kRedFill = 0x2B1718;
constexpr lv_opa_t kShadeOpacity = 184;  // 72% of LV_OPA_COVER.

constexpr UiRect childRect(UiRect absolute, UiRect parent)
{
    return UiRect{static_cast<int16_t>(absolute.x - parent.x),
                  static_cast<int16_t>(absolute.y - parent.y), absolute.w, absolute.h};
}

constexpr UiRect kTitle{16, 14, 240, 28};
constexpr UiRect kAmount{24, 50, 224, 38};
constexpr UiRect kDetail{16, 92, 240, 28};
constexpr UiRect kCountdown{24, 124, 224, 20};
constexpr UiRect kConfirm = childRect(kModalConfirm, kModalRect);
constexpr UiRect kBack = childRect(kModalCancel, kModalRect);
constexpr UiRect kHoldTrack{12, 46, 200, 8};
constexpr UiRect kHoldLabel{12, 8, 200, 26};

static_assert(kConfirm.x == 24 && kConfirm.y == 150 &&
              kConfirm.w == 224 && kConfirm.h == 64,
              "confirmation control must use panel-relative kModalConfirm geometry");
static_assert(kHoldTrack.x >= 12 &&
              kConfirm.w - (kHoldTrack.x + kHoldTrack.w) >= 12,
              "hold track requires at least 12px horizontal insets");
static_assert(kHoldTrack.h == 8, "hold track must be exactly 8px high");
static_assert(kConfirm.h - (kHoldTrack.y + kHoldTrack.h) >= 10,
              "hold track requires at least 10px bottom inset");
static_assert(kHoldTrack.y - (kHoldLabel.y + kHoldLabel.h) >= 6,
              "hold label and track require at least 6px separation");

const char *safeText(const char *text)
{
    return text == nullptr ? "" : text;
}

void setLabelText(lv_obj_t *label, const char *text)
{
    if (label == nullptr) return;
    const char *next = safeText(text);
    const char *current = lv_label_get_text(label);
    if (current == nullptr || strcmp(current, next) != 0) lv_label_set_text(label, next);
}

void setBorder(lv_obj_t *object, uint32_t color, uint8_t width)
{
    if (object == nullptr) return;
    lv_obj_set_style_border_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(object, width, 0);
}

void setTextColor(lv_obj_t *label, uint32_t color)
{
    if (label != nullptr) lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

void makeNonOverflowing(lv_obj_t *object)
{
    if (object == nullptr) return;
    lv_obj_clear_flag(object, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void createBack(UiModal &modal)
{
    if (modal.panel == nullptr || modal.back != nullptr) return;
    modal.back = uiBox(modal.panel, kBack, kControl, kLine, 6);
    if (modal.back == nullptr) return;
    makeNonOverflowing(modal.back);
    uiBindTap(modal.back, UiEventKind::Back);
    modal.backLabel = uiLabel(modal.back, "BACK", UiRect{8, 7, 160, 26},
                              &lv_font_montserrat_16, kText);
}

void destroyBack(UiModal &modal)
{
    if (modal.back != nullptr) lv_obj_del(modal.back);
    modal.back = nullptr;
    modal.backLabel = nullptr;
}

void updateBack(UiModal &modal, const UiModalView &view)
{
    const bool visible = view.showBack && !view.submitting;
    if (visible) createBack(modal);
    else destroyBack(modal);

    if (modal.back == nullptr) return;
    lv_obj_set_style_bg_color(modal.back,
                              lv_color_hex(view.backFocused ? kFocused : kControl), 0);
    setBorder(modal.back, view.backFocused ? kGreen : kLine,
              view.backFocused ? 2 : 1);
    setTextColor(modal.backLabel, view.backFocused ? kGreen : kText);
}

void updateConfirm(UiModal &modal, const UiModalView &view)
{
    if (modal.confirm == nullptr || modal.holdTrack == nullptr ||
        modal.confirmFill == nullptr) return;

    const uint32_t accent = view.insufficient ? kRed : kGreen;
    const uint32_t controlBg = view.insufficient ? kRedFill :
                               (view.confirmFocused ? kFocused : kControl);
    const uint32_t border = view.submitting ? kLine :
                            (view.confirmFocused || view.insufficient ? accent : kLine);
    const uint32_t labelColor = view.submitting ? kMuted :
                                (view.confirmFocused || view.insufficient ? accent : kText);

    lv_obj_set_style_bg_color(modal.confirm, lv_color_hex(controlBg), 0);
    setBorder(modal.confirm, border,
              !view.submitting && (view.confirmFocused || view.insufficient) ? 2 : 1);
    setTextColor(modal.holdLabel, labelColor);
    lv_obj_set_style_bg_color(modal.confirmFill,
                              lv_color_hex(view.insufficient ? kRed : kGreenFill), 0);

    const uint16_t fillWidth = uiModalHoldFillWidth(view.holdPermille);
    lv_obj_set_width(modal.confirmFill, fillWidth);
    if (fillWidth == 0) lv_obj_add_flag(modal.confirmFill, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(modal.confirmFill, LV_OBJ_FLAG_HIDDEN);

    if (view.submitting) lv_obj_clear_flag(modal.confirm, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_add_flag(modal.confirm, LV_OBJ_FLAG_CLICKABLE);
}

}

uint16_t uiModalHoldFillWidth(uint16_t permille)
{
    const uint16_t clamped = permille > 1000 ? 1000 : permille;
    return static_cast<uint16_t>((static_cast<uint32_t>(kHoldTrack.w) * clamped + 500u) /
                                 1000u);
}

void uiModalCreate(UiModal &modal, lv_obj_t *root, const UiModalView &view)
{
    uiModalDestroy(modal);
    if (root == nullptr) return;

    modal.shade = uiBox(root, UiRect{0, 0, 480, 480}, 0x000000, 0x000000, 0);
    if (modal.shade == nullptr) return;
    lv_obj_set_style_bg_opa(modal.shade, kShadeOpacity, 0);
    lv_obj_set_style_border_width(modal.shade, 0, 0);
    lv_obj_add_flag(modal.shade, LV_OBJ_FLAG_CLICKABLE);
    makeNonOverflowing(modal.shade);
    lv_obj_move_foreground(modal.shade);

    modal.panel = uiBox(modal.shade, kModalRect, kPanel,
                        view.insufficient ? kRed : kGreen, 8);
    if (modal.panel == nullptr) {
        uiModalDestroy(modal);
        return;
    }
    makeNonOverflowing(modal.panel);

    modal.titleLabel = uiLabel(modal.panel, safeText(view.title), kTitle,
                               &ui_font_16, kText);
    modal.amountLabel = uiLabel(modal.panel, safeText(view.amount), kAmount,
                                &lv_font_montserrat_32,
                                view.insufficient ? kRed : kGreen);
    modal.detailLabel = uiLabel(modal.panel, safeText(view.detail), kDetail,
                                &ui_font_16, kMuted);
    modal.countdown = uiLabel(modal.panel, safeText(view.countdownText), kCountdown,
                              &ui_font_14, kMuted);

    modal.confirm = uiBox(modal.panel, kConfirm, kControl, kLine, 6);
    if (modal.confirm == nullptr) {
        uiModalDestroy(modal);
        return;
    }
    makeNonOverflowing(modal.confirm);
    uiBindHold(modal.confirm);

    modal.holdTrack = uiBox(modal.confirm, kHoldTrack, kLine, kLine, 4);
    if (modal.holdTrack == nullptr) {
        uiModalDestroy(modal);
        return;
    }
    lv_obj_set_style_border_width(modal.holdTrack, 0, 0);
    makeNonOverflowing(modal.holdTrack);

    modal.confirmFill = uiBox(modal.holdTrack, UiRect{0, 0, 0, kHoldTrack.h},
                              kGreenFill, kGreenFill, 4);
    if (modal.confirmFill == nullptr) {
        uiModalDestroy(modal);
        return;
    }
    lv_obj_set_style_border_width(modal.confirmFill, 0, 0);
    lv_obj_clear_flag(modal.confirmFill, LV_OBJ_FLAG_CLICKABLE);
    makeNonOverflowing(modal.confirmFill);

    // Created after the fill so text always remains on the upper layer.
    modal.holdLabel = uiLabel(modal.confirm, safeText(view.confirmText), kHoldLabel,
                              &ui_font_16, kText);

    uiModalUpdate(modal, view);
}

void uiModalUpdate(UiModal &modal, const UiModalView &view)
{
    if (modal.panel == nullptr) return;

    setLabelText(modal.titleLabel, view.title);
    setLabelText(modal.amountLabel, view.amount);
    setLabelText(modal.detailLabel, view.detail);
    setLabelText(modal.countdown, view.countdownText);
    setLabelText(modal.holdLabel, view.confirmText);

    const uint32_t accent = view.insufficient ? kRed : kGreen;
    setBorder(modal.panel, accent, 1);
    setTextColor(modal.amountLabel, accent);
    setTextColor(modal.countdown, view.insufficient ? kRed : kMuted);

    updateConfirm(modal, view);
    updateBack(modal, view);
}

void uiModalDestroy(UiModal &modal)
{
    if (modal.shade != nullptr) lv_obj_del(modal.shade);
    modal = UiModal{};
}
