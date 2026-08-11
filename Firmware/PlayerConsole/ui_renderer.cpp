#include "ui_renderer.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_state.h"
#include "demo_data.h"
#include "remote_avatar_cache.h"
#include "src/assets/grid_city_tile_images.h"
#include "src/fonts/ui_fonts.h"
#include "ui_carousel.h"
#include "ui_center_list.h"
#include "ui_handwriting.h"
#include "ui_modal.h"
#include "ui_motion.h"
#include "ui_primitives.h"

// Keep the call sites compact while replacing LVGL's incomplete built-in CJK
// font with the project's verified Noto Sans SC subset.
#define lv_font_simsun_16_cjk ui_font_16

namespace {

constexpr uint32_t kBg = 0x090E10;
constexpr uint32_t kPanel = 0x11191B;
constexpr uint32_t kLine = 0x263234;
constexpr uint32_t kText = 0xEDF3F1;
constexpr uint32_t kMuted = 0x81908C;
constexpr uint32_t kGreen = 0x52DCB7;
constexpr uint32_t kYellow = 0xF2C453;
constexpr uint32_t kRed = 0xEF7168;
constexpr uint32_t kBlue = 0x58A7EB;
constexpr uint32_t kActionAuctionBid = 1u << 13;
constexpr uint32_t kActionAuctionPass = 1u << 14;
constexpr uint32_t kDiceSettleMs = 1900u;
constexpr uint8_t kSyncedAssetCapacity = 28;
constexpr uint8_t kTouchQueueCapacity = 16;

lv_obj_t *root = nullptr;
lv_obj_t *outerRing = nullptr;
lv_obj_t *turnReminderLabel = nullptr;
lv_obj_t *diceObjects[2]{};
lv_obj_t *diceLabels[2]{};
lv_obj_t *cardObject = nullptr;
lv_obj_t *identityCountdownLabel = nullptr;
lv_obj_t *avatarPreloadArc = nullptr;
lv_obj_t *avatarPreloadBar = nullptr;
lv_obj_t *avatarPreloadLabel = nullptr;
uint32_t renderedRevision = 0;
uint32_t lastDynamicDrawMs = 0;
UiCarousel homeCarousel{};
UiCenterList centerList{};
UiModal activeModal{};
struct TapBinding {
    lv_obj_t *object;
    lv_obj_t *label;
    TouchAction action;
    uint32_t accent;
    uint32_t focusedBackground;
};
TapBinding tapBindings[16] = {};
uint8_t tapBindingCount = 0;
struct AvatarRowBinding {
    lv_obj_t *object;
    lv_obj_t *numberLabel;
    lv_obj_t *eyebrowLabel;
    lv_obj_t *valueLabel;
    lv_obj_t *affordanceLabel;
    lv_obj_t *divider;
};
AvatarRowBinding avatarRowBindings[5] = {};
TouchAction touchQueue[kTouchQueueCapacity] = {};
uint8_t touchHead = 0;
uint8_t touchTail = 0;
uint8_t touchCount = 0;
int8_t focusMotion = 0;
bool hasRenderedState = false;
ScreenPage previousPage = ScreenPage::Home;
uint8_t previousFocus = 0;
InlineEditField previousInlineEditField = InlineEditField::None;
uint32_t previousBoundaryPulseRevision = 0;
AppState previousRenderedState{};
UiRendererTestStats rendererTestStats{};
bool artworkInvalidated = false;
uint8_t prefetchedRollTarget = 0xFF;
lv_style_transition_dsc_t pressTransition;
const lv_style_prop_t pressTransitionProps[] = {
    LV_STYLE_TRANSFORM_ZOOM,
    LV_STYLE_BG_OPA,
    static_cast<lv_style_prop_t>(0),
};

constexpr UiRect kHomeTitle{70, 66, 340, 44};
constexpr UiRect kHomeArtwork{70, 130, 144, 144};
constexpr UiRect kHomeCashLabel{218, 144, 188, 22};
constexpr UiRect kHomeCashAmount{210, 170, 204, 48};
constexpr UiRect kHomeTileNumber{220, 224, 180, 18};
constexpr UiRect kHomeLocation{216, 244, 192, 24};
constexpr UiRect kHomeDivider{96, 282, 288, 1};
constexpr UiRect kTurnDots[5] = {
    UiRect{200, 114, 8, 8}, UiRect{218, 114, 8, 8}, UiRect{236, 114, 8, 8},
    UiRect{254, 114, 8, 8}, UiRect{272, 114, 8, 8},
};
constexpr UiRect kDetailPrimary{128, 278, 224, 56};
constexpr UiRect kTradeReceiver{104, 116, 272, 44};
constexpr UiRect kTradeAssets{104, 166, 272, 44};
constexpr UiRect kTradeAmount{104, 216, 272, 44};
constexpr UiRect kTradeSubmit{128, 272, 224, 50};
constexpr UiRect kReceiverPickerHeader{104, 72, 272, 42};
constexpr UiRect kReceiverPickerBack{152, 358, 176, 40};
constexpr UiRect kConnectionLostBadge{218, 10, 44, 38};
constexpr UiRect kActivityBadge{218, 10, 44, 38};
// A full-width rectangle at y=8 is clipped by the circular panel. Keep the
// transient activity surface inside the top chord of the 480x480 raster.
constexpr UiRect kActivityBanner{104, 50, 272, 48};
constexpr uint8_t kDemoListCount = 6;

constexpr const char *kHairNames[10] = {
    "Angular crop", "Asymmetric bob", "Textured quiff", "Curly fade",
    "Classic side part", "Short coils", "Shoulder waves", "Braided crown",
    "High bun", "Long straight",
};
constexpr const char *kHairColorNames[20] = {
    "Graphite", "Copper", "Raven", "Espresso", "Chestnut", "Golden",
    "Platinum", "City teal", "Ash brown", "Auburn", "Mahogany",
    "Honey blonde", "Strawberry", "Silver", "Snow white", "Burgundy",
    "Rose pink", "Violet", "Cobalt blue", "Emerald",
};
constexpr uint32_t kHairColorSwatches[20] = {
    0x68747C, 0xB0532B, 0x28303A, 0x50372F, 0x8E5A3C, 0xD1A44F,
    0xD8CCB0, 0x2D848A, 0x705C52, 0x8B362A, 0x682A2C, 0xC8974E,
    0xD58B5C, 0xA6B2BC, 0xEBEAEA, 0x75253F, 0xC25577, 0x6F4EA0,
    0x375DA8, 0x307D5B,
};
constexpr const char *kFaceNames[10] = {
    "Angular oval", "Soft square", "Round youthful", "Heart taper",
    "Elegant oblong", "Diamond angular", "Broad strong", "Soft triangle",
    "Narrow refined", "Mature sculpted",
};
constexpr const char *kSkinNames[8] = {
    "Porcelain", "Fair", "Warm", "Golden", "Olive", "Bronze", "Deep", "Ebony",
};
constexpr uint32_t kSkinSwatches[8] = {
    0xEFCAAD, 0xE8B58B, 0xE09E64, 0xCA8D52,
    0xAD8053, 0xA1603E, 0x895338, 0x5B352A,
};
constexpr const char *kOutfitNames[10] = {
    "Utility bomber", "Operations jacket", "Streetline hoodie", "Metro varsity",
    "Civic blazer", "Workshop vest", "Signal knit", "Denim commuter",
    "Rainline shell", "Gala tailored",
};

uint32_t activityAccent(uint8_t kind);
void formatActivitySummary(const AppState &state, const TransportGameEvent &event,
                           char *output, size_t outputSize);

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t kLockedMarkerMap[] = {
    0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x7F, 0xF8, 0x20, 0x20, 0x01, 0x80,
    0xFF, 0xFC, 0x63, 0x26, 0x7F, 0xFE, 0x00, 0x0C, 0x7D, 0x24, 0x7F, 0xFE,
    0x00, 0x0C, 0xC0, 0x20, 0x40, 0x02, 0x60, 0x0C, 0xFB, 0xFE, 0x7F, 0xFE,
    0x60, 0x0C, 0x7B, 0x06, 0xFF, 0xFE, 0x7F, 0xFC, 0x22, 0x22, 0x01, 0x80,
    0x60, 0x0C, 0xFA, 0x26, 0x11, 0x80, 0x60, 0x00, 0xFA, 0x26, 0x11, 0x80,
    0x60, 0x00, 0x22, 0x26, 0x31, 0xFC, 0x60, 0x02, 0x22, 0x66, 0x31, 0x80,
    0x60, 0x06, 0x3E, 0x72, 0x39, 0x80, 0x60, 0x06, 0x39, 0xDC, 0x6D, 0x80,
    0x3F, 0xFC, 0x37, 0x86, 0xC7, 0xFF, 0x00, 0x00, 0x06, 0x02, 0xC0, 0x00,
};

const lv_img_dsc_t *lockedMarkerImage()
{
    static lv_img_dsc_t image{};
    static bool initialized = false;
    if (!initialized) {
        image.header.cf = LV_IMG_CF_ALPHA_1BIT;
        image.header.always_zero = 0;
        image.header.reserved = 0;
        image.header.w = 48;
        image.header.h = 16;
        image.data_size = sizeof(kLockedMarkerMap);
        image.data = kLockedMarkerMap;
        initialized = true;
    }
    return &image;
}

void setAnimatedX(void *object, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(value));
}

void setAnimatedOpacity(void *object, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void deleteAnimatedObject(lv_anim_t *animation)
{
    if (animation == nullptr || animation->var == nullptr) return;
    lv_obj_del_async(static_cast<lv_obj_t *>(animation->var));
}

void animateActivityBannerExit(lv_obj_t *banner)
{
    if (banner == nullptr) return;
    lv_obj_update_layout(banner);
    const int16_t startX = lv_obj_get_x(banner);

    lv_anim_t move;
    lv_anim_init(&move);
    lv_anim_set_var(&move, banner);
    lv_anim_set_exec_cb(&move, setAnimatedX);
    lv_anim_set_values(&move, startX, startX - 28);
    lv_anim_set_time(&move, 180);
    lv_anim_set_path_cb(&move, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&move, deleteAnimatedObject);
    lv_anim_start(&move);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, banner);
    lv_anim_set_exec_cb(&fade, setAnimatedOpacity);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fade, 150);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_in);
    lv_anim_start(&fade);
}

void animateActivityBannerEntry(lv_obj_t *banner)
{
    if (banner == nullptr) return;
    lv_obj_update_layout(banner);
    const int16_t targetX = lv_obj_get_x(banner);
    constexpr uint32_t kEntryDelayMs = 45;
    lv_obj_set_x(banner, targetX + 28);
    lv_obj_set_style_opa(banner, LV_OPA_TRANSP, 0);

    lv_anim_t move;
    lv_anim_init(&move);
    lv_anim_set_var(&move, banner);
    lv_anim_set_exec_cb(&move, setAnimatedX);
    lv_anim_set_values(&move, targetX + 28, targetX);
    lv_anim_set_time(&move, 220);
    lv_anim_set_delay(&move, kEntryDelayMs);
    lv_anim_set_path_cb(&move, lv_anim_path_ease_out);
    lv_anim_start(&move);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, banner);
    lv_anim_set_exec_cb(&fade, setAnimatedOpacity);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&fade, 170);
    lv_anim_set_delay(&fade, kEntryDelayMs);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
}

void animateFocusEntry(lv_obj_t *obj)
{
    lv_obj_update_layout(obj);
    const int16_t targetX = lv_obj_get_x(obj);
    const int16_t offset = focusMotion == 0 ? 0 : static_cast<int16_t>(focusMotion * 14);
    lv_obj_set_x(obj, targetX + offset);
    lv_obj_set_style_opa(obj, LV_OPA_40, 0);

    lv_anim_t move;
    lv_anim_init(&move);
    lv_anim_set_var(&move, obj);
    lv_anim_set_exec_cb(&move, setAnimatedX);
    lv_anim_set_values(&move, targetX + offset, targetX);
    lv_anim_set_time(&move, 160);
    lv_anim_set_path_cb(&move, lv_anim_path_ease_out);
    lv_anim_start(&move);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, obj);
    lv_anim_set_exec_cb(&fade, setAnimatedOpacity);
    lv_anim_set_values(&fade, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&fade, 140);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_start(&fade);
}

void enqueueTouch(TouchAction action)
{
    if (touchCount >= kTouchQueueCapacity) return;
    touchQueue[touchTail] = action;
    touchTail = static_cast<uint8_t>((touchTail + 1) % kTouchQueueCapacity);
    ++touchCount;
}

const GridCityVisualDefinition &visualOrFallback(const GridCityVisualDefinition *visual)
{
    return visual == nullptr ? gridCityFallbackVisual() : *visual;
}

lv_obj_t *drawArtwork(UiRect rect, const GridCityVisualDefinition &visual)
{
    lv_obj_t *frame = uiBox(root, rect, kPanel, visual.accent, 8);
    if (frame == nullptr) return nullptr;
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    const lv_img_dsc_t *source = gridCityArtworkImage(visual);
    if (source == nullptr) {
        lv_obj_t *spinner = lv_spinner_create(frame, 780, 88);
        if (spinner != nullptr) {
            lv_obj_set_size(spinner, 44, 44);
            lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
            lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(spinner, lv_color_hex(kLine), LV_PART_MAIN);
            lv_obj_set_style_arc_color(spinner, lv_color_hex(visual.accent),
                                       LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
            lv_obj_center(spinner);
            lv_obj_clear_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
        }
        return frame;
    }

    lv_obj_t *art = lv_img_create(frame);
    if (art == nullptr) return frame;
    lv_img_set_src(art, source);
    const uint16_t available = static_cast<uint16_t>(
        (rect.w < rect.h ? rect.w : rect.h) - 10
    );
    const uint16_t sourceSize = source->header.w > source->header.h
        ? source->header.w : source->header.h;
    if (sourceSize != 0 && sourceSize != available) {
        lv_img_set_zoom(art, static_cast<uint16_t>(256u * available / sourceSize));
    }
    lv_obj_center(art);
    lv_obj_clear_flag(art, LV_OBJ_FLAG_CLICKABLE);
    return frame;
}

void prefetchRollTargetArtwork(const AppState &state)
{
    const bool rollFlowActive = state.rollAnimating || state.rollResolved ||
        state.moveArrivalPending || state.page == ScreenPage::DiceStage ||
        state.page == ScreenPage::MoveGuide;
    if (!rollFlowActive || state.rollTarget == 0xFF) {
        prefetchedRollTarget = 0xFF;
        return;
    }
    if (prefetchedRollTarget == state.rollTarget) return;
    const GridCityVisualDefinition *visual = appTileVisual(state, state.rollTarget);
    if (visual == nullptr) return;
    gridCityArtworkPrefetch(*visual);
    prefetchedRollTarget = state.rollTarget;
    ++rendererTestStats.artworkPrefetches;
}

void setTurnReminderOpacity(void *object, int32_t value)
{
    lv_obj_set_style_text_opa(static_cast<lv_obj_t *>(object),
                              static_cast<lv_opa_t>(value), 0);
}

void startTurnReminder(lv_obj_t *object)
{
    if (object == nullptr) return;
    setTurnReminderOpacity(object, LV_OPA_70);
}

void updateTurnReminder(uint32_t nowMs)
{
    if (turnReminderLabel == nullptr) return;
    const uint16_t phase = static_cast<uint16_t>(nowMs % 1440u);
    const uint16_t triangle = phase <= 720u ? phase : static_cast<uint16_t>(1440u - phase);
    const uint8_t opacity = static_cast<uint8_t>(LV_OPA_50 +
        static_cast<uint32_t>(LV_OPA_COVER - LV_OPA_50) * triangle / 720u);
    setTurnReminderOpacity(turnReminderLabel, opacity);
}

void handleUiEvent(const UiEvent &event)
{
    if (event.kind == UiEventKind::HoldDown) {
        enqueueTouch(TouchAction::PressDown);
    } else if (event.kind == UiEventKind::HoldUp) {
        enqueueTouch(TouchAction::PressUp);
    } else if (event.kind == UiEventKind::ActivateFocused) {
        enqueueTouch(static_cast<TouchAction>(event.value));
    } else if (event.kind == UiEventKind::SelectHomeAction && event.value >= 0 && event.value < 5) {
        enqueueTouch(static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::HomeAssets) + event.value));
    } else if (event.kind == UiEventKind::SelectListItem && event.value >= 0 &&
               event.value < kPlayerDetailAssetCapacity) {
        enqueueTouch(static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::ListItem0) + event.value));
    } else if (event.kind == UiEventKind::ListPrevious) {
        enqueueTouch(TouchAction::ListPrevious);
    } else if (event.kind == UiEventKind::ListNext) {
        enqueueTouch(TouchAction::ListNext);
    } else if (event.kind == UiEventKind::SelectFooter) {
        enqueueTouch(TouchAction::Footer);
    } else if (event.kind == UiEventKind::Back) {
        enqueueTouch(TouchAction::Back);
    }
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int16_t x, int16_t y, int16_t width,
                const lv_font_t *font, uint32_t rgb, lv_text_align_t align = LV_TEXT_ALIGN_CENTER)
{
    return uiLabel(parent, text, UiRect{x, y, width, 32}, font, rgb, align);
}

lv_obj_t *auctionLabel(const char *text, UiRect rect, const lv_font_t *font,
                       uint32_t rgb)
{
    lv_obj_t *object = uiLabel(root, text, rect, font, rgb);
    if (object != nullptr) lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    return object;
}

lv_obj_t *drawMoneyMetric(const char *title, int32_t amount, UiRect titleRect,
                          UiRect amountRect, uint32_t amountColor)
{
    lv_obj_t *titleLabel = uiLabel(root, title, titleRect, &lv_font_montserrat_10, kMuted);
    if (titleLabel != nullptr) lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_CLIP);
    char text[24];
    snprintf(text, sizeof(text), "$%ld", static_cast<long>(amount));
    lv_obj_t *amountLabel = uiLabel(root, text, amountRect, &lv_font_montserrat_24, amountColor);
    if (amountLabel != nullptr) lv_label_set_long_mode(amountLabel, LV_LABEL_LONG_CLIP);
    return amountLabel;
}

void drawAuctionHeader(const char *title, const char *eyebrow)
{
    auctionLabel(eyebrow, UiRect{100, 55, 280, 32}, &ui_font_14, kMuted);
    auctionLabel(title, UiRect{70, 77, 340, 32}, &lv_font_simsun_16_cjk, kText);
}

lv_obj_t *box(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t bg,
              uint32_t border, uint8_t radius = 6)
{
    return uiBox(parent, UiRect{x, y, w, h}, bg, border, radius);
}

void makeClickable(lv_obj_t *obj, TouchAction action, lv_obj_t *focusLabel = nullptr,
                   uint32_t accent = 0, uint32_t focusedBackground = 0x16302A)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(obj);
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_obj_set_style_transition(obj, &pressTransition, LV_STATE_DEFAULT);
    lv_obj_set_style_transform_zoom(obj, 244, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, LV_STATE_PRESSED);
    uiBindTap(obj, UiEventKind::ActivateFocused, static_cast<int16_t>(action));
    if (tapBindingCount < sizeof(tapBindings) / sizeof(tapBindings[0])) {
        tapBindings[tapBindingCount++] =
            TapBinding{obj, focusLabel, action, accent, focusedBackground};
    }
}

uint32_t focusAccent(const AppState &state)
{
    switch (state.page) {
        case ScreenPage::Players:
        case ScreenPage::PlayerDetail:
        case ScreenPage::PlayerAssets:
        case ScreenPage::PlayerFinance: return kYellow;
        case ScreenPage::Activity: return kBlue;
        case ScreenPage::Trade:
        case ScreenPage::TradeAssetSelect:
        case ScreenPage::TradeOffer:
        case ScreenPage::DemoLab: return kBlue;
        case ScreenPage::Debt:
        case ScreenPage::DebtAssets:
        case ScreenPage::Bankruptcy: return kRed;
        case ScreenPage::ExtraRollReward:
        case ScreenPage::Auction: return kYellow;
        case ScreenPage::TileEvent: {
            const GridCityVisualDefinition *visual = appTileVisual(state, state.rollTarget);
            return visual == nullptr ? kYellow : visual->accent;
        }
        case ScreenPage::CardReveal: return state.cardChance ? kYellow : kGreen;
        case ScreenPage::Purchase: return kBlue;
        case ScreenPage::AvatarLoading:
        case ScreenPage::AvatarSetup:
        case ScreenPage::NameReview:
        case ScreenPage::NameHandwriting:
        case ScreenPage::PlayerReady: return kGreen;
        default: return kGreen;
    }
}

bool isCenterListPage(ScreenPage page)
{
    return page == ScreenPage::Assets || page == ScreenPage::Players ||
           page == ScreenPage::PlayerAssets || page == ScreenPage::PlayerFinance ||
           page == ScreenPage::Activity ||
           page == ScreenPage::TradeAssetSelect || page == ScreenPage::DemoLab ||
           page == ScreenPage::DebtAssets;
}

bool textStateUnchanged(const char *current, const char *rendered)
{
    if (current == rendered) return true;
    if (current == nullptr || rendered == nullptr) return false;
    return strcmp(current, rendered) == 0;
}

bool modalVisualStateUnchanged(const ModalState &state, const ModalState &rendered)
{
    if (state.kind != rendered.kind) return false;
    if (state.kind == ModalKind::None) return true;
    return state.focus == rendered.focus &&
           state.cancelAllowed == rendered.cancelAllowed &&
           state.holding == rendered.holding &&
           state.submitting == rendered.submitting &&
           state.insufficient == rendered.insufficient &&
           state.transactionId == rendered.transactionId &&
           state.deadlineMs == rendered.deadlineMs &&
           state.holdStartMs == rendered.holdStartMs &&
           state.amount == rendered.amount &&
           state.tradeOperation == rendered.tradeOperation &&
           textStateUnchanged(state.title, rendered.title) &&
           textStateUnchanged(state.counterparty, rendered.counterparty) &&
           textStateUnchanged(state.purpose, rendered.purpose);
}

bool identityVisualStateUnchanged(const IdentityState &state,
                                  const IdentityState &rendered)
{
    return state.phase == rendered.phase &&
           state.authorityPhase == rendered.authorityPhase &&
           state.lastResult == rendered.lastResult &&
           memcmp(&state.draftRecipe, &rendered.draftRecipe,
                  sizeof(state.draftRecipe)) == 0 &&
           memcmp(&state.confirmedRecipe, &rendered.confirmedRecipe,
                  sizeof(state.confirmedRecipe)) == 0 &&
           memcmp(state.seats, rendered.seats, sizeof(state.seats)) == 0 &&
           state.revision == rendered.revision &&
           state.ownSeatRevision == rendered.ownSeatRevision &&
           state.pendingRequestId == rendered.pendingRequestId &&
           state.countdownDeadlineMs == rendered.countdownDeadlineMs &&
           state.seatCount == rendered.seatCount &&
           state.humanMask == rendered.humanMask &&
           state.avatarReadyMask == rendered.avatarReadyMask &&
           state.nameReadyMask == rendered.nameReadyMask &&
           state.readyMask == rendered.readyMask &&
           state.onlineMask == rendered.onlineMask &&
           state.editingValue == rendered.editingValue &&
           state.draftInitialized == rendered.draftInitialized &&
           state.draftDirty == rendered.draftDirty &&
           state.nameDirty == rendered.nameDirty &&
           strcmp(state.draftName, rendered.draftName) == 0;
}

bool samePageVisibleStateUnchanged(const AppState &state, const AppState &rendered)
{
    const bool assetFocusOnly = state.page == ScreenPage::Assets;
    const bool playerFocusOnly = state.page == ScreenPage::Players;
    const bool playerAssetFocusOnly = state.page == ScreenPage::PlayerAssets;
    const bool playerFinanceFocusOnly = state.page == ScreenPage::PlayerFinance;
    const bool activityFocusOnly = state.page == ScreenPage::Activity;
    const bool tradeAssetFocusOnly = state.page == ScreenPage::TradeAssetSelect;
    const bool demoFocusOnly = state.page == ScreenPage::DemoLab;
    return state.page == rendered.page &&
           appPresentedHomePhase(state) == appPresentedHomePhase(rendered) &&
           state.endTurnPresentation == rendered.endTurnPresentation &&
           state.endTurnPresentationStartedMs == rendered.endTurnPresentationStartedMs &&
           modalVisualStateUnchanged(state.modal, rendered.modal) &&
           state.selectedAsset == rendered.selectedAsset &&
           state.selectedPlayer == rendered.selectedPlayer &&
           (assetFocusOnly || state.assetListIndex == rendered.assetListIndex) &&
           (playerFocusOnly || state.playerListIndex == rendered.playerListIndex) &&
           (playerAssetFocusOnly ||
            state.playerAssetListIndex == rendered.playerAssetListIndex) &&
           (playerFinanceFocusOnly ||
            state.playerFinanceListIndex == rendered.playerFinanceListIndex) &&
           (activityFocusOnly || state.activityListIndex == rendered.activityListIndex) &&
           (tradeAssetFocusOnly ||
            state.tradeAssetListIndex == rendered.tradeAssetListIndex) &&
           (demoFocusOnly || state.demoListIndex == rendered.demoListIndex) &&
           state.debtListIndex == rendered.debtListIndex &&
           state.tradeReceiver == rendered.tradeReceiver &&
           state.tradeReceiverPickerOpen == rendered.tradeReceiverPickerOpen &&
           state.tradeReceiverPickerIndex == rendered.tradeReceiverPickerIndex &&
           state.tradeGiveAssetMask == rendered.tradeGiveAssetMask &&
           state.tradeAmount == rendered.tradeAmount &&
           state.inlineEditField == rendered.inlineEditField &&
           state.tradeEntryMode == rendered.tradeEntryMode &&
           memcmp(&state.tradeOffer, &rendered.tradeOffer,
                  sizeof(state.tradeOffer)) == 0 &&
           state.money == rendered.money &&
           state.position == rendered.position &&
           state.rollTarget == rendered.rollTarget &&
           state.debtCreditorId == rendered.debtCreditorId &&
           state.debtPaymentEvent == rendered.debtPaymentEvent &&
           state.debtAmount == rendered.debtAmount &&
           state.landingEventAcknowledged == rendered.landingEventAcknowledged &&
           state.turnsUntilYou == rendered.turnsUntilYou &&
           state.authorityOnline == rendered.authorityOnline &&
           state.fullAuthoritySnapshotValid == rendered.fullAuthoritySnapshotValid &&
           state.rosterSnapshotValid == rendered.rosterSnapshotValid &&
           state.playerDetail.loadState == rendered.playerDetail.loadState &&
           state.playerDetail.requestId == rendered.playerDetail.requestId &&
           state.playerDetail.stateVersion == rendered.playerDetail.stateVersion &&
           state.playerDetail.cash == rendered.playerDetail.cash &&
           state.playerDetail.playerId == rendered.playerDetail.playerId &&
           state.playerDetail.position == rendered.playerDetail.position &&
           state.playerDetail.assetCount == rendered.playerDetail.assetCount &&
           state.playerDetail.financialRecordCount ==
               rendered.playerDetail.financialRecordCount &&
           state.boardCatalogCompatible == rendered.boardCatalogCompatible &&
           state.authorityAssetCount == rendered.authorityAssetCount &&
           state.boardIdHash == rendered.boardIdHash &&
           state.lastEventSequence == rendered.lastEventSequence &&
           state.auctionCurrentBidderId == rendered.auctionCurrentBidderId &&
           state.auctionHighestBidderId == rendered.auctionHighestBidderId &&
           state.auctionPassedMask == rendered.auctionPassedMask &&
           state.auctionFlags == rendered.auctionFlags &&
           state.auctionReadyMask == rendered.auctionReadyMask &&
           state.auctionRequiredReadyMask == rendered.auctionRequiredReadyMask &&
           state.auctionCurrentBid == rendered.auctionCurrentBid &&
           state.auctionMinimumBid == rendered.auctionMinimumBid &&
           state.auctionGeneration == rendered.auctionGeneration &&
           state.auctionPresentation == rendered.auctionPresentation &&
           state.auctionResultAssetIndex == rendered.auctionResultAssetIndex &&
           state.auctionWinnerPlayerId == rendered.auctionWinnerPlayerId &&
           state.auctionResultAmount == rendered.auctionResultAmount &&
           state.auctionPresentationUntilMs == rendered.auctionPresentationUntilMs &&
           state.cardPresentation == rendered.cardPresentation &&
           state.cardEventSequence == rendered.cardEventSequence &&
           state.pendingCardFlags == rendered.pendingCardFlags &&
           state.cardIndex == rendered.cardIndex &&
           state.cardFlags == rendered.cardFlags &&
           state.cardInstanceId == rendered.cardInstanceId &&
           state.cardCatalogId == rendered.cardCatalogId &&
           state.cardEffectId == rendered.cardEffectId &&
           state.cardTargetPlayerId == rendered.cardTargetPlayerId &&
           state.cardTargetPosition == rendered.cardTargetPosition &&
           state.cardOutcome == rendered.cardOutcome &&
           state.cardAmount == rendered.cardAmount &&
           state.cardChance == rendered.cardChance &&
           state.cardResultValid == rendered.cardResultValid &&
           state.cardPresentationAcknowledged == rendered.cardPresentationAcknowledged &&
           state.cardEffectApplied == rendered.cardEffectApplied &&
           memcmp(state.authorityPlayers, rendered.authorityPlayers,
                  sizeof(state.authorityPlayers)) == 0 &&
           memcmp(state.authorityAssets, rendered.authorityAssets,
                  sizeof(state.authorityAssets)) == 0 &&
           memcmp(state.rosterNames, rendered.rosterNames,
                  sizeof(state.rosterNames)) == 0 &&
           memcmp(&state.activity, &rendered.activity, sizeof(state.activity)) == 0 &&
           memcmp(state.playerDetail.assets, rendered.playerDetail.assets,
                  sizeof(state.playerDetail.assets)) == 0 &&
           memcmp(state.playerDetail.financialRecords,
                  rendered.playerDetail.financialRecords,
                  sizeof(state.playerDetail.financialRecords)) == 0 &&
           state.debt.transactionId == rendered.debt.transactionId &&
           state.debt.amountDue == rendered.debt.amountDue &&
           state.debt.cashBefore == rendered.debt.cashBefore &&
           state.debt.selectedMask == rendered.debt.selectedMask &&
           state.debt.eligibleMask == rendered.debt.eligibleMask &&
           state.debt.submittedMortgageRequestId == rendered.debt.submittedMortgageRequestId &&
           state.debt.submittedMortgageMask == rendered.debt.submittedMortgageMask &&
           state.debt.bankruptcyPending == rendered.debt.bankruptcyPending &&
            state.debt.bankruptcyResolved == rendered.debt.bankruptcyResolved &&
            state.auctionPassed == rendered.auctionPassed &&
            identityVisualStateUnchanged(state.identity, rendered.identity) &&
            state.toastUntilMs == rendered.toastUntilMs &&
           textStateUnchanged(state.toast, rendered.toast);
}

uint8_t tradeFocusForAction(const AppState &state, TouchAction action)
{
    const bool locked = appTradeReceiverLocked(state);
    if (action == TouchAction::TradeReceiver) return locked ? 0xff : 0;
    if (action == TouchAction::TradeAssets) return locked ? 0 : 1;
    if (action == TouchAction::TradeAmount) return locked ? 1 : 2;
    if (action == TouchAction::TradeConfirm) return locked ? 2 : 3;
    return locked ? 3 : 4;
}

bool bindingIsFocused(const AppState &state, TouchAction action)
{
    if (action == TouchAction::Footer || action == TouchAction::Back) {
        return appFocusIsFooter(state);
    }
    switch (state.page) {
        case ScreenPage::Home:
            return false;
        case ScreenPage::Assets:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Asset0) + state.focus);
        case ScreenPage::Players:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::Player0) + state.focus);
        case ScreenPage::AssetDetail:
            if (state.focus == 0) return action == TouchAction::DetailPrimary;
            if (state.focus == 1) return action == TouchAction::DetailSecondary;
            if (state.focus == 2) return action == TouchAction::DetailTertiary;
            if (state.focus == 3) return action == TouchAction::DetailRefresh;
            return action == TouchAction::DetailBack || action == TouchAction::Footer;
        case ScreenPage::PlayerDetail:
            if (state.playerDetail.loadState == PlayerDetailLoadState::Loading) {
                return action == TouchAction::DetailBack || action == TouchAction::Footer;
            }
            if (state.playerDetail.loadState != PlayerDetailLoadState::Ready) {
                return action == (state.focus == 0 ? TouchAction::DetailRefresh
                                                   : TouchAction::DetailBack);
            }
            if (state.focus == 0) return action == TouchAction::DetailPrimary;
            if (state.focus == 1) return action == TouchAction::DetailSecondary;
            if (state.focus == 2) return action == TouchAction::DetailTertiary;
            if (state.focus == 3) return action == TouchAction::DetailRefresh;
            return action == TouchAction::DetailBack || action == TouchAction::Footer;
        case ScreenPage::PlayerAssets:
        case ScreenPage::PlayerFinance:
        case ScreenPage::Activity:
            return action == TouchAction::Footer ? appFocusIsFooter(state) : false;
        case ScreenPage::Trade:
            return tradeFocusForAction(state, action) == state.focus;
        case ScreenPage::TradeAssetSelect:
            return action == TouchAction::Footer ? appFocusIsFooter(state) : false;
        case ScreenPage::TradeOffer:
            if (state.focus == 0) return action == TouchAction::DetailPrimary;
            if (state.focus == 1) return action == TouchAction::DetailSecondary;
            if (state.focus == 2) return action == TouchAction::DetailTertiary;
            return action == TouchAction::Footer || action == TouchAction::Back;
        case ScreenPage::DemoLab:
            return action == static_cast<TouchAction>(static_cast<uint16_t>(TouchAction::DemoWaiting) + state.focus);
        case ScreenPage::Debt:
            return action == TouchAction::DetailPrimary && state.focus == 0;
        case ScreenPage::MoveGuide:
        case ScreenPage::TileEvent:
        case ScreenPage::CardReveal:
        case ScreenPage::ExtraRollReward:
        case ScreenPage::Bankruptcy:
            return action == TouchAction::DetailPrimary && state.focus == 0;
        case ScreenPage::Purchase:
        case ScreenPage::Auction:
            return action == (state.focus == 0 ? TouchAction::DetailPrimary
                                               : TouchAction::DetailSecondary);
        case ScreenPage::AvatarSetup:
            if (state.focus < static_cast<uint8_t>(AvatarEditField::Confirm)) {
                return action == static_cast<TouchAction>(
                    static_cast<uint16_t>(TouchAction::IdentityRow0) + state.focus);
            }
            return action == TouchAction::IdentityConfirm;
        case ScreenPage::AvatarLoading:
            return false;
        case ScreenPage::NameReview:
            if (state.focus == 0) return action == TouchAction::NameEdit;
            if (state.focus == 1) return action == TouchAction::NameConfirm;
            return action == TouchAction::NameBack;
        case ScreenPage::NameHandwriting:
            return action == (state.focus == 0 ? TouchAction::NameDelete
                                               : TouchAction::HandwritingConfirm);
        case ScreenPage::PlayerReady:
            return false;
        default: return false;
    }
}

void refreshSamePageFocus(const AppState &state)
{
    if (state.page == ScreenPage::AvatarSetup) {
        for (uint8_t row = 0; row < 5; ++row) {
            AvatarRowBinding &binding = avatarRowBindings[row];
            if (binding.object == nullptr) continue;
            const bool focused = state.focus == row;
            const bool editing = focused && state.identity.editingValue;
            lv_obj_set_style_bg_color(
                binding.object, lv_color_hex(focused ? 0x16302A : 0x061017), 0
            );
            lv_obj_set_style_border_color(
                binding.object, lv_color_hex(focused ? kGreen : 0x061017), 0
            );
            lv_obj_set_style_text_color(
                binding.numberLabel, lv_color_hex(focused ? kGreen : kMuted), 0
            );
            lv_obj_set_style_text_color(
                binding.eyebrowLabel, lv_color_hex(focused ? kGreen : kMuted), 0
            );
            lv_obj_set_style_text_color(
                binding.valueLabel, lv_color_hex(focused ? kGreen : kText), 0
            );
            lv_label_set_text(binding.affordanceLabel, editing ? "<>" : ">");
            lv_obj_set_style_text_color(
                binding.affordanceLabel, lv_color_hex(focused ? kGreen : kMuted), 0
            );
            lv_obj_set_style_bg_opa(binding.divider, focused ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
            lv_obj_set_style_border_opa(
                binding.divider, focused ? LV_OPA_TRANSP : LV_OPA_COVER, 0
            );
            if (focused) animateFocusEntry(binding.object);
        }

        for (uint8_t index = 0; index < tapBindingCount; ++index) {
            TapBinding &binding = tapBindings[index];
            if (binding.action != TouchAction::IdentityConfirm) continue;
            const bool focused = bindingIsFocused(state, binding.action);
            lv_obj_set_style_bg_color(
                binding.object,
                lv_color_hex(focused ? binding.focusedBackground : kPanel), 0
            );
            lv_obj_set_style_border_color(
                binding.object, lv_color_hex(focused ? kGreen : kLine), 0
            );
            if (binding.label != nullptr) {
                lv_obj_set_style_text_color(
                    binding.label, lv_color_hex(focused ? kGreen : kText), 0
                );
            }
            if (focused) animateFocusEntry(binding.object);
        }
        return;
    }

    const uint32_t pageAccent = focusAccent(state);
    for (uint8_t index = 0; index < tapBindingCount; ++index) {
        TapBinding &binding = tapBindings[index];
        const bool focused = bindingIsFocused(state, binding.action);
        const uint32_t accent = binding.accent == 0 ? pageAccent : binding.accent;
        lv_obj_set_style_bg_color(
            binding.object,
            lv_color_hex(focused ? binding.focusedBackground : kPanel), 0
        );
        lv_obj_set_style_border_color(binding.object, lv_color_hex(focused ? accent : kLine), 0);
        if (binding.label != nullptr) {
            lv_obj_set_style_text_color(binding.label, lv_color_hex(focused ? accent : kText), 0);
        }
        if (focused) animateFocusEntry(binding.object);
    }
}

void fillAssetItems(const AppState &state,
                    UiListItemView (&items)[kSyncedAssetCapacity],
                    char (&meta)[kSyncedAssetCapacity][24])
{
    const uint8_t count = appVisibleAssetCount(state);
    for (uint8_t row = 0; row < count; ++row) {
        const uint8_t assetIndex = appVisibleAssetIndex(state, row);
        if (appAssetMortgaged(state, assetIndex)) {
            snprintf(meta[row], sizeof(meta[row]), "MORTGAGED");
        } else {
            snprintf(meta[row], sizeof(meta[row]), "$%ld",
                     static_cast<long>(appAssetValue(state, assetIndex)));
        }
        const GridCityVisualDefinition &visual = visualOrFallback(
            appAssetVisual(state, assetIndex)
        );
        items[row] = UiListItemView{appAssetDisplayName(state, assetIndex),
                                    meta[row], "", true, false,
                                    visual.accent, visual.groupCode};
    }
}

void fillTradeAssetItems(const AppState &state,
                         UiListItemView (&items)[kSyncedAssetCapacity],
                         char (&meta)[kSyncedAssetCapacity][24])
{
    const uint8_t count = appTradeAssetCount(state);
    for (uint8_t row = 0; row < count; ++row) {
        const uint8_t assetIndex = appTradeAssetIndex(state, row);
        const bool eligible = appTradeAssetEligible(state, assetIndex);
        if (appAssetMortgaged(state, assetIndex)) {
            snprintf(meta[row], sizeof(meta[row]), "MORTGAGED");
        } else if (appAssetBuildingLevel(state, assetIndex) != 0) {
            snprintf(meta[row], sizeof(meta[row]), "SELL BUILDINGS FIRST");
        } else {
            snprintf(meta[row], sizeof(meta[row]), "$%ld",
                     static_cast<long>(appAssetValue(state, assetIndex)));
        }
        const GridCityVisualDefinition &visual = visualOrFallback(
            appAssetVisual(state, assetIndex)
        );
        items[row] = UiListItemView{appAssetDisplayName(state, assetIndex), meta[row], "",
                                    eligible, appTradeAssetSelected(state, assetIndex),
                                    visual.accent, visual.groupCode, true};
    }
}

void fillPlayerItems(const AppState &state, UiListItemView (&items)[kMaxPlayerCount],
                     char (&meta)[kMaxPlayerCount][32])
{
    for (uint8_t index = 0; index < state.playerCount; ++index) {
        const uint8_t playerId = static_cast<uint8_t>(index + 1);
        const bool connected = state.authoritySnapshotValid &&
                               (state.authorityPlayers[index].flags & (1u << 2)) != 0;
        if (playerId == state.selfSeatId) {
            snprintf(meta[index], sizeof(meta[index]), "YOU | %s",
                     connected ? "ONLINE" : "LOCAL");
        } else if (playerId == state.activePlayerId) {
            snprintf(meta[index], sizeof(meta[index]), "CURRENT TURN | %s",
                     connected ? "ONLINE" : "OFFLINE");
        } else {
            snprintf(meta[index], sizeof(meta[index]), "%s | OPEN DETAILS",
                     connected ? "ONLINE" : "OFFLINE");
        }
        items[index] = UiListItemView{appPlayerDisplayName(state, index),
                                     meta[index], "", true, false};
    }
}

void fillDemoItems(UiListItemView (&items)[kDemoListCount])
{
    static const char *titles[kDemoListCount] = {
        "Waiting turn", "Next turn", "My turn", "20s rent", "10s payment", "Debt resolution",
    };
    for (uint8_t index = 0; index < kDemoListCount; ++index) {
        items[index] = UiListItemView{titles[index], "", "", true, false};
    }
}

const char *financialKindLabel(uint8_t kind)
{
    switch (kind) {
        case 1: return "OPENING BALANCE";
        case 6: return "START BONUS";
        case 8: return "PURCHASE";
        case 9: return "RENT";
        case 10: return "FEE";
        case 11: return "CARD";
        case 14: return "MORTGAGE";
        case 15: return "UNMORTGAGE";
        case 16: return "BUILDING";
        case 17: return "AUCTION";
        case 18: return "BANKRUPTCY";
        case 21: return "TRADE";
        case 26: return "DEBT";
        default: return "ACCOUNT";
    }
}

const char *activityCardTitle(uint8_t deckId, uint8_t cardIndex)
{
    static const char *chance[8] = {
        "CITY GRANT", "SERVICE CITATION", "ADVANCE TO START", "GO TO HOLD",
        "STEP BACK", "CITY REPAIR BILL", "STARTUP WINDFALL", "TRANSIT SURCHARGE",
    };
    static const char *community[8] = {
        "COMMUNITY DIVIDEND", "COMMUNITY DUES", "ADVANCE TO START", "GO TO HOLD",
        "STEP BACK", "NEIGHBORHOOD REPAIR", "COMMUNITY REWARD", "NEIGHBORHOOD BONUS",
    };
    const uint8_t index = cardIndex < 8 ? cardIndex : 0;
    return deckId == 2 ? community[index] : chance[index];
}

const char *activityPlayerReference(const AppState &state, uint8_t playerId)
{
    return playerId != 0 && playerId == state.selfSeatId
        ? "YOU" : appPlayerNameById(state, playerId);
}

uint32_t activityAccent(uint8_t kind)
{
    switch (kind) {
        case 9:
        case 10:
        case 18:
        case 25:
        case 26: return kRed;
        case 17:
        case 22:
        case 23:
        case 24: return kYellow;
        case 2:
        case 3:
        case 5:
        case 6:
        case 12:
        case 13: return kBlue;
        default: return kGreen;
    }
}

void formatActivitySummary(const AppState &state, const TransportGameEvent &event,
                           char *output, size_t outputSize)
{
    const long amount = static_cast<long>(event.amount < 0 ? -event.amount : event.amount);
    const char *asset = event.assetIndex == 0xFF
        ? "PROPERTY" : appAssetDisplayName(state, event.assetIndex);
    const char *target = activityPlayerReference(state, event.targetId);
    switch (event.kind) {
        case 2: snprintf(output, outputSize, "STARTED THEIR TURN"); break;
        case 3: snprintf(output, outputSize, "ROLLED %ld", amount); break;
        case 5:
            snprintf(output, outputSize, "MOVED TO %s",
                     appTileDisplayName(state, static_cast<uint8_t>(event.amount)));
            break;
        case 6: snprintf(output, outputSize, "PASSED START +$%ld", amount); break;
        case 7: snprintf(output, outputSize, "LANDED ON %s", asset); break;
        case 8: snprintf(output, outputSize, "BOUGHT %s FOR $%ld", asset, amount); break;
        case 9: snprintf(output, outputSize, "PAID %s $%ld RENT", target, amount); break;
        case 10: snprintf(output, outputSize, "PAID CITY BANK $%ld", amount); break;
        case 11: {
            const uint8_t deckId = static_cast<uint8_t>(((event.detail >> 19) & 0x01u) + 1u);
            const uint8_t cardIndex = static_cast<uint8_t>((event.detail >> 16) & 0x07u);
            snprintf(output, outputSize, "DREW %s: %s",
                     deckId == 1 ? "CHANCE" : "COMMUNITY CHEST",
                     activityCardTitle(deckId, cardIndex));
            break;
        }
        case 12: snprintf(output, outputSize, "WENT TO HOLD"); break;
        case 13: snprintf(output, outputSize, "LEFT HOLD"); break;
        case 14: snprintf(output, outputSize, "MORTGAGED %s", asset); break;
        case 15: snprintf(output, outputSize, "REDEEMED %s", asset); break;
        case 16:
            snprintf(output, outputSize, "%s ON %s - LEVEL %lu",
                     event.amount < 0 ? "BUILT" : "SOLD A BUILDING",
                     asset, static_cast<unsigned long>(event.detail));
            break;
        case 17: snprintf(output, outputSize, "WON %s FOR $%ld", asset, amount); break;
        case 18: snprintf(output, outputSize, "WENT BANKRUPT"); break;
        case 19: snprintf(output, outputSize, "ENDED THEIR TURN"); break;
        case 20: snprintf(output, outputSize, "WON THE GAME"); break;
        case 21: snprintf(output, outputSize, "TRADED WITH %s", target); break;
        case 22: snprintf(output, outputSize, "STARTED AUCTION: %s", asset); break;
        case 23: snprintf(output, outputSize, "BID $%ld ON %s", amount, asset); break;
        case 24: snprintf(output, outputSize, "PASSED ON %s", asset); break;
        case 25: snprintf(output, outputSize, "OWES %s $%ld", target, amount); break;
        case 26: snprintf(output, outputSize, "PAID %s $%ld", target, amount); break;
        case 28: snprintf(output, outputSize, "SENT AN OFFER TO %s", target); break;
        case 29: snprintf(output, outputSize, "UPDATED OFFER WITH %s", target); break;
        case 30: snprintf(output, outputSize, "TRADE WITH %s CLOSED", target); break;
        default: snprintf(output, outputSize, "GAME UPDATE"); break;
    }
}

void fillActivityItems(const AppState &state,
                       UiListItemView (&items)[kActivityCapacity],
                       char (&titles)[kActivityCapacity][112])
{
    for (uint8_t row = 0; row < appActivityCount(state); ++row) {
        const ActivityEntry *entry = appActivityEntryAt(state, row);
        if (entry == nullptr) continue;
        char summary[88]{};
        formatActivitySummary(state, entry->event, summary, sizeof(summary));
        snprintf(titles[row], sizeof(titles[row]), "%s: %s",
                 appPlayerNameById(state, entry->event.actorId), summary);
        items[row] = UiListItemView{titles[row], "", "", true, false,
                                    activityAccent(entry->event.kind), "", false,
                                    entry->selfOwnedAsset ? "MY" : ""};
    }
}

void fillPlayerAssetItems(const AppState &state,
                          UiListItemView (&items)[kPlayerDetailAssetCapacity],
                          char (&meta)[kPlayerDetailAssetCapacity][24])
{
    for (uint8_t row = 0; row < state.playerDetail.assetCount; ++row) {
        const TransportPlayerAsset &asset = state.playerDetail.assets[row];
        const uint8_t level = asset.state & 0x07u;
        if ((asset.state & 0x08u) != 0) {
            snprintf(meta[row], sizeof(meta[row]), "MORTGAGED");
        } else if (level != 0) {
            snprintf(meta[row], sizeof(meta[row]), "LEVEL %u", level);
        } else {
            snprintf(meta[row], sizeof(meta[row]), "OWNED");
        }
        const GridCityVisualDefinition &visual = visualOrFallback(
            appAssetVisual(state, asset.assetIndex)
        );
        items[row] = UiListItemView{
            appAssetDisplayName(state, asset.assetIndex), meta[row], "", true, false,
            visual.accent, visual.groupCode
        };
    }
}

void fillPlayerFinanceItems(const AppState &state,
                            UiListItemView (&items)[kPlayerFinanceCapacity],
                            char (&titles)[kPlayerFinanceCapacity][32],
                            char (&meta)[kPlayerFinanceCapacity][24])
{
    for (uint8_t row = 0; row < state.playerDetail.financialRecordCount; ++row) {
        const TransportFinancialRecord &record = state.playerDetail.financialRecords[row];
        if (record.assetIndex != 0xFF) {
            snprintf(titles[row], sizeof(titles[row]), "%s | %.15s",
                     financialKindLabel(record.kind),
                     appAssetDisplayName(state, record.assetIndex));
        } else {
            snprintf(titles[row], sizeof(titles[row]), "%s", financialKindLabel(record.kind));
        }
        const int64_t absoluteAmount = record.amount < 0
            ? -static_cast<int64_t>(record.amount) : record.amount;
        if (record.counterpartyId != 0) {
            snprintf(meta[row], sizeof(meta[row]), "%c$%lld  %.9s",
                     record.amount >= 0 ? '+' : '-', static_cast<long long>(absoluteAmount),
                     appPlayerNameById(state, record.counterpartyId));
        } else {
            snprintf(meta[row], sizeof(meta[row]), "%c$%lld",
                     record.amount >= 0 ? '+' : '-', static_cast<long long>(absoluteAmount));
        }
        items[row] = UiListItemView{titles[row], meta[row], "", true, false};
    }
}

void updateCenterListFocus(const AppState &state)
{
    if (state.page == ScreenPage::Assets) {
        UiListItemView items[kSyncedAssetCapacity]{};
        char meta[kSyncedAssetCapacity][24]{};
        const uint8_t count = appVisibleAssetCount(state);
        fillAssetItems(state, items, meta);
        uiCenterListUpdate(centerList, items, count, state.assetListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::Players) {
        UiListItemView items[kMaxPlayerCount]{};
        char meta[kMaxPlayerCount][32]{};
        fillPlayerItems(state, items, meta);
        uiCenterListUpdate(centerList, items, state.playerCount, state.playerListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::PlayerAssets) {
        UiListItemView items[kPlayerDetailAssetCapacity]{};
        char meta[kPlayerDetailAssetCapacity][24]{};
        fillPlayerAssetItems(state, items, meta);
        uiCenterListUpdate(centerList, items, state.playerDetail.assetCount,
                           state.playerAssetListIndex, "BACK", true,
                           appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::PlayerFinance) {
        UiListItemView items[kPlayerFinanceCapacity]{};
        char titles[kPlayerFinanceCapacity][32]{};
        char meta[kPlayerFinanceCapacity][24]{};
        fillPlayerFinanceItems(state, items, titles, meta);
        uiCenterListUpdate(centerList, items, state.playerDetail.financialRecordCount,
                           state.playerFinanceListIndex, "BACK", true,
                           appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::Activity) {
        UiListItemView items[kActivityCapacity]{};
        char titles[kActivityCapacity][112]{};
        fillActivityItems(state, items, titles);
        uiCenterListUpdate(centerList, items, appActivityCount(state),
                           state.activityListIndex, "BACK", true,
                           appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::TradeAssetSelect) {
        UiListItemView items[kSyncedAssetCapacity]{};
        char meta[kSyncedAssetCapacity][24]{};
        const uint8_t count = appTradeAssetCount(state);
        fillTradeAssetItems(state, items, meta);
        uiCenterListUpdate(centerList, items, count, state.tradeAssetListIndex,
                           "BACK", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::DemoLab) {
        UiListItemView items[kDemoListCount]{};
        fillDemoItems(items);
        uiCenterListUpdate(centerList, items, kDemoListCount, state.demoListIndex,
                           "返回", true, appFocusIsFooter(state), true);
    } else if (state.page == ScreenPage::DebtAssets) {
        UiListItemView items[kSyncedAssetCapacity]{};
        char meta[kSyncedAssetCapacity][24]{};
        const uint8_t count = appVisibleAssetCount(state);
        for (uint8_t row = 0; row < count; ++row) {
            const uint8_t assetIndex = appVisibleAssetIndex(state, row);
            snprintf(meta[row], sizeof(meta[row]), "+$%ld",
                     static_cast<long>(appAssetMortgageValue(state, assetIndex)));
            const GridCityVisualDefinition &visual = visualOrFallback(
                appAssetVisual(state, assetIndex)
            );
            items[row] = UiListItemView{appAssetDisplayName(state, assetIndex), meta[row], "",
                                       appDebtAssetEligible(state, assetIndex),
                                       appDebtAssetSelected(state, assetIndex),
                                       visual.accent, visual.groupCode, true};
        }
        uiCenterListUpdate(centerList, items, count, state.debtListIndex,
                           appDebtCanConfirm(state) ? "CONFIRM" : "SELECT ASSETS",
                           appDebtCanConfirm(state), appFocusIsFooter(state), true);
    }
}

void drawTurnDots(uint8_t turnsUntilYou)
{
    const uint8_t filled = turnsUntilYou > 5 ? 5 : turnsUntilYou;
    for (uint8_t index = 0; index < 5; ++index) {
        lv_obj_t *dot = uiBox(root, kTurnDots[index], index < filled ? kGreen : kBg, kGreen, 4);
        if (dot != nullptr && index >= filled) lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    }
}

lv_obj_t *drawFooter(const char *text, bool focused)
{
    lv_obj_t *footer = uiBox(root, kNormalFooter, focused ? 0x16302A : kPanel,
                             focused ? kGreen : kLine, 6);
    if (footer == nullptr) return nullptr;
    uiLabel(footer, text, UiRect{8, 16, static_cast<int16_t>(kNormalFooter.w - 16), 28},
            &ui_font_16, focused ? kGreen : kText);
    makeClickable(footer, TouchAction::Footer);
    if (focused) animateFocusEntry(footer);
    return footer;
}

void drawOuterRing(uint32_t accent = kGreen)
{
    outerRing = uiBox(root, kOuterRing, kBg, accent, 209);
    lv_obj_set_style_bg_opa(outerRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outerRing, 5, 0);
    lv_obj_set_style_border_opa(outerRing, LV_OPA_COVER, 0);
    lv_obj_t *inner = uiBox(root, kInnerRing, kBg, kLine, 196);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 2, 0);
}

void drawHeader(const char *title, const char *eyebrow = "GRIDOPOLY")
{
    label(root, eyebrow, 100, 55, 280, &ui_font_14, kMuted);
    label(root, title, 70, 77, 340, &lv_font_simsun_16_cjk, kText);
}

void drawToast(const AppState &state, uint32_t nowMs)
{
    if (state.toastUntilMs == 0 ||
        static_cast<int32_t>(nowMs - state.toastUntilMs) >= 0) return;
    lv_obj_t *toast = box(root, 112, 397, 256, 36, 0x172224, kGreen, 6);
    label(toast, state.toast, 8, 7, 240, &lv_font_simsun_16_cjk, kText);
}

void drawConnectionStatus(const AppState &state)
{
    if (state.authorityOnline) return;

    lv_obj_t *badge = uiBox(root, kConnectionLostBadge, kBg, kRed, 8);
    if (badge == nullptr) return;
    lv_obj_set_style_bg_opa(badge, LV_OPA_90, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = uiLabel(badge, LV_SYMBOL_WIFI, UiRect{7, 3, 30, 30},
                             &lv_font_montserrat_24, kRed);
    if (icon != nullptr) lv_obj_set_style_text_opa(icon, LV_OPA_COVER, 0);

    static lv_point_t slashPoints[] = {{10, 5}, {33, 29}};
    lv_obj_t *slash = lv_line_create(badge);
    if (slash != nullptr) {
        lv_line_set_points(slash, slashPoints, 2);
        lv_obj_set_style_line_color(slash, lv_color_hex(kRed), 0);
        lv_obj_set_style_line_width(slash, 3, 0);
        lv_obj_set_style_line_rounded(slash, true, 0);
    }
    lv_obj_move_foreground(badge);
}

lv_obj_t *createActivityBanner(const AppState &state, const ActivityEntry &entry,
                               bool clickable)
{
    const uint32_t accent = activityAccent(entry.event.kind);
    lv_obj_t *banner = uiBox(root, kActivityBanner, 0x0D171A, accent, 8);
    if (banner == nullptr) return nullptr;
    lv_obj_set_style_bg_opa(banner, LV_OPA_90, 0);
    lv_obj_set_style_border_width(banner, 2, 0);
    char summary[88]{};
    char line[128]{};
    formatActivitySummary(state, entry.event, summary, sizeof(summary));
    snprintf(line, sizeof(line), "%s  |  %s",
             appPlayerNameById(state, entry.event.actorId), summary);
    const UiRect labelRect = entry.selfOwnedAsset
        ? UiRect{10, 10, 210, 28} : UiRect{10, 10, 252, 28};
    lv_obj_t *activityLabel = uiLabel(
        banner, line, labelRect, &ui_font_14, accent,
        LV_TEXT_ALIGN_LEFT
    );
    if (activityLabel != nullptr) {
        lv_label_set_long_mode(activityLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    if (entry.selfOwnedAsset) {
        lv_obj_t *ownershipTag = uiBox(
            banner, UiRect{228, 13, 34, 22}, kBg, accent, 7
        );
        if (ownershipTag != nullptr) {
            lv_obj_set_style_bg_opa(ownershipTag, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ownershipTag, 1, 0);
            lv_obj_clear_flag(ownershipTag, LV_OBJ_FLAG_CLICKABLE);
            uiLabel(ownershipTag, "MY", UiRect{1, 2, 32, 18},
                    &lv_font_montserrat_10, accent, LV_TEXT_ALIGN_CENTER);
        }
    }
    if (clickable) makeClickable(banner, TouchAction::ActivityOpen);
    else lv_obj_clear_flag(banner, LV_OBJ_FLAG_CLICKABLE);
    return banner;
}

void drawActivityHud(const AppState &state, uint32_t nowMs)
{
    if (!state.authorityOnline || state.page == ScreenPage::Activity ||
        appActivityCount(state) == 0) return;

    if (appActivityBannerVisible(state, nowMs)) {
        const ActivityEntry *entry = appActivityBannerEntry(state);
        if (entry == nullptr) return;
        const bool replacingVisibleBanner = hasRenderedState &&
            previousPage == state.page &&
            previousRenderedState.authorityOnline &&
            previousRenderedState.page != ScreenPage::Activity &&
            appActivityBannerVisible(previousRenderedState, nowMs) &&
            previousRenderedState.activity.bannerSequence != state.activity.bannerSequence;
        if (replacingVisibleBanner) {
            const ActivityEntry *oldEntry = appActivityBannerEntry(previousRenderedState);
            if (oldEntry != nullptr) {
                lv_obj_t *oldBanner = createActivityBanner(
                    previousRenderedState, *oldEntry, false
                );
                animateActivityBannerExit(oldBanner);
            }
        }

        lv_obj_t *banner = createActivityBanner(state, *entry, true);
        if (banner == nullptr) return;
        if (replacingVisibleBanner) animateActivityBannerEntry(banner);
        else lv_obj_fade_in(banner, 160, 0);
        lv_obj_move_foreground(banner);
        return;
    }

    lv_obj_t *badge = uiBox(root, kActivityBadge, kBg, kLine, 8);
    if (badge == nullptr) return;
    lv_obj_set_style_bg_opa(badge, LV_OPA_90, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_t *activityButtonLabel = uiLabel(
        badge, "ACT", UiRect{0, 0, 44, 16}, &lv_font_montserrat_10, kMuted
    );
    if (activityButtonLabel != nullptr) {
        lv_obj_set_size(activityButtonLabel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_center(activityButtonLabel);
    }
    makeClickable(badge, TouchAction::ActivityOpen);
    lv_obj_move_foreground(badge);
}

void drawHome(const AppState &state, uint32_t nowMs)
{
    drawOuterRing(kGreen);
    const GridCityVisualDefinition &tileVisual = visualOrFallback(
        appTileVisual(state, state.position)
    );

    const char *title = "WAITING";
    const HomePhase visualPhase = appPresentedHomePhase(state);
    if (visualPhase == HomePhase::Waiting) {
        title = "WAITING";
    } else if (visualPhase == HomePhase::NextPlayer) {
        title = "NEXT PLAYER";
    } else {
        title = state.extraRollPresentation == ExtraRollPresentationPhase::Ready
            ? "EXTRA ROLL" : "YOUR TURN";
    }
    uiLabel(root, title, kHomeTitle, &lv_font_montserrat_16, kText);
    const bool ownTurn = visualPhase == HomePhase::MyTurn ||
                         visualPhase == HomePhase::MyTurnEnd;
    if (!ownTurn) {
        drawTurnDots(state.turnsUntilYou);
    } else {
        const char *reminder = visualPhase == HomePhase::MyTurn
            ? (state.extraRollPresentation == ExtraRollPresentationPhase::Ready
                ? "DOUBLES - ROLL AGAIN" : "ROLL NOW")
            : "END TURN READY";
        turnReminderLabel = uiLabel(
            root, reminder,
            UiRect{140, 108, 200, 20}, &lv_font_montserrat_12, kGreen);
        startTurnReminder(turnReminderLabel);
    }
    drawArtwork(kHomeArtwork, tileVisual);
    uiLabel(root, "AVAILABLE CASH", kHomeCashLabel, &lv_font_montserrat_14, kMuted);
    char money[24];
    snprintf(money, sizeof(money), "$ %ld", static_cast<long>(state.money));
    const uint8_t moneyPx = uiMoneyFontPx(state.money);
    const lv_font_t *moneyFont = moneyPx == 40 ? &lv_font_montserrat_40 :
                                 (moneyPx == 32 ? &lv_font_montserrat_32 :
                                                  &lv_font_montserrat_24);
    uiLabel(root, money, kHomeCashAmount, moneyFont, kText);
    char tileNumber[20];
    snprintf(tileNumber, sizeof(tileNumber), "TILE %u", state.position);
    uiLabel(root, tileNumber, kHomeTileNumber, &lv_font_montserrat_10, kMuted);
    uiLabel(root, appTileDisplayName(state, state.position), kHomeLocation,
            &lv_font_montserrat_12, tileVisual.accent);
    lv_obj_t *divider = uiBox(root, kHomeDivider, kLine, kLine, 0);
    if (divider != nullptr) lv_obj_set_style_border_width(divider, 0, 0);

    HomeAction actions[5]{};
    const uint8_t actionCount = uiCarouselActions(visualPhase, actions);
    if (actionCount != 0 && visualPhase == HomePhase::MyTurn &&
        state.extraRollPresentation == ExtraRollPresentationPhase::Ready) {
        actions[0] = HomeAction::ExtraRoll;
    }
    uiCarouselCreate(homeCarousel, root, actions, actionCount, state.focus);
    if (state.endTurnPresentation == EndTurnPresentationPhase::Exiting) {
        uiCarouselSetEndTurnExitProgress(
            homeCarousel, appEndTurnExitProgressPermille(state, nowMs)
        );
    }
}

void drawListRow(int16_t y, const char *title, const char *meta, bool selected, TouchAction action)
{
    lv_obj_t *row = box(root, 72, y, 336, 48, selected ? 0x16302A : kPanel,
                        selected ? kGreen : kLine, 6);
    label(row, title, 14, 7, 190, &lv_font_simsun_16_cjk, selected ? kGreen : kText, LV_TEXT_ALIGN_LEFT);
    label(row, meta, 210, 7, 110, &lv_font_simsun_16_cjk, kMuted, LV_TEXT_ALIGN_RIGHT);
    makeClickable(row, action);
    if (selected) animateFocusEntry(row);
}

void drawAssets(const AppState &state)
{
    drawOuterRing(kGreen);
    char subtitle[20];
    const uint8_t count = appVisibleAssetCount(state);
    snprintf(subtitle, sizeof(subtitle), "%u ASSETS", count);
    drawHeader("MY ASSETS", subtitle);
    UiListItemView items[kSyncedAssetCapacity]{};
    char meta[kSyncedAssetCapacity][24]{};
    fillAssetItems(state, items, meta);
    uiCenterListCreate(centerList, root, items, count, state.assetListIndex,
                       "返回", true, appFocusIsFooter(state));
}

void drawAssetDetail(const AppState &state)
{
    const GridCityVisualDefinition &visual = visualOrFallback(
        appAssetVisual(state, state.selectedAsset)
    );
    drawOuterRing(visual.accent);
    const char *assetName = appAssetDisplayName(state, state.selectedAsset);
    char category[32];
    if (visual.groupCode[0] != '\0') {
        snprintf(category, sizeof(category), "GROUP %s | %s", visual.groupCode,
                 visual.groupName);
    } else {
        snprintf(category, sizeof(category), "%s", visual.category);
    }
    drawHeader(assetName, category);
    drawArtwork(kAssetDetailArtwork, visual);
    char groupProgress[48];
    uint8_t ownedInGroup = 0;
    uint8_t totalInGroup = 0;
    if (appAssetGroupProgress(state, state.selectedAsset, ownedInGroup, totalInGroup)) {
        snprintf(groupProgress, sizeof(groupProgress), "COLOR GROUP  %u / %u OWNED",
                 ownedInGroup, totalInGroup);
    } else {
        snprintf(groupProgress, sizeof(groupProgress), "%s", visual.category);
    }
    uiLabel(root, groupProgress, kAssetDetailGroup, &lv_font_montserrat_12,
            visual.accent);
    char info[64];
    snprintf(info, sizeof(info), "VALUE $%ld  |  RENT $%ld",
             static_cast<long>(appAssetValue(state, state.selectedAsset)),
             static_cast<long>(appAssetRent(state, state.selectedAsset)));
    uiLabel(root, info, kAssetDetailMetrics, &lv_font_montserrat_14, kText);
    char cash[40];
    snprintf(cash, sizeof(cash), "AVAILABLE CASH  $%ld", static_cast<long>(state.money));
    uiLabel(root, cash, kAssetDetailCash, &lv_font_montserrat_12, kGreen);
    constexpr UiRect fourActionRects[4] = {
        kAssetDetailAction0, kAssetDetailAction1,
        kAssetDetailAction2, kAssetDetailAction3,
    };
    constexpr TouchAction touchActions[4] = {
        TouchAction::DetailPrimary, TouchAction::DetailSecondary,
        TouchAction::DetailTertiary, TouchAction::DetailRefresh,
    };
    const uint8_t actionCount = appAssetDetailActionCount(state);
    uint8_t visibleSlots[4]{};
    uint8_t visibleCount = 0;
    for (uint8_t index = 0; index < actionCount; ++index) {
        const AssetDetailAction action = appAssetDetailActionAt(state, index);
        if (appAssetDetailActionVisible(state, action)) visibleSlots[visibleCount++] = index;
    }
    for (uint8_t ordinal = 0; ordinal < visibleCount; ++ordinal) {
        const uint8_t index = visibleSlots[ordinal];
        const AssetDetailAction action = appAssetDetailActionAt(state, index);
        const bool enabled = appAssetDetailActionEnabled(state, action);
        const bool focused = state.focus == index;
        UiRect rect = fourActionRects[ordinal];
        if (visibleCount == 1) rect = kAssetDetailSingleAction;
        else if (visibleCount == 2) {
            rect = ordinal == 0 ? kAssetDetailPairLeft : kAssetDetailPairRight;
        } else if (visibleCount == 3) {
            constexpr UiRect tripleRects[3] = {
                kAssetDetailTripleTopLeft,
                kAssetDetailTripleTopRight,
                kAssetDetailTripleBottom,
            };
            rect = tripleRects[ordinal];
        }
        const uint32_t accent = enabled ? visual.accent : kMuted;
        lv_obj_t *button = uiBox(root, rect, focused ? 0x16302A : kPanel,
                                 focused ? accent : kLine, 6);
        if (button == nullptr) continue;

        char actionText[32];
        switch (action) {
            case AssetDetailAction::MortgageOrRedeem:
                snprintf(actionText, sizeof(actionText), "%s",
                         appAssetMortgaged(state, state.selectedAsset)
                             ? "REDEEM" : "MORTGAGE");
                break;
            case AssetDetailAction::Build: {
                const int32_t cost = appAssetBuildingCost(state, state.selectedAsset);
                if (cost > 0) snprintf(actionText, sizeof(actionText), "BUILD  $%ld",
                                       static_cast<long>(cost));
                else snprintf(actionText, sizeof(actionText), "BUILD");
                break;
            }
            case AssetDetailAction::SellBuilding: {
                const int32_t proceeds = appAssetBuildingCost(state, state.selectedAsset) / 2;
                if (proceeds > 0) snprintf(actionText, sizeof(actionText), "SELL  +$%ld",
                                           static_cast<long>(proceeds));
                else snprintf(actionText, sizeof(actionText), "SELL BUILDING");
                break;
            }
            case AssetDetailAction::Trade:
                snprintf(actionText, sizeof(actionText), "TRADE");
                break;
        }
        lv_obj_t *actionLabel = uiLabel(
            button, actionText,
            UiRect{6, 7, static_cast<int16_t>(rect.w - 12), 24},
            &lv_font_montserrat_12, focused ? accent : (enabled ? kText : kMuted)
        );
        makeClickable(button, touchActions[index], actionLabel, accent);
        if (focused) animateFocusEntry(button);
    }
    drawFooter("BACK", state.focus == actionCount);
}

void drawPlayers(const AppState &state)
{
    drawOuterRing(kYellow);
    char subtitle[16];
    snprintf(subtitle, sizeof(subtitle), "%u PLAYERS", state.playerCount);
    drawHeader("玩家情况", subtitle);
    UiListItemView items[kMaxPlayerCount]{};
    char meta[kMaxPlayerCount][32]{};
    fillPlayerItems(state, items, meta);
    uiCenterListCreate(centerList, root, items, state.playerCount, state.playerListIndex,
                       "返回", true, appFocusIsFooter(state));
}

lv_obj_t *drawDecisionButton(UiRect rect, const char *text, bool focused,
                             uint32_t accent, TouchAction action);
const TransportIdentitySeat *identitySeatForPlayer(const AppState &state,
                                                   uint8_t playerId,
                                                   uint8_t &seatIndex);
const char *identitySeatName(const AppState &state, uint8_t index,
                             char *fallback, size_t fallbackSize);
void drawFinalAvatar(lv_obj_t *parent, const AppState &state,
                     const TransportIdentitySeat *seat, uint8_t seatIndex,
                     UiRect rect, uint32_t accent);

void drawPlayerDetail(const AppState &state)
{
    drawOuterRing(kYellow);
    const uint8_t playerId = static_cast<uint8_t>(state.selectedPlayer + 1);
    uint8_t identitySeatIndex = 0xFF;
    const TransportIdentitySeat *identitySeat = identitySeatForPlayer(
        state, playerId, identitySeatIndex
    );
    char token[8];
    snprintf(token, sizeof(token), "P%u", static_cast<unsigned>(playerId));
    drawHeader(appPlayerDisplayName(state, state.selectedPlayer), token);
    drawFinalAvatar(root, state, identitySeat, identitySeatIndex,
                    kPlayerDetailAvatar, kYellow);

    if (state.playerDetail.loadState != PlayerDetailLoadState::Ready) {
        const bool failed = state.playerDetail.loadState != PlayerDetailLoadState::Loading;
        uiLabel(root, failed ? "DETAILS UNAVAILABLE" : "REQUESTING DETAILS",
                UiRect{kPlayerDetailSummary.x, kPlayerDetailSummary.y + 8,
                       kPlayerDetailSummary.w, 28},
                &lv_font_montserrat_14, failed ? kRed : kYellow,
                LV_TEXT_ALIGN_LEFT);
        uiLabel(root, failed ? "PRESS RETRY TO REQUEST AGAIN" : "ONE-TIME SERVER QUERY",
                UiRect{kPlayerDetailSummary.x, kPlayerDetailSummary.y + 44,
                       kPlayerDetailSummary.w, 32},
                &lv_font_montserrat_10, kMuted, LV_TEXT_ALIGN_LEFT);
        if (failed) {
            drawDecisionButton(UiRect{128, 260, 224, 56}, "RETRY", state.focus == 0,
                               kRed, TouchAction::DetailRefresh);
        }
        drawFooter("BACK", state.focus == (failed ? 1 : 0));
        return;
    }

    char cash[28];
    snprintf(cash, sizeof(cash), "$ %ld", static_cast<long>(state.playerDetail.cash));
    uiLabel(root, cash,
            UiRect{kPlayerDetailSummary.x, kPlayerDetailSummary.y,
                   kPlayerDetailSummary.w, 32},
            &lv_font_montserrat_24, kText, LV_TEXT_ALIGN_LEFT);
    char location[64];
    snprintf(location, sizeof(location), "TILE %u  %.30s", state.playerDetail.position,
             appTileDisplayName(state, state.playerDetail.position));
    lv_obj_t *locationLabel = uiLabel(
        root, location,
        UiRect{kPlayerDetailSummary.x, kPlayerDetailSummary.y + 38,
               kPlayerDetailSummary.w, 22},
        &lv_font_montserrat_12, kYellow, LV_TEXT_ALIGN_LEFT
    );
    if (locationLabel != nullptr) lv_label_set_long_mode(locationLabel, LV_LABEL_LONG_CLIP);
    char version[28];
    snprintf(version, sizeof(version), "SNAPSHOT V%lu",
             static_cast<unsigned long>(state.playerDetail.stateVersion));
    uiLabel(root, version,
            UiRect{kPlayerDetailSummary.x, kPlayerDetailSummary.y + 66,
                   kPlayerDetailSummary.w, 16},
            &lv_font_montserrat_10, kMuted, LV_TEXT_ALIGN_LEFT);

    char assets[24];
    char finance[24];
    snprintf(assets, sizeof(assets), "ASSETS  %u", state.playerDetail.assetCount);
    snprintf(finance, sizeof(finance), "FINANCE  %u", state.playerDetail.financialRecordCount);
    drawDecisionButton(kPlayerDetailAssets, assets, state.focus == 0,
                       kYellow, TouchAction::DetailPrimary);
    drawDecisionButton(kPlayerDetailFinance, finance, state.focus == 1,
                       kYellow, TouchAction::DetailSecondary);
    drawDecisionButton(kPlayerDetailTrade, "TRADE", state.focus == 2,
                       kYellow, TouchAction::DetailTertiary);
    drawDecisionButton(kPlayerDetailRefresh, "REFRESH", state.focus == 3,
                       kBlue, TouchAction::DetailRefresh);
    drawFooter("BACK", state.focus == 4);
}

void drawPlayerAssets(const AppState &state)
{
    drawOuterRing(kYellow);
    char subtitle[24];
    snprintf(subtitle, sizeof(subtitle), "%u OWNED", state.playerDetail.assetCount);
    drawHeader("PLAYER ASSETS", subtitle);
    UiListItemView items[kPlayerDetailAssetCapacity]{};
    char meta[kPlayerDetailAssetCapacity][24]{};
    fillPlayerAssetItems(state, items, meta);
    uiCenterListCreate(centerList, root, items, state.playerDetail.assetCount,
                       state.playerAssetListIndex, "BACK", true,
                       appFocusIsFooter(state));
}

void drawPlayerFinance(const AppState &state)
{
    drawOuterRing(kYellow);
    char subtitle[24];
    snprintf(subtitle, sizeof(subtitle), "LATEST %u", state.playerDetail.financialRecordCount);
    drawHeader("FINANCIAL REPORT", subtitle);
    UiListItemView items[kPlayerFinanceCapacity]{};
    char titles[kPlayerFinanceCapacity][32]{};
    char meta[kPlayerFinanceCapacity][24]{};
    fillPlayerFinanceItems(state, items, titles, meta);
    uiCenterListCreate(centerList, root, items, state.playerDetail.financialRecordCount,
                       state.playerFinanceListIndex, "BACK", true,
                       appFocusIsFooter(state));
}

void drawActivity(const AppState &state)
{
    drawOuterRing(kBlue);
    char subtitle[24]{};
    snprintf(subtitle, sizeof(subtitle), "LATEST %u", appActivityCount(state));
    drawHeader("LIVE ACTIVITY", subtitle);
    UiListItemView items[kActivityCapacity]{};
    char titles[kActivityCapacity][112]{};
    fillActivityItems(state, items, titles);
    uiCenterListCreate(centerList, root, items, appActivityCount(state),
                       state.activityListIndex, "BACK", true,
                       appFocusIsFooter(state));
}

void drawTradeField(UiRect rect, const char *text, const char *marker,
                    bool focused, bool editing, TouchAction action, bool enabled)
{
    lv_obj_t *field = uiBox(root, rect, focused ? 0x14242A : kPanel,
                            focused ? kBlue : kLine, 6);
    if (field == nullptr) return;
    if (editing) {
        lv_obj_t *inner = uiBox(field, UiRect{4, 4, static_cast<int16_t>(rect.w - 8),
                                             static_cast<int16_t>(rect.h - 8)},
                                kPanel, kBlue, 4);
        if (inner != nullptr) {
            lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    uiLabel(field, text, UiRect{12, 9, 184, 28}, &ui_font_16,
            enabled ? (focused ? kBlue : kText) : kMuted, LV_TEXT_ALIGN_LEFT);
    if (!enabled && !editing) {
        lv_obj_t *lockedMarker = lv_img_create(field);
        if (lockedMarker != nullptr) {
            lv_img_set_src(lockedMarker, lockedMarkerImage());
            lv_obj_set_pos(lockedMarker, 210, 17);
            lv_obj_set_style_img_recolor(lockedMarker, lv_color_hex(kMuted), 0);
            lv_obj_set_style_img_recolor_opa(lockedMarker, LV_OPA_COVER, 0);
            lv_obj_clear_flag(lockedMarker, LV_OBJ_FLAG_CLICKABLE);
        }
    } else {
        uiLabel(field, editing ? "EDIT" : marker, UiRect{198, 9, 60, 28},
                &lv_font_montserrat_14, editing ? kBlue : kMuted,
                LV_TEXT_ALIGN_RIGHT);
    }
    if (enabled) makeClickable(field, action);
    if (focused && !editing) animateFocusEntry(field);
}

void drawTrade(const AppState &state)
{
    drawOuterRing(kBlue);
    char subtitle[48];
    if (state.tradeEntryMode == TradeEntryMode::CounterLocked) {
        uint8_t receiveCount = 0;
        uint32_t receiveMask = state.tradeOffer.counterpartyAssetMask;
        while (receiveMask != 0) {
            receiveCount += static_cast<uint8_t>(receiveMask & 1u);
            receiveMask >>= 1;
        }
        snprintf(subtitle, sizeof(subtitle), "RECEIVE %u ASSETS + $%ld",
                 receiveCount,
                 static_cast<long>(state.tradeOffer.counterpartyGivesCash));
    } else {
        snprintf(subtitle, sizeof(subtitle), "BUILD YOUR OFFER");
    }
    drawHeader(state.tradeEntryMode == TradeEntryMode::CounterLocked
                   ? "COUNTER OFFER" : "TRADE OFFER",
               subtitle);
    const bool receiverLocked = appTradeReceiverLocked(state);
    const uint8_t receiverFocus = 0;
    const uint8_t assetsFocus = receiverLocked ? 0 : 1;
    const uint8_t amountFocus = receiverLocked ? 1 : 2;
    const uint8_t submitFocus = receiverLocked ? 2 : 3;
    const uint8_t backFocus = receiverLocked ? 3 : 4;
    char receiver[48];
    snprintf(receiver, sizeof(receiver), "RECEIVER  %s",
             appPlayerDisplayName(state, state.tradeReceiver));
    uint8_t selectedAssetCount = 0;
    for (uint8_t assetIndex = 0; assetIndex < 32; ++assetIndex) {
        if ((state.tradeGiveAssetMask & (static_cast<uint32_t>(1u) << assetIndex)) != 0) {
            ++selectedAssetCount;
        }
    }
    char assets[48];
    snprintf(assets, sizeof(assets), "GIVE ASSETS  %u SELECTED", selectedAssetCount);
    char amount[48];
    snprintf(amount, sizeof(amount), "GIVE CASH  $ %ld", static_cast<long>(state.tradeAmount));
    drawTradeField(kTradeReceiver, receiver, "",
                   !receiverLocked && state.focus == receiverFocus,
                   appInlineFieldEditing(state, InlineEditField::TradeReceiver),
                   TouchAction::TradeReceiver, !receiverLocked);
    drawTradeField(kTradeAssets, assets, "OPEN", state.focus == assetsFocus,
                   false, TouchAction::TradeAssets, true);
    drawTradeField(kTradeAmount, amount, "", state.focus == amountFocus,
                   appInlineFieldEditing(state, InlineEditField::TradeAmount),
                   TouchAction::TradeAmount, true);
    lv_obj_t *submit = uiBox(root, kTradeSubmit,
                             state.focus == submitFocus ? 0x14242A : kPanel,
                             state.focus == submitFocus ? kBlue : kLine, 6);
    if (submit != nullptr) {
        lv_obj_t *submitLabel = uiLabel(
            submit, "SUBMIT OFFER",
            UiRect{8, 13, static_cast<int16_t>(kTradeSubmit.w - 16), 26},
            &ui_font_16, state.focus == submitFocus ? kBlue : kText
        );
        makeClickable(submit, TouchAction::TradeConfirm, submitLabel, kBlue, 0x14242A);
        if (state.focus == submitFocus) animateFocusEntry(submit);
    }
    drawFooter("BACK", state.focus == backFocus);
}

void drawTradeReceiverPicker(const AppState &state)
{
    if (!state.tradeReceiverPickerOpen) return;
    lv_obj_t *shade = uiBox(root, UiRect{0, 0, 480, 480}, kBg, kBg, 0);
    if (shade != nullptr) {
        lv_obj_set_style_bg_opa(shade, LV_OPA_80, 0);
        lv_obj_set_style_border_width(shade, 0, 0);
        makeClickable(shade, TouchAction::TradeReceiverCancel);
        if (!hasRenderedState || !previousRenderedState.tradeReceiverPickerOpen) {
            lv_obj_fade_in(shade, 120, 0);
        }
    }

    uiLabel(root, "CHOOSE RECEIVER", kReceiverPickerHeader,
            &lv_font_montserrat_18, kText);
    const uint8_t count = appTradeReceiverCandidateCount(state);
    constexpr uint32_t avatarColors[6] = {
        0x58A7EB, 0xEF7168, 0x52DCB7, 0xF2C453, 0xC28AE8, 0xEA8A55,
    };
    const int16_t startY = static_cast<int16_t>(
        count >= 5 ? 116 : 128 + (5 - count) * 7
    );
    for (uint8_t candidate = 0; candidate < count; ++candidate) {
        const uint8_t playerIndex = appTradeReceiverCandidateAt(state, candidate);
        if (playerIndex == 0xFF) continue;
        const bool focused = candidate == state.tradeReceiverPickerIndex;
        const bool current = playerIndex == state.tradeReceiver;
        const uint32_t accent = avatarColors[playerIndex % 6];
        const int16_t y = static_cast<int16_t>(startY + candidate * 46);
        lv_obj_t *row = uiBox(
            root, UiRect{104, y, 272, 40},
            focused ? 0x14242A : kPanel,
            focused ? accent : kLine, 6
        );
        if (row == nullptr) continue;
        uint8_t identitySeatIndex = playerIndex;
        const TransportIdentitySeat *identitySeat = identitySeatForPlayer(
            state, static_cast<uint8_t>(playerIndex + 1), identitySeatIndex
        );
        drawFinalAvatar(row, state, identitySeat, identitySeatIndex,
                        UiRect{8, 4, 32, 32}, accent);
        uiLabel(row, appPlayerDisplayName(state, playerIndex),
                UiRect{48, 7, 132, 26}, &ui_font_14,
                focused ? accent : kText, LV_TEXT_ALIGN_LEFT);
        const bool connected = state.authoritySnapshotValid &&
            (state.authorityPlayers[playerIndex].flags & (1u << 2)) != 0;
        uiLabel(row, connected ? "ONLINE" : "OFFLINE",
                UiRect{180, 8, 70, 22}, &lv_font_montserrat_12,
                connected ? kGreen : kMuted, LV_TEXT_ALIGN_RIGHT);
        if (current) {
            lv_obj_t *dot = uiBox(row, UiRect{254, 16, 8, 8}, accent, accent, 4);
            if (dot != nullptr) lv_obj_set_style_border_width(dot, 0, 0);
        }
        makeClickable(
            row,
            static_cast<TouchAction>(
                static_cast<uint16_t>(TouchAction::TradeReceiverOption0) + candidate
            )
        );
        if (focused) animateFocusEntry(row);
    }
    const bool backFocused = state.tradeReceiverPickerIndex == count;
    lv_obj_t *backButton = uiBox(
        root, kReceiverPickerBack, backFocused ? 0x14242A : kPanel,
        backFocused ? kBlue : kLine, 6
    );
    if (backButton != nullptr) {
        lv_obj_t *backLabel = uiLabel(
            backButton, "BACK", UiRect{8, 8, 160, 24}, &ui_font_14,
            backFocused ? kBlue : kText
        );
        makeClickable(backButton, TouchAction::TradeReceiverCancel, backLabel, kBlue);
        if (backFocused) animateFocusEntry(backButton);
    }
}

void drawTradeAssetSelect(const AppState &state)
{
    drawOuterRing(kBlue);
    uint8_t selectedCount = 0;
    for (uint8_t assetIndex = 0; assetIndex < 32; ++assetIndex) {
        if ((state.tradeGiveAssetMask & (static_cast<uint32_t>(1u) << assetIndex)) != 0) {
            ++selectedCount;
        }
    }
    char subtitle[24];
    snprintf(subtitle, sizeof(subtitle), "%u SELECTED", selectedCount);
    drawHeader("GIVE ASSETS", subtitle);
    UiListItemView items[kSyncedAssetCapacity]{};
    char meta[kSyncedAssetCapacity][24]{};
    const uint8_t count = appTradeAssetCount(state);
    fillTradeAssetItems(state, items, meta);
    uiCenterListCreate(centerList, root, items, count, state.tradeAssetListIndex,
                       "BACK", true, appFocusIsFooter(state));
}

uint8_t tradeMaskCount(uint32_t mask)
{
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1u);
        mask >>= 1;
    }
    return count;
}

void drawTradeOffer(const AppState &state)
{
    drawOuterRing(kBlue);
    const bool waiting = appTradeOfferWaiting(state);
    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "REVISION %u",
             static_cast<unsigned>(state.tradeOffer.revision));
    drawHeader(waiting ? "OFFER SENT" : "TRADE RECEIVED", subtitle);

    const uint8_t counterpartyIndex = state.tradeOffer.counterpartyId == 0
        ? 0 : static_cast<uint8_t>(state.tradeOffer.counterpartyId - 1);
    char counterparty[48];
    snprintf(counterparty, sizeof(counterparty), "WITH  %.24s",
             appPlayerDisplayName(state, counterpartyIndex));
    uiLabel(root, counterparty, UiRect{90, 106, 300, 24},
            &lv_font_montserrat_14, kText);

    const UiRect receiveRect{82, 140, 146, 122};
    const UiRect giveRect{252, 140, 146, 122};
    lv_obj_t *receive = uiBox(root, receiveRect, 0x101D1B, kGreen, 7);
    lv_obj_t *give = uiBox(root, giveRect, 0x1D1515, kRed, 7);
    if (receive != nullptr) {
        char assets[28];
        char cash[28];
        snprintf(assets, sizeof(assets), "%u ASSETS",
                 tradeMaskCount(state.tradeOffer.counterpartyAssetMask));
        snprintf(cash, sizeof(cash), "+$%ld",
                 static_cast<long>(state.tradeOffer.counterpartyGivesCash));
        uiLabel(receive, "YOU RECEIVE", UiRect{8, 12, 130, 18},
                &lv_font_montserrat_10, kGreen);
        uiLabel(receive, assets, UiRect{8, 43, 130, 24},
                &lv_font_montserrat_14, kText);
        uiLabel(receive, cash, UiRect{8, 78, 130, 28},
                &lv_font_montserrat_20, kGreen);
    }
    if (give != nullptr) {
        char assets[28];
        char cash[28];
        snprintf(assets, sizeof(assets), "%u ASSETS",
                 tradeMaskCount(state.tradeOffer.selfAssetMask));
        snprintf(cash, sizeof(cash), "-$%ld",
                 static_cast<long>(state.tradeOffer.selfGivesCash));
        uiLabel(give, "YOU GIVE", UiRect{8, 12, 130, 18},
                &lv_font_montserrat_10, kRed);
        uiLabel(give, assets, UiRect{8, 43, 130, 24},
                &lv_font_montserrat_14, kText);
        uiLabel(give, cash, UiRect{8, 78, 130, 28},
                &lv_font_montserrat_20, kRed);
    }

    uiLabel(root, waiting ? "WAITING FOR RESPONSE" : "YOUR DECISION",
            UiRect{90, 272, 300, 20},
            &lv_font_montserrat_10, waiting ? kMuted : kYellow);

    const bool canCancel = state.tradeOffer.active &&
        (state.tradeOffer.flags & (1u << 3)) != 0;
    if (canCancel) {
        drawDecisionButton(UiRect{142, 302, 196, 48}, "CANCEL OFFER",
                           state.focus == 0, kRed, TouchAction::DetailPrimary);
    } else if (!waiting) {
        drawDecisionButton(UiRect{82, 300, 96, 46}, "ACCEPT",
                           state.focus == 0, kGreen, TouchAction::DetailPrimary);
        drawDecisionButton(UiRect{192, 300, 96, 46}, "COUNTER",
                           state.focus == 1, kBlue, TouchAction::DetailSecondary);
        drawDecisionButton(UiRect{302, 300, 96, 46}, "REJECT",
                           state.focus == 2, kRed, TouchAction::DetailTertiary);
    } else {
        uiLabel(root, "OFFER LOCKED AT THIS REVISION", UiRect{90, 312, 300, 22},
                &lv_font_montserrat_12, kMuted);
    }
    drawFooter("BACK", appFocusIsFooter(state));
}

void drawDemoLab(const AppState &state)
{
    drawOuterRing(kBlue);
    drawHeader("Demo Lab", "SCENARIOS");
    UiListItemView items[kDemoListCount]{};
    fillDemoItems(items);
    uiCenterListCreate(centerList, root, items, kDemoListCount, state.demoListIndex,
                       "返回", true, appFocusIsFooter(state));
}

void drawDebt(const AppState &state)
{
    drawOuterRing(kRed);
    drawHeader("INSUFFICIENT FUNDS", "DEBT RESOLUTION");
    drawMoneyMetric("AMOUNT DUE", state.debt.amountDue,
                    UiRect{68, 130, 168, 18}, UiRect{68, 150, 168, 34}, kRed);
    drawMoneyMetric("AVAILABLE CASH", state.money,
                    UiRect{244, 130, 168, 18}, UiRect{244, 150, 168, 34}, kText);
    char shortfall[40];
    snprintf(shortfall, sizeof(shortfall), "SHORTFALL  $%ld",
             static_cast<long>(appDebtShortfall(state)));
    uiLabel(root, shortfall, UiRect{90, 202, 300, 28}, &lv_font_montserrat_16, kRed);
    uiLabel(root, "MORTGAGE ASSETS TO COMPLETE PAYMENT", UiRect{70, 236, 340, 20},
            &lv_font_montserrat_10, kMuted);
    lv_obj_t *button = uiBox(root, kDetailPrimary,
                             state.focus == 0 ? 0x2B1718 : kPanel,
                             state.focus == 0 ? kRed : kLine, 6);
    if (button != nullptr) {
        uiLabel(button, "CHOOSE ASSETS",
                UiRect{8, 16, static_cast<int16_t>(kDetailPrimary.w - 16), 28},
                &lv_font_montserrat_16, state.focus == 0 ? kRed : kText);
        makeClickable(button, TouchAction::DetailPrimary);
        if (state.focus == 0) animateFocusEntry(button);
    }
    drawFooter("BACK", state.focus == 1);
}

lv_obj_t *drawDecisionButton(UiRect rect, const char *text, bool focused,
                             uint32_t accent, TouchAction action)
{
    lv_obj_t *button = uiBox(root, rect, focused ? 0x16302A : kPanel,
                             focused ? accent : kLine, 7);
    if (button == nullptr) return nullptr;
    lv_obj_t *buttonLabel = uiLabel(
        button, text, UiRect{8, 16, static_cast<int16_t>(rect.w - 16), 28},
        &ui_font_16, focused ? accent : kText
    );
    makeClickable(button, action, buttonLabel, accent);
    if (focused) animateFocusEntry(button);
    return button;
}

void updateDiceVisual(const AppState &state, uint32_t nowMs)
{
    if (diceObjects[0] == nullptr || diceObjects[1] == nullptr) return;
    uint32_t elapsed = state.rollFailed ? kDiceSettleMs : nowMs - state.rollStartedMs;
    if (!state.rollResolved && elapsed >= 1800u) elapsed %= 1800u;
    for (uint8_t index = 0; index < 2; ++index) {
        const uint8_t finalFace = index == 0 ? state.dieA : state.dieB;
        const DicePose pose = uiDicePose(elapsed, index, finalFace);
        lv_obj_set_pos(diceObjects[index], pose.x - 45, pose.y - 45);
        lv_obj_set_style_transform_angle(diceObjects[index], pose.angleTenths, 0);
        lv_obj_set_style_transform_zoom(diceObjects[index], pose.zoom, 0);
        char face[4];
        snprintf(face, sizeof(face), "%u", pose.face == 0 ? 1 : pose.face);
        lv_label_set_text(diceLabels[index], face);
    }
}

void updateExtraRollVisual(const AppState &state, uint32_t nowMs)
{
    if (diceObjects[0] == nullptr || diceObjects[1] == nullptr) return;
    const uint32_t elapsed = nowMs - state.extraRollRewardStartedMs;
    const uint16_t phase = static_cast<uint16_t>(elapsed % 600u);
    const uint16_t triangle = phase <= 300u ? phase : static_cast<uint16_t>(600u - phase);
    const int16_t bounce = static_cast<int16_t>(triangle * 12u / 300u);
    const uint16_t zoom = static_cast<uint16_t>(244u + triangle * 12u / 300u);
    for (uint8_t index = 0; index < 2; ++index) {
        lv_obj_set_pos(diceObjects[index], static_cast<lv_coord_t>(132 + index * 124),
                       static_cast<lv_coord_t>(150 - bounce));
        const int16_t direction = index == 0 ? 1 : -1;
        lv_obj_set_style_transform_angle(
            diceObjects[index], static_cast<int16_t>(direction * (elapsed % 1200u) * 900u / 1200u), 0);
        lv_obj_set_style_transform_zoom(diceObjects[index], zoom, 0);
    }
}

void drawDiceStage(const AppState &state, uint32_t nowMs)
{
    const bool resultVisible = appDiceResultVisible(state, nowMs);
    const bool doubles = resultVisible && state.dieA != 0 && state.dieA == state.dieB;
    drawOuterRing(kYellow);
    uiLabel(root, state.rollFailed ? "ROLL NOT ACCEPTED" :
                  (doubles ? "DOUBLES!" : (resultVisible ? "ROLL COMPLETE" : "ROLLING DICE")),
            UiRect{90, 68, 300, 32}, &lv_font_montserrat_16,
            state.rollFailed ? kRed : (resultVisible ? kGreen : kYellow));
    uiLabel(root, doubles ? "EXTRA ROLL EARNED" : "YOUR TURN",
            UiRect{130, 104, 220, 20}, &lv_font_montserrat_12,
            doubles ? kYellow : kMuted);
    for (uint8_t index = 0; index < 2; ++index) {
        diceObjects[index] = uiBox(root, UiRect{0, 0, 90, 90}, 0x202516, kYellow, 14);
        if (diceObjects[index] == nullptr) continue;
        lv_obj_set_style_border_width(diceObjects[index], 3, 0);
        lv_obj_set_style_transform_pivot_x(diceObjects[index], 45, 0);
        lv_obj_set_style_transform_pivot_y(diceObjects[index], 45, 0);
        diceLabels[index] = uiLabel(diceObjects[index], "1", UiRect{0, 20, 90, 48},
                                    &lv_font_montserrat_40, kText);
    }
    char result[40];
    if (state.rollFailed) {
        snprintf(result, sizeof(result), "PLEASE TRY AGAIN");
    } else if (doubles) {
        snprintf(result, sizeof(result), "%u + %u = %u",
                 state.dieA, state.dieB, state.rolledSteps);
    } else if (resultVisible) {
        snprintf(result, sizeof(result), "%u SPACES", state.rolledSteps);
    } else {
        snprintf(result, sizeof(result), "WAITING FOR RESULT");
    }
    uiLabel(root, result, UiRect{90, 310, 300, 38}, &lv_font_montserrat_24,
            state.rollFailed ? kRed : (resultVisible ? kGreen : kMuted));
    if (resultVisible && state.rollTarget != 0xFF) {
        char target[32];
        snprintf(target, sizeof(target), "TARGET TILE %u", state.rollTarget);
        uiLabel(root, target, UiRect{120, 354, 240, 24}, &lv_font_montserrat_14, kMuted);
    }
    updateDiceVisual(state, nowMs);
}

void drawExtraRollReward(const AppState &state, uint32_t nowMs)
{
    const bool doubleSixes = state.extraRollDieA == 6 && state.extraRollDieB == 6;
    drawOuterRing(kYellow);
    uiLabel(root, "DOUBLES BONUS", UiRect{120, 62, 240, 20},
            &lv_font_montserrat_12, kYellow);
    uiLabel(root, doubleSixes ? "DOUBLE SIXES" : "DOUBLES",
            UiRect{80, 88, 320, 34}, &lv_font_montserrat_24, kText);

    for (uint8_t index = 0; index < 2; ++index) {
        diceObjects[index] = uiBox(root, UiRect{0, 0, 92, 92}, 0x2B2618, kYellow, 15);
        if (diceObjects[index] == nullptr) continue;
        lv_obj_set_style_border_width(diceObjects[index], 3, 0);
        lv_obj_set_style_transform_pivot_x(diceObjects[index], 46, 0);
        lv_obj_set_style_transform_pivot_y(diceObjects[index], 46, 0);
        char face[4];
        snprintf(face, sizeof(face), "%u",
                 index == 0 ? state.extraRollDieA : state.extraRollDieB);
        diceLabels[index] = uiLabel(diceObjects[index], face, UiRect{0, 21, 92, 48},
                                    &lv_font_montserrat_40, kText);
    }

    uiLabel(root, "YOU ROLL AGAIN", UiRect{80, 258, 320, 34},
            &lv_font_montserrat_24, kGreen);
    char streak[36];
    snprintf(streak, sizeof(streak), "DOUBLE STREAK %u / 3", state.extraRollStreak);
    uiLabel(root, streak, UiRect{100, 300, 280, 22},
            &lv_font_montserrat_14, kYellow);
    uiLabel(root, state.extraRollStreak >= 2
                      ? "ONE MORE DOUBLE SENDS YOU TO HOLD"
                      : "BONUS ROLL READY",
            UiRect{80, 330, 320, 18}, &lv_font_montserrat_10,
            state.extraRollStreak >= 2 ? kRed : kMuted);
    drawDecisionButton(UiRect{150, 360, 180, 46}, "CONTINUE", true,
                       kYellow, TouchAction::DetailPrimary);
    updateExtraRollVisual(state, nowMs);
}

void drawMoveGuide(const AppState &state)
{
    const GridCityVisualDefinition &visual = visualOrFallback(
        appTileVisual(state, state.rollTarget)
    );
    drawOuterRing(visual.accent);
    drawHeader("ARRIVAL CHECK", "MOVE YOUR TOKEN");
    drawArtwork(UiRect{176, 94, 128, 128}, visual);
    char location[48];
    snprintf(location, sizeof(location), "TILE %u  /  %u SPACES",
             state.rollTarget, state.rolledSteps);
    uiLabel(root, location, UiRect{100, 224, 280, 18}, &lv_font_montserrat_10,
            visual.accent);
    uiLabel(root, appArrivalDisplayName(state), UiRect{70, 244, 340, 28},
            &lv_font_montserrat_20, kText);

    char ownership[64] = "SPECIAL BOARD SPACE";
    char nextEvent[80] = "NO REQUIRED ACTION";
    const ArrivalKind kind = appArrivalKind(state);
    if (kind == ArrivalKind::Asset) {
        const uint8_t assetIndex = appArrivalAssetIndex(state);
        const uint8_t ownerId = appArrivalOwnerId(state);
        if (ownerId == 0) {
            snprintf(ownership, sizeof(ownership), "UNOWNED PROPERTY");
            snprintf(nextEvent, sizeof(nextEvent), "NEXT: BUY OR AUCTION");
        } else if (ownerId == state.selfSeatId) {
            snprintf(ownership, sizeof(ownership), "YOUR PROPERTY");
            snprintf(nextEvent, sizeof(nextEvent), "NO RENT DUE");
        } else if (appAssetMortgaged(state, assetIndex)) {
            snprintf(ownership, sizeof(ownership), "OWNED BY %s",
                     appPlayerDisplayName(state, static_cast<uint8_t>(ownerId - 1)));
            snprintf(nextEvent, sizeof(nextEvent), "NO RENT: MORTGAGED");
        } else {
            snprintf(ownership, sizeof(ownership), "OWNED BY %s",
                     appPlayerDisplayName(state, static_cast<uint8_t>(ownerId - 1)));
            snprintf(nextEvent, sizeof(nextEvent), "NEXT: RENT PAYMENT");
        }
    } else if (kind == ArrivalKind::Fee) {
        snprintf(ownership, sizeof(ownership), "CITY BANK SPACE");
        snprintf(nextEvent, sizeof(nextEvent), "NEXT: PAY $%ld FEE",
                 static_cast<long>(appArrivalAmount(state)));
    } else if (kind == ArrivalKind::CityEvent) {
        snprintf(nextEvent, sizeof(nextEvent), "NEXT: DRAW CHANCE");
    } else if (kind == ArrivalKind::CivicFund) {
        snprintf(nextEvent, sizeof(nextEvent), "NEXT: DRAW COMMUNITY CHEST");
    } else if (kind == ArrivalKind::GoToHold) {
        snprintf(nextEvent, sizeof(nextEvent), "NEXT: MOVE TO HOLD");
    } else if (kind == ArrivalKind::Start) {
        snprintf(nextEvent, sizeof(nextEvent), "START AWARD APPLIES");
    }
    uiLabel(root, ownership, UiRect{80, 273, 320, 18}, &lv_font_montserrat_10, kMuted);
    uiLabel(root, nextEvent, UiRect{72, 294, 336, 20}, &lv_font_montserrat_12, kYellow);

    drawDecisionButton(UiRect{142, 320, 196, 58},
                       state.moveArrivalConfirmed ? "CONTINUE" : "I'M THERE",
                       state.focus == 0,
                       visual.accent, TouchAction::DetailPrimary);
    uiLabel(root, state.moveArrivalConfirmed ? "RFID ARRIVAL CONFIRMED" :
                                               "RFID AUTO-CHECK / MANUAL FALLBACK",
            UiRect{130, 382, 220, 18},
            &lv_font_montserrat_10, kMuted);
}

void updateCardVisual(const AppState &state, uint32_t nowMs)
{
    if (cardObject == nullptr || state.cardPresentation != CardPresentationPhase::Drawing) return;
    const uint32_t elapsed = nowMs - state.cardStartedMs;
    const uint16_t cycle = static_cast<uint16_t>(elapsed % 900u);
    const uint16_t sweep = cycle <= 450u ? cycle : static_cast<uint16_t>(900u - cycle);
    const int16_t offset = static_cast<int16_t>(static_cast<uint32_t>(sweep) * 62u / 450u);
    lv_obj_set_pos(cardObject, static_cast<lv_coord_t>(145 + offset),
                   static_cast<lv_coord_t>(112 - offset / 5));
    lv_obj_set_style_transform_angle(cardObject,
                                     static_cast<int16_t>(-55 + offset * 2), 0);
    lv_obj_set_style_transform_zoom(cardObject,
                                    static_cast<uint16_t>(238 + offset / 3), 0);
    lv_obj_set_style_opa(cardObject,
                         static_cast<lv_opa_t>(LV_OPA_70 + offset), 0);
}

void drawResultCardPanel(const char *eyebrow, const char *title, const char *action,
                         const char *outcome, const char *meta, uint32_t accent,
                         const lv_font_t *titleFont = &lv_font_montserrat_24)
{
    lv_obj_t *card = uiBox(root, UiRect{76, 108, 328, 220}, 0x11191B, accent, 9);
    if (card == nullptr) return;
    lv_obj_set_style_border_width(card, 4, 0);
    uiLabel(card, eyebrow, UiRect{20, 18, 288, 18},
            &lv_font_montserrat_10, accent);
    uiLabel(card, title, UiRect{18, 54, 292, 34}, titleFont, kText);
    uiLabel(card, action, UiRect{18, 104, 292, 24},
            &lv_font_montserrat_14, kMuted);
    uiLabel(card, outcome, UiRect{18, 142, 292, 30},
            &lv_font_montserrat_16, accent);
    uiLabel(card, meta, UiRect{18, 184, 292, 16},
            &lv_font_montserrat_10, kMuted);
}

void cardResultCopy(const AppState &state, const char *&title, const char *&action,
                    char *outcome, size_t outcomeSize)
{
    const long amount = static_cast<long>(state.cardAmount < 0
        ? -state.cardAmount : state.cardAmount);
    switch (state.cardIndex % 8u) {
        case 0:
            title = state.cardChance ? "CITY GRANT" : "COMMUNITY DIVIDEND";
            action = "COLLECT FROM CITY BANK";
            snprintf(outcome, outcomeSize, "+$%ld CASH", amount);
            return;
        case 1:
            title = state.cardChance ? "SERVICE CITATION" : "COMMUNITY DUES";
            action = "PAY THE CITY BANK";
            snprintf(outcome, outcomeSize, "$%ld PAYMENT REQUIRED", amount);
            return;
        case 2:
            title = "ADVANCE TO START";
            action = "MOVE TO GRID CENTRAL";
            snprintf(outcome, outcomeSize, "COLLECT $%ld", amount);
            return;
        case 3:
            title = "GO TO HOLD";
            action = "MOVE DIRECTLY TO HOLD";
            snprintf(outcome, outcomeSize, "YOUR TURN ENDS");
            return;
        case 4:
            title = "STEP BACK";
            action = "MOVE BACK 3 SPACES";
            snprintf(outcome, outcomeSize, "RESOLVE THE NEW TILE");
            return;
        case 5:
            title = state.cardChance ? "CITY REPAIR BILL" : "NEIGHBORHOOD REPAIR";
            action = "PAY THE CITY BANK";
            snprintf(outcome, outcomeSize, "$%ld PAYMENT REQUIRED", amount);
            return;
        case 6:
            title = state.cardChance ? "STARTUP WINDFALL" : "COMMUNITY REWARD";
            action = "COLLECT FROM CITY BANK";
            snprintf(outcome, outcomeSize, "+$%ld CASH", amount);
            return;
        default:
            if (state.cardChance) {
                title = "TRANSIT SURCHARGE";
                action = "PAY THE CITY BANK";
                snprintf(outcome, outcomeSize, "$%ld PAYMENT REQUIRED", amount);
            } else {
                title = "NEIGHBORHOOD BONUS";
                action = "COLLECT FROM CITY BANK";
                snprintf(outcome, outcomeSize, "+$%ld CASH", amount);
            }
            return;
    }
}

void drawCardReveal(const AppState &state, uint32_t nowMs)
{
    const uint32_t accent = state.cardChance ? kYellow : kGreen;
    const char *deckName = state.cardChance ? "CHANCE" : "COMMUNITY CHEST";
    drawOuterRing(accent);
    const char *stageTitle = state.cardPresentation == CardPresentationPhase::Drawing
        ? "DRAW A CARD"
        : (state.cardPresentation == CardPresentationPhase::Settling
               ? "APPLYING CARD" : "CARD RESULT");
    drawHeader(deckName, stageTitle);

    if (state.cardPresentation == CardPresentationPhase::Drawing) {
        uiBox(root, UiRect{133, 126, 190, 224}, 0x101719, kLine, 9);
        uiBox(root, UiRect{140, 119, 190, 224}, 0x141D1F, accent, 9);
        cardObject = uiBox(root, UiRect{145, 112, 190, 224}, kPanel, accent, 9);
        if (cardObject != nullptr) {
            lv_obj_set_style_border_width(cardObject, 4, 0);
            lv_obj_set_style_transform_pivot_x(cardObject, 95, 0);
            lv_obj_set_style_transform_pivot_y(cardObject, 112, 0);
            const GridCityVisualDefinition &visual = visualOrFallback(
                appTileVisual(state, state.rollTarget)
            );
            const lv_img_dsc_t *source = gridCityArtworkImage(visual);
            if (source != nullptr) {
                lv_obj_t *art = lv_img_create(cardObject);
                lv_img_set_src(art, source);
                lv_img_set_zoom(art, 246);
                lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 12);
            }
            uiLabel(cardObject, deckName, UiRect{10, 174, 170, 24},
                    &lv_font_montserrat_14, accent);
            uiLabel(cardObject, "?", UiRect{66, 132, 58, 42},
                    &lv_font_montserrat_32, kText);
        }
        uiLabel(root, state.cardResultValid ? "REVEALING CARD..." : "DRAWING CARD...",
                UiRect{100, 365, 280, 26}, &lv_font_montserrat_16, accent);
        updateCardVisual(state, nowMs);
        return;
    }

    const char *title = "CARD RESULT";
    const char *action = "FOLLOW THE INSTRUCTION";
    char outcome[48] = "CONTINUE TO RESOLVE";
    cardResultCopy(state, title, action, outcome, sizeof(outcome));
    char sequence[28];
    snprintf(sequence, sizeof(sequence), "CARD #%u", state.cardIndex + 1u);
    drawResultCardPanel(deckName, title, action, outcome, sequence, accent);
    if (state.cardPresentation == CardPresentationPhase::Settling) {
        lv_obj_t *processing = uiBox(root, UiRect{142, 344, 196, 54}, kPanel, kLine, 6);
        if (processing != nullptr) {
            uiLabel(processing, state.cardEffectApplied ? "RESOLVED" : "PROCESSING...",
                    UiRect{10, 14, 176, 24}, &lv_font_montserrat_14,
                    state.cardEffectApplied ? accent : kMuted);
        }
    } else {
        drawDecisionButton(UiRect{142, 344, 196, 54}, "CONTINUE", state.focus == 0,
                           accent, TouchAction::DetailPrimary);
    }
}

void drawTileEvent(const AppState &state)
{
    const GridCityVisualDefinition &visual = visualOrFallback(
        appTileVisual(state, state.rollTarget)
    );
    const bool rent = appArrivalKind(state) == ArrivalKind::Asset;
    const char *eventName = rent ? "RENT DUE" : "TAXES";
    drawOuterRing(visual.accent);
    drawHeader(eventName, "TILE EVENT");

    char action[64];
    char outcome[48];
    char meta[64];
    const long amount = static_cast<long>(state.debtAmount < 0
        ? -state.debtAmount : state.debtAmount);
    if (rent) {
        const char *owner = state.debtCreditorId == 0
            ? "ANOTHER PLAYER" : appPlayerNameById(state, state.debtCreditorId);
        snprintf(action, sizeof(action), "PROPERTY OWNED BY %.28s", owner);
        snprintf(outcome, sizeof(outcome), "$%ld RENT DUE", amount);
        snprintf(meta, sizeof(meta), "PAY %.24s  /  CASH $%ld", owner,
                 static_cast<long>(state.money));
    } else {
        snprintf(action, sizeof(action), "CITY TAX IS DUE ON ARRIVAL");
        snprintf(outcome, sizeof(outcome), "$%ld TAX DUE", amount);
        snprintf(meta, sizeof(meta), "PAY CITY BANK  /  CASH $%ld",
                 static_cast<long>(state.money));
    }
    drawResultCardPanel(rent ? "PROPERTY RENT" : "CITY TAX",
                        appArrivalDisplayName(state), action, outcome, meta,
                        visual.accent, &lv_font_montserrat_20);
    drawDecisionButton(UiRect{142, 344, 196, 54}, "CONTINUE", state.focus == 0,
                       visual.accent, TouchAction::DetailPrimary);
}

void drawPurchase(const AppState &state)
{
    const GridCityVisualDefinition &visual = visualOrFallback(
        appAssetVisual(state, state.tileAssetIndex)
    );
    drawOuterRing(visual.accent);
    drawHeader("PROPERTY AVAILABLE", "PURCHASE DECISION");
    const char *asset = appAssetDisplayName(state, state.tileAssetIndex);
    drawArtwork(UiRect{176, 94, 128, 128}, visual);
    uiLabel(root, asset, UiRect{80, 225, 320, 28}, &lv_font_montserrat_20, kText);
    drawMoneyMetric("PURCHASE PRICE", appAssetValue(state, state.tileAssetIndex),
                    UiRect{68, 258, 168, 16}, UiRect{68, 277, 168, 30}, kYellow);
    drawMoneyMetric("AVAILABLE CASH", state.money,
                    UiRect{244, 258, 168, 16}, UiRect{244, 277, 168, 30}, kGreen);
    drawDecisionButton(UiRect{88, 321, 144, 58}, "BUY", state.focus == 0,
                       kGreen, TouchAction::DetailPrimary);
    drawDecisionButton(UiRect{248, 321, 144, 58}, "AUCTION", state.focus == 1,
                       kYellow, TouchAction::DetailSecondary);
    uiLabel(root, "ROTATE TO CHOOSE / PRESS TO CONFIRM", UiRect{130, 382, 220, 18},
            &lv_font_montserrat_10, kMuted);
}

void drawAuction(const AppState &state)
{
    const bool result = state.auctionPresentation == AuctionPresentationPhase::Result;
    const uint8_t assetIndex = result ? state.auctionResultAssetIndex
                                      : state.auctionAssetIndex;
    const GridCityVisualDefinition &visual = visualOrFallback(
        appAssetVisual(state, assetIndex)
    );
    const bool sold = result && state.auctionWinnerPlayerId != 0;
    const bool selfWon = sold && state.auctionWinnerPlayerId == state.selfSeatId;
    const uint32_t stageAccent = selfWon ? kGreen : visual.accent;
    drawOuterRing(stageAccent);

    const bool intro = state.auctionPresentation == AuctionPresentationPhase::Intro;
    const bool opening = !result && (appAuctionOpening(state) ||
        state.auctionPresentation == AuctionPresentationPhase::OpeningWait);
    char openingProgress[24]{};
    if (opening) {
        uint8_t readyCount = 0;
        uint8_t requiredCount = 0;
        for (uint8_t bit = 0; bit < 6; ++bit) {
            const uint8_t mask = static_cast<uint8_t>(1u << bit);
            if ((state.auctionRequiredReadyMask & mask) != 0) ++requiredCount;
            if ((state.auctionReadyMask & state.auctionRequiredReadyMask & mask) != 0) {
                ++readyCount;
            }
        }
        snprintf(openingProgress, sizeof(openingProgress), "READY  %u / %u",
                 readyCount, requiredCount);
    }
    if (result) drawAuctionHeader("AUCTION CLOSED", "FINAL RESULT");
    else if (intro) drawAuctionHeader("AUCTION OPENING", "CITY AUCTION");
    else if (opening) drawAuctionHeader("WAITING FOR PLAYERS", openingProgress);
    else drawAuctionHeader("LIVE AUCTION", "CITY AUCTION");

    drawArtwork(UiRect{72, 116, 112, 112}, visual);
    auctionLabel(appAssetDisplayName(state, assetIndex), UiRect{198, 116, 210, 24},
                 &lv_font_montserrat_16, kText);
    auctionLabel("UP FOR AUCTION", UiRect{198, 144, 210, 16},
                 &lv_font_montserrat_10, stageAccent);
    const int32_t shownBid = result ? state.auctionResultAmount :
        (state.auctionCurrentBid == 0 ? state.auctionMinimumBid : state.auctionCurrentBid);
    drawMoneyMetric(result ? "FINAL BID" :
                    (state.auctionCurrentBid == 0 ? "STARTING BID" : "CURRENT BID"),
                    shownBid, UiRect{198, 166, 210, 16}, UiRect{198, 184, 210, 30},
                    result ? kText : kYellow);
    drawMoneyMetric("AVAILABLE CASH", state.money,
                    UiRect{198, 216, 210, 16}, UiRect{198, 234, 210, 30}, kGreen);
    uiBox(root, UiRect{72, 270, 336, 2}, kLine, kLine, 0);

    if (intro) {
        auctionLabel("ALL ACTIVE PLAYERS MAY BID", UiRect{90, 292, 300, 22},
                     &lv_font_montserrat_12, kText);
        auctionLabel("BIDDING OPENS SHORTLY", UiRect{100, 326, 280, 22},
                     &lv_font_montserrat_12, kYellow);
        return;
    }

    if (result) {
        char winner[40];
        if (!sold) snprintf(winner, sizeof(winner), "NO SALE");
        else if (selfWon) snprintf(winner, sizeof(winner), "YOU WON");
        else snprintf(winner, sizeof(winner), "%.20s WON",
                      appPlayerDisplayName(state, state.auctionWinnerPlayerId - 1));
        auctionLabel(winner, UiRect{70, 286, 340, 34}, &lv_font_montserrat_24,
                     sold ? stageAccent : kYellow);
        auctionLabel(sold ? "PROPERTY AWARDED" : "NO VALID BIDS",
                     UiRect{100, 322, 280, 22}, &lv_font_montserrat_12,
                     sold ? stageAccent : kMuted);
        auctionLabel("RETURNING TO GAME...", UiRect{100, 354, 280, 20},
                     &lv_font_montserrat_10, kMuted);
        return;
    }

    const bool canAct = !opening && !state.auctionPassed &&
                        state.decisionPlayerId == state.selfSeatId &&
                        (state.availableActions & kActionAuctionPass) != 0;
    if (!opening) {
        auctionLabel(canAct ? "YOUR DECISION" : "LIVE BIDDING",
                     UiRect{110, 278, 260, 18}, &lv_font_montserrat_10,
                     canAct ? kYellow : kGreen);
    }
    char leader[40];
    if (state.auctionHighestBidderId == 0) snprintf(leader, sizeof(leader), "NO LEADER YET");
    else snprintf(leader, sizeof(leader), "LEADER  %.24s",
                  appPlayerDisplayName(state, state.auctionHighestBidderId - 1));
    if (!opening) {
        uiLabel(root, leader, UiRect{90, 298, 300, 20}, &lv_font_montserrat_12, kText);
    }
    char nextBid[40];
    snprintf(nextBid, sizeof(nextBid), "YOU PAY IF BIDDING  $%ld",
             static_cast<long>(state.auctionMinimumBid));
    if (!opening) {
        uiLabel(root, nextBid, UiRect{90, 320, 300, 20}, &lv_font_montserrat_10, kYellow);
    }
    char bidder[40];
    if (opening) {
        snprintf(bidder, sizeof(bidder), "%s", openingProgress);
    } else {
        const uint8_t bidderId = state.auctionCurrentBidderId != 0
            ? state.auctionCurrentBidderId : state.decisionPlayerId;
        snprintf(bidder, sizeof(bidder), "%.24s TO ACT",
                 bidderId == 0 ? "WAITING" : appPlayerDisplayName(state, bidderId - 1));
    }
    if (!opening) {
        uiLabel(root, bidder, UiRect{100, 340, 280, 20},
                &lv_font_montserrat_12, canAct ? kYellow : kGreen);
    }
    if (canAct) {
        const bool canBid = (state.availableActions & kActionAuctionBid) != 0;
        drawDecisionButton(UiRect{80, 366, 152, 50}, canBid ? "BID" : "NO FUNDS",
                           canBid && state.focus == 0, canBid ? kYellow : kMuted,
                           TouchAction::DetailPrimary);
        drawDecisionButton(UiRect{248, 366, 152, 50}, "PASS", state.focus == 1,
                           kRed, TouchAction::DetailSecondary);
    } else {
        lv_obj_t *status = uiBox(root, UiRect{112, opening ? 326 : 366, 256, 50},
                                 0x101A18, kLine, 7);
        uiLabel(status, opening ? "BIDDING STARTS WHEN ALL READY" :
                        (state.auctionPassed ? "YOU PASSED - WATCHING" :
                                               "WAITING FOR NEXT BID"),
                UiRect{8, 12, 240, 26}, &lv_font_montserrat_12,
                opening ? kYellow : (state.auctionPassed ? kMuted : kGreen));
    }
}

void drawDebtAssets(const AppState &state)
{
    drawOuterRing(kRed);
    drawHeader("RAISE FUNDS", "DEBT RESOLUTION");
    char summary[80];
    snprintf(summary, sizeof(summary), "DUE $%ld   CASH $%ld   SELECTED +$%ld",
             static_cast<long>(state.debt.amountDue), static_cast<long>(state.money),
             static_cast<long>(appDebtSelectedProceeds(state)));
    uiLabel(root, summary, UiRect{70, 110, 340, 18}, &lv_font_montserrat_10, kRed);
    UiListItemView items[kSyncedAssetCapacity]{};
    char meta[kSyncedAssetCapacity][32]{};
    const uint8_t count = appVisibleAssetCount(state);
    for (uint8_t row = 0; row < count; ++row) {
        const uint8_t assetIndex = appVisibleAssetIndex(state, row);
        const uint8_t buildingLevel = appAssetBuildingLevel(state, assetIndex);
        const bool canSellBuilding = appDebtBuildingSaleEligible(state, assetIndex);
        const bool canMortgage = appDebtAssetEligible(state, assetIndex);
        if (buildingLevel != 0) {
            if (canSellBuilding) {
                snprintf(meta[row], sizeof(meta[row]), "SELL +$%ld",
                         static_cast<long>(appDebtBuildingSaleProceeds(state, assetIndex)));
            } else {
                snprintf(meta[row], sizeof(meta[row]), "SELL EVENLY");
            }
        } else if (canMortgage) {
            snprintf(meta[row], sizeof(meta[row]), "MORTGAGE +$%ld",
                     static_cast<long>(appAssetMortgageValue(state, assetIndex)));
        } else if (appAssetMortgaged(state, assetIndex)) {
            snprintf(meta[row], sizeof(meta[row]), "MORTGAGED");
        } else {
            snprintf(meta[row], sizeof(meta[row]), "NOT AVAILABLE");
        }
        const GridCityVisualDefinition &visual = visualOrFallback(
            appAssetVisual(state, assetIndex)
        );
        items[row] = UiListItemView{appAssetDisplayName(state, assetIndex), meta[row], "",
                                   canSellBuilding || canMortgage,
                                   appDebtAssetSelected(state, assetIndex),
                                   visual.accent, visual.groupCode, true};
    }
    uiCenterListCreate(centerList, root, items, count, state.debtListIndex,
                       appDebtCanConfirm(state) ? "MORTGAGE" : "SELECT MORTGAGES",
                       appDebtCanConfirm(state), appFocusIsFooter(state));
}

void drawBankruptcy(const AppState &state)
{
    drawOuterRing(kRed);
    drawHeader("INSOLVENT", "DEBT RESOLUTION");
    uiLabel(root, "!", UiRect{180, 130, 120, 90}, &lv_font_montserrat_40, kRed);
    uiLabel(root, "ASSETS CANNOT COVER THE DEBT", UiRect{70, 230, 340, 28},
            &lv_font_montserrat_14, kText);
    drawDecisionButton(kDetailPrimary, "DECLARE BANKRUPTCY", state.focus == 0,
                       kRed, TouchAction::DetailPrimary);
}

uint8_t recipeIndex(uint8_t value, uint8_t count)
{
    return value == 0 || value > count ? 0 : static_cast<uint8_t>(value - 1);
}

const char *identitySeatName(const AppState &state, uint8_t index,
                             char *fallback, size_t fallbackSize)
{
    if (index >= state.identity.seatCount) return "";
    const TransportIdentitySeat &seat = state.identity.seats[index];
    if (seat.name[0] != '\0') return seat.name;
    const char *rosterName = appPlayerDisplayName(state, index);
    if (rosterName != nullptr && rosterName[0] != '\0' &&
        strncmp(rosterName, "PLAYER ", 7) != 0) return rosterName;
    const bool human = (state.identity.humanMask & (1u << index)) != 0;
    snprintf(fallback, fallbackSize, human ? "Player %u" : "Bot %u",
             static_cast<unsigned>(seat.playerId == 0 ? index + 1 : seat.playerId));
    return fallback;
}

const TransportIdentitySeat *identitySeatForPlayer(const AppState &state,
                                                   uint8_t playerId,
                                                   uint8_t &seatIndex)
{
    seatIndex = 0xFF;
    if (playerId == 0) return nullptr;
    const uint8_t count = state.identity.seatCount > 6 ? 6 : state.identity.seatCount;
    for (uint8_t index = 0; index < count; ++index) {
        if (state.identity.seats[index].playerId == playerId) {
            seatIndex = index;
            return &state.identity.seats[index];
        }
    }
    return nullptr;
}

bool identitySeatHasFinalAvatar(const AppState &state,
                                const TransportIdentitySeat *seat,
                                uint8_t seatIndex)
{
    if (seat == nullptr || seatIndex >= 6) return false;
    const uint8_t playerId = seat->playerId == 0
        ? static_cast<uint8_t>(seatIndex + 1) : seat->playerId;
    if (playerId == 0 || playerId > 6) return false;
    const uint8_t playerBit = static_cast<uint8_t>(1u << (playerId - 1));
    return (state.identity.avatarReadyMask & playerBit) != 0 ||
           (seat->avatarRevision != 0 && seat->avatarCacheTag != 0);
}

void drawFinalAvatar(lv_obj_t *parent, const AppState &state,
                     const TransportIdentitySeat *seat, uint8_t seatIndex,
                     UiRect rect, uint32_t accent)
{
    if (parent == nullptr || rect.w <= 0 || rect.h <= 0) return;
    lv_obj_t *viewport = uiBox(parent, rect, 0x061017, accent,
                               static_cast<int16_t>(rect.w / 2));
    if (viewport == nullptr) return;
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_clip_corner(viewport, true, 0);
    // Keep the clip viewport borderless. LVGL positions children from the
    // content box, so an inset border shifts an otherwise centered image by
    // the border width and leaves an exposed crescent at the top/left.
    lv_obj_set_style_border_width(viewport, 0, 0);

    const bool finalExpected = identitySeatHasFinalAvatar(state, seat, seatIndex);
    const lv_img_dsc_t *avatar = nullptr;
    if (finalExpected && seat != nullptr) {
        avatar = remoteAvatarFinal(state.authorityRoomId, seat->playerId,
                                   seat->avatarRevision, seat->avatarCacheTag);
    }
    if (avatar != nullptr) {
        lv_obj_t *image = lv_img_create(viewport);
        if (image == nullptr) return;
        lv_img_set_src(image, avatar);
        lv_img_set_antialias(image, true);
        lv_img_set_pivot(image, 64, 64);
        const int16_t target = rect.w < rect.h ? rect.w : rect.h;
        lv_img_set_zoom(image, static_cast<uint16_t>((target * 256) / 128));
        lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    if (finalExpected) {
        const int16_t size = rect.w < 38 ? static_cast<int16_t>(rect.w - 8) : 30;
        lv_obj_t *spinner = lv_spinner_create(viewport, 760, 88);
        if (spinner == nullptr) return;
        lv_obj_set_size(spinner, size, size);
        lv_obj_set_pos(spinner, static_cast<int16_t>((rect.w - size) / 2),
                       static_cast<int16_t>((rect.h - size) / 2));
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kLine), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(accent), LV_PART_INDICATOR);
        return;
    }

    char token[8];
    const uint8_t playerId = seat != nullptr && seat->playerId != 0
        ? seat->playerId
        : (seatIndex < 6 ? static_cast<uint8_t>(seatIndex + 1) : 0);
    if (playerId == 0) snprintf(token, sizeof(token), "--");
    else snprintf(token, sizeof(token), "P%u", static_cast<unsigned>(playerId));
    uiLabel(viewport, token,
            UiRect{0, static_cast<int16_t>(rect.h / 2 - 12), rect.w, 24},
            &lv_font_montserrat_14, accent);
}

void drawAvatarSilhouette(const TransportAvatarRecipe &recipe, bool loading)
{
    // The portrait owns a clipped right-side viewport. Wide hair/outfit layers
    // can never paint over the Focus Stack controls on the left.
    lv_obj_t *viewport = lv_obj_create(root);
    lv_obj_remove_style_all(viewport);
    lv_obj_set_pos(viewport, 220, 82);
    lv_obj_set_size(viewport, 224, 270);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);

    const lv_img_dsc_t *preview = remoteAvatarPreview(recipe);
    if (preview == nullptr) {
        lv_obj_t *spinner = lv_spinner_create(viewport, 760, 88);
        lv_obj_set_size(spinner, 54, 54);
        lv_obj_set_pos(spinner, 85, 72);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kLine), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(kGreen), LV_PART_INDICATOR);
        uiLabel(viewport, loading ? "SAVING AVATAR" : "LOADING PREVIEW",
                UiRect{16, 140, 192, 22},
                &lv_font_montserrat_10, kMuted);
        return;
    }
    lv_obj_t *image = lv_img_create(viewport);
    lv_img_set_src(image, preview);
    lv_img_set_antialias(image, true);
    lv_img_set_zoom(image, 230);
    // The source canvas contains transparent side/top margins. Offset the
    // canvas inside the clipped viewport so the visible bust moves left/up.
    lv_obj_set_pos(image, -6, -12);
}

void updateAvatarPreloadVisual(const AppState &state)
{
    const uint8_t total = state.identity.avatarPreloadTotalCount == 0
        ? 30 : state.identity.avatarPreloadTotalCount;
    const uint8_t ready = state.identity.avatarPreloadReadyCount > total
        ? total : state.identity.avatarPreloadReadyCount;
    if (avatarPreloadArc != nullptr) {
        lv_arc_set_range(avatarPreloadArc, 0, total);
        lv_arc_set_value(avatarPreloadArc, ready);
    }
    if (avatarPreloadBar != nullptr) {
        lv_bar_set_range(avatarPreloadBar, 0, total);
        // This function runs every UI tick. Restarting an LVGL animation for
        // an unchanged value forces continuous partial framebuffer refreshes.
        lv_bar_set_value(avatarPreloadBar, ready, LV_ANIM_OFF);
    }
    if (avatarPreloadLabel != nullptr) {
        char text[32];
        snprintf(text, sizeof(text), "CACHING  %u / %u", ready, total);
        lv_label_set_text(avatarPreloadLabel, text);
    }
}

void drawAvatarLoading(const AppState &state)
{
    drawOuterRing(kGreen);
    drawHeader("PREPARING AVATAR", "ONE-TIME SETUP");
    avatarPreloadArc = lv_arc_create(root);
    lv_obj_set_size(avatarPreloadArc, 70, 70);
    lv_obj_set_pos(avatarPreloadArc, 205, 132);
    lv_arc_set_bg_angles(avatarPreloadArc, 0, 360);
    lv_arc_set_rotation(avatarPreloadArc, 270);
    lv_obj_set_style_arc_width(avatarPreloadArc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(avatarPreloadArc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(avatarPreloadArc, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_arc_color(avatarPreloadArc, lv_color_hex(kGreen),
                               LV_PART_INDICATOR);
    lv_obj_remove_style(avatarPreloadArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(avatarPreloadArc, LV_OBJ_FLAG_CLICKABLE);
    uiLabel(root, "DOWNLOADING STYLE LIBRARY", UiRect{90, 220, 300, 24},
            &lv_font_montserrat_14, kText);
    avatarPreloadBar = lv_bar_create(root);
    lv_obj_set_pos(avatarPreloadBar, 100, 264);
    lv_obj_set_size(avatarPreloadBar, 280, 14);
    lv_obj_set_style_radius(avatarPreloadBar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(avatarPreloadBar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(avatarPreloadBar, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_bg_color(avatarPreloadBar, lv_color_hex(kGreen), LV_PART_INDICATOR);
    avatarPreloadLabel = uiLabel(root, "", UiRect{120, 292, 240, 24},
                                 &lv_font_montserrat_12, kGreen);
    uiLabel(root, "KEEP THIS CONSOLE POWERED ON", UiRect{100, 330, 280, 18},
            &lv_font_montserrat_10, kMuted);
    updateAvatarPreloadVisual(state);
}

void drawIdentityRow(const AppState &state, uint8_t row, int16_t x, int16_t y,
                     int16_t width, const char *eyebrow, const char *value,
                     uint32_t swatch = 0)
{
    const bool focused = state.focus == row;
    const bool editing = focused && state.identity.editingValue;
    // Browse and edit share one Focus Stack style. Edit mode changes only the
    // compact affordance, avoiding a geometry/color jump after the press.
    const uint32_t accent = kGreen;
    lv_obj_t *item = uiBox(root, UiRect{x, y, width, 38},
                           focused ? 0x16302A : 0x061017,
                           focused ? accent : 0x061017, focused ? 6 : 0);
    if (item == nullptr) return;
    char rowNumber[4];
    snprintf(rowNumber, sizeof(rowNumber), "%02u", static_cast<unsigned>(row + 1));
    lv_obj_t *numberLabel = uiLabel(
        item, rowNumber, UiRect{5, 11, 18, 16}, &lv_font_montserrat_8,
        focused ? accent : kMuted, LV_TEXT_ALIGN_LEFT
    );
    lv_obj_t *eyebrowLabel = uiLabel(
        item, eyebrow, UiRect{25, 3, static_cast<int16_t>(width - 42), 12},
        &lv_font_montserrat_8, focused ? accent : kMuted, LV_TEXT_ALIGN_LEFT
    );
    int16_t valueX = 25;
    int16_t valueWidth = static_cast<int16_t>(width - 43);
    if (swatch != 0) {
        uiBox(item, UiRect{25, 18, 10, 10}, swatch, swatch, 2);
        valueX = 41;
        valueWidth = static_cast<int16_t>(width - 59);
    }
    lv_obj_t *valueLabel = uiLabel(item, value, UiRect{valueX, 15, valueWidth, 18},
                                   &lv_font_montserrat_10,
                                   focused ? accent : kText, LV_TEXT_ALIGN_LEFT);
    if (valueLabel != nullptr) lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
    lv_obj_t *affordanceLabel = uiLabel(
        item, editing ? "<>" : ">",
        UiRect{static_cast<int16_t>(width - 25), 12, 20, 18},
        &lv_font_montserrat_10, focused ? accent : kMuted
    );
    lv_obj_t *divider = uiBox(
        item, UiRect{20, 37, static_cast<int16_t>(width - 24), 1}, kLine, kLine, 0
    );
    if (divider != nullptr && focused) {
        lv_obj_set_style_bg_opa(divider, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(divider, LV_OPA_TRANSP, 0);
    }
    avatarRowBindings[row] = AvatarRowBinding{
        item, numberLabel, eyebrowLabel, valueLabel, affordanceLabel, divider
    };
    makeClickable(item, static_cast<TouchAction>(
        static_cast<uint16_t>(TouchAction::IdentityRow0) + row), valueLabel, accent);
    if (focused) animateFocusEntry(item);
}

void drawAvatarSetup(const AppState &state)
{
    drawOuterRing(kGreen);
    drawHeader("AVATAR SETUP", "PLAYER / JOINED");
    const TransportAvatarRecipe &recipe = state.identity.draftRecipe;
    const uint8_t hair = recipeIndex(recipe.hairPresetId, 10);
    const uint8_t hairColor = recipeIndex(recipe.hairColorId, 20);
    const uint8_t face = recipeIndex(recipe.facePresetId, 10);
    const uint8_t skin = recipeIndex(recipe.skinToneId, 8);
    const uint8_t outfit = recipeIndex(recipe.outfitPresetId, 10);
    constexpr int16_t rowY[5] = {108, 154, 200, 246, 292};
    // Concave left edge follows the circular safe area; every row terminates at
    // x=220, establishing a stable split from the clipped portrait viewport.
    constexpr int16_t rowX[5] = {70, 58, 52, 58, 70};
    constexpr int16_t rowW[5] = {150, 162, 168, 162, 150};
    drawIdentityRow(state, 0, rowX[0], rowY[0], rowW[0], "HAIR", kHairNames[hair]);
    drawIdentityRow(state, 1, rowX[1], rowY[1], rowW[1], "HAIR COLOR",
                    kHairColorNames[hairColor], kHairColorSwatches[hairColor]);
    drawIdentityRow(state, 2, rowX[2], rowY[2], rowW[2], "FACE", kFaceNames[face]);
    drawIdentityRow(state, 3, rowX[3], rowY[3], rowW[3], "SKIN TONE",
                    kSkinNames[skin], kSkinSwatches[skin]);
    drawIdentityRow(state, 4, rowX[4], rowY[4], rowW[4], "OUTFIT", kOutfitNames[outfit]);

    const bool submitting = state.identity.phase == IdentityClientPhase::AvatarSubmitting;
    drawAvatarSilhouette(recipe, submitting);
    const bool focused = state.focus == static_cast<uint8_t>(AvatarEditField::Confirm);
    lv_obj_t *confirm = uiBox(root, UiRect{74, 342, 146, 46},
                              focused ? 0x16302A : kPanel,
                              focused ? kGreen : kLine, 6);
    lv_obj_t *confirmLabel = uiLabel(confirm, submitting ? "SAVING..." : "CONFIRM",
                                     UiRect{6, 12, 138, 22}, &lv_font_montserrat_12,
                                     focused ? kGreen : kText);
    makeClickable(confirm, TouchAction::IdentityConfirm, confirmLabel, kGreen);
    if (focused) animateFocusEntry(confirm);
}

void drawNameReview(const AppState &state)
{
    drawOuterRing(kGreen);
    uiLabel(root, state.identity.draftName[0] == '\0' ? "YOUR NAME" : state.identity.draftName,
            UiRect{80, 118, 320, 54}, &lv_font_montserrat_24,
            state.identity.draftName[0] == '\0' ? kMuted : kText);
    uiLabel(root, "PRESS THE NAME TO WRITE", UiRect{110, 180, 260, 20},
            &lv_font_montserrat_10, kMuted);

    constexpr UiRect buttons[3] = {
        UiRect{120, 226, 240, 48}, UiRect{120, 282, 240, 48}, UiRect{152, 344, 176, 46},
    };
    constexpr const char *texts[3] = {"EDIT NAME", "CONFIRM NAME", "BACK"};
    constexpr TouchAction actions[3] = {
        TouchAction::NameEdit, TouchAction::NameConfirm, TouchAction::NameBack,
    };
    for (uint8_t index = 0; index < 3; ++index) {
        const bool focused = state.focus == index;
        const uint32_t accent = index == 1 ? kGreen : kBlue;
        lv_obj_t *button = uiBox(root, buttons[index], focused ? 0x16302A : kPanel,
                                 focused ? accent : kLine, 6);
        lv_obj_t *buttonLabel = uiLabel(button, texts[index],
                                        UiRect{8, 13, static_cast<int16_t>(buttons[index].w - 16), 22},
                                        &lv_font_montserrat_12,
                                        focused ? accent : kText);
        makeClickable(button, actions[index], buttonLabel, accent);
        if (focused) animateFocusEntry(button);
    }
}

void drawNameHandwriting(const AppState &state)
{
    drawOuterRing(kBlue);
    uiLabel(root, state.identity.draftName[0] == '\0' ? "_" : state.identity.draftName,
            UiRect{78, 64, 324, 42}, &lv_font_montserrat_24, kText);
    lv_obj_t *canvas = uiHandwritingCreate(root, UiRect{78, 112, 324, 220},
                                           0x0D1518, kBlue, kBlue);
    if (canvas != nullptr) {
        lv_obj_set_style_border_width(canvas, 2, 0);
        uiLabel(canvas, "DRAW ONE LETTER", UiRect{44, 88, 236, 22},
                &lv_font_montserrat_12, kMuted);
        uiLabel(canvas, "HANDWRITING AREA", UiRect{54, 116, 216, 18},
                &lv_font_montserrat_10, kLine);
    }
    constexpr UiRect buttons[2] = {UiRect{96, 350, 136, 46}, UiRect{248, 350, 136, 46}};
    constexpr const char *texts[2] = {"DELETE", "DONE"};
    constexpr TouchAction actions[2] = {TouchAction::NameDelete,
                                        TouchAction::HandwritingConfirm};
    for (uint8_t index = 0; index < 2; ++index) {
        const bool focused = state.focus == index;
        const uint32_t accent = index == 0 ? kRed : kGreen;
        lv_obj_t *button = uiBox(root, buttons[index], focused ? 0x16302A : kPanel,
                                 focused ? accent : kLine, 6);
        lv_obj_t *buttonLabel = uiLabel(button, texts[index], UiRect{8, 13, 120, 22},
                                        &lv_font_montserrat_12,
                                        focused ? accent : kText);
        makeClickable(button, actions[index], buttonLabel, accent);
        if (focused) animateFocusEntry(button);
    }
}

void updateIdentityCountdown(const AppState &state, uint32_t nowMs)
{
    if (identityCountdownLabel == nullptr) return;
    const uint32_t remaining = appIdentityCountdownRemainingMs(state, nowMs);
    char text[28];
    if (state.identity.phase == IdentityClientPhase::Countdown) {
        const uint32_t seconds = (remaining + 999u) / 1000u;
        snprintf(text, sizeof(text), "GAME STARTS IN %lu", static_cast<unsigned long>(seconds));
    } else {
        snprintf(text, sizeof(text), "WAITING FOR PLAYERS");
    }
    lv_label_set_text(identityCountdownLabel, text);
}

void drawPlayerReady(const AppState &state, uint32_t nowMs)
{
    drawOuterRing(kGreen);
    drawHeader("PLAYERS READY", "ROOM SETUP");
    const uint8_t count = state.identity.seatCount > 6 ? 6 : state.identity.seatCount;
    const uint8_t columns = count > 4 ? 3 : 2;
    const int16_t cardWidth = columns == 3 ? 86 : 120;
    const int16_t cardHeight = columns == 3 ? 102 : 108;
    const int16_t gapX = columns == 3 ? 8 : 16;
    const int16_t gapY = columns == 3 ? 12 : 10;
    const int16_t firstY = 108;
    for (uint8_t index = 0; index < count; ++index) {
        const uint8_t rowIndex = static_cast<uint8_t>(index / columns);
        const uint8_t columnIndex = static_cast<uint8_t>(index % columns);
        const uint8_t rowStart = static_cast<uint8_t>(rowIndex * columns);
        const uint8_t rowCount = static_cast<uint8_t>(
            (count - rowStart) < columns ? (count - rowStart) : columns
        );
        const int16_t rowWidth = static_cast<int16_t>(
            rowCount * cardWidth + (rowCount - 1) * gapX
        );
        const int16_t x = static_cast<int16_t>(
            (480 - rowWidth) / 2 + columnIndex * (cardWidth + gapX)
        );
        const int16_t y = static_cast<int16_t>(
            firstY + rowIndex * (cardHeight + gapY)
        );
        const TransportIdentitySeat &seat = state.identity.seats[index];
        const uint8_t playerId = seat.playerId == 0
            ? static_cast<uint8_t>(index + 1) : seat.playerId;
        const uint8_t playerBit = static_cast<uint8_t>(1u << (playerId - 1));
        const bool ready = (state.identity.readyMask & playerBit) != 0;
        const bool online = (state.identity.onlineMask & playerBit) != 0;
        const uint32_t accent = ready ? kGreen : (online ? kYellow : kMuted);
        lv_obj_t *card = uiBox(root, UiRect{x, y, cardWidth, cardHeight},
                               0x0D1518, ready ? kLine : accent, 6);
        if (card == nullptr) continue;
        const int16_t avatarSize = columns == 3 ? 52 : 64;
        drawFinalAvatar(card, state, &seat, index,
                        UiRect{static_cast<int16_t>((cardWidth - avatarSize) / 2), 5,
                               avatarSize, avatarSize}, accent);
        char fallback[20];
        const char *name = identitySeatName(state, index, fallback, sizeof(fallback));
        const int16_t nameY = static_cast<int16_t>(avatarSize + 10);
        lv_obj_t *nameLabel = uiLabel(card, name, UiRect{5, nameY,
                                                        static_cast<int16_t>(cardWidth - 10), 18},
                                      &lv_font_montserrat_10, kText);
        if (nameLabel != nullptr) lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
        uiLabel(card, ready ? "READY" : (online ? "SETTING UP" : "OFFLINE"),
                UiRect{4, static_cast<int16_t>(nameY + 18),
                       static_cast<int16_t>(cardWidth - 8), 14},
                &lv_font_montserrat_8, accent);
    }
    char progress[24];
    snprintf(progress, sizeof(progress), "%u / %u READY",
             appIdentityReadyCount(state), count);
    uiLabel(root, progress, UiRect{140, 342, 200, 20}, &lv_font_montserrat_10, kMuted);
    identityCountdownLabel = uiLabel(root, "", UiRect{100, 366, 280, 28},
                                     &lv_font_montserrat_14, kGreen);
    updateIdentityCountdown(state, nowMs);
}

void applyModal(const AppState &state, uint32_t nowMs, bool create)
{
    if (state.modal.kind == ModalKind::None) return;
    const char *amountPrefix = "AMOUNT";
    switch (state.modal.kind) {
        case ModalKind::CollectRent: amountPrefix = "RENT DUE"; break;
        case ModalKind::ForcedPayment:
        case ModalKind::VoluntaryUnmortgage: amountPrefix = "PAY"; break;
        case ModalKind::VoluntaryMortgage: amountPrefix = "RECEIVE"; break;
        case ModalKind::TradeCreate: amountPrefix = "TRANSFER"; break;
        case ModalKind::TradeAction: amountPrefix = "YOU GIVE"; break;
        case ModalKind::DebtMortgageConfirm: amountPrefix = "RAISE"; break;
        case ModalKind::DebtSellBuildingConfirm: amountPrefix = "RAISE"; break;
        case ModalKind::None: break;
    }
    char amount[40];
    snprintf(amount, sizeof(amount), "%s  $%ld", amountPrefix,
             static_cast<long>(state.modal.amount));
    char detail[96];
    snprintf(detail, sizeof(detail), "%s / %s", state.modal.counterparty,
             state.modal.purpose);
    char cash[40];
    snprintf(cash, sizeof(cash), "AVAILABLE CASH  $%ld", static_cast<long>(state.money));
    char countdown[32] = "";
    if (state.modal.deadlineMs != 0) {
        snprintf(countdown, sizeof(countdown), "%.1fs remaining",
                 appModalRemainingMs(state, nowMs) / 1000.0f);
    }
    const char *confirmText = state.modal.submitting
        ? "SUBMITTING..."
        : (state.modal.insufficient || state.modal.focus == ModalFocus::ResolveAssets
               ? "RESOLVE ASSETS"
               : "HOLD 1.2S TO CONFIRM");
    const bool confirmFocused = state.modal.focus == ModalFocus::Confirm ||
                                state.modal.focus == ModalFocus::ResolveAssets;
    const bool showRecipient = state.modal.kind == ModalKind::ForcedPayment;
    const char *recipientName = state.modal.counterparty == nullptr ||
                                state.modal.counterparty[0] == '\0'
        ? "CITY BANK" : state.modal.counterparty;
    if (showRecipient) {
        if (state.debtCreditorId == 0) {
            recipientName = "CITY BANK";
        } else {
            const char *const rosterName = appPlayerNameById(state, state.debtCreditorId);
            if (rosterName != nullptr && rosterName[0] != '\0' &&
                strcmp(rosterName, "PLAYER") != 0) {
                recipientName = rosterName;
            }
        }
    }
    const bool recipientIsSystem = strcmp(recipientName, "CITY BANK") == 0 ||
                                   strcmp(recipientName, "City Bank") == 0;
    constexpr uint32_t recipientColors[6] = {
        0x58A7EB, 0xEF7168, 0x52DCB7, 0xF2C453, 0xC28AE8, 0xEA8A55,
    };
    uint32_t recipientAccent = kYellow;
    if (!recipientIsSystem) {
        uint8_t colorIndex = state.debtCreditorId > 0 && state.debtCreditorId <= 6
            ? static_cast<uint8_t>(state.debtCreditorId - 1) : 0;
        if (state.debtCreditorId == 0) {
            uint8_t hash = 0;
            for (const char *cursor = recipientName; *cursor != '\0'; ++cursor) {
                hash = static_cast<uint8_t>(hash * 33u + static_cast<uint8_t>(*cursor));
            }
            colorIndex = static_cast<uint8_t>(hash % 6u);
        }
        recipientAccent = recipientColors[colorIndex];
    }
    char recipientToken[4] = "$";
    if (!recipientIsSystem) {
        uint8_t tokenLength = 0;
        bool takeNext = true;
        for (const char *cursor = recipientName; *cursor != '\0' && tokenLength < 2; ++cursor) {
            const char character = *cursor;
            const bool alphaNumeric = (character >= 'A' && character <= 'Z') ||
                                      (character >= 'a' && character <= 'z') ||
                                      (character >= '0' && character <= '9');
            if (takeNext && alphaNumeric) {
                recipientToken[tokenLength++] = character >= 'a' && character <= 'z'
                    ? static_cast<char>(character - ('a' - 'A')) : character;
                takeNext = false;
            }
            if (character == ' ' || character == '-' || character == '_') takeNext = true;
        }
        if (tokenLength == 0) recipientToken[tokenLength++] = 'P';
        recipientToken[tokenLength] = '\0';
    }
    char recipientMeta[80];
    snprintf(recipientMeta, sizeof(recipientMeta), "%s  |  CASH $%ld",
             state.modal.purpose, static_cast<long>(state.money));
    const UiModalView view{
        state.modal.title,
        amount,
        showRecipient ? recipientMeta : detail,
        showRecipient ? "" : cash,
        countdown,
        confirmText,
        appHoldProgressPermille(state, nowMs),
        state.modal.cancelAllowed && !state.modal.submitting,
        confirmFocused,
        state.modal.focus == ModalFocus::Cancel,
        state.modal.submitting,
        state.modal.insufficient,
        "PAY TO",
        recipientName,
        recipientToken,
        recipientAccent,
        showRecipient,
    };
    if (create) uiModalCreate(activeModal, root, view);
    else uiModalUpdate(activeModal, view);
}

void drawModal(const AppState &state, uint32_t nowMs)
{
    applyModal(state, nowMs, true);
}

void rebuild(const AppState &state, uint32_t nowMs)
{
    tapBindingCount = 0;
    memset(avatarRowBindings, 0, sizeof(avatarRowBindings));
    uiCarouselDestroy(homeCarousel);
    uiCenterListDestroy(centerList);
    uiModalDestroy(activeModal);
    lv_obj_clean(root);
    lv_obj_set_style_bg_color(
        root, lv_color_hex((state.page == ScreenPage::AvatarLoading ||
                            state.page == ScreenPage::AvatarSetup)
                               ? 0x061017 : kBg), 0);
    outerRing = nullptr;
    turnReminderLabel = nullptr;
    diceObjects[0] = nullptr;
    diceObjects[1] = nullptr;
    diceLabels[0] = nullptr;
    diceLabels[1] = nullptr;
    cardObject = nullptr;
    identityCountdownLabel = nullptr;
    avatarPreloadArc = nullptr;
    avatarPreloadBar = nullptr;
    avatarPreloadLabel = nullptr;
    switch (state.page) {
        case ScreenPage::Home: drawHome(state, nowMs); break;
        case ScreenPage::Assets: drawAssets(state); break;
        case ScreenPage::AssetDetail: drawAssetDetail(state); break;
        case ScreenPage::Players: drawPlayers(state); break;
        case ScreenPage::PlayerDetail: drawPlayerDetail(state); break;
        case ScreenPage::PlayerAssets: drawPlayerAssets(state); break;
        case ScreenPage::PlayerFinance: drawPlayerFinance(state); break;
        case ScreenPage::Activity: drawActivity(state); break;
        case ScreenPage::Trade: drawTrade(state); break;
        case ScreenPage::TradeAssetSelect: drawTradeAssetSelect(state); break;
        case ScreenPage::TradeOffer: drawTradeOffer(state); break;
        case ScreenPage::DemoLab: drawDemoLab(state); break;
        case ScreenPage::Debt: drawDebt(state); break;
        case ScreenPage::DiceStage: drawDiceStage(state, nowMs); break;
        case ScreenPage::ExtraRollReward: drawExtraRollReward(state, nowMs); break;
        case ScreenPage::MoveGuide: drawMoveGuide(state); break;
        case ScreenPage::TileEvent: drawTileEvent(state); break;
        case ScreenPage::CardReveal: drawCardReveal(state, nowMs); break;
        case ScreenPage::DebtAssets: drawDebtAssets(state); break;
        case ScreenPage::Bankruptcy: drawBankruptcy(state); break;
        case ScreenPage::Purchase: drawPurchase(state); break;
        case ScreenPage::Auction: drawAuction(state); break;
        case ScreenPage::AvatarLoading: drawAvatarLoading(state); break;
        case ScreenPage::AvatarSetup: drawAvatarSetup(state); break;
        case ScreenPage::NameReview: drawNameReview(state); break;
        case ScreenPage::NameHandwriting: drawNameHandwriting(state); break;
        case ScreenPage::PlayerReady: drawPlayerReady(state, nowMs); break;
    }
    drawToast(state, nowMs);
    drawActivityHud(state, nowMs);
    drawModal(state, nowMs);
    drawTradeReceiverPicker(state);
    drawConnectionStatus(state);

    // Coalesce all object invalidations into one full-screen refresh. This keeps
    // every LCD framebuffer identical after a page rebuild.
    lv_obj_invalidate(root);
    lv_disp_t *display = lv_obj_get_disp(root);
    if (display != nullptr && display->refr_timer != nullptr) {
        lv_timer_ready(display->refr_timer);
    }
}

void updateDynamic(const AppState &state, uint32_t nowMs)
{
    if (state.page == ScreenPage::Home &&
        state.endTurnPresentation == EndTurnPresentationPhase::Exiting) {
        uiCarouselSetEndTurnExitProgress(
            homeCarousel, appEndTurnExitProgressPermille(state, nowMs)
        );
    }
    if (state.page == ScreenPage::DiceStage) updateDiceVisual(state, nowMs);
    if (state.page == ScreenPage::ExtraRollReward) updateExtraRollVisual(state, nowMs);
    if (state.page == ScreenPage::CardReveal) updateCardVisual(state, nowMs);
    if (state.page == ScreenPage::PlayerReady) updateIdentityCountdown(state, nowMs);
    if (state.page == ScreenPage::AvatarLoading) updateAvatarPreloadVisual(state);
    if (state.page == ScreenPage::Home &&
        (state.homePhase == HomePhase::MyTurn ||
         state.homePhase == HomePhase::MyTurnEnd)) updateTurnReminder(nowMs);
    applyModal(state, nowMs, false);
}

} // namespace

bool uiRendererBegin()
{
    root = lv_obj_create(nullptr);
    if (root == nullptr) return false;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_style_transition_dsc_init(
        &pressTransition,
        pressTransitionProps,
        lv_anim_path_ease_out,
        110,
        0,
        nullptr
    );
    lv_scr_load(root);
    uiSetEventSink(handleUiEvent);
    renderedRevision = 0;
    return true;
}

void uiRendererRender(const AppState &state, uint32_t nowMs)
{
    if (root == nullptr) return;
    prefetchRollTargetArtwork(state);
    if (artworkInvalidated) {
        artworkInvalidated = false;
        hasRenderedState = false;
        renderedRevision = state.revision == 0 ? UINT32_MAX : state.revision - 1;
    }
    if (state.revision != renderedRevision) {
        const bool homeActionsUnchanged = state.page != ScreenPage::Home ||
            appPresentedHomePhase(state) == appPresentedHomePhase(previousRenderedState);
        const bool inlineEditUnchanged = state.inlineEditField == previousInlineEditField;
        const bool visibleStateUnchanged =
            samePageVisibleStateUnchanged(state, previousRenderedState);
        const bool boundaryPulseChanged =
            state.boundaryPulseRevision != previousBoundaryPulseRevision;
        if (hasRenderedState && state.page == previousPage &&
            (state.focus != previousFocus || boundaryPulseChanged) &&
            homeActionsUnchanged && inlineEditUnchanged && visibleStateUnchanged) {
            ++rendererTestStats.incrementalRenders;
            const uint8_t count = appFocusCount(state);
            focusMotion = static_cast<uint8_t>((previousFocus + 1) % count) == state.focus ? 1 : -1;
            if (state.page == ScreenPage::Home) {
                uiCarouselSetSelection(homeCarousel, state.focus, true);
            } else if (isCenterListPage(state.page)) {
                if (state.focus != previousFocus) updateCenterListFocus(state);
                else uiCenterListBoundaryPulse(centerList, state.boundaryPulseDirection,
                                               state.boundaryPulseRevision);
            } else {
                refreshSamePageFocus(state);
            }
            renderedRevision = state.revision;
            previousFocus = state.focus;
            previousBoundaryPulseRevision = state.boundaryPulseRevision;
            previousRenderedState = state;
            lastDynamicDrawMs = nowMs;
            return;
        } else {
            focusMotion = 0;
        }
        ++rendererTestStats.rebuildRenders;
        rebuild(state, nowMs);
        renderedRevision = state.revision;
        previousPage = state.page;
        previousFocus = state.focus;
        previousInlineEditField = state.inlineEditField;
        previousBoundaryPulseRevision = state.boundaryPulseRevision;
        previousRenderedState = state;
        hasRenderedState = true;
        lastDynamicDrawMs = nowMs;
        return;
    }
    const HomePhase visualHomePhase = appPresentedHomePhase(state);
    const bool homeReminder = state.page == ScreenPage::Home &&
        (visualHomePhase == HomePhase::MyTurn || visualHomePhase == HomePhase::MyTurnEnd);
    const bool endTurnExit = state.page == ScreenPage::Home &&
        state.endTurnPresentation == EndTurnPresentationPhase::Exiting;
    const uint32_t dynamicPeriod = (state.page == ScreenPage::DiceStage ||
                                    state.page == ScreenPage::ExtraRollReward ||
                                    state.page == ScreenPage::CardReveal ||
                                    state.page == ScreenPage::AvatarLoading ||
                                    state.page == ScreenPage::PlayerReady ||
                                    endTurnExit) ? 16u : 50u;
    if ((state.modal.kind != ModalKind::None || state.page == ScreenPage::DiceStage ||
         state.page == ScreenPage::ExtraRollReward ||
         state.page == ScreenPage::CardReveal ||
         state.page == ScreenPage::AvatarLoading ||
         state.page == ScreenPage::PlayerReady || homeReminder || endTurnExit) &&
        nowMs - lastDynamicDrawMs >= dynamicPeriod) {
        updateDynamic(state, nowMs);
        lastDynamicDrawMs = nowMs;
    }
}

bool uiRendererPollTouch(TouchAction &action)
{
    if (touchCount == 0) return false;
    action = touchQueue[touchHead];
    touchHead = static_cast<uint8_t>((touchHead + 1) % kTouchQueueCapacity);
    --touchCount;
    return true;
}

bool uiRendererPollHandwriting(char &character, uint32_t nowMs)
{
    return uiHandwritingPoll(character, nowMs);
}

void uiRendererInvalidateArtwork()
{
    artworkInvalidated = true;
}

void uiRendererResetForTest()
{
    uiCarouselDestroy(homeCarousel);
    uiCenterListDestroy(centerList);
    uiModalDestroy(activeModal);
    if (root != nullptr) lv_obj_clean(root);
    uiHandwritingReset();
    root = nullptr;
    renderedRevision = 0;
    lastDynamicDrawMs = 0;
    tapBindingCount = 0;
    memset(avatarRowBindings, 0, sizeof(avatarRowBindings));
    touchHead = 0;
    touchTail = 0;
    touchCount = 0;
    focusMotion = 0;
    hasRenderedState = false;
    previousPage = ScreenPage::Home;
    previousFocus = 0;
    previousInlineEditField = InlineEditField::None;
    previousBoundaryPulseRevision = 0;
    previousRenderedState = AppState{};
    rendererTestStats = UiRendererTestStats{};
    artworkInvalidated = false;
    prefetchedRollTarget = 0xFF;
    identityCountdownLabel = nullptr;
    avatarPreloadArc = nullptr;
    avatarPreloadBar = nullptr;
    avatarPreloadLabel = nullptr;
    uiSetEventSink(nullptr);
}

void uiRendererResetTestStats()
{
    rendererTestStats = UiRendererTestStats{};
}

UiRendererTestStats uiRendererGetTestStats()
{
    return rendererTestStats;
}

void uiRendererShowFault(const char *code)
{
    if (root == nullptr && !uiRendererBegin()) return;
    uiCarouselDestroy(homeCarousel);
    uiCenterListDestroy(centerList);
    uiModalDestroy(activeModal);
    lv_obj_clean(root);
    drawOuterRing(kRed);
    drawHeader("设备故障", "GRIDOPOLY");
    label(root, code, 90, 205, 300, &lv_font_montserrat_24, kRed);
}
