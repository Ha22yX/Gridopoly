#pragma once

#include <GridopolyProtocol.h>
#include <cstring>
#include "tag_pair_types.h"

inline TransportTagPairSnapshot tagPairFromWire(const gridopoly::protocol::TagPairSnapshot &source)
{
    TransportTagPairSnapshot result{};
    result.state = static_cast<TransportTagPairState>(source.state);
    result.result = static_cast<uint8_t>(source.result);
    result.selfPlayerId = source.selfPlayerId;
    result.activePlayerId = source.activePlayerId;
    result.requiredHumanMask = source.requiredHumanMask;
    result.boundMask = source.boundMask;
    result.flags = source.flags;
    result.readerMapIndex = source.readerMapIndex;
    result.revision = source.revision;
    result.sessionId = source.sessionId;
    result.expiresInMs = source.expiresInMs;
    result.readerTagRevision = source.readerTagRevision;
    result.tagUid = source.tagUid;
    std::memcpy(result.moduleId, source.moduleId.data(), 32);
    std::memcpy(result.playerName, source.playerName.data(), 16);
    return result;
}

static_assert(static_cast<uint8_t>(TransportTagPairState::Complete) ==
              static_cast<uint8_t>(gridopoly::protocol::TagPairState::Complete), "pairing states match wire");
static_assert(kTagPairCanEditIdentity == gridopoly::protocol::TagPairFlagCanEditIdentity,
              "pairing gate matches wire");
