#pragma once

#include <cstddef>
#include <cstdint>

namespace gridopoly::tile {

inline constexpr std::uint8_t kMaximumPlayers = 6;

struct Rgb {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

enum class TileKind : std::uint8_t {
  Start,
  Property,
  Transit,
  Utility,
  Card,
  Fee,
  Hold,
  Rest,
  GoToHold,
  Disabled,
};

enum class TileActivity : std::uint8_t {
  Idle,
  Available,
  Arriving,
  RentDue,
  Owned,
  DrawCard,
  PayFee,
  Reward,
  Restricted,
  Disabled,
  Fault,
};

enum class ArtworkId : std::uint8_t {
  None,
  RivetRow,
  CopperLane,
  LanternAvenue,
  TidewayDrive,
  BeaconBoulevard,
  CanvasStreet,
  BloomTerrace,
  AuroraAvenue,
  ArchiveWay,
  ForumDrive,
  MeridianAvenue,
  PulseStreet,
  PrismBoulevard,
  NovaAvenue,
  SunstepTerrace,
  HelixWay,
  HorizonDrive,
  CanopyLane,
  VerdantAvenue,
  SummitBoulevard,
  CrownPromenade,
  GrandMeridian,
  WestlineTerminal,
  NorthloopStation,
  EastgateTerminal,
  SouthlineDepot,
  MetroGrid,
  BluewaterWorks,
  Chance,
  CommunityFund,
  IncomeTax,
  LuxuryTax,
  CentralLaunch,
  CivicHold,
  FreePlaza,
  HoldOrder,
  Count,
};

struct TileState {
  std::uint8_t map_index = 0;
  char tile_id[16]{};
  char display_name[25]{};
  char subtitle[25]{};
  TileKind kind = TileKind::Disabled;
  TileActivity activity = TileActivity::Disabled;
  ArtworkId artwork = ArtworkId::None;
  Rgb accent{};
  std::uint16_t purchase_price = 0;
  std::uint16_t current_rent = 0;
  std::uint8_t owner_player = 0;
  char owner_display_name[21]{};
  Rgb owner_color{};
  std::uint8_t building_level = 0;
  bool mortgaged = false;
  std::uint8_t occupied_players = 0;
  std::uint8_t active_player = 0;
  char status[29]{};
  char detail[37]{};
};

const char *tileKindLabel(TileKind kind);
const char *tileActivityLabel(TileActivity activity);
const char *ownerLabel(const TileState &state);
bool isOwnable(TileKind kind);
bool hasOwner(const TileState &state);
void normalizeTileState(TileState &state);
void copyTileText(char *destination, std::size_t capacity, const char *source);

}  // namespace gridopoly::tile
