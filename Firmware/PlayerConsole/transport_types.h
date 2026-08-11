#pragma once

#include <stdint.h>

enum class TransportCommandKind : uint8_t {
    RollRequest,
    PayNow,
    ClaimRent,
    TradeQuery,
    TradeCreate,
    TradeUpdate,
    TradeConfirm,
    TradeReject,
    TradeCancel,
    MortgageBatchRequest,
    MoveManualConfirmRequest,
    BuyRequest,
    DeclinePurchaseRequest,
    EndTurnRequest,
    UnmortgageRequest,
    BuildRequest,
    SellBuildingRequest,
    DeclareBankruptcyRequest,
    AuctionBidRequest,
    AuctionPassRequest,
    AuctionReadyRequest,
    PlayerDetailRequest,
    CardContinueRequest,
    IdentityRequest,
};

enum class TransportEventKind : uint8_t {
    None,
    ConnectionLost,
    StateSnapshotApplied,
    AuthoritySnapshotApplied,
    RosterSnapshotApplied,
    GameEventReceived,
    RollResult,
    MoveGuidanceStarted,
    RfidPositionConfirmed,
    RfidPositionRejected,
    PaymentRequired,
    PaymentCompleted,
    DebtResolutionRequired,
    MortgageBatchCompleted,
    BankruptcyResolved,
    CommandCompleted,
    CommandRejected,
    PlayerDetailReceived,
    PlayerCardDrawn,
    PlayerCardEffectApplied,
    TradeResponseReceived,
    IdentitySnapshotReceived,
};

inline bool transportEventAdvancesAppliedStateVersion(
    TransportEventKind kind, bool identitySetupActive)
{
    if (kind == TransportEventKind::IdentitySnapshotReceived) return true;
    return !identitySetupActive &&
           kind == TransportEventKind::AuthoritySnapshotApplied;
}

enum class TransportIdentityOperation : uint8_t {
    Query = 1,
    ConfirmAvatar = 2,
    ConfirmName = 3,
};

inline bool identitySnapshotCompletesPendingOperation(
    TransportIdentityOperation operation, uint8_t selfPlayerId,
    uint8_t avatarReadyMask, uint8_t nameReadyMask)
{
    if (selfPlayerId == 0 || selfPlayerId > 6) return false;
    const uint8_t selfBit = static_cast<uint8_t>(1u << (selfPlayerId - 1u));
    if (operation == TransportIdentityOperation::ConfirmAvatar) {
        return (avatarReadyMask & selfBit) != 0;
    }
    if (operation == TransportIdentityOperation::ConfirmName) {
        return (nameReadyMask & selfBit) != 0;
    }
    return false;
}

enum class TransportIdentityPhase : uint8_t {
    Inactive = 0,
    AwaitAvatar = 1,
    GeneratingAvatar = 2,
    AwaitName = 3,
    Ready = 4,
    Countdown = 5,
    Active = 6,
};

enum class TransportIdentityResult : uint8_t {
    Ok = 0,
    InvalidRequest = 1,
    Unauthorized = 2,
    StateVersionStale = 3,
    SeatRevisionStale = 4,
    CatalogMismatch = 5,
    InvalidRecipe = 6,
    InvalidName = 7,
    DuplicateName = 8,
    NotAllowed = 9,
    RequestIdConflict = 10,
    AvatarGenerationFailed = 11,
};

struct TransportAvatarRecipe {
    uint16_t catalogVersion = 1;
    uint8_t hairPresetId = 1;
    uint8_t hairColorId = 1;
    uint8_t facePresetId = 1;
    uint8_t skinToneId = 1;
    uint8_t outfitPresetId = 1;
};

inline TransportAvatarRecipe normalizedTransportAvatarRecipe(
    const TransportAvatarRecipe &source)
{
    TransportAvatarRecipe normalized = source;
    if (normalized.catalogVersion != 1) normalized.catalogVersion = 1;
    if (normalized.hairPresetId < 1 || normalized.hairPresetId > 10) {
        normalized.hairPresetId = 1;
    }
    if (normalized.hairColorId < 1 || normalized.hairColorId > 20) {
        normalized.hairColorId = 1;
    }
    if (normalized.facePresetId < 1 || normalized.facePresetId > 10) {
        normalized.facePresetId = 1;
    }
    if (normalized.skinToneId < 1 || normalized.skinToneId > 8) {
        normalized.skinToneId = 1;
    }
    if (normalized.outfitPresetId < 1 || normalized.outfitPresetId > 10) {
        normalized.outfitPresetId = 1;
    }
    return normalized;
}

struct TransportIdentitySeat {
    uint16_t seatRevision = 0;
    uint16_t avatarRevision = 0;
    uint64_t avatarCacheTag = 0;
    TransportAvatarRecipe recipe{};
    uint8_t playerId = 0;
    uint8_t flags = 0;
    uint8_t colorIndex = 0;
    char name[17]{};
};

struct TransportIdentityPayload {
    TransportIdentitySeat seats[6]{};
};

enum class TransportError : uint8_t {
    None,
    InsufficientCash,
    AssetChanged,
    StaleState,
    ActionNotAllowed,
};

enum class TransportTradeOperation : uint8_t {
    Query = 1,
    Create = 2,
    Update = 3,
    Confirm = 4,
    Reject = 5,
    Cancel = 6,
};

enum class TransportTradeStatus : uint8_t {
    None = 0,
    Offered = 1,
    Countered = 2,
    Settled = 3,
    Rejected = 4,
    Cancelled = 5,
    Expired = 6,
    Invalidated = 7,
};

enum class TransportTradeResult : uint8_t {
    Ok = 0,
    NoActiveTrade = 1,
    InvalidRequest = 2,
    Unauthorized = 3,
    StateVersionStale = 4,
    RevisionStale = 5,
    ParticipantBusy = 6,
    RuleViolation = 7,
    NotEnoughCash = 8,
    AssetUnavailable = 9,
    Expired = 10,
    RequestIdConflict = 11,
};

enum class AuthorityPhase : uint8_t {
    Lobby = 0,
    AwaitRoll = 1,
    AwaitMoveConfirm = 2,
    AwaitPurchase = 3,
    AwaitAuction = 4,
    AwaitDebt = 5,
    TurnEnd = 6,
    GameOver = 7,
    AwaitCard = 8,
};

struct AuthorityPlayerSummary {
    uint8_t playerId = 0;
    uint8_t position = 0;
    int32_t cash = 0;
    uint8_t flags = 0;
    uint8_t failedHoldRolls = 0;
    uint8_t doublesStreak = 0;
};

struct AuthorityAssetSummary {
    uint8_t ownerId = 0;
    uint8_t buildingLevel = 0;
    uint8_t flags = 0;
};

struct TransportGameEvent {
    uint32_t sequence = 0;
    uint8_t kind = 0;
    uint8_t actorId = 0;
    uint8_t targetId = 0;
    uint8_t assetIndex = 0xFF;
    int32_t amount = 0;
    uint32_t detail = 0;
};

constexpr uint8_t kPlayerDetailAssetCapacity = 28;
constexpr uint8_t kPlayerFinanceCapacity = 10;

struct TransportPlayerAsset {
    uint8_t assetIndex = 0xFF;
    uint8_t state = 0;  // bits0-2 building level, bit3 mortgaged.
};

struct TransportFinancialRecord {
    uint32_t sequence = 0;
    int32_t amount = 0;  // Signed from the requested player's perspective.
    uint8_t kind = 0;
    uint8_t counterpartyId = 0;
    uint8_t assetIndex = 0xFF;
    uint8_t flags = 0;
};

struct TransportPlayerDetailPayload {
    TransportPlayerAsset assets[kPlayerDetailAssetCapacity]{};
    TransportFinancialRecord financialRecords[kPlayerFinanceCapacity]{};
};

struct TransportCommand {
    TransportCommandKind kind{};
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t transactionId = 0;
    uint32_t clientTimeMs = 0;
    uint32_t assetMask = 0;
    uint32_t counterpartyAssetMask = 0;
    int32_t argument = 0;
    int32_t counterpartyArgument = 0;
    uint32_t tradeId = 0;
    uint16_t tradeRevision = 0;
    TransportTradeOperation tradeOperation = TransportTradeOperation::Query;
    TransportIdentityOperation identityOperation = TransportIdentityOperation::Query;
    uint32_t identitySeatRevision = 0;
    TransportAvatarRecipe avatarRecipe{};
    char identityName[17]{};
    uint8_t assetIndex = 0xFF;
    uint8_t targetPosition = 0;
    uint8_t targetPlayerId = 0;
};

struct TransportEvent {
    TransportEventKind kind = TransportEventKind::None;
    TransportError error = TransportError::None;
    uint32_t roomId = 0;
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t lastEventSequence = 0;
    uint32_t boardIdHash = 0;
    uint32_t transactionId = 0;
    uint32_t deadlineMs = 0;
    int32_t amount = 0;
    int32_t cash = 0;
    uint32_t assetMask = 0;
    uint32_t counterpartyAssetMask = 0;
    uint32_t tradeId = 0;
    uint32_t tradeExpiresInMs = 0;
    uint16_t tradeRevision = 0;
    int32_t tradeSelfGivesCash = 0;
    int32_t tradeCounterpartyGivesCash = 0;
    TransportTradeOperation tradeOperation = TransportTradeOperation::Query;
    TransportTradeStatus tradeStatus = TransportTradeStatus::None;
    TransportTradeResult tradeResult = TransportTradeResult::Ok;
    TransportIdentityPhase identityPhase = TransportIdentityPhase::Inactive;
    TransportIdentityResult identityResult = TransportIdentityResult::Ok;
    uint32_t identityRevision = 0;
    uint8_t identitySeatCount = 0;
    uint8_t identityHumanMask = 0;
    uint8_t identityAvatarReadyMask = 0;
    uint8_t identityNameReadyMask = 0;
    uint8_t identityReadyMask = 0;
    uint8_t identityOnlineMask = 0;
    uint8_t identitySelfStage = 0;
    uint8_t tradeFlags = 0;
    uint8_t tradeCounterpartyId = 0;
    uint8_t tradeConfirmedMask = 0;
    uint8_t tradeOriginatorId = 0;
    uint8_t dieA = 0;
    uint8_t dieB = 0;
    uint8_t playerPosition = 0;
    uint8_t targetPosition = 0;
    uint8_t observedPosition = 0;
    uint8_t selfSeatId = 0;
    uint8_t activePlayerId = 0;
    uint8_t decisionPlayerId = 0;
    uint8_t playerCount = 0;
    uint8_t boardSize = 0;
    uint8_t pendingTarget = 0xFF;
    uint8_t tileAssetIndex = 0xFF;
    uint8_t tileOwnerId = 0;
    uint8_t tileBuildingLevel = 0;
    uint8_t tileFlags = 0;
    uint8_t debtCreditorId = 0;
    uint8_t debtAssetIndex = 0xFF;
    uint8_t auctionAssetIndex = 0xFF;
    uint8_t auctionCurrentBidderId = 0;
    uint8_t auctionHighestBidderId = 0;
    uint8_t auctionPassedMask = 0;
    uint8_t auctionFlags = 0;
    uint8_t auctionReadyMask = 0;
    uint8_t auctionRequiredReadyMask = 0;
    uint8_t winnerPlayerId = 0;
    uint8_t assetCount = 0;
    uint8_t pendingMoveFlags = 0;
    uint8_t pendingMovePlayerId = 0;
    uint8_t pendingMoveOrigin = 0;
    uint8_t pendingMoveDieA = 0;
    uint8_t pendingMoveDieB = 0;
    uint8_t pendingPurchaseFlags = 0;
    uint8_t pendingPurchasePlayerId = 0;
    uint8_t pendingPurchaseAssetIndex = 0xFF;
    uint8_t debtFlags = 0;
    uint8_t debtDebtorId = 0;
    uint8_t debtPaymentEvent = 0;
    uint8_t debtContinuation = 0;
    uint8_t debtDieA = 0;
    uint8_t debtDieB = 0;
    int32_t debtAmount = 0;
    int32_t auctionCurrentBid = 0;
    int32_t auctionMinimumBid = 0;
    uint32_t auctionGeneration = 0;
    uint8_t pendingCardFlags = 0;
    uint8_t pendingCardPlayerId = 0;
    uint8_t pendingCardDeckId = 0;
    uint8_t pendingCardIndex = 0;
    uint16_t pendingCardInstanceId = 0;
    uint16_t pendingCardCatalogId = 0;
    uint16_t pendingCardEffectId = 0;
    int32_t pendingCardDisplayAmount = 0;
    uint8_t pendingCardTargetPlayerId = 0;
    uint8_t pendingCardTargetPosition = 0;
    uint32_t pendingCardDrawEventSequence = 0;
    uint8_t cardStage = 0;
    uint8_t cardPlayerId = 0;
    uint8_t cardDeckId = 0;
    uint8_t cardIndex = 0;
    uint8_t cardFlags = 0;
    uint16_t cardInstanceId = 0;
    uint16_t cardCatalogId = 0;
    uint16_t cardEffectId = 0;
    int32_t cardAmount = 0;
    uint8_t cardTargetPlayerId = 0;
    uint8_t cardTargetPosition = 0;
    uint8_t cardOutcome = 0;
    uint32_t cardEventSequence = 0;
    AuthorityPhase phase = AuthorityPhase::Lobby;
    uint32_t availableActions = 0;
    AuthorityPlayerSummary players[6]{};
    AuthorityAssetSummary assets[28]{};
    char playerNames[6][17]{};
    TransportGameEvent gameEvent{};
    // Valid until the transport's next tick/poll cycle. The app copies it immediately.
    const TransportPlayerDetailPayload *playerDetail = nullptr;
    // Valid until the transport's next tick/poll cycle. The app copies it immediately.
    const TransportIdentityPayload *identity = nullptr;
    uint8_t detailPlayerId = 0;
    uint8_t detailPosition = 0;
    uint8_t detailAssetCount = 0;
    uint8_t financialRecordCount = 0;
    int32_t detailCash = 0;
    const char *targetName = "";
    bool manual = false;
    bool resync = false;
};

static_assert(sizeof(TransportEvent) <= 512,
              "bounded transport event exceeded the ESP32 console memory budget");
