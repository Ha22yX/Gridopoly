#pragma once

#include <GridopolyProtocol.h>

#include "transport_types.h"

bool authoritySnapshotToEvent(const gridopoly::protocol::StateSnapshot &snapshot,
                              bool resync, TransportEvent &event);
bool authoritySnapshotsEqual(const gridopoly::protocol::StateSnapshot &left,
                             const gridopoly::protocol::StateSnapshot &right);
