#pragma once

#include <stdint.h>

enum class GridCityVisualKind : uint8_t {
    Property,
    Transit,
    Utility,
    CityEvent,
    CivicFund,
    Fee,
    Corner,
};

enum class GridCityArtwork : uint8_t {
    Fallback,
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

struct GridCityVisualDefinition {
    const char *id;
    const char *name;
    const char *category;
    const char *groupCode;
    const char *groupName;
    uint32_t accent;
    GridCityVisualKind kind;
    GridCityArtwork artwork;
};

const GridCityVisualDefinition *gridCityVisualById(const char *id);
const GridCityVisualDefinition &gridCityFallbackVisual();
