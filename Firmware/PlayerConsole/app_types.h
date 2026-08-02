#pragma once

#include <stdint.h>

#include "transport_types.h"

enum class InputKind : uint8_t {
    Rotate,
    ButtonDown,
    ButtonUp,
};

struct InputEvent {
    InputKind kind = InputKind::Rotate;
    int16_t delta = 0;
    uint32_t timestampMs = 0;
};

enum class ScreenPage : uint8_t {
    Home,
    Assets,
    AssetDetail,
    Players,
    PlayerDetail,
    Trade,
    More,
    DemoLab,
    Debt,
    DiceStage,
    MoveGuide,
    DebtAssets,
    Bankruptcy,
};

enum class HomePhase : uint8_t {
    Waiting,
    NextPlayer,
    MyTurn,
};

enum class ModalKind : uint8_t {
    None,
    CollectRent,
    ForcedPayment,
    VoluntaryMortgage,
    TradeCreate,
    DebtMortgageConfirm,
};

enum class ModalFocus : uint8_t { Confirm, Cancel, ResolveAssets };

enum class TouchAction : uint16_t {
    None = 0,
    PressDown = 1,
    PressUp = 2,
    HomeAssets = 100,
    HomePlayers = 101,
    HomeTrade = 102,
    HomeMore = 103,
    Asset0 = 200,
    Asset1 = 201,
    Asset2 = 202,
    Asset3 = 203,
    Asset4 = 204,
    Player0 = 300,
    Player1 = 301,
    Player2 = 302,
    Player3 = 303,
    Player4 = 304,
    TradeReceiver = 400,
    TradeAmount = 401,
    TradeConfirm = 402,
    TradeBack = 403,
    MoreDemoLab = 500,
    MoreAbout = 501,
    DemoWaiting = 600,
    DemoNext = 601,
    DemoTurn = 602,
    DemoRent = 603,
    DemoPayment = 604,
    DemoDebt = 605,
    DemoBack = 606,
    DetailPrimary = 700,
    DetailBack = 701,
};

struct PlayerData {
    const char *name;
    const char *token;
    int32_t money;
    uint8_t position;
};

struct AssetData {
    uint8_t id;
    const char *name;
    const char *group;
    int16_t value;
    int16_t rent;
    int16_t mortgageValue;
    uint8_t buildingLevel;
    bool mortgaged;
    bool ruleLocked;
};

struct ModalState {
    ModalKind kind = ModalKind::None;
    ModalFocus focus = ModalFocus::Confirm;
    bool cancelAllowed = false;
    bool holding = false;
    bool submitting = false;
    bool insufficient = false;
    uint32_t transactionId = 0;
    uint32_t deadlineMs = 0;
    uint32_t holdStartMs = 0;
    int32_t amount = 0;
    const char *title = "";
    const char *counterparty = "";
    const char *purpose = "";
};

struct NavigationEntry {
    ScreenPage page = ScreenPage::Home;
    uint8_t focus = 0;
    uint8_t listAnchor = 0;
};

struct NavigationState {
    NavigationEntry current{};
    NavigationEntry stack[4]{};
    uint8_t depth = 0;
};

enum class UiEventKind : uint8_t {
    ActivateFocused,
    SelectHomeAction,
    SelectListItem,
    Back,
    HoldDown,
    HoldUp,
    ListPrevious,
    ListNext,
};

struct UiEvent {
    UiEventKind kind = UiEventKind::ActivateFocused;
    int16_t value = 0;
};

struct AppState {
    NavigationState nav{};
    // Kept synchronized with nav for the existing renderer until it moves to NavigationState.
    ScreenPage page = ScreenPage::Home;
    HomePhase homePhase = HomePhase::Waiting;
    ModalState modal{};
    uint8_t focus = 0;
    uint8_t selectedAsset = 0;
    uint8_t selectedPlayer = 1;
    uint8_t assetListIndex = 0;
    uint8_t playerListIndex = 0;
    uint8_t demoListIndex = 0;
    uint8_t debtListIndex = 0;
    uint8_t tradeReceiver = 1;
    int32_t tradeAmount = 100;
    int32_t money = 1860;
    uint8_t position = 17;
    bool authorityOnline = true;
    TransportCommand commandQueue[4]{};
    uint8_t commandHead = 0;
    uint8_t commandTail = 0;
    uint8_t commandCount = 0;
    uint32_t nextRequestId = 1;
    uint32_t stateVersion = 1;
    uint8_t pendingCommandMask = 0;
    uint32_t pendingRequestIds[6]{};
    uint32_t buttonDownMs = 0;
    bool buttonHeld = false;
    bool holdActionConsumed = false;
    uint32_t revision = 1;
    uint32_t toastUntilMs = 0;
    const char *toast = "";
};
