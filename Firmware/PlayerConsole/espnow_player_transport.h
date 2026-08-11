#pragma once

#include <Arduino.h>
#include <array>
#include <esp_now.h>
#include <GridopolyProtocol.h>

#include "player_console_transport.h"
#include "transport_event_cursor.h"

class EspNowPlayerTransport : public PlayerConsoleTransport {
public:
    void begin(uint32_t nowMs) override;
    bool send(const TransportCommand &command, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;
    bool poll(TransportEvent &event) override;

protected:
    enum class LinkState : uint8_t { Scanning, Pairing, AwaitSnapshot, Online };
    enum class TxKind : uint8_t {
        Unknown, PairRequest, Heartbeat, Action, Query, Trade, Identity
    };

    struct RxItem {
        std::array<uint8_t, 6> mac{};
        uint16_t length = 0;
        std::array<uint8_t, gridopoly::protocol::kMaxFrameSize> bytes{};
    };

    struct PendingAction {
        bool active = false;
        bool awaitingSnapshot = false;
        bool resyncRequested = false;
        TransportCommand command{};
        gridopoly::protocol::ActionCode action = gridopoly::protocol::ActionCode::Roll;
        uint32_t wireSequence = 0;
        uint32_t resultStateVersion = 0;
        uint32_t startedMs = 0;
        uint32_t lastSendMs = 0;
        uint32_t remainingAssetMask = 0;
        uint32_t completedAssetMask = 0;
        uint32_t currentAssetMask = 0;
        uint16_t frameLength = 0;
        uint8_t sendAttempts = 0;
        uint8_t previousPosition = 0;
        std::array<uint8_t, gridopoly::protocol::kMaxFrameSize> frame{};
    };

    struct PendingPlayerDetailQuery {
        bool active = false;
        TransportCommand command{};
        uint32_t wireSequence = 0;
        uint32_t startedMs = 0;
        uint32_t lastSendMs = 0;
        uint16_t frameLength = 0;
        uint8_t sendAttempts = 0;
        std::array<uint8_t, gridopoly::protocol::kMaxFrameSize> frame{};
    };

    struct PendingTradeRequest {
        bool active = false;
        TransportCommand command{};
        uint32_t wireSequence = 0;
        uint32_t startedMs = 0;
        uint32_t lastSendMs = 0;
        uint8_t sendAttempts = 0;
    };

    struct PendingIdentityRequest {
        bool active = false;
        TransportCommand command{};
        uint32_t wireSequence = 0;
        uint32_t startedMs = 0;
        uint32_t lastSendMs = 0;
        uint8_t sendAttempts = 0;
    };

    struct TxResult {
        TxKind kind = TxKind::Unknown;
        bool success = false;
        std::array<uint8_t, 6> mac{};
    };

    static constexpr uint8_t kRxCapacity = 8;
    // One full sync can contain four projections plus all 32 retained game
    // events. Keep enough headroom for a pending-card replay and link events.
    static constexpr uint8_t kEventCapacity = 48;
    static constexpr uint8_t kTxCapacity = 8;
    static_assert(kEventCapacity >= 40,
                  "full projection plus retained event history must fit");
    static EspNowPlayerTransport *instance_;

    std::array<RxItem, kRxCapacity> rxQueue_{};
    volatile uint8_t rxRead_ = 0;
    volatile uint8_t rxWrite_ = 0;
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
    std::array<TxKind, kTxCapacity> txPending_{};
    volatile uint8_t txPendingRead_ = 0;
    volatile uint8_t txPendingWrite_ = 0;
    std::array<TxResult, kTxCapacity> txResults_{};
    volatile uint8_t txResultRead_ = 0;
    volatile uint8_t txResultWrite_ = 0;
    portMUX_TYPE txMux_ = portMUX_INITIALIZER_UNLOCKED;
    std::array<TransportEvent, kEventCapacity> events_{};
    uint8_t eventHead_ = 0;
    uint8_t eventTail_ = 0;
    uint8_t eventCount_ = 0;
    std::array<uint8_t, 6> serverMac_{};
    std::array<uint8_t, 32> pskHash_{};
    gridopoly::protocol::StateSnapshot snapshot_{};
    gridopoly::protocol::AuthoritySnapshot authoritySnapshot_{};
    gridopoly::protocol::RosterSnapshot rosterSnapshot_{};
    PendingAction pending_{};
    PendingPlayerDetailQuery pendingPlayerDetail_{};
    PendingTradeRequest pendingTrade_{};
    PendingIdentityRequest pendingIdentity_{};
    TransportPlayerDetailPayload playerDetailPayload_{};
    TransportIdentityPayload identityPayload_{};
    LinkState linkState_ = LinkState::Scanning;
    uint32_t deviceId_ = 0;
    uint32_t deviceNonce_ = 0;
    uint32_t serverDeviceId_ = 0;
    uint32_t roomId_ = 0;
    uint32_t pendingRoomId_ = 0;
    uint32_t nextSequence_ = 1;
    uint32_t lastServerFrameMs_ = 0;
    uint32_t lastHeartbeatMs_ = 0;
    uint32_t lastScanStepMs_ = 0;
    uint32_t pairStartedMs_ = 0;
    uint32_t appliedStateVersion_ = 0;
    TransportEventCursor eventCursor_{};
    uint32_t privateCardDrawSequence_ = 0;
    uint32_t heartbeatEnqueueOk_ = 0;
    uint32_t heartbeatEnqueueFail_ = 0;
    uint32_t heartbeatDeliveryOk_ = 0;
    uint32_t heartbeatDeliveryFail_ = 0;
    uint32_t lmkTag_ = 0;
    uint8_t channel_ = 1;
    uint8_t seatId_ = 0;
    bool snapshotValid_ = false;
    bool authoritySnapshotValid_ = false;
    bool rosterSnapshotValid_ = false;
    bool identitySnapshotValid_ = false;
    bool identitySetupActive_ = false;
    bool eventGapDetected_ = false;
    bool lossReported_ = false;
    bool ready_ = false;

    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length);
    static void onSend(const esp_now_send_info_t *info, esp_now_send_status_t status);
    void enqueue(const uint8_t *mac, const uint8_t *data, int length);
    bool dequeue(RxItem &item);
    bool push(const TransportEvent &event);
    void process(const RxItem &item, uint32_t nowMs);
    void processDiscover(const RxItem &item, const gridopoly::protocol::DecodedFrame &frame,
                         uint32_t nowMs);
    void processPairAccept(const RxItem &item, const gridopoly::protocol::DecodedFrame &frame,
                           uint32_t nowMs);
    void processSnapshot(const gridopoly::protocol::DecodedFrame &frame, uint32_t nowMs);
    void processAuthoritySnapshot(const gridopoly::protocol::DecodedFrame &frame);
    void processRosterSnapshot(const gridopoly::protocol::DecodedFrame &frame);
    void processGameEventBatch(const gridopoly::protocol::DecodedFrame &frame);
    void processPlayerCardEvent(const gridopoly::protocol::DecodedFrame &frame);
    void processActionResult(const gridopoly::protocol::DecodedFrame &frame);
    void processPlayerDetailResponse(const gridopoly::protocol::DecodedFrame &frame);
    void processTradeResponse(const gridopoly::protocol::DecodedFrame &frame);
    void processIdentitySnapshot(const gridopoly::protocol::DecodedFrame &frame,
                                 uint32_t nowMs);
    bool queuePrivateCardSkipBefore(uint32_t nextSequence, uint32_t stateVersion);
    void clearProjection(bool clearQueuedEvents);
    void resetSession(uint32_t nowMs);
    bool setChannel(uint8_t channel);
    bool addServerPeer(bool encrypted);
    virtual bool sendFrame(TxKind kind, const uint8_t *data, size_t length);
    void recordSendResult(const uint8_t *mac, esp_now_send_status_t status);
    void drainSendResults();
    void logPeerState(const char *reason) const;
    bool sendPairRequest(uint32_t nowMs);
    bool sendHeartbeat();
    bool sendAction(gridopoly::protocol::ActionCode action, uint8_t assetIndex,
                    int32_t argument, uint32_t stateVersion);
    bool beginPlayerDetailQuery(const TransportCommand &command, uint32_t nowMs);
    bool resendPlayerDetailQuery(uint32_t nowMs);
    void rejectPlayerDetailQuery(TransportError error);
    bool beginTradeRequest(const TransportCommand &command, uint32_t nowMs);
    bool sendTradeRequest(uint32_t nowMs);
    void rejectTradeRequest(TransportError error);
    bool beginIdentityRequest(const TransportCommand &command, uint32_t nowMs);
    bool sendIdentityRequest(uint32_t nowMs);
    void rejectIdentityRequest(TransportError error);
    bool beginPendingAction(const TransportCommand &command, uint32_t nowMs);
    bool resendPendingAction(uint32_t nowMs);
    bool sendNextMortgage();
    void completePendingFromSnapshot();
    void rejectPending(TransportError error);
};
