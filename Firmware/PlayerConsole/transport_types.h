#pragma once

#include <stdint.h>

enum class TransportCommandKind : uint8_t {
    RollRequest,
    PayNow,
    ClaimRent,
    TradeCreate,
    MortgageBatchRequest,
    MoveManualConfirmRequest,
};

enum class TransportEventKind : uint8_t {
    None,
    ConnectionLost,
    StateSnapshotApplied,
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
};

enum class TransportError : uint8_t {
    None,
    InsufficientCash,
    AssetChanged,
    StaleState,
    ActionNotAllowed,
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
};

struct AuthorityPlayerSummary {
    uint8_t playerId = 0;
    uint8_t position = 0;
    int32_t cash = 0;
    uint8_t flags = 0;
};

struct TransportCommand {
    TransportCommandKind kind{};
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t transactionId = 0;
    uint32_t clientTimeMs = 0;
    uint32_t assetMask = 0;
    uint8_t targetPosition = 0;
};

struct TransportEvent {
    TransportEventKind kind = TransportEventKind::None;
    TransportError error = TransportError::None;
    uint32_t requestId = 0;
    uint32_t stateVersion = 0;
    uint32_t transactionId = 0;
    uint32_t deadlineMs = 0;
    int32_t amount = 0;
    int32_t cash = 0;
    uint32_t assetMask = 0;
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
    AuthorityPhase phase = AuthorityPhase::Lobby;
    uint32_t availableActions = 0;
    AuthorityPlayerSummary players[6]{};
    const char *targetName = "";
    bool manual = false;
    bool resync = false;
};
