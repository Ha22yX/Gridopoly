#include "tile_artwork.h"

#include <Arduino.h>

namespace gridopoly::tile {
namespace {

constexpr std::uint16_t kArtworkWidth = 160;
constexpr std::uint16_t kArtworkHeight = 160;
constexpr std::size_t kArtworkPixels =
    static_cast<std::size_t>(kArtworkWidth) * kArtworkHeight;

const std::uint16_t kRivetRow[kArtworkPixels] PROGMEM = {
#include "assets/rivet_row_rgb565.inc"
};
const std::uint16_t kCopperLane[kArtworkPixels] PROGMEM = {
#include "assets/copper_lane_rgb565.inc"
};
const std::uint16_t kLanternAvenue[kArtworkPixels] PROGMEM = {
#include "assets/lantern_avenue_rgb565.inc"
};
const std::uint16_t kTidewayDrive[kArtworkPixels] PROGMEM = {
#include "assets/tideway_drive_rgb565.inc"
};
const std::uint16_t kBeaconBoulevard[kArtworkPixels] PROGMEM = {
#include "assets/beacon_boulevard_rgb565.inc"
};
const std::uint16_t kCanvasStreet[kArtworkPixels] PROGMEM = {
#include "assets/canvas_street_rgb565.inc"
};
const std::uint16_t kBloomTerrace[kArtworkPixels] PROGMEM = {
#include "assets/bloom_terrace_rgb565.inc"
};
const std::uint16_t kAuroraAvenue[kArtworkPixels] PROGMEM = {
#include "assets/aurora_avenue_rgb565.inc"
};
const std::uint16_t kArchiveWay[kArtworkPixels] PROGMEM = {
#include "assets/archive_way_rgb565.inc"
};
const std::uint16_t kForumDrive[kArtworkPixels] PROGMEM = {
#include "assets/forum_drive_rgb565.inc"
};
const std::uint16_t kMeridianAvenue[kArtworkPixels] PROGMEM = {
#include "assets/meridian_avenue_rgb565.inc"
};
const std::uint16_t kPulseStreet[kArtworkPixels] PROGMEM = {
#include "assets/pulse_street_rgb565.inc"
};
const std::uint16_t kPrismBoulevard[kArtworkPixels] PROGMEM = {
#include "assets/prism_boulevard_rgb565.inc"
};
const std::uint16_t kNovaAvenue[kArtworkPixels] PROGMEM = {
#include "assets/nova_avenue_rgb565.inc"
};
const std::uint16_t kSunstepTerrace[kArtworkPixels] PROGMEM = {
#include "assets/sunstep_terrace_rgb565.inc"
};
const std::uint16_t kHelixWay[kArtworkPixels] PROGMEM = {
#include "assets/helix_way_rgb565.inc"
};
const std::uint16_t kHorizonDrive[kArtworkPixels] PROGMEM = {
#include "assets/horizon_drive_rgb565.inc"
};
const std::uint16_t kCanopyLane[kArtworkPixels] PROGMEM = {
#include "assets/canopy_lane_rgb565.inc"
};
const std::uint16_t kVerdantAvenue[kArtworkPixels] PROGMEM = {
#include "assets/verdant_avenue_rgb565.inc"
};
const std::uint16_t kSummitBoulevard[kArtworkPixels] PROGMEM = {
#include "assets/summit_boulevard_rgb565.inc"
};
const std::uint16_t kCrownPromenade[kArtworkPixels] PROGMEM = {
#include "assets/crown_promenade_rgb565.inc"
};
const std::uint16_t kGrandMeridian[kArtworkPixels] PROGMEM = {
#include "assets/grand_meridian_rgb565.inc"
};
const std::uint16_t kWestlineTerminal[kArtworkPixels] PROGMEM = {
#include "assets/westline_terminal_rgb565.inc"
};
const std::uint16_t kNorthloopStation[kArtworkPixels] PROGMEM = {
#include "assets/northloop_station_rgb565.inc"
};
const std::uint16_t kEastgateTerminal[kArtworkPixels] PROGMEM = {
#include "assets/eastgate_terminal_rgb565.inc"
};
const std::uint16_t kSouthlineDepot[kArtworkPixels] PROGMEM = {
#include "assets/southline_depot_rgb565.inc"
};
const std::uint16_t kMetroGrid[kArtworkPixels] PROGMEM = {
#include "assets/metro_grid_rgb565.inc"
};
const std::uint16_t kBluewaterWorks[kArtworkPixels] PROGMEM = {
#include "assets/bluewater_works_rgb565.inc"
};
const std::uint16_t kChance[kArtworkPixels] PROGMEM = {
#include "assets/chance_rgb565.inc"
};
const std::uint16_t kCommunityFund[kArtworkPixels] PROGMEM = {
#include "assets/community_fund_rgb565.inc"
};
const std::uint16_t kIncomeTax[kArtworkPixels] PROGMEM = {
#include "assets/income_tax_rgb565.inc"
};
const std::uint16_t kLuxuryTax[kArtworkPixels] PROGMEM = {
#include "assets/luxury_tax_rgb565.inc"
};
const std::uint16_t kCentralLaunch[kArtworkPixels] PROGMEM = {
#include "assets/central_launch_rgb565.inc"
};
const std::uint16_t kCivicHold[kArtworkPixels] PROGMEM = {
#include "assets/civic_hold_rgb565.inc"
};
const std::uint16_t kFreePlaza[kArtworkPixels] PROGMEM = {
#include "assets/free_plaza_rgb565.inc"
};
const std::uint16_t kHoldOrder[kArtworkPixels] PROGMEM = {
#include "assets/hold_order_rgb565.inc"
};

const TileArtwork kArtwork[] = {
    {0, 0, nullptr},
    {kArtworkWidth, kArtworkHeight, kRivetRow},
    {kArtworkWidth, kArtworkHeight, kCopperLane},
    {kArtworkWidth, kArtworkHeight, kLanternAvenue},
    {kArtworkWidth, kArtworkHeight, kTidewayDrive},
    {kArtworkWidth, kArtworkHeight, kBeaconBoulevard},
    {kArtworkWidth, kArtworkHeight, kCanvasStreet},
    {kArtworkWidth, kArtworkHeight, kBloomTerrace},
    {kArtworkWidth, kArtworkHeight, kAuroraAvenue},
    {kArtworkWidth, kArtworkHeight, kArchiveWay},
    {kArtworkWidth, kArtworkHeight, kForumDrive},
    {kArtworkWidth, kArtworkHeight, kMeridianAvenue},
    {kArtworkWidth, kArtworkHeight, kPulseStreet},
    {kArtworkWidth, kArtworkHeight, kPrismBoulevard},
    {kArtworkWidth, kArtworkHeight, kNovaAvenue},
    {kArtworkWidth, kArtworkHeight, kSunstepTerrace},
    {kArtworkWidth, kArtworkHeight, kHelixWay},
    {kArtworkWidth, kArtworkHeight, kHorizonDrive},
    {kArtworkWidth, kArtworkHeight, kCanopyLane},
    {kArtworkWidth, kArtworkHeight, kVerdantAvenue},
    {kArtworkWidth, kArtworkHeight, kSummitBoulevard},
    {kArtworkWidth, kArtworkHeight, kCrownPromenade},
    {kArtworkWidth, kArtworkHeight, kGrandMeridian},
    {kArtworkWidth, kArtworkHeight, kWestlineTerminal},
    {kArtworkWidth, kArtworkHeight, kNorthloopStation},
    {kArtworkWidth, kArtworkHeight, kEastgateTerminal},
    {kArtworkWidth, kArtworkHeight, kSouthlineDepot},
    {kArtworkWidth, kArtworkHeight, kMetroGrid},
    {kArtworkWidth, kArtworkHeight, kBluewaterWorks},
    {kArtworkWidth, kArtworkHeight, kChance},
    {kArtworkWidth, kArtworkHeight, kCommunityFund},
    {kArtworkWidth, kArtworkHeight, kIncomeTax},
    {kArtworkWidth, kArtworkHeight, kLuxuryTax},
    {kArtworkWidth, kArtworkHeight, kCentralLaunch},
    {kArtworkWidth, kArtworkHeight, kCivicHold},
    {kArtworkWidth, kArtworkHeight, kFreePlaza},
    {kArtworkWidth, kArtworkHeight, kHoldOrder},
};

static_assert(sizeof(kArtwork) / sizeof(kArtwork[0]) ==
              static_cast<std::size_t>(ArtworkId::Count));

}  // namespace

const TileArtwork *tileArtwork(ArtworkId id) {
  const auto index = static_cast<std::size_t>(id);
  if (index == 0 || index >= (sizeof(kArtwork) / sizeof(kArtwork[0]))) {
    return nullptr;
  }
  return &kArtwork[index];
}

const char *artworkKey(ArtworkId id) {
  switch (id) {
    case ArtworkId::RivetRow: return "a1-rivet-row";
    case ArtworkId::CopperLane: return "a2-copper-lane";
    case ArtworkId::LanternAvenue: return "b1-lantern-avenue";
    case ArtworkId::TidewayDrive: return "b2-tideway-drive";
    case ArtworkId::BeaconBoulevard: return "b3-beacon-boulevard";
    case ArtworkId::CanvasStreet: return "c1-canvas-street";
    case ArtworkId::BloomTerrace: return "c2-bloom-terrace";
    case ArtworkId::AuroraAvenue: return "c3-aurora-avenue";
    case ArtworkId::ArchiveWay: return "d1-archive-way";
    case ArtworkId::ForumDrive: return "d2-forum-drive";
    case ArtworkId::MeridianAvenue: return "d3-meridian-avenue";
    case ArtworkId::PulseStreet: return "e1-pulse-street";
    case ArtworkId::PrismBoulevard: return "e2-prism-boulevard";
    case ArtworkId::NovaAvenue: return "e3-nova-avenue";
    case ArtworkId::SunstepTerrace: return "f1-sunstep-terrace";
    case ArtworkId::HelixWay: return "f2-helix-way";
    case ArtworkId::HorizonDrive: return "f3-horizon-drive";
    case ArtworkId::CanopyLane: return "g1-canopy-lane";
    case ArtworkId::VerdantAvenue: return "g2-verdant-avenue";
    case ArtworkId::SummitBoulevard: return "g3-summit-boulevard";
    case ArtworkId::CrownPromenade: return "h1-crown-promenade";
    case ArtworkId::GrandMeridian: return "h2-grand-meridian";
    case ArtworkId::WestlineTerminal: return "transit-westline-terminal";
    case ArtworkId::NorthloopStation: return "transit-northloop-station";
    case ArtworkId::EastgateTerminal: return "transit-eastgate-terminal";
    case ArtworkId::SouthlineDepot: return "transit-southline-depot";
    case ArtworkId::MetroGrid: return "utility-metro-grid";
    case ArtworkId::BluewaterWorks: return "utility-bluewater-works";
    case ArtworkId::Chance: return "cover-chance";
    case ArtworkId::CommunityFund: return "cover-community-fund";
    case ArtworkId::IncomeTax: return "cover-income-tax";
    case ArtworkId::LuxuryTax: return "cover-luxury-tax";
    case ArtworkId::CentralLaunch: return "corner-central-launch";
    case ArtworkId::CivicHold: return "corner-civic-hold";
    case ArtworkId::FreePlaza: return "corner-free-plaza";
    case ArtworkId::HoldOrder: return "corner-hold-order";
    case ArtworkId::Count:
    case ArtworkId::None: return "";
  }
  return "";
}

}  // namespace gridopoly::tile
