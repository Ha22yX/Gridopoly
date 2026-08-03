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
    const char *targetName = "";
    bool manual = false;
};
