#pragma once

#include <stdint.h>

enum class TransportTagPairOperation : uint8_t { Query = 1, Retry = 2, Cancel = 3 };
enum class TransportTagPairState : uint8_t {
    Disabled = 0, WaitingReader = 1, ClearRequired = 2, AwaitingTag = 3,
    SuccessRemove = 4, AlreadyBound = 5, MultipleTags = 6, ReaderOffline = 7,
    TimedOut = 8, Cancelled = 9, Complete = 10,
};

constexpr uint8_t kTagPairEnabled = 1u;
constexpr uint8_t kTagPairSelfBound = 2u;
constexpr uint8_t kTagPairCanEditIdentity = 4u;

struct TransportTagPairSnapshot {
    TransportTagPairState state = TransportTagPairState::Disabled;
    uint8_t result = 0;
    uint8_t selfPlayerId = 0;
    uint8_t activePlayerId = 0;
    uint8_t requiredHumanMask = 0;
    uint8_t boundMask = 0;
    uint8_t flags = 0;
    uint8_t readerMapIndex = 0xFF;
    uint32_t revision = 0;
    uint32_t sessionId = 0;
    uint32_t expiresInMs = 0;
    uint32_t readerTagRevision = 0;
    uint32_t tagUid = 0;
    char moduleId[33]{};
    char playerName[17]{};
};

inline bool tagPairMayEditIdentity(const TransportTagPairSnapshot &snapshot)
{
    return (snapshot.flags & (kTagPairSelfBound | kTagPairCanEditIdentity)) ==
           (kTagPairSelfBound | kTagPairCanEditIdentity);
}

inline bool tagPairRevisionIsOlder(uint32_t incoming, uint32_t current)
{
    return incoming != current && ((incoming - current) & 0x80000000u) != 0;
}
