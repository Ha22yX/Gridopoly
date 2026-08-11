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

constexpr UiRect kTitle{16, 10, 240, 24};
constexpr UiRect kAmount{16, 36, 240, 34};
constexpr UiRect kDetail{16, 72, 240, 22};
constexpr UiRect kCash{20, 98, 232, 18};
constexpr UiRect kCountdown{24, 122, 224, 20};
constexpr UiRect kRecipientCard{20, 36, 232, 46};
constexpr UiRect kRecipientBadge{8, 7, 32, 32};
constexpr UiRect kRecipientBadgeLabel{4, 5, 24, 20};
constexpr UiRect kRecipientCaption{50, 4, 168, 14};
constexpr UiRect kRecipientName{50, 18, 168, 24};
constexpr UiRect kRecipientAmount{16, 86, 240, 30};
constexpr UiRect kRecipientPurpose{20, 118, 232, 18};
constexpr UiRect kRecipientCountdown{20, 136, 232, 12};
constexpr UiRect kConfirm = childRect(kModalConfirm, kModalRect);
constexpr UiRect kBack = childRect(kModalCancel, kModalRect);
constexpr UiRect kHoldTrack{12, 46, 200, 8};
constexpr UiRect kHoldLabel{12, 8, 200, 26};

static_assert(kConfirm.x == 24 && kConfirm.y == 150 &&
              kConfirm.w == 224 && kConfirm.h == 64,
              "confirmation control must use panel-relative kModalConfirm geometry");
static_assert(kTitle.y + kTitle.h <= kAmount.y &&
              kAmount.y + kAmount.h <= kDetail.y &&
              kDetail.y + kDetail.h <= kCash.y &&
              kCash.y + kCash.h <= kCountdown.y &&
              kCountdown.y + kCountdown.h <= kConfirm.y,
              "modal payment summary rows must not overlap confirmation controls");
static_assert(kTitle.y + kTitle.h <= kRecipientCard.y &&
              kRecipientCard.y + kRecipientCard.h <= kRecipientAmount.y &&
              kRecipientAmount.y + kRecipientAmount.h <= kRecipientPurpose.y &&
              kRecipientPurpose.y + kRecipientPurpose.h <= kRecipientCountdown.y &&
              kRecipientCountdown.y + kRecipientCountdown.h <= kConfirm.y,
              "recipient identity layout must remain above confirmation controls");
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
    const lv_color_t desired = lv_color_hex(color);
    if (lv_obj_get_style_border_color(object, 0).full != desired.full) {
        lv_obj_set_style_border_color(object, desired, 0);
    }
    if (lv_obj_get_style_border_width(object, 0) != width) {
        lv_obj_set_style_border_width(object, width, 0);
    }
}

void setTextColor(lv_obj_t *label, uint32_t color)
{
    if (label == nullptr) return;
    const lv_color_t desired = lv_color_hex(color);
    if (lv_obj_get_style_text_color(label, 0).full != desired.full) {
        lv_obj_set_style_text_color(label, desired, 0);
    }
}

void setBackgroundColor(lv_obj_t *object, uint32_t color)
{
    if (object == nullptr) return;
    const lv_color_t desired = lv_color_hex(color);
    if (lv_obj_get_style_bg_color(object, 0).full != desired.full) {
        lv_obj_set_style_bg_color(object, desired, 0);
    }
}

void setHidden(lv_obj_t *object, bool hidden)
{
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void setClickable(lv_obj_t *object, bool clickable)
{
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) == clickable) return;
    if (clickable) lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
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
    setBackgroundColor(modal.back, view.backFocused ? kFocused : kControl);
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

    setBackgroundColor(modal.confirm, controlBg);
    setBorder(modal.confirm, border,
              !view.submitting && (view.confirmFocused || view.insufficient) ? 2 : 1);
    setTextColor(modal.holdLabel, labelColor);
    setBackgroundColor(modal.confirmFill, view.insufficient ? kRed : kGreenFill);

    const uint16_t fillWidth = uiModalHoldFillWidth(view.holdPermille);
    if (lv_obj_get_width(modal.confirmFill) != fillWidth) {
        lv_obj_set_width(modal.confirmFill, fillWidth);
    }
    setHidden(modal.confirmFill, fillWidth == 0);

    setClickable(modal.confirm, !view.submitting);
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
    const UiRect amountRect = view.showRecipient ? kRecipientAmount : kAmount;
    const UiRect detailRect = view.showRecipient ? kRecipientPurpose : kDetail;
    const UiRect countdownRect = view.showRecipient ? kRecipientCountdown : kCountdown;
    modal.amountLabel = uiLabel(modal.panel, safeText(view.amount), amountRect,
                                &lv_font_montserrat_24,
                                view.insufficient ? kRed : kGreen);
    modal.detailLabel = uiLabel(modal.panel, safeText(view.detail), detailRect,
                                view.showRecipient ? &lv_font_montserrat_12 : &ui_font_14,
                                kMuted,
                                view.showRecipient ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT);
    if (!view.showRecipient) {
        modal.cashLabel = uiLabel(modal.panel, safeText(view.cashText), kCash,
                                  &lv_font_montserrat_12, kText);
    }
    modal.countdown = uiLabel(modal.panel, safeText(view.countdownText), countdownRect,
                              view.showRecipient ? &lv_font_montserrat_10 : &ui_font_14,
                              kMuted, view.showRecipient ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER);
    if (view.showRecipient) {
        modal.recipientCard = uiBox(modal.panel, kRecipientCard,
                                    kControl, view.recipientAccent, 7);
        if (modal.recipientCard != nullptr) {
            makeNonOverflowing(modal.recipientCard);
        }
        modal.recipientBadge = uiBox(modal.recipientCard, kRecipientBadge,
                                     view.recipientAccent, view.recipientAccent, 20);
        if (modal.recipientBadge != nullptr) {
            makeNonOverflowing(modal.recipientBadge);
            modal.recipientBadgeLabel = uiLabel(
            modal.recipientBadge, safeText(view.recipientToken), kRecipientBadgeLabel,
                &lv_font_montserrat_16, kPanel
            );
        }
        modal.recipientCaptionLabel = uiLabel(
            modal.recipientCard, safeText(view.recipientCaption), kRecipientCaption,
            &lv_font_montserrat_10, view.recipientAccent, LV_TEXT_ALIGN_LEFT
        );
        modal.recipientNameLabel = uiLabel(
            modal.recipientCard, safeText(view.recipientName), kRecipientName,
            &ui_font_16, kText, LV_TEXT_ALIGN_LEFT
        );
        if (modal.recipientCaptionLabel != nullptr) {
            lv_label_set_long_mode(modal.recipientCaptionLabel, LV_LABEL_LONG_CLIP);
        }
        if (modal.recipientNameLabel != nullptr) {
            lv_label_set_long_mode(modal.recipientNameLabel, LV_LABEL_LONG_CLIP);
        }
    }
    lv_obj_t *summaryLabels[] = {
        modal.titleLabel, modal.amountLabel, modal.detailLabel, modal.cashLabel, modal.countdown,
    };
    for (lv_obj_t *label : summaryLabels) {
        if (label != nullptr) lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    }

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
    setLabelText(modal.cashLabel, view.cashText);
    setLabelText(modal.countdown, view.countdownText);
    setLabelText(modal.holdLabel, view.confirmText);
    setLabelText(modal.recipientBadgeLabel, view.recipientToken);
    setLabelText(modal.recipientCaptionLabel, view.recipientCaption);
    setLabelText(modal.recipientNameLabel, view.recipientName);

    const uint32_t accent = view.insufficient ? kRed : kGreen;
    setBorder(modal.panel, accent, 1);
    setTextColor(modal.amountLabel, accent);
    setTextColor(modal.countdown, view.insufficient ? kRed : kMuted);
    if (modal.recipientBadge != nullptr) {
        setBackgroundColor(modal.recipientBadge, view.recipientAccent);
        setBorder(modal.recipientBadge, view.recipientAccent, 1);
    }
    setBorder(modal.recipientCard, view.recipientAccent, 1);
    setTextColor(modal.recipientCaptionLabel, view.recipientAccent);

    updateConfirm(modal, view);
    updateBack(modal, view);
}

void uiModalDestroy(UiModal &modal)
{
    if (modal.shade != nullptr) lv_obj_del(modal.shade);
    modal = UiModal{};
}
