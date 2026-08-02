#include "demo_transport.h"

namespace {

constexpr uint32_t kAuthoritativeStateVersion = 42;
constexpr int32_t kAuthoritativeCash = 1860;
constexpr uint8_t kAuthoritativePosition = 17;
constexpr uint8_t kRollTargetPosition = 24;
constexpr uint32_t kEligibleMortgageAssets = 0x57;
constexpr char kRollTargetName[] = "霓虹港湾";

bool isDue(uint32_t nowMs, uint32_t dueMs)
{
    return static_cast<int32_t>(nowMs - dueMs) >= 0;
}

} // namespace

void DemoTransport::begin(uint32_t nowMs)
{
    for (ScheduledEvent &scheduled : scheduled_) scheduled = ScheduledEvent{};
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
    activeRoll_ = false;
    offline_ = scenario_ == DemoScenario::ConnectionDropAndRecover;

    if (!offline_) return;

    TransportEvent events[2]{};
    uint32_t dueMs[2]{};
    events[0].kind = TransportEventKind::ConnectionLost;
    dueMs[0] = nowMs;
    events[1].kind = TransportEventKind::StateSnapshotApplied;
    events[1].stateVersion = kAuthoritativeStateVersion;
    events[1].cash = kAuthoritativeCash;
    events[1].playerPosition = kAuthoritativePosition;
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
        TransportEvent rejected{};
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
        const TransportEvent event = scheduled.event;
        scheduled = ScheduledEvent{};
        if (event.kind == TransportEventKind::StateSnapshotApplied) offline_ = false;
        if (event.kind == TransportEventKind::RollResult &&
            event.transactionId == activeRollTransactionId_) {
            lastRollResultEmitted_ = true;
        }
        if (event.transactionId == activeRollTransactionId_ &&
            (event.kind == TransportEventKind::MoveGuidanceStarted ||
             event.kind == TransportEventKind::RfidPositionConfirmed ||
             event.kind == TransportEventKind::RfidPositionRejected)) {
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
    TransportEvent events[4]{};
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
    TransportEvent event{};
    event.requestId = command.requestId;
    event.stateVersion = kAuthoritativeStateVersion;
    event.transactionId = command.transactionId == 0 ? nextTransactionId_ : command.transactionId;

    if (scenario_ == DemoScenario::RentClaim && command.kind == TransportCommandKind::ClaimRent) {
        event.kind = TransportEventKind::PaymentRequired;
        event.amount = 120;
        event.cash = kAuthoritativeCash;
        event.deadlineMs = nowMs + 20000;
        event.targetName = kRollTargetName;
    } else if (scenario_ == DemoScenario::PaymentCash && command.kind == TransportCommandKind::PayNow) {
        event.kind = TransportEventKind::PaymentCompleted;
        event.amount = 175;
        event.cash = kAuthoritativeCash - event.amount;
    } else if (scenario_ == DemoScenario::PaymentDebt && command.kind == TransportCommandKind::PayNow) {
        event.kind = TransportEventKind::DebtResolutionRequired;
        event.error = TransportError::InsufficientCash;
        event.amount = 240;
        event.cash = 80;
        event.assetMask = kEligibleMortgageAssets;
    } else if (scenario_ == DemoScenario::DebtMortgage &&
               command.kind == TransportCommandKind::MortgageBatchRequest) {
        event.kind = TransportEventKind::MortgageBatchCompleted;
        event.amount = 575;
        event.cash = 655;
        event.assetMask = command.assetMask & kEligibleMortgageAssets;
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

    const uint32_t dueMs = nowMs + 100;
    if (!scheduleBatch(&event, &dueMs, 1)) return false;
    if (command.transactionId == 0) ++nextTransactionId_;
    return true;
}
