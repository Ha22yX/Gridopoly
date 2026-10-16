#pragma once
#include <cstdio>
#include <cstring>
#include "grid_city_visual_catalog.h"
#include "src/assets/legacy_tile_checksums.h"
// The umbrella include lets Arduino discover GridopolyCore before nested headers.
#include <GridopolyCore.h>
#include <gridopoly/assets/DevelopmentArtworkCatalog.h>

constexpr uint8_t kArtworkLevelUnknown = 0xFF;
struct RemoteArtworkRequest {
    GridCityArtwork artwork = GridCityArtwork::Fallback;
    const char *key = nullptr;
    uint8_t level = kArtworkLevelUnknown;
    uint8_t catalog = 0; // 0: legacy source, 1: immutable DevelopmentV1
    uint32_t crc = 0;
    bool valid() const { return key != nullptr; }
};
inline bool sameArtwork(const RemoteArtworkRequest &a, const RemoteArtworkRequest &b)
{
    return a.artwork == b.artwork && a.level == b.level && a.catalog == b.catalog &&
        ((a.key == nullptr || b.key == nullptr) ? a.key == b.key : std::strcmp(a.key, b.key) == 0);
}
inline RemoteArtworkRequest artworkRequest(GridCityArtwork artwork, const char *key, uint8_t level)
{
    using namespace gridopoly::assets;
    const auto *legacy = legacyTileChecksum(key);
    if (!legacy || artwork == GridCityArtwork::Fallback || artwork >= GridCityArtwork::Count) return {};
    const auto *property = developmentProperty(key);
    // Unknown levels retain the old source without claiming a known empty lot.
    if (!property || (level > 5 && level != kArtworkLevelUnknown)) level = kArtworkLevelUnknown;
    RemoteArtworkRequest request{artwork, legacy->key, level, 0, legacy->crc};
    if (property && validDevelopmentLevel(level)) {
        request.catalog = 1;
        request.crc = developmentCrc32(property, level, DevelopmentTarget::Player);
    }
    return request;
}
inline bool artworkPath(const RemoteArtworkRequest &request, char *buffer, size_t capacity)
{
    if (!request.valid() || !buffer || capacity == 0) return false;
    const int length = request.catalog == 1
        ? std::snprintf(buffer, capacity, "%splayer/%s-l%u.rgb565",
                        gridopoly::assets::kDevelopmentAssetPrefix, request.key, request.level)
        : std::snprintf(buffer, capacity, "/assets/tiles/%s.rgb565", request.key);
    return length >= 0 && static_cast<size_t>(length) < capacity;
}
