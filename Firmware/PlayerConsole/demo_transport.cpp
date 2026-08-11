#include "demo_transport.h"

namespace {

constexpr uint32_t kAuthoritativeStateVersion = 42;
constexpr int32_t kAuthoritativeCash = 1860;
constexpr uint8_t kAuthoritativePosition = 17;
constexpr uint8_t kRollTargetPosition = 24;
constexpr uint32_t kEligibleMortgageAssets = 0x57;
constexpr uint32_t kPaymentCashDeadlineMs = 10000;
constexpr uint8_t kTradeFlagSelfConfirmed = 1u << 0;
constexpr uint8_t kTradeFlagSelfOriginated = 1u << 2;
constexpr uint8_t kTradeFlagSelfLastEdited = 1u << 3;
constexpr uint8_t kTradeFlagTerminal = 1u << 5;
constexpr char kRollTargetName[] = "霓虹港湾";

bool isDue(uint32_t nowMs, uint32_t dueMs)
{
    return static_cast<int32_t>(nowMs - dueMs) >= 0;
}

} // namespace

void DemoTransport::begin(uint32_t nowMs)
{
    for (ScheduledEvent &scheduled : scheduled_) scheduled = ScheduledEvent{};
    for (TransportEvent &event : scratch_) event = TransportEvent{};
    outputHead_ = 0;
    outputTail_ = 0;
    outputCount_ = 0;
    nextScheduleSequence_ = 0;
    nextTransactionId_ = 1;
    activeRollRequestId_ = 0;
    activeRollTransactionId_ = 0;
    lastRollRequestId_ = 0;
    lastRollResult_ = TransportEvent{};
    lastRollResultValid_ = false;
    lastRollResultEmitted_ = false;
    paymentCashDeadlineMs_ = 0;
    paymentCashTransactionId_ = 0;
    paymentCashActive_ = false;
    paymentCashCompleted_ = false;
    tradeActive_ = false;
    tradeId_ = 0;
    tradeRevision_ = 0;
    tradeCounterpartyId_ = 0;
    tradeSelfAssetMask_ = 0;
    tradeCounterpartyAssetMask_ = 0;
    tradeSelfCash_ = 0;
    tradeCounterpartyCash_ = 0;
    activeRoll_ = false;
    offline_ = scenario_ == DemoScenario::ConnectionDropAndRecover;

    if (scenario_ == DemoScenario::PaymentCash) {
        TransportEvent &event = scratch_[0];
        event = TransportEvent{};
        paymentCashDeadlineMs_ = nowMs + kPaymentCashDeadlineMs;
        paymentCashTransactionId_ = nextTransactionId_++;
        event.kind = TransportEventKind::PaymentRequired;
        event.stateVersion = kAuthoritativeStateVersion;
        event.transactionId = paymentCashTransactionId_;
        event.amount = 175;
        event.cash = kAuthoritativeCash;
        event.deadlineMs = paymentCashDeadlineMs_;
        event.targetName = kRollTargetName;
        paymentCashActive_ = scheduleBatch(&event, &nowMs, 1);
        return;
    }

    if (!offline_) return;

    TransportEvent *events = scratch_;
    events[0] = TransportEvent{};
    events[1] = TransportEvent{};
    uint32_t dueMs[2]{};
    events[0].kind = TransportEventKind::ConnectionLost;
    dueMs[0] = nowMs;
    events[1].kind = TransportEventKind::StateSnapshotApplied;
    events[1].stateVersion = kAuthoritativeStateVersion;
    events[1].cash = kAuthoritativeCash;
    events[1].playerPosition = kAuthoritativePosition;
    events[1].resync = true;
    dueMs[1] = nowMs + 3000;
    scheduleBatch(events, dueMs, 2);
}

bool DemoTransport::send(const TransportCommand &command, uint32_t nowMs)
{
    if (offline_) return false;

    if (command.kind != TransportCommandKind::RollRequest) {
        return scheduleScenarioCommand(command, nowMs);
    }

    if (activeRoll_ && command.requestId == activeRollRequestId_) {
        return lastRollResultEmitted_ ? push(lastRollResult_) : true;
    }
    if (lastRollResultValid_ && command.requestId == lastRollRequestId_) return push(lastRollResult_);

    if (activeRoll_) {
        TransportEvent &rejected = scratch_[0];
        rejected = TransportEvent{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::ActionNotAllowed;
        rejected.requestId = command.requestId;
        rejected.stateVersion = kAuthoritativeStateVersion;
        return push(rejected);
    }

    return scheduleRoll(command, nowMs);
}

void DemoTransport::tick(uint32_t nowMs)
{
    while (true) {
        int8_t selected = -1;
        for (uint8_t i = 0; i < kQueueCapacity; ++i) {
            const ScheduledEvent &candidate = scheduled_[i];
            if (!candidate.active || !isDue(nowMs, candidate.dueMs)) continue;
            if (selected < 0) {
                selected = i;
                continue;
            }
            const ScheduledEvent &current = scheduled_[selected];
            const int32_t dueDifference = static_cast<int32_t>(candidate.dueMs - current.dueMs);
            if (dueDifference < 0 ||
                (dueDifference == 0 &&
                 static_cast<int32_t>(candidate.sequence - current.sequence) < 0)) {
                selected = i;
            }
        }
        if (selected < 0) return;

        ScheduledEvent &scheduled = scheduled_[selected];
        if (!push(scheduled.event)) return;
        const TransportEventKind kind = scheduled.event.kind;
        const uint32_t transactionId = scheduled.event.transactionId;
        scheduled = ScheduledEvent{};
        if (kind == TransportEventKind::StateSnapshotApplied) offline_ = false;
        if (kind == TransportEventKind::PaymentCompleted &&
            transactionId == paymentCashTransactionId_) {
            paymentCashActive_ = false;
            paymentCashCompleted_ = true;
        }
        if (kind == TransportEventKind::RollResult &&
            transactionId == activeRollTransactionId_) {
            lastRollResultEmitted_ = true;
        }
        if (transactionId == activeRollTransactionId_ &&
            (kind == TransportEventKind::MoveGuidanceStarted ||
             kind == TransportEventKind::RfidPositionConfirmed ||
             kind == TransportEventKind::RfidPositionRejected)) {
            activeRoll_ = hasScheduledTransaction(activeRollTransactionId_);
        }
    }
}

bool DemoTransport::poll(TransportEvent &event)
{
    if (outputCount_ == 0) return false;
    event = output_[outputHead_];
    outputHead_ = static_cast<uint8_t>((outputHead_ + 1) % kQueueCapacity);
    --outputCount_;
    return true;
}

void DemoTransport::setScenario(DemoScenario scenario)
{
    scenario_ = scenario;
}

bool DemoTransport::scheduleBatch(const TransportEvent *events, const uint32_t *dueMs, uint8_t count)
{
    uint8_t available = 0;
    for (ScheduledEvent &scheduled : scheduled_) {
        if (!scheduled.active) ++available;
    }
    if (available < count) return false;

    uint8_t index = 0;
    for (ScheduledEvent &scheduled : scheduled_) {
        if (scheduled.active) continue;
        scheduled.event = events[index];
        scheduled.dueMs = dueMs[index];
        scheduled.sequence = nextScheduleSequence_++;
        scheduled.active = true;
        ++index;
        if (index == count) return true;
    }
    return false;
}

bool DemoTransport::push(const TransportEvent &event)
{
    if (outputCount_ == kQueueCapacity) return false;
    output_[outputTail_] = event;
    outputTail_ = static_cast<uint8_t>((outputTail_ + 1) % kQueueCapacity);
    ++outputCount_;
    return true;
}

bool DemoTransport::hasScheduledTransaction(uint32_t transactionId) const
{
    for (const ScheduledEvent &scheduled : scheduled_) {
        if (scheduled.active && scheduled.event.transactionId == transactionId) return true;
    }
    return false;
}

bool DemoTransport::scheduleRoll(const TransportCommand &command, uint32_t nowMs)
{
    const uint32_t transactionId = nextTransactionId_;
    TransportEvent *events = scratch_;
    for (uint8_t index = 0; index < 4; ++index) events[index] = TransportEvent{};
    uint32_t dueMs[4]{};
    uint8_t eventCount = 2;

    TransportEvent &result = events[0];
    result.kind = TransportEventKind::RollResult;
    result.requestId = command.requestId;
    result.stateVersion = kAuthoritativeStateVersion;
    result.transactionId = transactionId;
    result.dieA = 3;
    result.dieB = 4;
    result.playerPosition = kAuthoritativePosition;

    const uint32_t resultDelay = scenario_ == DemoScenario::RollAuthorityDelayed ? 420 : 120;
    dueMs[0] = nowMs + resultDelay;

    TransportEvent &guidance = events[1];
    guidance.kind = TransportEventKind::MoveGuidanceStarted;
    guidance.requestId = command.requestId;
    guidance.stateVersion = kAuthoritativeStateVersion;
    guidance.transactionId = transactionId;
    guidance.playerPosition = kAuthoritativePosition;
    guidance.targetPosition = kRollTargetPosition;
    guidance.targetName = kRollTargetName;
    guidance.manual = command.targetPosition != 0;

    const uint32_t guidanceDelay = resultDelay + 30;
    dueMs[1] = nowMs + guidanceDelay;

    if (scenario_ == DemoScenario::RollRfidSuccess) {
        TransportEvent &confirmed = events[eventCount];
        confirmed = guidance;
        confirmed.kind = TransportEventKind::RfidPositionConfirmed;
        confirmed.observedPosition = kRollTargetPosition;
        dueMs[eventCount++] = nowMs + guidanceDelay + 200;
    } else if (scenario_ == DemoScenario::RollRfidWrongThenSuccess) {
        TransportEvent &rejected = events[eventCount];
        rejected = guidance;
        rejected.kind = TransportEventKind::RfidPositionRejected;
        rejected.observedPosition = 23;
        dueMs[eventCount++] = nowMs + guidanceDelay + 150;
        TransportEvent &confirmed = events[eventCount];
        confirmed = guidance;
        confirmed.kind = TransportEventKind::RfidPositionConfirmed;
        confirmed.observedPosition = kRollTargetPosition;
        dueMs[eventCount++] = nowMs + guidanceDelay + 300;
    } else if (scenario_ == DemoScenario::RollRfidTimeout) {
        TransportEvent &rejected = events[eventCount];
        rejected = guidance;
        rejected.kind = TransportEventKind::RfidPositionRejected;
        rejected.deadlineMs = nowMs + guidanceDelay + 3000;
        dueMs[eventCount++] = nowMs + guidanceDelay + 3000;
    }

    if (!scheduleBatch(events, dueMs, eventCount)) return false;
    ++nextTransactionId_;
    activeRoll_ = true;
    activeRollRequestId_ = command.requestId;
    activeRollTransactionId_ = transactionId;
    lastRollRequestId_ = command.requestId;
    lastRollResult_ = result;
    lastRollResultValid_ = true;
    lastRollResultEmitted_ = false;
    return true;
}

bool DemoTransport::scheduleScenarioCommand(const TransportCommand &command, uint32_t nowMs)
{
    if (command.kind == TransportCommandKind::PlayerDetailRequest) {
        playerDetailPayload_ = TransportPlayerDetailPayload{};
        playerDetailPayload_.assets[0] = TransportPlayerAsset{1, 0};
        playerDetailPayload_.assets[1] = TransportPlayerAsset{4, 0x08u};
        playerDetailPayload_.assets[2] = TransportPlayerAsset{7, 2};
        playerDetailPayload_.assets[3] = TransportPlayerAsset{11, 0};

        const int32_t amounts[kPlayerFinanceCapacity] = {
            -220, 36, -100, 200, -50, 75, -150, 24, -60, 200,
        };
        const uint8_t kinds[kPlayerFinanceCapacity] = {
            8, 9, 16, 6, 10, 21, 14, 9, 11, 6,
        };
        for (uint8_t i = 0; i < kPlayerFinanceCapacity; ++i) {
            TransportFinancialRecord &record = playerDetailPayload_.financialRecords[i];
            record.sequence = 120u - i;
            record.amount = amounts[i];
            record.kind = kinds[i];
            record.counterpartyId = static_cast<uint8_t>((i % 4) + 1);
            record.assetIndex = static_cast<uint8_t>((i * 3) % 14);
        }

        TransportEvent &detail = scratch_[0];
        detail = TransportEvent{};
        detail.kind = TransportEventKind::PlayerDetailReceived;
        detail.requestId = command.requestId;
        detail.stateVersion = kAuthoritativeStateVersion;
        detail.detailPlayerId = command.targetPlayerId;
        detail.detailPosition = static_cast<uint8_t>(5 + command.targetPlayerId * 3);
        detail.detailAssetCount = 4;
        detail.financialRecordCount = kPlayerFinanceCapacity;
        detail.detailCash = 1450 - static_cast<int32_t>(command.targetPlayerId) * 85;
        detail.playerDetail = &playerDetailPayload_;
        const uint32_t dueMs = nowMs + 180;
        return scheduleBatch(&detail, &dueMs, 1);
    }

    if (scenario_ == DemoScenario::PaymentCash && command.kind == TransportCommandKind::PayNow) {
        if (command.transactionId != paymentCashTransactionId_) return false;
        if (!paymentCashActive_) return paymentCashCompleted_;
        for (ScheduledEvent &scheduled : scheduled_) {
            if (scheduled.active && scheduled.event.kind == TransportEventKind::PaymentCompleted &&
                scheduled.event.transactionId == paymentCashTransactionId_) {
                return scheduled.event.requestId == command.requestId;
            }
        }
        TransportEvent &completed = scratch_[0];
        completed = TransportEvent{};
        completed.kind = TransportEventKind::PaymentCompleted;
        completed.requestId = command.requestId;
        completed.stateVersion = kAuthoritativeStateVersion;
        completed.transactionId = paymentCashTransactionId_;
        completed.amount = 175;
        completed.cash = kAuthoritativeCash - completed.amount;
        const uint32_t dueMs = nowMs + 120;
        return scheduleBatch(&completed, &dueMs, 1);
    }

    TransportEvent &event = scratch_[0];
    event = TransportEvent{};
    event.requestId = command.requestId;
    event.stateVersion = kAuthoritativeStateVersion;
    event.transactionId = command.transactionId == 0 ? nextTransactionId_ : command.transactionId;

    const bool tradeCommand = command.kind == TransportCommandKind::TradeQuery ||
        command.kind == TransportCommandKind::TradeCreate ||
        command.kind == TransportCommandKind::TradeUpdate ||
        command.kind == TransportCommandKind::TradeConfirm ||
        command.kind == TransportCommandKind::TradeReject ||
        command.kind == TransportCommandKind::TradeCancel;
    if (tradeCommand) {
        event.kind = TransportEventKind::TradeResponseReceived;
        event.tradeOperation = command.tradeOperation;
        event.tradeResult = TransportTradeResult::Ok;
        event.tradeCounterpartyId = tradeCounterpartyId_ == 0
            ? command.targetPlayerId : tradeCounterpartyId_;

        if (command.kind == TransportCommandKind::TradeCreate ||
            command.kind == TransportCommandKind::TradeUpdate) {
            tradeActive_ = true;
            if (tradeId_ == 0) tradeId_ = nextTransactionId_++;
            tradeRevision_ = command.kind == TransportCommandKind::TradeCreate
                ? 1 : static_cast<uint16_t>(command.tradeRevision + 1);
            tradeCounterpartyId_ = command.targetPlayerId;
            tradeSelfAssetMask_ = command.assetMask;
            tradeCounterpartyAssetMask_ = command.counterpartyAssetMask;
            tradeSelfCash_ = command.argument;
            tradeCounterpartyCash_ = command.counterpartyArgument;
            event.tradeStatus = command.kind == TransportCommandKind::TradeCreate
                ? TransportTradeStatus::Offered : TransportTradeStatus::Countered;
            event.tradeFlags = kTradeFlagSelfConfirmed | kTradeFlagSelfLastEdited;
            if (command.kind == TransportCommandKind::TradeCreate) {
                event.tradeFlags |= kTradeFlagSelfOriginated;
            }
        } else if (command.kind == TransportCommandKind::TradeQuery) {
            if (!tradeActive_) {
                event.tradeResult = TransportTradeResult::NoActiveTrade;
                event.tradeStatus = TransportTradeStatus::None;
            } else {
                event.tradeStatus = TransportTradeStatus::Offered;
                event.tradeFlags = kTradeFlagSelfConfirmed | kTradeFlagSelfLastEdited;
            }
        } else {
            event.tradeStatus = command.kind == TransportCommandKind::TradeConfirm
                ? TransportTradeStatus::Settled
                : command.kind == TransportCommandKind::TradeReject
                    ? TransportTradeStatus::Rejected
                    : TransportTradeStatus::Cancelled;
            event.tradeFlags = kTradeFlagTerminal;
            tradeActive_ = false;
        }

        event.tradeId = tradeId_;
        event.tradeRevision = tradeRevision_;
        event.tradeExpiresInMs = tradeActive_ ? 120000 : 0;
        event.assetMask = tradeSelfAssetMask_;
        event.counterpartyAssetMask = tradeCounterpartyAssetMask_;
        event.tradeSelfGivesCash = tradeSelfCash_;
        event.tradeCounterpartyGivesCash = tradeCounterpartyCash_;
    } else if (command.kind == TransportCommandKind::ClaimRent ||
        command.kind == TransportCommandKind::AuctionReadyRequest ||
        command.kind == TransportCommandKind::CardContinueRequest) {
        event.kind = TransportEventKind::CommandCompleted;
    } else if (scenario_ == DemoScenario::PaymentDebt && command.kind == TransportCommandKind::PayNow) {
        TransportEvent *events = scratch_;
        events[0] = TransportEvent{};
        events[1] = TransportEvent{};
        uint32_t dueMs[2]{};
        events[0] = event;
        events[0].kind = TransportEventKind::CommandRejected;
        events[0].error = TransportError::InsufficientCash;
        events[1] = events[0];
        events[1].kind = TransportEventKind::DebtResolutionRequired;
        events[1].amount = 240;
        events[1].cash = 80;
        events[1].assetMask = kEligibleMortgageAssets;
        dueMs[0] = nowMs + 120;
        dueMs[1] = nowMs + 121;
        if (!scheduleBatch(events, dueMs, 2)) return false;
        if (command.transactionId == 0) ++nextTransactionId_;
        return true;
    } else if (scenario_ == DemoScenario::DebtMortgage &&
               command.kind == TransportCommandKind::MortgageBatchRequest) {
        event.kind = TransportEventKind::MortgageBatchCompleted;
        event.amount = 575;
        event.cash = 655;
        event.assetMask = command.assetMask;
    } else if (scenario_ == DemoScenario::DebtBankruptcy &&
               command.kind == TransportCommandKind::MortgageBatchRequest) {
        event.kind = TransportEventKind::BankruptcyResolved;
        event.amount = 240;
        event.cash = 0;
    } else if (command.kind == TransportCommandKind::MoveManualConfirmRequest) {
        event.kind = TransportEventKind::RfidPositionConfirmed;
        event.targetPosition = command.targetPosition;
        event.observedPosition = command.targetPosition;
        event.manual = true;
    } else {
        event.kind = TransportEventKind::CommandRejected;
        event.error = TransportError::ActionNotAllowed;
    }

    const uint32_t dueMs = nowMs +
        ((scenario_ == DemoScenario::DebtMortgage || scenario_ == DemoScenario::DebtBankruptcy) ? 150 : 120);
    if (!scheduleBatch(&event, &dueMs, 1)) return false;
    if (command.transactionId == 0) ++nextTransactionId_;
    return true;
}
