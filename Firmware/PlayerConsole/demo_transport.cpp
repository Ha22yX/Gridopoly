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
    nextTransactionId_ = 1;
    activeRollRequestId_ = 0;
    activeRollTransactionId_ = 0;
    lastRollRequestId_ = 0;
    lastRollResult_ = TransportEvent{};
    activeRoll_ = false;
    offline_ = scenario_ == DemoScenario::ConnectionDropAndRecover;

    if (!offline_) return;

    TransportEvent lost{};
    lost.kind = TransportEventKind::ConnectionLost;
    schedule(lost, nowMs);

    TransportEvent snapshot{};
    snapshot.kind = TransportEventKind::StateSnapshotApplied;
    snapshot.stateVersion = kAuthoritativeStateVersion;
    snapshot.cash = kAuthoritativeCash;
    snapshot.playerPosition = kAuthoritativePosition;
    schedule(snapshot, nowMs + 3000);
}

bool DemoTransport::send(const TransportCommand &command, uint32_t nowMs)
{
    if (offline_) return false;

    if (command.kind != TransportCommandKind::RollRequest) {
        scheduleScenarioCommand(command, nowMs);
        return true;
    }

    if (activeRoll_ && command.requestId == activeRollRequestId_) return true;
    if (command.requestId == lastRollRequestId_) return push(lastRollResult_);

    if (activeRoll_) {
        TransportEvent rejected{};
        rejected.kind = TransportEventKind::CommandRejected;
        rejected.error = TransportError::ActionNotAllowed;
        rejected.requestId = command.requestId;
        rejected.stateVersion = kAuthoritativeStateVersion;
        return push(rejected);
    }

    scheduleRoll(command, nowMs);
    return true;
}

void DemoTransport::tick(uint32_t nowMs)
{
    for (ScheduledEvent &scheduled : scheduled_) {
        if (!scheduled.active || !isDue(nowMs, scheduled.dueMs)) continue;
        if (!push(scheduled.event)) return;

        const TransportEvent event = scheduled.event;
        scheduled = ScheduledEvent{};
        if (event.kind == TransportEventKind::StateSnapshotApplied) offline_ = false;
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

bool DemoTransport::schedule(const TransportEvent &event, uint32_t dueMs)
{
    for (ScheduledEvent &scheduled : scheduled_) {
        if (scheduled.active) continue;
        scheduled.event = event;
        scheduled.dueMs = dueMs;
        scheduled.active = true;
        return true;
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

void DemoTransport::scheduleRoll(const TransportCommand &command, uint32_t nowMs)
{
    const uint32_t transactionId = nextTransactionId_++;
    activeRoll_ = true;
    activeRollRequestId_ = command.requestId;
    activeRollTransactionId_ = transactionId;

    TransportEvent result{};
    result.kind = TransportEventKind::RollResult;
    result.requestId = command.requestId;
    result.stateVersion = kAuthoritativeStateVersion;
    result.transactionId = transactionId;
    result.dieA = 3;
    result.dieB = 4;
    result.playerPosition = kAuthoritativePosition;
    lastRollRequestId_ = command.requestId;
    lastRollResult_ = result;

    const uint32_t resultDelay = scenario_ == DemoScenario::RollAuthorityDelayed ? 420 : 120;
    schedule(result, nowMs + resultDelay);

    TransportEvent guidance{};
    guidance.kind = TransportEventKind::MoveGuidanceStarted;
    guidance.requestId = command.requestId;
    guidance.stateVersion = kAuthoritativeStateVersion;
    guidance.transactionId = transactionId;
    guidance.playerPosition = kAuthoritativePosition;
    guidance.targetPosition = kRollTargetPosition;
    guidance.targetName = kRollTargetName;
    guidance.manual = command.targetPosition != 0;

    const uint32_t guidanceDelay = resultDelay + 30;
    schedule(guidance, nowMs + guidanceDelay);

    if (scenario_ == DemoScenario::RollRfidSuccess) {
        TransportEvent confirmed = guidance;
        confirmed.kind = TransportEventKind::RfidPositionConfirmed;
        confirmed.observedPosition = kRollTargetPosition;
        schedule(confirmed, nowMs + guidanceDelay + 200);
    } else if (scenario_ == DemoScenario::RollRfidWrongThenSuccess) {
        TransportEvent rejected = guidance;
        rejected.kind = TransportEventKind::RfidPositionRejected;
        rejected.observedPosition = 23;
        schedule(rejected, nowMs + guidanceDelay + 150);
        TransportEvent confirmed = guidance;
        confirmed.kind = TransportEventKind::RfidPositionConfirmed;
        confirmed.observedPosition = kRollTargetPosition;
        schedule(confirmed, nowMs + guidanceDelay + 300);
    } else if (scenario_ == DemoScenario::RollRfidTimeout) {
        TransportEvent rejected = guidance;
        rejected.kind = TransportEventKind::RfidPositionRejected;
        rejected.deadlineMs = nowMs + guidanceDelay + 3000;
        schedule(rejected, nowMs + guidanceDelay + 3000);
    }
}

void DemoTransport::scheduleScenarioCommand(const TransportCommand &command, uint32_t nowMs)
{
    TransportEvent event{};
    event.requestId = command.requestId;
    event.stateVersion = kAuthoritativeStateVersion;
    event.transactionId = command.transactionId == 0 ? nextTransactionId_++ : command.transactionId;

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

    schedule(event, nowMs + 100);
}
