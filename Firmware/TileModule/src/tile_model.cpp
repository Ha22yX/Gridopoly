#include "tile_model.h"

#include <cstring>

namespace gridopoly::tile {

const char *tileKindLabel(TileKind kind) {
  switch (kind) {
    case TileKind::Start: return "START";
    case TileKind::Property: return "PROPERTY";
    case TileKind::Transit: return "TRANSIT";
    case TileKind::Utility: return "UTILITY";
    case TileKind::Card: return "CARD";
    case TileKind::Fee: return "FEE";
    case TileKind::Hold: return "HOLD";
    case TileKind::Rest: return "REST";
    case TileKind::GoToHold: return "GO TO HOLD";
    case TileKind::Disabled: return "DISABLED";
  }
  return "UNKNOWN";
}

const char *tileActivityLabel(TileActivity activity) {
  switch (activity) {
    case TileActivity::Idle: return "IDLE";
    case TileActivity::Available: return "AVAILABLE";
    case TileActivity::Arriving: return "ARRIVING";
    case TileActivity::RentDue: return "RENT DUE";
    case TileActivity::Owned: return "OWNED";
    case TileActivity::DrawCard: return "DRAW CARD";
    case TileActivity::PayFee: return "PAY FEE";
    case TileActivity::Reward: return "REWARD";
    case TileActivity::Restricted: return "RESTRICTED";
    case TileActivity::Disabled: return "DISABLED";
    case TileActivity::Fault: return "FAULT";
  }
  return "UNKNOWN";
}

bool isOwnable(TileKind kind) {
  return kind == TileKind::Property || kind == TileKind::Transit ||
         kind == TileKind::Utility;
}

bool hasOwner(const TileState &state) {
  return isOwnable(state.kind) && state.owner_player >= 1 &&
         state.owner_player <= kMaximumPlayers;
}

const char *ownerLabel(const TileState &state) {
  if (!isOwnable(state.kind)) {
    return "NOT OWNABLE";
  }
  if (state.mortgaged) {
    return "MORTGAGED";
  }
  return hasOwner(state) ? "PLAYER OWNER" : "CITY BANK";
}

void copyTileText(char *destination, std::size_t capacity, const char *source) {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

void normalizeTileState(TileState &state) {
  if (state.owner_player > kMaximumPlayers) {
    state.owner_player = 0;
  }
  if (state.active_player > kMaximumPlayers) {
    state.active_player = 0;
  }
  state.building_level = state.building_level > 5 ? 5 : state.building_level;
  state.occupied_players &= static_cast<std::uint8_t>((1U << kMaximumPlayers) - 1U);
  if (!isOwnable(state.kind)) {
    state.owner_player = 0;
    state.owner_color = {};
    state.building_level = 0;
    state.mortgaged = false;
  }
  if (state.mortgaged) {
    state.current_rent = 0;
  }
  if (!hasOwner(state)) {
    state.owner_display_name[0] = '\0';
  }
}

}  // namespace gridopoly::tile

