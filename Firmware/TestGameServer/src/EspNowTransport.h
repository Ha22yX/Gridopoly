#pragma once

#include <Arduino.h>
#include <array>
#include <esp_now.h>
#include <gridopoly/core/GameModel.h>
#include <gridopoly/protocol/Protocol.h>

namespace gridopoly::server {

class ServerApp;

class EspNowTransport {
 public:
  struct Diagnostics {
    std::uint32_t rxFrames{};
    std::uint32_t txFrames{};
    std::uint32_t txAttempts{};
    std::uint32_t rxDropped{};
    std::uint32_t txQueueFailures{};
    std::uint32_t txNoMemoryFailures{};
    std::uint32_t txOtherImmediateFailures{};
    std::uint32_t txDeliveryFailures{};
    std::uint32_t txDeliveryRetries{};
    std::uint32_t discoverDeliveryFailures{};
    std::uint8_t priorityQueueDepth{};
    std::uint8_t normalQueueDepth{};
    bool txInFlight{};
    std::uint32_t pairRequests{};
    std::uint32_t reconnects{};
    std::uint32_t disconnects{};
    std::uint32_t duplicateActionReplays{};
    std::uint32_t fullResyncRequests{};
    std::uint32_t validRxFrames{};
    std::uint32_t heartbeatRx{};
    std::uint32_t heartbeatAcks{};
    std::uint32_t heartbeatAckFailures{};
    std::uint32_t heartbeatAckDeliveries{};
    std::uint32_t heartbeatAckDeliveryFailures{};
    std::uint32_t pairAccepts{};
    std::uint32_t pairAcceptDeliveries{};
    std::uint32_t pairAcceptDeliveryFailures{};
    std::uint32_t maxPeerSilenceMs{};
  };

  explicit EspNowTransport(ServerApp& app);
  bool begin(const char* testPsk);
  void loop();
  void notifyRoomChanged();
  void notifyNetworkRecovered();
  std::uint8_t peerCount() const;
  Diagnostics diagnostics() const;
  std::uint32_t serverDeviceId() const { return serverDeviceId_; }

 private:
  struct Peer {
    bool active{};
    std::array<std::uint8_t, 6> mac{};
    std::uint32_t deviceId{};
    std::uint32_t nonce{};
    std::uint8_t seatId{};
    bool encrypted{};
    std::uint32_t lastSeenMs{};
    std::uint32_t promoteEncryptionAtMs{};
    std::uint32_t lastPairAcceptAtMs{};
    std::uint32_t pairRequestSequence{};
    std::uint32_t disconnectedAtMs{};
    std::uint32_t lastSnapshotVersion{};
    std::uint32_t lastAuthorityVersion{};
    std::uint32_t lastEventSequence{};
    std::uint32_t lastRxSequence{};
    std::uint32_t nextSequence{1};
    std::uint32_t cachedActionSequence{};
    std::array<std::uint8_t, 12> cachedActionResult{};
    std::uint32_t cachedPlayerDetailRequestId{};
    std::uint32_t cachedPlayerDetailExpectedVersion{};
    std::uint16_t cachedPlayerDetailLength{};
    std::uint8_t cachedPlayerDetailTargetId{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxPlayerDetailResponseSize>
        cachedPlayerDetailResponse{};
    std::uint8_t syncStage{};
    bool syncResync{};
    bool hasCachedActionResult{};
    bool hasCachedPlayerDetailResponse{};
    std::uint8_t pairAcceptAttempts{};
  };

  struct RxItem {
    std::array<std::uint8_t, 6> mac{};
    std::uint16_t length{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxFrameSize> bytes{};
  };

  struct TxItem {
    std::array<std::uint8_t, 6> mac{};
    std::uint16_t length{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxFrameSize> bytes{};
    gridopoly::protocol::MessageType type{gridopoly::protocol::MessageType::Discover};
    std::uint8_t attempts{};
    std::uint8_t attemptLimit{1};
  };

  enum class TxLane : std::uint8_t { None, Priority, Normal };

  ServerApp& app_;
  std::array<Peer, 6> peers_{};
  std::array<RxItem, 8> rxQueue_{};
  volatile std::uint8_t rxRead_{};
  volatile std::uint8_t rxWrite_{};
  volatile std::uint32_t rxFrames_{};
  volatile std::uint32_t rxDropped_{};
  volatile std::uint32_t txDeliveryFailures_{};
  volatile std::uint32_t discoverDeliveryFailures_{};
  std::array<TxItem, 16> priorityTxQueue_{};
  std::array<TxItem, 32> normalTxQueue_{};
  volatile std::uint8_t priorityTxRead_{};
  volatile std::uint8_t priorityTxWrite_{};
  volatile std::uint8_t normalTxRead_{};
  volatile std::uint8_t normalTxWrite_{};
  volatile bool txInFlight_{};
  volatile TxLane txActiveLane_{TxLane::None};
  mutable portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::array<std::uint8_t, 32> pskHash_{};
  std::uint32_t serverDeviceId_{};
  std::uint32_t lastDiscoverAt_{};
  std::uint32_t lastSnapshotScanAt_{};
  std::uint32_t lastSyncSendAt_{};
  std::uint8_t nextSyncPeer_{};
  std::uint32_t txFrames_{};
  std::uint32_t txAttempts_{};
  std::uint32_t txQueueFailures_{};
  std::uint32_t txNoMemoryFailures_{};
  std::uint32_t txOtherImmediateFailures_{};
  std::uint32_t txDeliveryRetries_{};
  std::uint32_t pairRequests_{};
  std::uint32_t reconnects_{};
  std::uint32_t disconnects_{};
  std::uint32_t duplicateActionReplays_{};
  std::uint32_t fullResyncRequests_{};
  std::uint32_t validRxFrames_{};
  std::uint32_t heartbeatRx_{};
  std::uint32_t heartbeatAcks_{};
  std::uint32_t heartbeatAckFailures_{};
  volatile std::uint32_t heartbeatAckDeliveries_{};
  volatile std::uint32_t heartbeatAckDeliveryFailures_{};
  std::uint32_t pairAccepts_{};
  volatile std::uint32_t pairAcceptDeliveries_{};
  volatile std::uint32_t pairAcceptDeliveryFailures_{};
  bool ready_{};

  static EspNowTransport* instance_;
  static void onReceive(const esp_now_recv_info_t* info, const std::uint8_t* data, int length);
  static void onSend(const esp_now_send_info_t* info, esp_now_send_status_t status);
  void enqueue(const std::uint8_t* mac, const std::uint8_t* data, int length);
  bool enqueueTx(const std::uint8_t* mac, gridopoly::protocol::MessageType type,
                 const std::uint8_t* data, std::size_t length);
  void serviceTx();
  void completeTx(bool delivered);
  bool dequeue(RxItem& item);
  void process(const RxItem& item);
  void processPair(const std::uint8_t* mac, const gridopoly::protocol::DecodedFrame& frame);
  void processAction(Peer& peer, const gridopoly::protocol::DecodedFrame& frame);
  void processPlayerDetail(Peer& peer, const gridopoly::protocol::DecodedFrame& frame);
  bool sendPairAccept(Peer& peer);
  bool sendCachedActionResult(Peer& peer);
  bool sendCachedPlayerDetail(Peer& peer, std::uint32_t acknowledgement);
  bool sendHeartbeatAck(Peer& peer, std::uint32_t acknowledgement);
  Peer* findPeer(const std::uint8_t* mac);
  Peer* allocatePeer(const std::uint8_t* mac, std::uint32_t deviceId, std::uint32_t nonce);
  std::uint8_t nextFreeSeat() const;
  bool addPlainPeer(Peer& peer);
  bool addEncryptedPeer(Peer& peer);
  void deriveLmk(const Peer& peer, std::uint8_t output[16]) const;
  void broadcastDiscover();
  void requestSync(Peer& peer, bool resync);
  void serviceSync(std::uint32_t now);
  bool sendSnapshot(Peer& peer, bool resync = false);
  bool sendAuthoritySnapshot(Peer& peer, bool resync);
  bool sendRosterSnapshot(Peer& peer, bool resync);
  bool sendPendingCard(Peer& peer, bool resync);
  bool sendCardEvent(Peer& peer, const gridopoly::core::GameEvent& event, bool resync);
  bool sendEventBatch(Peer& peer, bool resync, bool& complete);
  bool sendFrame(const std::uint8_t* mac, gridopoly::protocol::Header header,
                 const std::uint8_t* payload, std::size_t payloadLength);
};

}  // namespace gridopoly::server
