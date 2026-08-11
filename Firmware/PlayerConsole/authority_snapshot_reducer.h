#pragma once

#include <GridopolyProtocol.h>

#include "transport_types.h"

bool authoritySnapshotToEvent(const gridopoly::protocol::StateSnapshot &snapshot,
                              bool resync, TransportEvent &event);
bool authoritySnapshotsEqual(const gridopoly::protocol::StateSnapshot &left,
                             const gridopoly::protocol::StateSnapshot &right);
bool fullAuthoritySnapshotToEvent(const gridopoly::protocol::AuthoritySnapshot &snapshot,
                                  bool resync, TransportEvent &event);
bool fullAuthoritySnapshotsEqual(const gridopoly::protocol::AuthoritySnapshot &left,
                                 const gridopoly::protocol::AuthoritySnapshot &right);
bool rosterSnapshotToEvent(const gridopoly::protocol::RosterSnapshot &snapshot,
                           bool resync, TransportEvent &event);
bool rosterSnapshotsEqual(const gridopoly::protocol::RosterSnapshot &left,
                          const gridopoly::protocol::RosterSnapshot &right);
bool gameEventToTransportEvent(const gridopoly::protocol::GameEventRecord &record,
                               uint32_t stateVersion, bool resync, TransportEvent &event);
bool playerCardEventToTransportEvent(const gridopoly::protocol::PlayerCardEvent &card,
                                     bool resync, TransportEvent &event);
