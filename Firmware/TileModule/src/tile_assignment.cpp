#include "tile_assignment.h"

#include <cstring>

namespace gridopoly::tile {
namespace {

Rgb rgbFromPacked(std::uint32_t packed) {
  return {
      static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
      static_cast<std::uint8_t>(packed & 0xFFU),
  };
}

bool equals(const char *left, const char *right) {
  return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

}  // namespace

TileKind tileKindFromServer(const char *kind) {
  if (equals(kind, "START")) return TileKind::Start;
  if (equals(kind, "PROPERTY")) return TileKind::Property;
  if (equals(kind, "TRANSIT")) return TileKind::Transit;
  if (equals(kind, "UTILITY")) return TileKind::Utility;
  if (equals(kind, "CHANCE") || equals(kind, "COMMUNITY_CHEST")) {
    return TileKind::Card;
  }
  if (equals(kind, "FEE")) return TileKind::Fee;
  if (equals(kind, "HOLD")) return TileKind::Hold;
  if (equals(kind, "REST")) return TileKind::Rest;
  if (equals(kind, "GO_TO_HOLD")) return TileKind::GoToHold;
  return TileKind::Disabled;
}

ArtworkId artworkFromServerKey(const char *key) {
  if (equals(key, "a1-rivet-row")) return ArtworkId::RivetRow;
  if (equals(key, "a2-copper-lane")) return ArtworkId::CopperLane;
  if (equals(key, "b1-lantern-avenue")) return ArtworkId::LanternAvenue;
  if (equals(key, "b2-tideway-drive")) return ArtworkId::TidewayDrive;
  if (equals(key, "b3-beacon-boulevard")) return ArtworkId::BeaconBoulevard;
  if (equals(key, "c1-canvas-street")) return ArtworkId::CanvasStreet;
  if (equals(key, "c2-bloom-terrace")) return ArtworkId::BloomTerrace;
  if (equals(key, "c3-aurora-avenue")) return ArtworkId::AuroraAvenue;
  if (equals(key, "d1-archive-way")) return ArtworkId::ArchiveWay;
  if (equals(key, "d2-forum-drive")) return ArtworkId::ForumDrive;
  if (equals(key, "d3-meridian-avenue")) return ArtworkId::MeridianAvenue;
  if (equals(key, "e1-pulse-street")) return ArtworkId::PulseStreet;
  if (equals(key, "e2-prism-boulevard")) return ArtworkId::PrismBoulevard;
  if (equals(key, "e3-nova-avenue")) return ArtworkId::NovaAvenue;
  if (equals(key, "f1-sunstep-terrace")) return ArtworkId::SunstepTerrace;
  if (equals(key, "f2-helix-way")) return ArtworkId::HelixWay;
  if (equals(key, "f3-horizon-drive")) return ArtworkId::HorizonDrive;
  if (equals(key, "g1-canopy-lane")) return ArtworkId::CanopyLane;
  if (equals(key, "g2-verdant-avenue")) return ArtworkId::VerdantAvenue;
  if (equals(key, "g3-summit-boulevard")) return ArtworkId::SummitBoulevard;
  if (equals(key, "h1-crown-promenade")) return ArtworkId::CrownPromenade;
  if (equals(key, "h2-grand-meridian")) return ArtworkId::GrandMeridian;
  if (equals(key, "transit-westline-terminal")) return ArtworkId::WestlineTerminal;
  if (equals(key, "transit-northloop-station")) return ArtworkId::NorthloopStation;
  if (equals(key, "transit-eastgate-terminal")) return ArtworkId::EastgateTerminal;
  if (equals(key, "transit-southline-depot")) return ArtworkId::SouthlineDepot;
  if (equals(key, "utility-metro-grid")) return ArtworkId::MetroGrid;
  if (equals(key, "utility-bluewater-works")) return ArtworkId::BluewaterWorks;
  if (equals(key, "cover-chance")) return ArtworkId::Chance;
  if (equals(key, "cover-community-fund")) return ArtworkId::CommunityFund;
  if (equals(key, "cover-income-tax")) return ArtworkId::IncomeTax;
  if (equals(key, "cover-luxury-tax")) return ArtworkId::LuxuryTax;
  if (equals(key, "corner-central-launch") || equals(key, "corner-start"))
    return ArtworkId::CentralLaunch;
  if (equals(key, "corner-civic-hold") || equals(key, "corner-hold"))
    return ArtworkId::CivicHold;
  if (equals(key, "corner-free-plaza") || equals(key, "corner-rest"))
    return ArtworkId::FreePlaza;
  if (equals(key, "corner-hold-order") || equals(key, "corner-go-to-hold"))
    return ArtworkId::HoldOrder;
  return ArtworkId::None;
}

const char *tileSubtitleFromAssignment(const TileAssignmentDto &assignment) {
  const char group = assignment.tile_id[0];
  if (group == 'A') return "FORGE QUARTER";
  if (group == 'B') return "HARBOR QUARTER";
  if (group == 'C') return "ARTS QUARTER";
  if (group == 'D') return "CIVIC QUARTER";
  if (group == 'E') return "PULSE QUARTER";
  if (group == 'F') return "SOLAR QUARTER";
  if (group == 'G') return "CANOPY QUARTER";
  if (group == 'H') return "MERIDIAN QUARTER";
  return assignment.kind;
}

bool applyTileAssignment(const TileAssignmentDto &assignment, TileState &state) {
  if (!assignment.assigned || assignment.tile_id[0] == '\0' ||
      assignment.map_index > 63U) {
    return false;
  }
  const TileKind kind = tileKindFromServer(assignment.kind);
  if (kind == TileKind::Disabled) {
    return false;
  }

  state = {};
  state.map_index = assignment.map_index;
  copyTileText(state.tile_id, sizeof(state.tile_id), assignment.tile_id);
  copyTileText(state.display_name, sizeof(state.display_name),
               assignment.display_name);
  copyTileText(state.subtitle, sizeof(state.subtitle),
               tileSubtitleFromAssignment(assignment));
  state.kind = kind;
  state.artwork = artworkFromServerKey(assignment.artwork_key);
  state.accent = rgbFromPacked(assignment.accent_rgb);
  state.purchase_price = assignment.purchase_price;
  state.owner_player = assignment.owner_player;
  copyTileText(state.owner_display_name, sizeof(state.owner_display_name),
               assignment.owner_display_name);
  state.owner_color = rgbFromPacked(assignment.owner_rgb);

  if (isOwnable(kind)) {
    state.activity = assignment.owner_player == 0 ? TileActivity::Available
                                                   : TileActivity::Owned;
    copyTileText(state.status, sizeof(state.status),
                 assignment.owner_player == 0 ? "AVAILABLE" : "OWNED");
  } else if (kind == TileKind::Card) {
    state.activity = TileActivity::DrawCard;
    copyTileText(state.status, sizeof(state.status), "DRAW A CARD");
  } else if (kind == TileKind::Fee) {
    state.activity = TileActivity::PayFee;
    copyTileText(state.status, sizeof(state.status), "CITY FEE");
  } else if (kind == TileKind::Start) {
    state.activity = TileActivity::Reward;
    copyTileText(state.status, sizeof(state.status), "START TILE");
  } else if (kind == TileKind::Hold || kind == TileKind::GoToHold) {
    state.activity = TileActivity::Restricted;
    copyTileText(state.status, sizeof(state.status), "HOLDING AREA");
  } else {
    state.activity = TileActivity::Idle;
    copyTileText(state.status, sizeof(state.status), "REST AREA");
  }
  copyTileText(state.detail, sizeof(state.detail),
               assignment.manual ? "MANUAL SERVER ASSIGNMENT"
                                 : "AUTOMATIC SERVER CLAIM");
  normalizeTileState(state);
  return true;
}

}  // namespace gridopoly::tile
