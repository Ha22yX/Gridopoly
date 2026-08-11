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
    PlayerAssets,
    PlayerFinance,
    Activity,
    Trade,
    TradeAssetSelect,
    TradeOffer,
    DemoLab,
    Debt,
    DiceStage,
    ExtraRollReward,
    MoveGuide,
    TileEvent,
    CardReveal,
    DebtAssets,
    Bankruptcy,
    Purchase,
    Auction,
    AvatarSetup,
    NameReview,
    NameHandwriting,
    PlayerReady,
    AvatarLoading,
};

enum class IdentityClientPhase : uint8_t {
    Inactive,
    AvatarEditing,
    AvatarSubmitting,
    NameReview,
    NameHandwriting,
    NameSubmitting,
    Ready,
    Countdown,
    AvatarLoading,
};

enum class AvatarEditField : uint8_t {
    HairPreset,
    HairColor,
    FacePreset,
    SkinTone,
    OutfitPreset,
    Confirm,
};

enum class HomePhase : uint8_t {
    Waiting,
    NextPlayer,
    MyTurn,
    MyTurnEnd,
};

enum class EndTurnPresentationPhase : uint8_t {
    None,
    Exiting,
    WaitingHold,
};

enum class ExtraRollPresentationPhase : uint8_t {
    None,
    Pending,
    Reward,
    Ready,
};

enum class AuctionPresentationPhase : uint8_t {
    None,
    Intro,
    OpeningWait,
    Live,
    Result,
};

enum class CardPresentationPhase : uint8_t {
    None,
    Drawing,
    Revealed,
    Settling,
};

enum class ArrivalKind : uint8_t {
    Unknown,
    Start,
    Asset,
    CityEvent,
    CivicFund,
    Fee,
    Hold,
    Rest,
    GoToHold,
};

enum class ModalKind : uint8_t {
    None,
    CollectRent,
    ForcedPayment,
    VoluntaryMortgage,
    TradeCreate,
    DebtMortgageConfirm,
    VoluntaryUnmortgage,
    TradeAction,
    DebtSellBuildingConfirm,
};

enum class ModalFocus : uint8_t { Confirm, Cancel, ResolveAssets };

enum class InlineEditField : uint8_t {
    None,
    TradeReceiver,
    TradeAmount,
};

enum class TradeEntryMode : uint8_t {
    HomeEditable,
    PlayerLocked,
    CounterLocked,
};

enum class AssetDetailAction : uint8_t {
    MortgageOrRedeem,
    Build,
    SellBuilding,
    Trade,
};

enum class TouchAction : uint16_t {
    None = 0,
    PressDown = 1,
    PressUp = 2,
    ActivityOpen = 90,
    HomeAssets = 100,
    HomePlayers = 101,
    HomeTrade = 102,
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
    TradeAssets = 404,
    TradeReceiverOption0 = 410,
    TradeReceiverOption1 = 411,
    TradeReceiverOption2 = 412,
    TradeReceiverOption3 = 413,
    TradeReceiverOption4 = 414,
    TradeReceiverCancel = 415,
    DemoWaiting = 600,
    DemoNext = 601,
    DemoTurn = 602,
    DemoRent = 603,
    DemoPayment = 604,
    DemoDebt = 605,
    DemoBack = 606,
    DetailPrimary = 700,
    DetailBack = 701,
    DetailSecondary = 702,
    DetailTertiary = 703,
    DetailRefresh = 704,
    ListItem0 = 800,
    ListPrevious = 840,
    ListNext = 841,
    Back = 842,
    Footer = 843,
    IdentityRow0 = 900,
    IdentityRow1 = 901,
    IdentityRow2 = 902,
    IdentityRow3 = 903,
    IdentityRow4 = 904,
    IdentityConfirm = 905,
    NameEdit = 920,
    NameConfirm = 921,
    NameBack = 922,
    NameDelete = 923,
    HandwritingConfirm = 924,
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
    TransportTradeOperation tradeOperation = TransportTradeOperation::Create;
};

struct DebtState {
    uint32_t transactionId = 0;
    int32_t amountDue = 0;
    int32_t cashBefore = 0;
    uint32_t selectedMask = 0;
    uint32_t eligibleMask = 0;
    uint32_t submittedMortgageRequestId = 0;
    uint32_t submittedMortgageMask = 0;
    bool bankruptcyPending = false;
    bool bankruptcyResolved = false;
};

enum class PlayerDetailLoadState : uint8_t {
    Empty,
    Loading,
    Ready,
    Failed,
};

struct PlayerDetailState {
    PlayerDetailLoadState loadState = PlayerDetailLoadState::Empty;
    uint32_t requestId = 0;
    uint32_t requestedAtMs = 0;
    uint32_t stateVersion = 0;
    int32_t cash = 0;
    uint8_t playerId = 0;
    uint8_t position = 0;
    uint8_t assetCount = 0;
    uint8_t financialRecordCount = 0;
    TransportPlayerAsset assets[kPlayerDetailAssetCapacity]{};
    TransportFinancialRecord financialRecords[kPlayerFinanceCapacity]{};
};

struct TradeOfferState {
    bool active = false;
    uint32_t tradeId = 0;
    uint32_t expiresAtMs = 0;
    uint32_t selfAssetMask = 0;
    uint32_t counterpartyAssetMask = 0;
    uint16_t revision = 0;
    int32_t selfGivesCash = 0;
    int32_t counterpartyGivesCash = 0;
    TransportTradeStatus status = TransportTradeStatus::None;
    uint8_t flags = 0;
    uint8_t counterpartyId = 0;
    uint8_t confirmedMask = 0;
    uint8_t originatorId = 0;
};

constexpr uint8_t kActivityCapacity = 20;

struct ActivityEntry {
    TransportGameEvent event{};
    bool announced = false;
    // Captured when the event is received so later trades cannot rewrite the
    // ownership context shown in activity history.
    bool selfOwnedAsset = false;
};

struct ActivityState {
    ActivityEntry entries[kActivityCapacity]{};
    uint8_t head = 0;
    uint8_t count = 0;
    uint32_t bannerSequence = 0;
    uint32_t bannerUntilMs = 0;
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

struct IdentityState {
    IdentityClientPhase phase = IdentityClientPhase::Inactive;
    TransportIdentityPhase authorityPhase = TransportIdentityPhase::Inactive;
    TransportIdentityResult lastResult = TransportIdentityResult::Ok;
    TransportAvatarRecipe draftRecipe{};
    TransportAvatarRecipe confirmedRecipe{};
    TransportIdentitySeat seats[6]{};
    uint32_t revision = 0;
    uint32_t ownSeatRevision = 0;
    uint32_t pendingRequestId = 0;
    uint32_t countdownDeadlineMs = 0;
    uint8_t seatCount = 0;
    uint8_t humanMask = 0;
    uint8_t avatarReadyMask = 0;
    uint8_t nameReadyMask = 0;
    uint8_t readyMask = 0;
    uint8_t onlineMask = 0;
    uint8_t avatarPreloadReadyCount = 0;
    uint8_t avatarPreloadTotalCount = 30;
    uint8_t focus = 0;
    bool editingValue = false;
    bool draftInitialized = false;
    bool draftDirty = false;
    bool nameDirty = false;
    bool avatarAssetsReady = false;
    bool avatarEditOverride = false;
    char draftName[17]{};
};

enum class UiEventKind : uint8_t {
    ActivateFocused,
    SelectHomeAction,
    SelectListItem,
    SelectFooter,
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
    EndTurnPresentationPhase endTurnPresentation = EndTurnPresentationPhase::None;
    uint32_t endTurnPresentationStartedMs = 0;
    uint32_t endTurnPresentationUntilMs = 0;
    bool endTurnAccepted = false;
    ExtraRollPresentationPhase extraRollPresentation =
        ExtraRollPresentationPhase::None;
    uint32_t extraRollRewardStartedMs = 0;
    uint32_t extraRollRewardUntilMs = 0;
    uint8_t extraRollStreak = 0;
    uint8_t extraRollDieA = 0;
    uint8_t extraRollDieB = 0;
    ModalState modal{};
    DebtState debt{};
    PlayerDetailState playerDetail{};
    TradeOfferState tradeOffer{};
    IdentityState identity{};
    uint8_t focus = 0;
    uint8_t selectedAsset = 0;
    uint8_t selectedPlayer = 1;
    uint8_t assetListIndex = 0;
    uint8_t playerListIndex = 0;
    uint8_t playerAssetListIndex = 0;
    uint8_t playerFinanceListIndex = 0;
    uint8_t activityListIndex = 0;
    uint8_t demoListIndex = 0;
    uint8_t debtListIndex = 0;
    uint8_t tradeAssetListIndex = 0;
    uint8_t tradeReceiver = 1;
    bool tradeReceiverPickerOpen = false;
    uint8_t tradeReceiverPickerIndex = 0;
    uint32_t tradeGiveAssetMask = 0;
    int32_t tradeAmount = 0;
    InlineEditField inlineEditField = InlineEditField::None;
    TradeEntryMode tradeEntryMode = TradeEntryMode::HomeEditable;
    int32_t money = 1860;
    uint8_t position = 17;
    uint8_t turnsUntilYou = 3;
    uint8_t selfSeatId = 1;
    uint8_t playerCount = 5;
    uint8_t activePlayerId = 2;
    uint8_t decisionPlayerId = 2;
    uint8_t boardSize = 28;
    uint32_t availableActions = 0;
    AuthorityPhase authorityPhase = AuthorityPhase::AwaitRoll;
    uint8_t tileAssetIndex = 0xFF;
    uint8_t tileOwnerId = 0;
    uint8_t tileBuildingLevel = 0;
    uint8_t tileFlags = 0;
    uint8_t debtCreditorId = 0;
    uint8_t debtAssetIndex = 0xFF;
    uint8_t debtPaymentEvent = 0;
    uint8_t debtContinuation = 0;
    uint8_t debtDieA = 0;
    uint8_t debtDieB = 0;
    uint8_t auctionAssetIndex = 0xFF;
    uint8_t auctionCurrentBidderId = 0;
    uint8_t auctionHighestBidderId = 0;
    uint8_t auctionPassedMask = 0;
    uint8_t auctionFlags = 0;
    uint8_t auctionReadyMask = 0;
    uint8_t auctionRequiredReadyMask = 0;
    AuctionPresentationPhase auctionPresentation = AuctionPresentationPhase::None;
    uint8_t auctionResultAssetIndex = 0xFF;
    uint8_t auctionWinnerPlayerId = 0;
    uint8_t winnerPlayerId = 0;
    uint8_t authorityAssetCount = 0;
    int32_t debtAmount = 0;
    int32_t auctionCurrentBid = 0;
    int32_t auctionMinimumBid = 0;
    int32_t auctionResultAmount = 0;
    uint32_t auctionPresentationUntilMs = 0;
    uint32_t auctionGeneration = 0;
    uint32_t seenAuctionGeneration = 0;
    uint32_t completedAuctionGeneration = 0;
    uint32_t authorityRoomId = 0;
    uint8_t seenAuctionAssetIndex = 0xFF;
    uint8_t completedAuctionAssetIndex = 0xFF;
    uint32_t auctionReadyAttemptGeneration = 0;
    uint8_t auctionReadyAttemptAssetIndex = 0xFF;
    bool auctionPassed = false;
    uint32_t rollStartedMs = 0;
    uint32_t rollResolvedMs = 0;
    uint32_t rollRevealMs = 0;
    uint32_t rollFailedUntilMs = 0;
    uint32_t arrivalContinueAtMs = 0;
    uint32_t cardStartedMs = 0;
    uint32_t cardRevealAtMs = 0;
    uint32_t cardEventSequence = 0;
    uint32_t seenCardDrawEventSequence = 0;
    uint32_t completedCardDrawEventSequence = 0;
    uint8_t rollOrigin = 0;
    uint8_t rollTarget = 0xFF;
    uint8_t dieA = 0;
    uint8_t dieB = 0;
    uint8_t rolledSteps = 0;
    bool rollAnimating = false;
    bool rollResolved = false;
    bool rollRevealPresented = false;
    bool rollPresentationComplete = false;
    bool rollFailed = false;
    bool moveArrivalPending = false;
    bool moveArrivalConfirmed = false;
    bool landingEventAcknowledged = false;
    CardPresentationPhase cardPresentation = CardPresentationPhase::None;
    uint8_t pendingCardFlags = 0;
    uint8_t cardIndex = 0;
    uint8_t cardFlags = 0;
    uint8_t cardTargetPlayerId = 0;
    uint8_t cardTargetPosition = 0;
    uint8_t cardOutcome = 0;
    uint16_t cardInstanceId = 0;
    uint16_t seenCardInstanceId = 0;
    uint16_t completedCardInstanceId = 0;
    uint16_t cardCatalogId = 0;
    uint16_t cardEffectId = 0;
    int32_t cardAmount = 0;
    bool cardChance = false;
    bool cardResultValid = false;
    bool cardPresentationAcknowledged = false;
    bool cardEffectApplied = false;
    AuthorityPlayerSummary authorityPlayers[6]{};
    AuthorityAssetSummary authorityAssets[28]{};
    char rosterNames[6][17]{};
    TransportGameEvent lastGameEvent{};
    ActivityState activity{};
    uint32_t boardIdHash = 0;
    uint32_t lastEventSequence = 0;
    bool fullAuthoritySnapshotValid = false;
    bool rosterSnapshotValid = false;
    bool boardCatalogCompatible = true;
    bool authoritySnapshotValid = false;
    int8_t boundaryPulseDirection = 0;
    uint32_t boundaryPulseRevision = 0;
    bool authorityOnline = true;
    TransportCommand commandQueue[4]{};
    uint8_t commandHead = 0;
    uint8_t commandTail = 0;
    uint8_t commandCount = 0;
    uint32_t nextRequestId = 1;
    uint32_t stateVersion = 1;
    uint32_t pendingCommandMask = 0;
    uint32_t pendingRequestIds[24]{};
    uint32_t pendingPayNowStartedMs = 0;
    uint32_t buttonDownMs = 0;
    bool buttonHeld = false;
    bool holdActionConsumed = false;
    uint32_t revision = 1;
    uint32_t toastUntilMs = 0;
    const char *toast = "";
};
