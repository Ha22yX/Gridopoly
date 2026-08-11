#pragma once

#include <stdint.h>

enum class TransportSequenceDisposition : uint8_t {
    Duplicate,
    Accept,
    Gap,
};

// Tracks what has entered the app queue separately from what the app has
// actually consumed. A full projection resync may begin inside the retained
// event-history window, but must never acknowledge its final sequence before
// those events reach AppState.
struct TransportEventCursor {
    uint32_t applied = 0;
    uint32_t queued = 0;
    uint32_t resyncTarget = 0;
    bool resyncBaselinePending = false;

    void reset()
    {
        *this = TransportEventCursor{};
    }

    void beginResync(uint32_t latestSequence)
    {
        resyncTarget = latestSequence;
        resyncBaselinePending = latestSequence > queued;
    }

    TransportSequenceDisposition prepare(uint32_t sequence, bool resync)
    {
        if (sequence == 0) return TransportSequenceDisposition::Gap;
        if (sequence <= queued) return TransportSequenceDisposition::Duplicate;
        if (sequence == queued + 1u) {
            if (resync) resyncBaselinePending = false;
            return TransportSequenceDisposition::Accept;
        }
        if (resync && resyncBaselinePending && sequence <= resyncTarget) {
            // Events older than the server's retained history are unavailable.
            // Align only to the first replayed record, then require continuity.
            queued = sequence - 1u;
            resyncBaselinePending = false;
            return TransportSequenceDisposition::Accept;
        }
        return TransportSequenceDisposition::Gap;
    }

    void markQueued(uint32_t sequence)
    {
        queued = sequence;
        if (queued >= resyncTarget) resyncBaselinePending = false;
    }

    bool markApplied(uint32_t sequence, bool resync)
    {
        if (sequence <= applied) return true;
        if (sequence == applied + 1u ||
            (resync && sequence <= resyncTarget)) {
            applied = sequence;
            return true;
        }
        return false;
    }
};
