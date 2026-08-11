#include <gridopoly/protocol/Protocol.h>

#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>

using namespace gridopoly::protocol;

int main() {
  StateSnapshot snapshot{};
  snapshot.seatId = 1;
  snapshot.phase = 2;
  snapshot.activePlayerId = 1;
  snapshot.round = 12;
  snapshot.boardSize = 40;
  snapshot.selfPosition = 23;
  snapshot.selfCash = 1337;
  snapshot.availableActions = 0x9234;  // Includes AuctionReady bit 15.
  snapshot.playerCount = 2;
  snapshot.tileAssetIndex = 15;
  snapshot.tileOwnerId = 1;
  snapshot.tileBuildingLevel = 2;
  snapshot.stateVersion = 99;
  snapshot.decisionPlayerId = 2;
  snapshot.debtCreditorId = 1;
  snapshot.debtAssetIndex = 15;
  snapshot.debtAmount = 750;
  snapshot.auctionAssetIndex = 9;
  snapshot.auctionCurrentBid = 120;
  snapshot.auctionMinimumBid = 130;
  snapshot.auctionHighestBidderId = 2;
  snapshot.players[0] = {1, 23, 1337, 4};
  snapshot.players[1] = {2, 7, 900, 0};

  std::array<std::uint8_t, kMaxPayloadSize> payload{};
  std::size_t payloadLength = 0;
  assert(encodeStateSnapshot(snapshot, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 58);

  Header header{};
  header.type = MessageType::StateSnapshot;
  header.flags = FlagAckRequired;
  header.sequence = 42;
  header.roomId = 7;
  header.deviceId = 11;
  std::array<std::uint8_t, kMaxFrameSize> frame{};
  std::size_t frameLength = 0;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  assert(frameLength <= kMaxFrameSize);

  DecodedFrame decoded{};
  assert(decodeFrame(frame.data(), frameLength, decoded));
  assert(decoded.header.sequence == 42);
  StateSnapshot restored{};
  assert(decodeStateSnapshot(decoded.payload, decoded.header.payloadLength, restored));
  assert(restored.selfCash == 1337);
  assert(restored.players[1].position == 7);
  assert(restored.availableActions == 0x9234);
  assert(restored.decisionPlayerId == 2);
  assert(restored.debtAmount == 750);
  assert(restored.auctionMinimumBid == 130);

  frame[frameLength - 1] ^= 0x40;
  assert(!decodeFrame(frame.data(), frameLength, decoded));

  ActionRequest action{ActionCode::Build, 1, 15, 2, 99};
  assert(encodeActionRequest(action, payload.data(), payload.size(), payloadLength));
  ActionRequest actionRestored{};
  assert(decodeActionRequest(payload.data(), payloadLength, actionRestored));
  assert(actionRestored.action == ActionCode::Build);
  assert(actionRestored.assetIndex == 15);

  action = {ActionCode::AuctionBid, 2, 0xFF, 240, 101};
  assert(encodeActionRequest(action, payload.data(), payload.size(), payloadLength));
  assert(decodeActionRequest(payload.data(), payloadLength, actionRestored));
  assert(actionRestored.action == ActionCode::AuctionBid);
  assert(actionRestored.argument == 240);

  action = {ActionCode::AuctionReady, 2, 17, static_cast<std::int32_t>(0x81234567u), 102};
  assert(encodeActionRequest(action, payload.data(), payload.size(), payloadLength));
  assert(decodeActionRequest(payload.data(), payloadLength, actionRestored));
  assert(actionRestored.action == ActionCode::AuctionReady);
  assert(actionRestored.assetIndex == 17);
  assert(static_cast<std::uint32_t>(actionRestored.argument) == 0x81234567u);

  action = {ActionCode::CardContinue, 2, 0xFF, 0x4321, 103};
  assert(encodeActionRequest(action, payload.data(), payload.size(), payloadLength));
  assert(decodeActionRequest(payload.data(), payloadLength, actionRestored));
  assert(actionRestored.action == ActionCode::CardContinue);
  assert(actionRestored.argument == 0x4321);

  AuthoritySnapshot authority{};
  authority.phase = 5;
  authority.activePlayerId = 1;
  authority.decisionPlayerId = 2;
  authority.winnerPlayerId = 0;
  authority.boardSize = 40;
  authority.playerCount = 6;
  authority.assetCount = 28;
  authority.round = 27;
  authority.stateVersion = 1234;
  authority.lastEventSequence = 4321;
  authority.boardIdHash = 0xAABBCCDD;
  authority.pendingMoveFlags = 3;
  authority.pendingMovePlayerId = 2;
  authority.pendingMoveOrigin = 10;
  authority.pendingMoveTarget = 18;
  authority.pendingMoveDieA = 3;
  authority.pendingMoveDieB = 5;
  authority.pendingPurchaseFlags = 1;
  authority.pendingPurchasePlayerId = 2;
  authority.pendingPurchaseAssetIndex = 14;
  authority.debtFlags = 1;
  authority.debtDebtorId = 2;
  authority.debtCreditorId = 1;
  authority.debtAssetIndex = 14;
  authority.debtPaymentEvent = 9;
  authority.debtContinuation = 1;
  authority.debtDieA = 3;
  authority.debtDieB = 5;
  authority.debtAmount = 640;
  authority.auctionFlags = 3;
  authority.auctionAssetIndex = 17;
  authority.auctionLandingPlayerId = 4;
  authority.auctionCurrentBidderId = 5;
  authority.auctionHighestBidderId = 3;
  authority.auctionPassedMask = 0x12;
  authority.auctionReadyMask = 0x15;
  authority.auctionRequiredReadyMask = 0x3F;
  authority.auctionCurrentBid = 350;
  authority.auctionGeneration = 0x89ABCDEFu;
  authority.pendingCardFlags = 0x0F;
  authority.pendingCardPlayerId = 2;
  authority.pendingCardDeckId = 1;
  authority.pendingCardIndex = 5;
  authority.pendingCardInstanceId = 0x1234;
  authority.pendingCardCatalogId = 6;
  authority.pendingCardEffectId = 6;
  authority.pendingCardDisplayAmount = -75;
  authority.pendingCardTargetPlayerId = 0;
  authority.pendingCardTargetPosition = 14;
  authority.pendingCardDrawEventSequence = 4320;
  for (std::uint8_t i = 0; i < authority.playerCount; ++i) {
    authority.players[i] = {static_cast<std::uint8_t>(i + 1), i, 1200 - i * 100, i, i, static_cast<std::uint8_t>(i + 1)};
  }
  for (std::uint8_t i = 0; i < authority.assetCount; ++i) {
    authority.assets[i] = {static_cast<std::uint8_t>(i % 7), static_cast<std::uint8_t>(i % 6),
                           static_cast<std::uint8_t>(i & 1)};
  }
  assert(encodeAuthoritySnapshot(authority, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 218);
  header.type = MessageType::AuthoritySnapshot;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  assert(frameLength == 250);
  AuthoritySnapshot authorityRestored{};
  assert(decodeAuthoritySnapshot(payload.data(), payloadLength, authorityRestored));
  assert(authorityRestored.stateVersion == 1234);
  assert(authorityRestored.lastEventSequence == 4321);
  assert(authorityRestored.pendingMoveDieB == 5);
  assert(authorityRestored.auctionFlags == 3);
  assert(authorityRestored.auctionReadyMask == 0x15);
  assert(authorityRestored.auctionRequiredReadyMask == 0x3F);
  assert(authorityRestored.auctionGeneration == 0x89ABCDEFu);
  assert(authorityRestored.pendingCardFlags == 0x0F);
  assert(authorityRestored.pendingCardInstanceId == 0x1234);
  assert(authorityRestored.pendingCardDisplayAmount == -75);
  assert(authorityRestored.pendingCardDrawEventSequence == 4320);
  assert(authorityRestored.players[5].cash == 700);
  assert(authorityRestored.assets[27].buildingLevel == 3);

  // Decoder remains compatible with v1 authority payloads, whose player and
  // asset arrays started four bytes earlier and had no ready/generation data.
  auto authorityV1 = payload;
  std::memmove(authorityV1.data() + 56, authorityV1.data() + 80, payloadLength - 80);
  authorityV1[0] = 1;
  AuthoritySnapshot authorityV1Restored{};
  assert(decodeAuthoritySnapshot(authorityV1.data(), payloadLength - 24, authorityV1Restored));
  assert(authorityV1Restored.auctionCurrentBid == 350);
  assert(authorityV1Restored.auctionReadyMask == 0);
  assert(authorityV1Restored.auctionRequiredReadyMask == 0);
  assert(authorityV1Restored.auctionGeneration == 0);

  // v2 starts its dynamic arrays at byte 60 and carries auction readiness,
  // but does not contain the pending-card recovery block.
  auto authorityV2 = payload;
  std::memmove(authorityV2.data() + 60, authorityV2.data() + 80, payloadLength - 80);
  authorityV2[0] = 2;
  AuthoritySnapshot authorityV2Restored{};
  assert(decodeAuthoritySnapshot(authorityV2.data(), payloadLength - 20, authorityV2Restored));
  assert(authorityV2Restored.auctionGeneration == 0x89ABCDEFu);
  assert(authorityV2Restored.pendingCardFlags == 0);

  RosterSnapshot roster{};
  roster.stateVersion = 1234;
  roster.playerCount = 2;
  roster.playerIds[0] = 1;
  roster.playerIds[1] = 2;
  const char firstName[] = "Console One";
  const char secondName[] = "Bot 1";
  std::memcpy(roster.displayNames[0].data(), firstName, sizeof(firstName));
  std::memcpy(roster.displayNames[1].data(), secondName, sizeof(secondName));
  assert(encodeRosterSnapshot(roster, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 42);
  RosterSnapshot rosterRestored{};
  assert(decodeRosterSnapshot(payload.data(), payloadLength, rosterRestored));
  assert(rosterRestored.stateVersion == 1234);
  assert(rosterRestored.displayNames[0][0] == 'C');

  GameEventBatch events{};
  events.stateVersion = 1234;
  events.eventCount = static_cast<std::uint8_t>(kMaxEventsPerBatch);
  for (std::uint8_t i = 0; i < events.eventCount; ++i) {
    events.events[i] = {static_cast<std::uint32_t>(100 + i), 3, 1, 2, 5, -200, 0x10203040u + i};
  }
  assert(encodeGameEventBatch(events, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 214);
  header.type = MessageType::GameEvent;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  assert(frameLength == 246);
  GameEventBatch eventsRestored{};
  assert(decodeGameEventBatch(payload.data(), payloadLength, eventsRestored));
  assert(eventsRestored.eventCount == kMaxEventsPerBatch);
  assert(eventsRestored.events[12].sequence == 112);
  assert(eventsRestored.events[12].amount == -200);

  PlayerCardEvent card{};
  card.stage = PlayerCardStage::Drawn;
  card.domainEventType = kDomainEventCardDrawn;
  card.stateVersion = 1241;
  card.eventSequence = 4330;
  card.playerId = 2;
  card.deckId = 1;
  card.cardIndex = 5;
  card.flags = PlayerCardFlagReplay;
  card.cardInstanceId = 0x1234;
  card.cardCatalogId = 6;
  card.effectId = 6;
  card.amount = -75;
  card.targetPosition = 14;
  assert(encodePlayerCardEvent(card, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kPlayerCardEventSize);
  PlayerCardEvent cardRestored{};
  assert(decodePlayerCardEvent(payload.data(), payloadLength, cardRestored));
  assert(cardRestored.domainEventType == kDomainEventCardDrawn);
  assert(cardRestored.cardInstanceId == 0x1234);
  assert(cardRestored.amount == -75);
  card.stage = PlayerCardStage::EffectApplied;
  card.domainEventType = kDomainEventCardEffectApplied;
  card.flags = 0;
  card.outcome = 1;
  assert(encodePlayerCardEvent(card, payload.data(), payload.size(), payloadLength));
  assert(decodePlayerCardEvent(payload.data(), payloadLength, cardRestored));
  assert(cardRestored.stage == PlayerCardStage::EffectApplied);
  payload[31] = 1;
  assert(!decodePlayerCardEvent(payload.data(), payloadLength, cardRestored));

  Heartbeat heartbeat{1, 1234, 112};
  assert(encodeHeartbeat(heartbeat, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 12);
  Heartbeat heartbeatRestored{};
  assert(decodeHeartbeat(payload.data(), payloadLength, heartbeatRestored));
  assert(heartbeatRestored.flags == 1);
  assert(heartbeatRestored.appliedStateVersion == 1234);
  assert(heartbeatRestored.appliedEventSequence == 112);

  PlayerDetailRequest detailRequest{0x10203040u, 2, 1234};
  assert(encodePlayerDetailRequest(detailRequest, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kPlayerDetailRequestSize);
  assert(payload[0] == 1 && payload[1] == 2);
  assert(payload[4] == 0x40 && payload[7] == 0x10);
  PlayerDetailRequest detailRequestRestored{};
  assert(decodePlayerDetailRequest(payload.data(), payloadLength, detailRequestRestored));
  assert(detailRequestRestored.requestId == 0x10203040u);
  assert(detailRequestRestored.targetPlayerId == 2);
  assert(detailRequestRestored.expectedStateVersion == 1234);
  payload[2] = 1;
  assert(!decodePlayerDetailRequest(payload.data(), payloadLength, detailRequestRestored));

  PairAccept pairAccept{};
  pairAccept.accepted = 1;
  pairAccept.seatId = 3;
  pairAccept.wifiChannel = 0;
  pairAccept.serverDeviceId = 0xAABBCCDDu;
  pairAccept.stateVersion = 99;
  pairAccept.sessionId = 0x12345678u;
  assert(encodePairAccept(pairAccept, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 17 && payload[0] == 2);
  PairAccept pairAcceptRestored{};
  assert(decodePairAccept(payload.data(), payloadLength, pairAcceptRestored));
  assert(pairAcceptRestored.sessionId == 0x12345678u);
  // ESP-NOW v1 PairAccept remains decodable and has no UDP session.
  payload[0] = 1;
  assert(decodePairAccept(payload.data(), 13, pairAcceptRestored));
  assert(pairAcceptRestored.serverDeviceId == 0xAABBCCDDu);
  assert(pairAcceptRestored.sessionId == 0);

  PlayerDetailResponse detail{};
  detail.requestId = 0x10203040u;
  detail.stateVersion = 1240;
  detail.cash = -75;
  detail.targetPlayerId = 2;
  detail.position = 31;
  detail.flags = PlayerDetailFlagRequestedVersionStale;
  detail.assetCount = static_cast<std::uint8_t>(kMaxPlayerDetailAssets);
  detail.ledgerCount = static_cast<std::uint8_t>(kMaxPlayerDetailLedgerEntries);
  detail.totalOwnedAssets = detail.assetCount;
  for (std::uint8_t i = 0; i < detail.assetCount; ++i) {
    detail.assets[i] = {i, static_cast<std::uint8_t>((i % 6) | ((i & 1) ? PlayerDetailAssetMortgaged : 0))};
  }
  for (std::uint8_t i = 0; i < detail.ledgerCount; ++i) {
    detail.ledger[i] = {static_cast<std::uint32_t>(900 - i), -static_cast<std::int32_t>(10 + i),
                        9, 1, i, PlayerDetailLedgerFlagHasAsset};
  }
  assert(encodePlayerDetailResponse(detail, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kMaxPlayerDetailResponseSize);
  assert(payloadLength == 196);
  header.type = MessageType::PlayerDetailResponse;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  assert(frameLength == 228);
  PlayerDetailResponse detailRestored{};
  assert(decodePlayerDetailResponse(payload.data(), payloadLength, detailRestored));
  assert(detailRestored.requestId == detail.requestId);
  assert(detailRestored.cash == -75);
  assert(detailRestored.assets[27].assetIndex == 27);
  assert((detailRestored.assets[27].state & PlayerDetailAssetMortgaged) != 0);
  assert(detailRestored.ledger[9].sequence == 891);
  assert(detailRestored.ledger[9].amount == -19);
  assert(!decodePlayerDetailResponse(payload.data(), payloadLength - 1, detailRestored));

  TradeRequest tradeRequest{};
  tradeRequest.operation = TradeOperation::Update;
  tradeRequest.targetPlayerId = 2;
  tradeRequest.expectedRevision = 7;
  tradeRequest.requestId = 0x55667788u;
  tradeRequest.expectedStateVersion = 1240;
  tradeRequest.tradeId = 0x11223344u;
  tradeRequest.selfGivesCash = 125;
  tradeRequest.counterpartyGivesCash = 40;
  tradeRequest.selfAssetCount = 2;
  tradeRequest.counterpartyAssetCount = 1;
  tradeRequest.selfAssets[0] = 3;
  tradeRequest.selfAssets[1] = 9;
  tradeRequest.counterpartyAssets[0] = 17;
  auto unboundTradeRequest = tradeRequest;
  unboundTradeRequest.expectedStateVersion = 0;
  assert(!encodeTradeRequest(unboundTradeRequest, payload.data(), payload.size(), payloadLength));
  assert(encodeTradeRequest(tradeRequest, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 35);
  TradeRequest tradeRequestRestored{};
  assert(decodeTradeRequest(payload.data(), payloadLength, tradeRequestRestored));
  assert(tradeRequestRestored.operation == TradeOperation::Update);
  assert(tradeRequestRestored.tradeId == 0x11223344u);
  assert(tradeRequestRestored.selfAssets[1] == 9);
  assert(tradeRequestRestored.counterpartyAssets[0] == 17);
  payload[34] = 9;  // Duplicate asset across sides is rejected canonically.
  assert(!decodeTradeRequest(payload.data(), payloadLength, tradeRequestRestored));

  TradeResponse tradeResponse{};
  tradeResponse.operation = TradeOperation::Update;
  tradeResponse.result = TradeResultCode::Ok;
  tradeResponse.status = TradeStatus::Countered;
  tradeResponse.flags = TradeResponseFlagSelfConfirmed | TradeResponseFlagSelfLastEdited;
  tradeResponse.selfPlayerId = 1;
  tradeResponse.counterpartyId = 2;
  tradeResponse.revision = 8;
  tradeResponse.requestId = 0x55667788u;
  tradeResponse.stateVersion = 1241;
  tradeResponse.tradeId = 0x11223344u;
  tradeResponse.expiresInMs = 120000;
  tradeResponse.selfGivesCash = 125;
  tradeResponse.counterpartyGivesCash = 40;
  tradeResponse.confirmedMask = 1;
  tradeResponse.originatorId = 2;
  tradeResponse.selfAssetCount = 2;
  tradeResponse.counterpartyAssetCount = 1;
  tradeResponse.selfAssets[0] = 3;
  tradeResponse.selfAssets[1] = 9;
  tradeResponse.counterpartyAssets[0] = 17;
  assert(encodeTradeResponse(tradeResponse, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == 43);
  header.type = MessageType::TradeResponse;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  TradeResponse tradeResponseRestored{};
  assert(decodeTradeResponse(payload.data(), payloadLength, tradeResponseRestored));
  assert(tradeResponseRestored.status == TradeStatus::Countered);
  assert(tradeResponseRestored.revision == 8);
  assert(tradeResponseRestored.expiresInMs == 120000);
  assert(tradeResponseRestored.counterpartyAssets[0] == 17);
  tradeResponse.requestId = 0;
  tradeResponse.flags |= TradeResponseFlagResync;
  assert(encodeTradeResponse(tradeResponse, payload.data(), payload.size(), payloadLength));
  TradeResponse noTradeResponse{};
  noTradeResponse.operation = TradeOperation::Query;
  noTradeResponse.result = TradeResultCode::NoActiveTrade;
  noTradeResponse.flags = TradeResponseFlagResync;
  noTradeResponse.selfPlayerId = 1;
  noTradeResponse.stateVersion = 1241;
  assert(encodeTradeResponse(noTradeResponse, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kTradeResponseBaseSize);
  assert(decodeTradeResponse(payload.data(), payloadLength, tradeResponseRestored));
  assert(tradeResponseRestored.requestId == 0);
  assert(tradeResponseRestored.result == TradeResultCode::NoActiveTrade);
  assert(tradeResponseRestored.status == TradeStatus::None);

  TradeRequest maximumTradeRequest{};
  maximumTradeRequest.operation = TradeOperation::Create;
  maximumTradeRequest.targetPlayerId = 2;
  maximumTradeRequest.requestId = 99;
  maximumTradeRequest.expectedStateVersion = 1241;
  maximumTradeRequest.selfAssetCount = 28;
  for (std::uint8_t i = 0; i < 28; ++i) maximumTradeRequest.selfAssets[i] = i;
  assert(encodeTradeRequest(maximumTradeRequest, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kMaxTradeRequestSize && payloadLength == 60);
  maximumTradeRequest.selfAssetCount = 0;
  maximumTradeRequest.counterpartyAssetCount = 28;
  for (std::uint8_t i = 0; i < 28; ++i) maximumTradeRequest.counterpartyAssets[i] = i;
  assert(encodeTradeRequest(maximumTradeRequest, payload.data(), payload.size(), payloadLength));

  TradeResponse maximumTradeResponse{};
  maximumTradeResponse.operation = TradeOperation::Query;
  maximumTradeResponse.result = TradeResultCode::Ok;
  maximumTradeResponse.status = TradeStatus::Offered;
  maximumTradeResponse.selfPlayerId = 1;
  maximumTradeResponse.counterpartyId = 2;
  maximumTradeResponse.requestId = 100;
  maximumTradeResponse.tradeId = 1;
  maximumTradeResponse.revision = 1;
  maximumTradeResponse.selfAssetCount = 28;
  for (std::uint8_t i = 0; i < 28; ++i) maximumTradeResponse.selfAssets[i] = i;
  assert(encodeTradeResponse(maximumTradeResponse, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kMaxTradeResponseSize && payloadLength == 68);
  header.type = MessageType::TradeResponse;
  assert(encodeFrame(header, payload.data(), payloadLength, frame.data(), frame.size(), frameLength));
  assert(frameLength == 100);

  static_assert(static_cast<std::uint8_t>(MessageType::IdentityRequest) == 0x29);
  static_assert(static_cast<std::uint8_t>(MessageType::IdentitySnapshot) == 0x2A);

  IdentityRequest identityRequest{};
  identityRequest.operation = IdentityOperation::ConfirmAvatar;
  identityRequest.playerId = 3;
  identityRequest.requestId = 0x10203040u;
  identityRequest.expectedStateVersion = 0x50607080u;
  identityRequest.expectedSeatRevision = 0x1122u;
  identityRequest.avatarCatalogVersion = 1;
  identityRequest.recipe = {1, 10, 20, 9, 8, 7};
  assert(encodeIdentityRequest(identityRequest, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kIdentityRequestSize && payloadLength == 44);
  assert(payload[0] == 1);
  assert(payload[1] == static_cast<std::uint8_t>(IdentityOperation::ConfirmAvatar));
  assert(payload[2] == 3 && payload[3] == 0);
  assert(payload[4] == 0x40 && payload[7] == 0x10);
  assert(payload[8] == 0x80 && payload[11] == 0x50);
  assert(payload[12] == 0x22 && payload[13] == 0x11);
  assert(payload[14] == 1 && payload[15] == 0);
  assert(payload[16] == 10 && payload[17] == 20 && payload[18] == 9);
  assert(payload[19] == 8 && payload[20] == 7 && payload[21] == 0);
  for (std::size_t index = 22; index < 44; ++index) assert(payload[index] == 0);
  IdentityRequest identityRequestRestored{};
  assert(decodeIdentityRequest(payload.data(), payloadLength, identityRequestRestored));
  assert(identityRequestRestored.operation == IdentityOperation::ConfirmAvatar);
  assert(identityRequestRestored.expectedSeatRevision == 0x1122u);
  assert(identityRequestRestored.recipe.hairColorId == 20);

  IdentityRequest nameRequest{};
  nameRequest.operation = IdentityOperation::ConfirmName;
  nameRequest.playerId = 3;
  nameRequest.requestId = 0xA1A2A3A4u;
  nameRequest.expectedStateVersion = 99;
  nameRequest.expectedSeatRevision = 2;
  nameRequest.nameLength = 5;
  std::memcpy(nameRequest.name.data(), "Alice", 5);
  assert(encodeIdentityRequest(nameRequest, payload.data(), payload.size(), payloadLength));
  assert(payload[21] == 5);
  assert(std::memcmp(payload.data() + 22, "Alice", 5) == 0);
  assert(decodeIdentityRequest(payload.data(), payloadLength, identityRequestRestored));
  assert(identityRequestRestored.nameLength == 5);
  assert(std::memcmp(identityRequestRestored.name.data(), "Alice", 5) == 0);
  payload[43] = 1;
  assert(!decodeIdentityRequest(payload.data(), payloadLength, identityRequestRestored));
  payload[43] = 0;
  nameRequest.expectedStateVersion = 0;
  assert(!encodeIdentityRequest(nameRequest, payload.data(), payload.size(), payloadLength));
  nameRequest.expectedStateVersion = 99;
  nameRequest.expectedSeatRevision = 0;
  assert(!encodeIdentityRequest(nameRequest, payload.data(), payload.size(), payloadLength));

  IdentityRequest query{};
  query.operation = IdentityOperation::Query;
  query.playerId = 3;
  query.requestId = 7;
  assert(encodeIdentityRequest(query, payload.data(), payload.size(), payloadLength));
  assert(decodeIdentityRequest(payload.data(), payloadLength, identityRequestRestored));
  assert(identityRequestRestored.expectedStateVersion == 0);
  assert(identityRequestRestored.expectedSeatRevision == 0);

  IdentitySnapshot identity{};
  identity.roomPhase = IdentityRoomPhase::Countdown;
  identity.selfStage = IdentitySeatStage::Countdown;
  identity.result = IdentityResultCode::Ok;
  identity.requestId = 0x10203040u;
  identity.stateVersion = 0x50607080u;
  identity.identityRevision = 0x90A0B0C0u;
  identity.serverEpochMs = 0x0102030405060708ull;
  identity.countdownDeadlineEpochMs = 0x1112131415161718ull;
  identity.avatarCatalogVersion = 1;
  identity.playerCount = 6;
  identity.selfPlayerId = 3;
  identity.requiredHumanMask = 0x07;
  identity.avatarFinalMask = 0x3F;
  identity.nameFinalMask = 0x3F;
  identity.readyMask = 0x3F;
  identity.onlineMask = 0x05;
  identity.operationEcho = IdentityOperation::ConfirmName;
  identity.flags = IdentitySnapshotFlagReplay | IdentitySnapshotFlagResync;
  for (std::uint8_t index = 0; index < 6; ++index) {
    auto& seat = identity.seats[index];
    seat.playerId = static_cast<std::uint8_t>(index + 1);
    seat.flags = static_cast<std::uint8_t>(IdentitySeatPresent |
        (index < 3 ? IdentitySeatHuman : IdentitySeatBot) | IdentitySeatAvatarFinal |
        IdentitySeatNameFinal | IdentitySeatReady);
    seat.seatColorId = static_cast<std::uint8_t>(index + 1);
    seat.seatRevision = static_cast<std::uint16_t>(100 + index);
    seat.avatarRevision = static_cast<std::uint16_t>(200 + index);
    seat.avatarContentHash64 = 0xA0A1A2A3A4A5A600ull + index;
    seat.recipe = {1, static_cast<std::uint8_t>(index + 1), 20, 10, 8, 9};
  }
  assert(encodeIdentitySnapshot(identity, payload.data(), payload.size(), payloadLength));
  assert(payloadLength == kIdentitySnapshotSize && payloadLength == 182);
  assert(payload[0] == 1);
  assert(payload[1] == static_cast<std::uint8_t>(IdentityRoomPhase::Countdown));
  assert(payload[2] == static_cast<std::uint8_t>(IdentitySeatStage::Countdown));
  assert(payload[3] == static_cast<std::uint8_t>(IdentityResultCode::Ok));
  assert(payload[16] == 0x08 && payload[23] == 0x01);
  assert(payload[24] == 0x18 && payload[31] == 0x11);
  assert(payload[32] == 1 && payload[33] == 0 && payload[34] == 6 && payload[35] == 3);
  assert(payload[36] == 0x07 && payload[39] == 0x3F && payload[40] == 0x05);
  assert(payload[41] == static_cast<std::uint8_t>(IdentityOperation::ConfirmName));
  assert(payload[42] == 3 && payload[43] == 0);
  const std::size_t sixthSeat = 44 + 5 * kIdentitySeatRecordSize;
  assert(sixthSeat == 159 && payload[sixthSeat] == 6);
  assert(payload[sixthSeat + 4] == 105 && payload[sixthSeat + 5] == 0);
  assert(payload[sixthSeat + 6] == 205 && payload[sixthSeat + 7] == 0);
  assert(payload[sixthSeat + 8] == 5 && payload[sixthSeat + 15] == 0xA0);
  assert(payload[sixthSeat + 16] == 1 && payload[sixthSeat + 18] == 6);
  IdentitySnapshot identityRestored{};
  assert(decodeIdentitySnapshot(payload.data(), payloadLength, identityRestored));
  assert(identityRestored.identityRevision == 0x90A0B0C0u);
  assert(identityRestored.countdownDeadlineEpochMs == 0x1112131415161718ull);
  assert(identityRestored.seats[5].avatarContentHash64 == 0xA0A1A2A3A4A5A605ull);
  assert(identityRestored.seats[5].recipe.hairPresetId == 6);
  payload[43] = 1;
  assert(!decodeIdentitySnapshot(payload.data(), payloadLength, identityRestored));
  payload[43] = 0;
  payload[sixthSeat + 3] = 1;
  assert(!decodeIdentitySnapshot(payload.data(), payloadLength, identityRestored));

  std::cout << "GRIDOPOLY_PROTOCOL_TESTS_PASS\n";
  return 0;
}
