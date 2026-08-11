#include "grid_city_visual_catalog.h"

#include <string.h>

namespace {

constexpr uint32_t kForge = 0xC97852;
constexpr uint32_t kHarbor = 0x63C6E8;
constexpr uint32_t kArts = 0xD970AD;
constexpr uint32_t kCivic = 0xEE9B47;
constexpr uint32_t kPulse = 0xE85D63;
constexpr uint32_t kSolar = 0xE7C64B;
constexpr uint32_t kCanopy = 0x54BB78;
constexpr uint32_t kMeridian = 0x5F80D8;
constexpr uint32_t kTransit = 0xAAB8BD;
constexpr uint32_t kUtility = 0x50D1B1;
constexpr uint32_t kChance = 0xEF8C4A;
constexpr uint32_t kFund = 0x50D1B1;
constexpr uint32_t kCorner = 0xE9EEF0;
constexpr uint32_t kFee = 0xF0A65B;

#define PROPERTY(ID, NAME, CODE, GROUP, COLOR, ART) \
    {ID, NAME, "PROPERTY", CODE, GROUP, COLOR, GridCityVisualKind::Property, GridCityArtwork::ART}
#define TILE(ID, NAME, CATEGORY, COLOR, KIND, ART) \
    {ID, NAME, CATEGORY, "", "", COLOR, GridCityVisualKind::KIND, GridCityArtwork::ART}

constexpr GridCityVisualDefinition kVisuals[] = {
    TILE("CORNER-START", "Grid Central", "START", kCorner, Corner, CentralLaunch),
    PROPERTY("A1", "Rivet Row", "A", "FORGE QUARTER", kForge, RivetRow),
    TILE("CARD-CF-1", "Community Chest", "COMMUNITY CHEST", kFund, CivicFund, CommunityFund),
    PROPERTY("A2", "Copper Lane", "A", "FORGE QUARTER", kForge, CopperLane),
    TILE("FEE-CITY", "Income Tax", "FEE", kFee, Fee, IncomeTax),
    TILE("T-WEST", "Westline Terminal", "TRANSIT", kTransit, Transit, WestlineTerminal),
    PROPERTY("B1", "Lantern Avenue", "B", "HARBOR QUARTER", kHarbor, LanternAvenue),
    TILE("CARD-CE-1", "Chance", "CHANCE", kChance, CityEvent, Chance),
    PROPERTY("B2", "Tideway Drive", "B", "HARBOR QUARTER", kHarbor, TidewayDrive),
    PROPERTY("B3", "Beacon Boulevard", "B", "HARBOR QUARTER", kHarbor, BeaconBoulevard),
    TILE("CORNER-HOLD", "Holding Area", "VISITING", kCorner, Corner, CivicHold),
    PROPERTY("C1", "Canvas Street", "C", "ARTS QUARTER", kArts, CanvasStreet),
    TILE("U-ENERGY", "Metro Grid", "UTILITY", kUtility, Utility, MetroGrid),
    PROPERTY("C2", "Bloom Terrace", "C", "ARTS QUARTER", kArts, BloomTerrace),
    PROPERTY("C3", "Aurora Avenue", "C", "ARTS QUARTER", kArts, AuroraAvenue),
    TILE("T-NORTH", "Northloop Station", "TRANSIT", kTransit, Transit, NorthloopStation),
    PROPERTY("D1", "Archive Way", "D", "CIVIC QUARTER", kCivic, ArchiveWay),
    TILE("CARD-CF-2", "Community Chest", "COMMUNITY CHEST", kFund, CivicFund, CommunityFund),
    PROPERTY("D2", "Forum Drive", "D", "CIVIC QUARTER", kCivic, ForumDrive),
    PROPERTY("D3", "Meridian Avenue", "D", "CIVIC QUARTER", kCivic, MeridianAvenue),
    TILE("CORNER-REST", "Open Plaza", "FREE SPACE", kCorner, Corner, FreePlaza),
    PROPERTY("E1", "Pulse Street", "E", "PULSE QUARTER", kPulse, PulseStreet),
    TILE("CARD-CE-2", "Chance", "CHANCE", kChance, CityEvent, Chance),
    PROPERTY("E2", "Prism Boulevard", "E", "PULSE QUARTER", kPulse, PrismBoulevard),
    PROPERTY("E3", "Nova Avenue", "E", "PULSE QUARTER", kPulse, NovaAvenue),
    TILE("T-EAST", "Eastgate Terminal", "TRANSIT", kTransit, Transit, EastgateTerminal),
    PROPERTY("F1", "Sunstep Terrace", "F", "SOLAR QUARTER", kSolar, SunstepTerrace),
    PROPERTY("F2", "Helix Way", "F", "SOLAR QUARTER", kSolar, HelixWay),
    TILE("U-WATER", "Bluewater Works", "UTILITY", kUtility, Utility, BluewaterWorks),
    PROPERTY("F3", "Horizon Drive", "F", "SOLAR QUARTER", kSolar, HorizonDrive),
    TILE("CORNER-GOTO", "Go To Hold", "ORDER", kChance, Corner, HoldOrder),
    PROPERTY("G1", "Canopy Lane", "G", "CANOPY QUARTER", kCanopy, CanopyLane),
    PROPERTY("G2", "Verdant Avenue", "G", "CANOPY QUARTER", kCanopy, VerdantAvenue),
    TILE("CARD-CF-3", "Community Chest", "COMMUNITY CHEST", kFund, CivicFund, CommunityFund),
    PROPERTY("G3", "Summit Boulevard", "G", "CANOPY QUARTER", kCanopy, SummitBoulevard),
    TILE("T-SOUTH", "Southline Depot", "TRANSIT", kTransit, Transit, SouthlineDepot),
    TILE("CARD-CE-3", "Chance", "CHANCE", kChance, CityEvent, Chance),
    PROPERTY("H1", "Crown Promenade", "H", "MERIDIAN QUARTER", kMeridian, CrownPromenade),
    TILE("FEE-DENSITY", "Luxury Tax", "FEE", kFee, Fee, LuxuryTax),
    PROPERTY("H2", "Grand Meridian", "H", "MERIDIAN QUARTER", kMeridian, GrandMeridian),
};

constexpr GridCityVisualDefinition kFallback = {
    "UNKNOWN", "Board Tile", "TILE", "", "", kCorner,
    GridCityVisualKind::Corner, GridCityArtwork::Fallback
};

#undef PROPERTY
#undef TILE

} // namespace

const GridCityVisualDefinition *gridCityVisualById(const char *id)
{
    if (id == nullptr) return nullptr;
    for (const GridCityVisualDefinition &visual : kVisuals) {
        if (strcmp(visual.id, id) == 0) return &visual;
    }
    return nullptr;
}

const GridCityVisualDefinition &gridCityFallbackVisual()
{
    return kFallback;
}
