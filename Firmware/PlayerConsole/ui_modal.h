#pragma once

#include <stdint.h>

#include <lvgl.h>

struct UiModalView {
    const char *title;
    const char *amount;
    const char *detail;
    const char *countdownText;
    const char *confirmText;
    uint16_t holdPermille;
    bool showBack;
    bool confirmFocused;
    bool backFocused;
    bool submitting;
    bool insufficient;
};

struct UiModal {
    lv_obj_t *shade = nullptr;
    lv_obj_t *panel = nullptr;
    lv_obj_t *confirm = nullptr;
    lv_obj_t *holdTrack = nullptr;
    lv_obj_t *confirmFill = nullptr;
    lv_obj_t *holdLabel = nullptr;
    lv_obj_t *back = nullptr;
    lv_obj_t *countdown = nullptr;
    lv_obj_t *titleLabel = nullptr;
    lv_obj_t *amountLabel = nullptr;
    lv_obj_t *detailLabel = nullptr;
    lv_obj_t *backLabel = nullptr;
};

void uiModalCreate(UiModal &modal, lv_obj_t *root, const UiModalView &view);
void uiModalUpdate(UiModal &modal, const UiModalView &view);
void uiModalDestroy(UiModal &modal);
uint16_t uiModalHoldFillWidth(uint16_t permille);
