#pragma once

#include <Arduino.h>
#include <array>
#include <esp_now.h>
#include <GridopolyProtocol.h>

#include "player_console_transport.h"

class EspNowPlayerTransport : public PlayerConsoleTransport {
public:
    void begin(uint32_t nowMs) override;
    bool send(const TransportCommand &command, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;
    bool poll(TransportEvent &event) override;

private:
    enum class LinkState : uint8_t { Scanning, Pairing, AwaitSnapshot, Online };

    struct RxItem {
        std::array<uint8_t, 6> mac{};
        uint16_t length = 0;
        std::array<uint8_t, gridopoly::protocol::kMaxFrameSize> bytes{};
    };

    struct PendingAction {
        bool active = false;
        bool awaitingSnapshot = false;
        TransportCommand command{};
        gridopoly::protocol::ActionCode action = gridopoly::protocol::ActionCode::Roll;
        uint32_t wireSequence = 0;
        uint32_t resultStateVersion = 0;
        uint32_t remainingAssetMask = 0;
        uint32_t completedAssetMask = 0;
        uint32_t currentAssetMask = 0;
        uint8_t previousPosition = 0;
    };

    static constexpr uint8_t kRxCapacity = 8;
    static constexpr uint8_t kEventCapacity = 12;
    static EspNowPlayerTransport *instance_;

    std::array<RxItem, kRxCapacity> rxQueue_{};
    volatile uint8_t rxRead_ = 0;
    volatile uint8_t rxWrite_ = 0;
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
    std::array<TransportEvent, kEventCapacity> events_{};
    uint8_t eventHead_ = 0;
    uint8_t eventTail_ = 0;
    uint8_t eventCount_ = 0;
    std::array<uint8_t, 6> serverMac_{};
    std::array<uint8_t, 32> pskHash_{};
    gridopoly::protocol::StateSnapshot snapshot_{};
    PendingAction pending_{};
    LinkState linkState_ = LinkState::Scanning;
    uint32_t deviceId_ = 0;
    uint32_t deviceNonce_ = 0;
    uint32_t serverDeviceId_ = 0;
    uint32_t roomId_ = 0;
    uint32_t nextSequence_ = 1;
    uint32_t lastServerFrameMs_ = 0;
    uint32_t lastHeartbeatMs_ = 0;
    uint32_t lastScanStepMs_ = 0;
    uint32_t pairStartedMs_ = 0;
    uint8_t channel_ = 1;
    uint8_t seatId_ = 0;
    bool snapshotValid_ = false;
    bool lossReported_ = false;
    bool ready_ = false;

    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length);
    void enqueue(const uint8_t *mac, const uint8_t *data, int length);
    bool dequeue(RxItem &item);
    bool push(const TransportEvent &event);
    void process(const RxItem &item, uint32_t nowMs);
    void processDiscover(const RxItem &item, const gridopoly::protocol::DecodedFrame &frame,
                         uint32_t nowMs);
    void processPairAccept(const RxItem &item, const gridopoly::protocol::DecodedFrame &frame,
                           uint32_t nowMs);
    void processSnapshot(const gridopoly::protocol::DecodedFrame &frame, uint32_t nowMs);
    void processActionResult(const gridopoly::protocol::DecodedFrame &frame);
    void resetSession(uint32_t nowMs);
    bool setChannel(uint8_t channel);
    bool addServerPeer(bool encrypted);
    bool sendPairRequest(uint32_t nowMs);
    bool sendHeartbeat();
    bool sendAction(gridopoly::protocol::ActionCode action, uint8_t assetIndex,
                    int32_t argument, uint32_t stateVersion);
    bool beginPendingAction(const TransportCommand &command);
    bool sendNextMortgage();
    void completePendingFromSnapshot();
    void rejectPending(TransportError error);
};
