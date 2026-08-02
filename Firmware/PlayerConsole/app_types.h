#pragma once

#include <stdint.h>

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
};

enum class HomePhase : uint8_t {
    Waiting,
    NextPlayer,
    MyTurn,
};

enum class ModalKind : uint8_t {
    None,
    CollectRent,
    Payment,
    Mortgage,
    Trade,
};

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
    const char *name;
    const char *group;
    int16_t value;
    int16_t rent;
    bool mortgaged;
};

struct ModalState {
    ModalKind kind = ModalKind::None;
    const char *title = "";
    const char *counterparty = "";
    const char *purpose = "";
    int32_t amount = 0;
    uint32_t openedMs = 0;
    uint32_t deadlineMs = 0;
    uint32_t holdStartMs = 0;
    bool holding = false;
};

struct AppState {
    ScreenPage page = ScreenPage::Home;
    HomePhase homePhase = HomePhase::Waiting;
    ModalState modal{};
    uint8_t focus = 0;
    uint8_t selectedAsset = 0;
    uint8_t selectedPlayer = 1;
    uint8_t tradeReceiver = 1;
    int32_t tradeAmount = 100;
    int32_t money = 1860;
    uint8_t position = 17;
    uint32_t buttonDownMs = 0;
    bool buttonHeld = false;
    bool holdActionConsumed = false;
    uint32_t revision = 1;
    uint32_t toastUntilMs = 0;
    const char *toast = "";
};
