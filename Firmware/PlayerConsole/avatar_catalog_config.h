#pragma once

#include <stdint.h>

// Opt in only after the V2 component pack and server are deployed. Explicit
// recipes from either catalog remain readable regardless of this default.
#ifndef GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION
#define GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION 1
#endif

static_assert(GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION == 1 ||
              GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION == 2,
              "avatar default catalog must be 1 or 2");
constexpr uint16_t kAvatarDefaultCatalogVersion =
    GRIDOPOLY_AVATAR_DEFAULT_CATALOG_VERSION;

inline bool avatarCatalogSupported(uint16_t version)
{
    return version == 1 || version == 2;
}

inline uint8_t avatarCatalogComponentCount(uint16_t version)
{
    return version == 2 ? 31 : 30;
}
