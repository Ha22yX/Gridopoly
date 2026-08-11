#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <thread>

#include <gridopoly/protocol/Protocol.h>
#include <gridopoly/protocol/UdpEnvelope.h>

#include "AuthorityService.h"

namespace gridopoly::pi {

class UdpPlayerServer {
 public:
  struct Diagnostics {
    std::uint64_t rxDatagrams{};
    std::uint64_t validDatagrams{};
    std::uint64_t authFailures{};
    std::uint64_t replayDrops{};
    std::uint64_t txDatagrams{};
    std::uint64_t txErrors{};
    std::uint64_t pairRequests{};
    std::uint64_t pairAccepts{};
    std::uint64_t heartbeats{};
    std::uint64_t heartbeatAcks{};
    std::uint64_t duplicateActions{};
    std::uint64_t detailRequests{};
    std::uint64_t detailResponses{};
    std::uint64_t detailReplays{};
    std::uint64_t detailErrors{};
    std::uint64_t tradeRequests{};
    std::uint64_t tradeResponses{};
    std::uint64_t tradeReplays{};
    std::uint64_t tradeErrors{};
    std::uint64_t identityRequests{};
    std::uint64_t identityResponses{};
    std::uint64_t identityErrors{};
    std::uint64_t resyncs{};
    std::uint64_t endpointMigrations{};
    std::uint64_t connectedPeerSilenceMs{};
    std::uint64_t maxPeerSilenceMs{};
    std::uint32_t lastDetailRequestId{};
    std::uint32_t lastDetailExpectedVersion{};
    std::uint16_t lastDetailResponseBytes{};
    std::uint8_t lastDetailTargetId{};
    std::uint32_t lastTradeRequestId{};
    std::uint32_t lastTradeId{};
    std::uint16_t lastTradeRevision{};
    std::uint16_t lastTradeResponseBytes{};
    std::uint8_t lastTradeOperation{};
    std::uint8_t lastTradeResult{};
    std::uint32_t lastIdentityRequestId{};
    std::uint32_t lastIdentityRevision{};
    std::uint16_t lastIdentityResponseBytes{};
    std::uint8_t lastIdentityOperation{};
    std::uint8_t lastIdentityResult{};
    std::uint8_t peers{};
  };

  UdpPlayerServer(AuthorityService& authority, std::string psk,
                  std::filesystem::path registryPath,
                  std::string bindAddress = "0.0.0.0",
                  std::string broadcastAddress = "10.42.0.255",
                  std::uint16_t port = gridopoly::protocol::kGridopolyUdpPort);
  ~UdpPlayerServer();

  bool start();
  void stop();
  std::uint16_t port() const { return port_; }
  Diagnostics diagnostics() const;

 private:
  struct RegistryEntry {
    std::uint32_t deviceId{};
    std::uint8_t seatId{};
  };

  struct Session {
    bool active{};
    bool connected{};
    std::uint32_t sessionId{};
    std::uint32_t roomId{};
    std::uint32_t deviceId{};
    std::uint32_t deviceNonce{};
    std::uint8_t seatId{};
    sockaddr_in endpoint{};
    std::array<std::uint8_t, gridopoly::protocol::kUdpKeySize> key{};
    gridopoly::protocol::UdpReplayWindow replay{};
    std::uint64_t nextPacketSequence{1};
    std::uint32_t nextFrameSequence{1};
    std::uint32_t lastRxSequence{};
    std::uint32_t lastEventSequence{};
    std::uint32_t cachedActionSequence{};
    std::array<std::uint8_t, 12> cachedActionResult{};
    std::uint32_t cachedDetailRequestId{};
    std::uint32_t cachedDetailExpectedVersion{};
    std::uint8_t cachedDetailTargetId{};
    std::uint16_t cachedDetailLength{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxPlayerDetailResponseSize> cachedDetail{};
    std::uint32_t cachedTradeRequestId{};
    std::uint16_t cachedTradeRequestLength{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxTradeRequestSize> cachedTradeRequest{};
    std::uint16_t cachedTradeLength{};
    std::array<std::uint8_t, gridopoly::protocol::kMaxTradeResponseSize> cachedTrade{};
    std::uint8_t syncStage{};
    bool syncResync{};
    bool hasCachedAction{};
    bool hasCachedDetail{};
    bool hasCachedTrade{};
    std::chrono::steady_clock::time_point lastSeen{};
    std::chrono::steady_clock::time_point lastSyncSend{};
  };

  AuthorityService& authority_;
  std::string psk_;
  std::filesystem::path registryPath_;
  std::string bindAddress_;
  std::string broadcastAddress_;
  std::uint16_t port_{};
  std::array<std::uint8_t, gridopoly::protocol::kUdpKeySize> pairKey_{};
  std::array<Session, 6> sessions_{};
  std::array<RegistryEntry, 6> registry_{};
  std::thread thread_{};
  std::atomic<bool> running_{};
  int socket_{-1};
  std::uint32_t observedRoomId_{};
  std::uint32_t observedIdentityRevision_{};
  std::uint64_t broadcastPacketSequence_{1};
  std::chrono::steady_clock::time_point lastDiscover_{};
  mutable std::mutex diagnosticsMutex_;
  Diagnostics diagnostics_{};

  void run();
  bool openSocket();
  void closeSocket();
  void receiveOne();
  void processPairing(const std::uint8_t* bytes, std::size_t length,
                      const sockaddr_in& source);
  void processSession(Session& session, const std::uint8_t* bytes, std::size_t length,
                      const sockaddr_in& source);
  void processAction(Session& session, const gridopoly::protocol::DecodedFrame& frame);
  void processPlayerDetail(Session& session, const gridopoly::protocol::DecodedFrame& frame);
  void processTrade(Session& session, const gridopoly::protocol::DecodedFrame& frame);
  void processIdentity(Session& session, const gridopoly::protocol::DecodedFrame& frame);
  void processHeartbeat(Session& session, const gridopoly::protocol::DecodedFrame& frame);
  void maintainSessions();
  void serviceSync();
  void broadcastDiscover();
  void resetForNewRoom(std::uint32_t roomId);

  Session* findSession(std::uint32_t sessionId);
  Session* findSessionByDevice(std::uint32_t deviceId, std::uint32_t nonce);
  Session* allocateSession(std::uint32_t deviceId, std::uint32_t nonce,
                           std::uint8_t seatId, const sockaddr_in& endpoint);
  std::uint8_t seatForDevice(std::uint32_t deviceId);
  bool loadRegistry();
  bool saveRegistry() const;

  bool sendPairAccept(Session& session, std::uint32_t acknowledgement);
  bool sendFrame(Session& session, gridopoly::protocol::MessageType type,
                 std::uint16_t flags, std::uint32_t acknowledgement,
                 const std::uint8_t* payload, std::size_t payloadLength);
  bool sendPairingFrame(const sockaddr_in& endpoint, std::uint32_t sessionId,
                        gridopoly::protocol::Header header,
                        const std::uint8_t* payload, std::size_t payloadLength,
                        bool broadcast);
  bool sendHeartbeatAck(Session& session, std::uint32_t acknowledgement);
  bool sendCachedAction(Session& session);
  bool sendCachedDetail(Session& session, std::uint32_t acknowledgement);
  bool sendCachedTrade(Session& session, std::uint32_t acknowledgement,
                       bool resync = false);
  bool sendTradeResync(Session& session);
  bool sendTradeNotification(Session& session, std::uint32_t tradeId);
  bool sendIdentity(Session& session, bool resync);
  bool sendPendingCard(Session& session, bool resync);
  bool sendSnapshot(Session& session, bool resync);
  bool sendAuthority(Session& session, bool resync);
  bool sendRoster(Session& session, bool resync);
  bool sendEvents(Session& session, bool resync, bool& complete);
  bool sendCardEvent(Session& session, const gridopoly::core::GameEvent& event, bool resync);
  void requestSync(Session& session, bool resync);
  void updatePeerCount();
};

}  // namespace gridopoly::pi
