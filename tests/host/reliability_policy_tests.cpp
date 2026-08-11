#include "../../Firmware/TestGameServer/src/ReliabilityPolicy.h"

#include <cassert>
#include <iostream>

using namespace gridopoly::protocol;
using namespace gridopoly::server;

int main() {
  assert(classifyInbound(MessageType::ActionRequest, 11, 10, false, 0) ==
         InboundDisposition::Accept);
  assert(classifyInbound(MessageType::ActionRequest, 10, 10, true, 10) ==
         InboundDisposition::ReplayCachedAction);
  assert(classifyInbound(MessageType::ActionRequest, 9, 10, true, 10) ==
         InboundDisposition::Resync);
  assert(classifyInbound(MessageType::Heartbeat, 10, 10, true, 10) ==
         InboundDisposition::Resync);
  assert(classifyInbound(MessageType::Heartbeat, 0, 10, false, 0) ==
         InboundDisposition::Resync);
  assert(classifyInbound(MessageType::Heartbeat, 1, 0xFFFFFFFFu, false, 0) ==
         InboundDisposition::Accept);

  assert(classifyPlayerDetailInbound(11, 10, 100, 2, 20, false, 0, 0, 0) ==
         PlayerDetailDisposition::Generate);
  assert(classifyPlayerDetailInbound(11, 10, 100, 2, 20, true, 100, 2, 20) ==
         PlayerDetailDisposition::ReplayCached);
  // A lost response may be retried after newer Heartbeats; requestId is the
  // idempotency key and still replays the original projection.
  assert(classifyPlayerDetailInbound(8, 12, 100, 2, 20, true, 100, 2, 20) ==
         PlayerDetailDisposition::ReplayCached);
  assert(classifyPlayerDetailInbound(11, 10, 100, 3, 20, true, 100, 2, 20) ==
         PlayerDetailDisposition::RejectRequestIdCollision);
  assert(classifyPlayerDetailInbound(11, 10, 100, 2, 21, true, 100, 2, 20) ==
         PlayerDetailDisposition::RejectRequestIdCollision);
  assert(classifyPlayerDetailInbound(10, 10, 101, 2, 20, true, 100, 2, 20) ==
         PlayerDetailDisposition::Resync);

  assert(!heartbeatRequestsFullResync(20, 20, 30, 30, 0));
  assert(heartbeatRequestsFullResync(19, 20, 29, 30, 0));
  assert(heartbeatRequestsFullResync(21, 20, 30, 30, 0));
  assert(heartbeatRequestsFullResync(20, 20, 31, 30, 0));
  assert(heartbeatRequestsFullResync(20, 20, 30, 30, 1));

  const auto synchronizedHeartbeat = planHeartbeatResponse(true, 20, 20, 30, 30, 0);
  assert(synchronizedHeartbeat.sendAck);
  assert(!synchronizedHeartbeat.requestSync);
  assert(!synchronizedHeartbeat.fullResync);

  const auto eventCatchupHeartbeat = planHeartbeatResponse(true, 20, 20, 29, 30, 0);
  assert(eventCatchupHeartbeat.sendAck);
  assert(eventCatchupHeartbeat.requestSync);
  assert(!eventCatchupHeartbeat.fullResync);

  const auto resyncHeartbeat = planHeartbeatResponse(true, 19, 20, 29, 30, 1);
  assert(resyncHeartbeat.sendAck);
  assert(resyncHeartbeat.requestSync);
  assert(resyncHeartbeat.fullResync);

  const auto legacyHeartbeat = planHeartbeatResponse(false, 0, 20, 0, 30, 0);
  assert(legacyHeartbeat.sendAck);
  assert(legacyHeartbeat.requestSync);
  assert(!legacyHeartbeat.fullResync);

  const auto identityHeartbeat = planIdentityHeartbeatResponse(20, 20, 0);
  assert(identityHeartbeat.sendAck);
  assert(!identityHeartbeat.requestSync);
  assert(!identityHeartbeat.fullResync);
  const auto incompleteIdentityHeartbeat = planIdentityHeartbeatResponse(20, 20, 1);
  assert(incompleteIdentityHeartbeat.sendAck);
  assert(incompleteIdentityHeartbeat.requestSync);
  assert(incompleteIdentityHeartbeat.fullResync);
  const auto staleIdentityHeartbeat = planIdentityHeartbeatResponse(19, 20, 0);
  assert(staleIdentityHeartbeat.sendAck);
  assert(staleIdentityHeartbeat.requestSync);
  assert(staleIdentityHeartbeat.fullResync);

  assert(isPriorityResponse(MessageType::PairAccept));
  assert(isPriorityResponse(MessageType::ActionResult));
  assert(isPriorityResponse(MessageType::PlayerDetailResponse));
  assert(isPriorityResponse(MessageType::TradeResponse));
  assert(isPriorityResponse(MessageType::IdentitySnapshot));
  assert(isPriorityResponse(MessageType::Ack));
  assert(!isPriorityResponse(MessageType::StateSnapshot));
  assert(!isPriorityResponse(MessageType::Discover));
  assert(transmissionAttemptLimit(MessageType::Ack) == 3);
  assert(transmissionAttemptLimit(MessageType::StateSnapshot) == 2);
  assert(transmissionAttemptLimit(MessageType::Discover) == 1);

  assert(!shouldRetryPairAccept(false, 1900, 1, 1000, 1249));
  assert(shouldRetryPairAccept(false, 1900, 1, 1000, 1250));
  assert(!shouldRetryPairAccept(false, 1900, 3, 1000, 1500));
  assert(!shouldRetryPairAccept(true, 1900, 1, 1000, 1500));
  assert(!shouldRetryPairAccept(false, 1900, 1, 1000, 1900));

  std::cout << "GRIDOPOLY_RELIABILITY_POLICY_TESTS_PASS\n";
  return 0;
}
