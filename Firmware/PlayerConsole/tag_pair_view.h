#pragma once

#include "app_state.h"

struct TagPairView {
    const char *title;
    const char *instruction;
    const char *detail;
};

inline TagPairView tagPairView(const AppState &state)
{
    if (!state.authorityOnline) {
        return {"RECONNECTING", "KEEP YOUR TOKEN", "Setup resumes when the screen reconnects."};
    }
    if (!appTagPairSnapshotFresh(state)) {
        return {"SYNCING PAIRING", "KEEP YOUR TOKEN", "Waiting for current pairing instructions."};
    }
    const auto &pair = state.identity.tagPair;
    if (state.identity.tagPairSuccessUntilMs != 0) {
        return {"TOKEN PAIRED", "TAKE YOUR TOKEN BACK", "Your avatar setup opens next."};
    }
    if (tagPairMayEditIdentity(pair)) {
        return {"TOKEN PAIRED", "OPENING YOUR SETUP", "Your token is already paired."};
    }
    if (pair.result == 8) {
        return {"COULD NOT SAVE TOKEN", "PLEASE TRY AGAIN", "The server could not save this pairing."};
    }
    if (pair.activePlayerId != state.selfSeatId && !tagPairMayEditIdentity(pair)) {
        if (pair.state == TransportTagPairState::WaitingReader ||
            pair.state == TransportTagPairState::ReaderOffline) {
            return {"PAIRING PAUSED", "WAIT FOR THE TILE", "The pairing tile must be online to continue."};
        }
        return {"WAIT YOUR TURN", "KEEP YOUR TOKEN", "Your screen will tell you when it is your turn."};
    }
    switch (pair.state) {
        case TransportTagPairState::ClearRequired:
            return {"CLEAR THE TILE", "REMOVE ALL TOKENS", "Clear the highlighted tile before pairing."};
        case TransportTagPairState::AwaitingTag:
            return {"PAIR YOUR TOKEN", "PLACE YOUR TOKEN", "Put one token on the highlighted tile."};
        case TransportTagPairState::SuccessRemove:
            return {"TOKEN PAIRED", "TAKE YOUR TOKEN BACK", "Remove it so the next player can pair."};
        case TransportTagPairState::AlreadyBound:
            return {"TOKEN ALREADY USED", "CHOOSE ANOTHER TOKEN", "This token belongs to another player."};
        case TransportTagPairState::MultipleTags:
            return {"TOO MANY TOKENS", "USE ONE TOKEN", "Remove all tokens, then place only yours."};
        case TransportTagPairState::ReaderOffline:
            return {"TILE DISCONNECTED", "KEEP YOUR TOKEN", "Reconnect the tile or try another reader."};
        case TransportTagPairState::TimedOut:
            return {"PAIRING TIMED OUT", "TRY AGAIN", "Your avatar and name have not been changed."};
        case TransportTagPairState::Cancelled:
            return {"PAIRING CANCELLED", "TRY AGAIN WHEN READY", "A token is required before you can play."};
        case TransportTagPairState::Complete:
            return {"TOKEN PAIRED", "OPENING YOUR SETUP", "You can create your avatar and name next."};
        case TransportTagPairState::Disabled:
        case TransportTagPairState::WaitingReader:
            return {"WAITING FOR A TILE", "KEEP YOUR TOKEN", "Pairing starts when a reader is available."};
    }
    return {"PAIR YOUR TOKEN", "SYNCING SETUP", "Waiting for the server to update your screen."};
}
