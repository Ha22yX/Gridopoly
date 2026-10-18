#pragma once

#include <cstdint>

// Provisioned at build time. GPIO13 is NC, not a role strap.
#ifndef GRIDOPOLY_TILE_ROLE
#define GRIDOPOLY_TILE_ROLE 0
#endif
static_assert(GRIDOPOLY_TILE_ROLE >= 0 && GRIDOPOLY_TILE_ROLE <= 2,
              "Tile role must be 0 unknown, 1 long, or 2 corner");

namespace gridopoly::tile {
enum class ModuleRole : std::uint8_t { Unknown = 0, Long = 1, Corner = 2 };
inline constexpr ModuleRole kModuleRole = static_cast<ModuleRole>(GRIDOPOLY_TILE_ROLE);
inline constexpr const char *kFirmwareVersion = "V0.39";
inline constexpr const char *moduleRoleLabel(ModuleRole role) {
  switch (role) {
    case ModuleRole::Long: return "long";
    case ModuleRole::Corner: return "corner";
    default: return "unknown";
  }
}
inline constexpr const char *kModuleProfile =
    kModuleRole == ModuleRole::Long ? "tile_esp32s3_long" :
    kModuleRole == ModuleRole::Corner ? "tile_esp32s3_corner" : "tile_esp32s3";
}  // namespace gridopoly::tile
