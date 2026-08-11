#pragma once

#include <WiFiUdp.h>
#include <array>
#include <gridopoly/protocol/UdpEnvelope.h>

#include "espnow_player_transport.h"

class WifiUdpPlayerTransport final : public EspNowPlayerTransport {
public:
    void begin(uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;

protected:
    bool sendFrame(TxKind kind, const uint8_t *data, size_t length) override;

private:
    static constexpr uint32_t kWifiRecoveryMs = 30000;
    static constexpr uint32_t kLinkDegradedAfterMs = 9000;
    static constexpr uint32_t kSessionResetAfterMs = 15000;

    WiFiUDP udp_{};
    IPAddress serverAddress_{10, 42, 0, 1};
    std::array<uint8_t, gridopoly::protocol::kUdpKeySize> pairKey_{};
    std::array<uint8_t, gridopoly::protocol::kUdpKeySize> sessionKey_{};
    gridopoly::protocol::UdpReplayWindow sessionReplay_{};
    uint32_t sessionId_ = 0;
    uint64_t nextPacketSequence_ = 1;
    uint32_t lastWifiAttemptMs_ = 0;
    bool udpStarted_ = false;

    void beginWifi(uint32_t nowMs);
    void recoverWifi(uint32_t nowMs);
    bool startUdp();
    void resetUdpSession(uint32_t nowMs);
    void pollDatagrams(uint32_t nowMs);
    void processDatagram(const uint8_t *bytes, size_t length, const IPAddress &remote,
                         uint16_t remotePort, uint32_t nowMs);
    void processUdpFrame(const gridopoly::protocol::DecodedFrame &frame, uint32_t nowMs);
    void processUdpDiscover(const gridopoly::protocol::DecodedFrame &frame, uint32_t nowMs);
    void processUdpPairAccept(const gridopoly::protocol::DecodedUdpDatagram &datagram,
                              const gridopoly::protocol::DecodedFrame &frame, uint32_t nowMs);
};
