#include "grid_city_tile_images.h"

#include "sample_assets.h"
#include "../../remote_tile_cache.h"

const char *gridCityArtworkAssetKey(GridCityArtwork artwork)
{
    switch (artwork) {
        case GridCityArtwork::RivetRow: return "a1-rivet-row";
        case GridCityArtwork::CopperLane: return "a2-copper-lane";
        case GridCityArtwork::LanternAvenue: return "b1-lantern-avenue";
        case GridCityArtwork::TidewayDrive: return "b2-tideway-drive";
        case GridCityArtwork::BeaconBoulevard: return "b3-beacon-boulevard";
        case GridCityArtwork::CanvasStreet: return "c1-canvas-street";
        case GridCityArtwork::BloomTerrace: return "c2-bloom-terrace";
        case GridCityArtwork::AuroraAvenue: return "c3-aurora-avenue";
        case GridCityArtwork::ArchiveWay: return "d1-archive-way";
        case GridCityArtwork::ForumDrive: return "d2-forum-drive";
        case GridCityArtwork::MeridianAvenue: return "d3-meridian-avenue";
        case GridCityArtwork::PulseStreet: return "e1-pulse-street";
        case GridCityArtwork::PrismBoulevard: return "e2-prism-boulevard";
        case GridCityArtwork::NovaAvenue: return "e3-nova-avenue";
        case GridCityArtwork::SunstepTerrace: return "f1-sunstep-terrace";
        case GridCityArtwork::HelixWay: return "f2-helix-way";
        case GridCityArtwork::HorizonDrive: return "f3-horizon-drive";
        case GridCityArtwork::CanopyLane: return "g1-canopy-lane";
        case GridCityArtwork::VerdantAvenue: return "g2-verdant-avenue";
        case GridCityArtwork::SummitBoulevard: return "g3-summit-boulevard";
        case GridCityArtwork::CrownPromenade: return "h1-crown-promenade";
        case GridCityArtwork::GrandMeridian: return "h2-grand-meridian";
        case GridCityArtwork::WestlineTerminal: return "transit-westline-terminal";
        case GridCityArtwork::NorthloopStation: return "transit-northloop-station";
        case GridCityArtwork::EastgateTerminal: return "transit-eastgate-terminal";
        case GridCityArtwork::SouthlineDepot: return "transit-southline-depot";
        case GridCityArtwork::MetroGrid: return "utility-metro-grid";
        case GridCityArtwork::BluewaterWorks: return "utility-bluewater-works";
        case GridCityArtwork::Chance: return "cover-chance";
        case GridCityArtwork::CommunityFund: return "cover-community-fund";
        case GridCityArtwork::IncomeTax: return "cover-income-tax";
        case GridCityArtwork::LuxuryTax: return "cover-luxury-tax";
        case GridCityArtwork::CentralLaunch: return "corner-central-launch";
        case GridCityArtwork::CivicHold: return "corner-civic-hold";
        case GridCityArtwork::FreePlaza: return "corner-free-plaza";
        case GridCityArtwork::HoldOrder: return "corner-hold-order";
        case GridCityArtwork::Fallback:
        case GridCityArtwork::Count: return nullptr;
    }
    return nullptr;
}

const lv_img_dsc_t *gridCityArtworkImage(const GridCityVisualDefinition &visual)
{
    const char *key = gridCityArtworkAssetKey(visual.artwork);
    if (key == nullptr) return &asset_property_neon_harbor;
    return remoteTileCacheImage(visual.artwork, key);
}

void gridCityArtworkPrefetch(const GridCityVisualDefinition &visual)
{
    remoteTileCachePrefetch(
        visual.artwork, gridCityArtworkAssetKey(visual.artwork)
    );
}
