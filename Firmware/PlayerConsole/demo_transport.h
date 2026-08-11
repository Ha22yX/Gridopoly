#pragma once

#include "player_console_transport.h"

enum class DemoScenario : uint8_t {
    Waiting,
    NextTurn,
    MyTurn,
    RentClaim,
    PaymentCash,
    PaymentDebt,
    DebtMortgage,
    DebtBankruptcy,
    RollRfidSuccess,
    RollRfidWrongThenSuccess,
    RollRfidTimeout,
    RollAuthorityDelayed,
    ConnectionDropAndRecover,
};

class DemoTransport : public PlayerConsoleTransport {
public:
    void begin(uint32_t nowMs) override;
    bool send(const TransportCommand &command, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;
    bool poll(TransportEvent &event) override;

    void setScenario(DemoScenario scenario);

private:
    static constexpr uint8_t kQueueCapacity = 8;

    struct ScheduledEvent {
        TransportEvent event{};
        uint32_t dueMs = 0;
        uint32_t sequence = 0;
        bool active = false;
    };

    bool scheduleBatch(const TransportEvent *events, const uint32_t *dueMs, uint8_t count);
    bool push(const TransportEvent &event);
    bool hasScheduledTransaction(uint32_t transactionId) const;
    bool scheduleRoll(const TransportCommand &command, uint32_t nowMs);
    bool scheduleScenarioCommand(const TransportCommand &command, uint32_t nowMs);

    DemoScenario scenario_ = DemoScenario::Waiting;
    ScheduledEvent scheduled_[kQueueCapacity]{};
    TransportEvent output_[kQueueCapacity]{};
    TransportEvent scratch_[4]{};
    uint8_t outputHead_ = 0;
    uint8_t outputTail_ = 0;
    uint8_t outputCount_ = 0;
    uint32_t nextScheduleSequence_ = 0;
    uint32_t nextTransactionId_ = 1;
    uint32_t activeRollRequestId_ = 0;
    uint32_t activeRollTransactionId_ = 0;
    uint32_t lastRollRequestId_ = 0;
    TransportEvent lastRollResult_{};
    TransportPlayerDetailPayload playerDetailPayload_{};
    bool lastRollResultValid_ = false;
    bool lastRollResultEmitted_ = false;
    uint32_t paymentCashDeadlineMs_ = 0;
    uint32_t paymentCashTransactionId_ = 0;
    bool paymentCashActive_ = false;
    bool paymentCashCompleted_ = false;
    bool tradeActive_ = false;
    uint32_t tradeId_ = 0;
    uint16_t tradeRevision_ = 0;
    uint8_t tradeCounterpartyId_ = 0;
    uint32_t tradeSelfAssetMask_ = 0;
    uint32_t tradeCounterpartyAssetMask_ = 0;
    int32_t tradeSelfCash_ = 0;
    int32_t tradeCounterpartyCash_ = 0;
    bool activeRoll_ = false;
    bool offline_ = false;
};
