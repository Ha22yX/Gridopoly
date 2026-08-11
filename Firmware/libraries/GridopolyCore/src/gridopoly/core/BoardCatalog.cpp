#include "BoardCatalog.h"

#include <cstring>

namespace gridopoly::core {
namespace {

#define PROPERTY(ID, POS, GROUP, PRICE, R0, R1, R2, R3, R4, RM, BUILD, MORTGAGE) \
  {ID, TileKind::Property, POS, GROUP, {PRICE, {R0, R1, R2, R3, R4, RM}, BUILD, MORTGAGE}}
#define TRANSIT(ID, POS, PRICE, MORTGAGE, R1, R2, R3, R4) \
  {ID, TileKind::Transit, POS, kNoGroup, {PRICE, {R1, R2, R3, R4, 0, 0}, 0, MORTGAGE}}
#define UTILITY(ID, POS, PRICE, MORTGAGE, ONE, TWO) \
  {ID, TileKind::Utility, POS, kNoGroup, {PRICE, {ONE, TWO, 0, 0, 0, 0}, 0, MORTGAGE}}
#define TILE(ID, KIND, ASSET, AMOUNT) {ID, KIND, ASSET, AMOUNT}

constexpr AssetDefinition kAssets16[] = {
    PROPERTY("A1", 1, 0, 100, 2, 10, 30, 90, 133, 183, 20, 50),
    PROPERTY("A2", 3, 0, 120, 3, 13, 33, 100, 150, 200, 20, 60),
    PROPERTY("B1", 5, 1, 180, 5, 23, 67, 183, 250, 317, 30, 90),
    TRANSIT("T-WEST", 6, 210, 105, 25, 25, 25, 25),
    PROPERTY("B2", 7, 1, 200, 5, 23, 67, 183, 250, 317, 30, 100),
    PROPERTY("B3", 9, 1, 220, 5, 27, 73, 200, 267, 333, 30, 110),
    UTILITY("U-ENERGY", 10, 160, 80, 4, 10),
    PROPERTY("C1", 13, 2, 330, 12, 58, 167, 367, 433, 500, 70, 165),
    PROPERTY("C2", 15, 2, 380, 17, 67, 200, 467, 567, 667, 70, 190),
};

constexpr TileDefinition kTiles16[] = {
    TILE("CORNER-START", TileKind::Start, kNoAsset, 0),
    TILE("A1", TileKind::Property, 0, 0),
    TILE("CARD-CF-1", TileKind::CivicFund, kNoAsset, 0),
    TILE("A2", TileKind::Property, 1, 0),
    TILE("CORNER-HOLD", TileKind::Hold, kNoAsset, 0),
    TILE("B1", TileKind::Property, 2, 0),
    TILE("T-WEST", TileKind::Transit, 3, 0),
    TILE("B2", TileKind::Property, 4, 0),
    TILE("CORNER-REST", TileKind::Rest, kNoAsset, 0),
    TILE("B3", TileKind::Property, 5, 0),
    TILE("U-ENERGY", TileKind::Utility, 6, 0),
    TILE("CARD-CE-1", TileKind::CityEvent, kNoAsset, 0),
    TILE("CORNER-GOTO", TileKind::GoToHold, kNoAsset, 0),
    TILE("C1", TileKind::Property, 7, 0),
    TILE("FEE-CITY", TileKind::Fee, kNoAsset, 70),
    TILE("C2", TileKind::Property, 8, 0),
};

constexpr AssetDefinition kAssets24[] = {
    PROPERTY("A1", 1, 0, 60, 1, 5, 16, 48, 85, 133, 30, 30),
    PROPERTY("A2", 3, 0, 70, 2, 11, 32, 96, 171, 240, 30, 35),
    TRANSIT("T-WEST", 5, 160, 80, 20, 40, 80, 80),
    PROPERTY("B1", 7, 1, 100, 3, 16, 48, 144, 213, 293, 30, 50),
    PROPERTY("B2", 8, 1, 110, 3, 16, 48, 144, 213, 293, 30, 55),
    PROPERTY("B3", 9, 1, 120, 4, 21, 53, 160, 240, 320, 30, 60),
    UTILITY("U-ENERGY", 10, 110, 55, 4, 10),
    TRANSIT("T-NORTH", 11, 160, 80, 20, 40, 80, 80),
    PROPERTY("C1", 13, 2, 170, 7, 37, 107, 293, 400, 507, 50, 85),
    PROPERTY("C2", 15, 2, 180, 7, 37, 107, 293, 400, 507, 50, 90),
    PROPERTY("C3", 16, 2, 190, 9, 43, 117, 320, 427, 533, 50, 95),
    TRANSIT("T-EAST", 17, 160, 80, 20, 40, 80, 80),
    PROPERTY("D1", 19, 3, 260, 12, 59, 176, 427, 520, 613, 80, 130),
    PROPERTY("D2", 21, 3, 280, 13, 64, 192, 453, 547, 640, 80, 140),
    PROPERTY("E1", 22, 4, 430, 19, 93, 267, 587, 693, 800, 110, 215),
    PROPERTY("E2", 23, 4, 480, 27, 107, 320, 747, 907, 1067, 110, 240),
};

constexpr TileDefinition kTiles24[] = {
    TILE("CORNER-START", TileKind::Start, kNoAsset, 0),
    TILE("A1", TileKind::Property, 0, 0),
    TILE("CARD-CF-1", TileKind::CivicFund, kNoAsset, 0),
    TILE("A2", TileKind::Property, 1, 0),
    TILE("FEE-CITY", TileKind::Fee, kNoAsset, 110),
    TILE("T-WEST", TileKind::Transit, 2, 0),
    TILE("CORNER-HOLD", TileKind::Hold, kNoAsset, 0),
    TILE("B1", TileKind::Property, 3, 0),
    TILE("B2", TileKind::Property, 4, 0),
    TILE("B3", TileKind::Property, 5, 0),
    TILE("U-ENERGY", TileKind::Utility, 6, 0),
    TILE("T-NORTH", TileKind::Transit, 7, 0),
    TILE("CORNER-REST", TileKind::Rest, kNoAsset, 0),
    TILE("C1", TileKind::Property, 8, 0),
    TILE("CARD-CE-1", TileKind::CityEvent, kNoAsset, 0),
    TILE("C2", TileKind::Property, 9, 0),
    TILE("C3", TileKind::Property, 10, 0),
    TILE("T-EAST", TileKind::Transit, 11, 0),
    TILE("CORNER-GOTO", TileKind::GoToHold, kNoAsset, 0),
    TILE("D1", TileKind::Property, 12, 0),
    TILE("CARD-CF-2", TileKind::CivicFund, kNoAsset, 0),
    TILE("D2", TileKind::Property, 13, 0),
    TILE("E1", TileKind::Property, 14, 0),
    TILE("E2", TileKind::Property, 15, 0),
};

constexpr AssetDefinition kAssets32[] = {
    PROPERTY("A1", 1, 0, 60, 2, 8, 24, 72, 128, 200, 40, 30),
    PROPERTY("A2", 3, 0, 70, 3, 16, 48, 144, 256, 360, 40, 35),
    TRANSIT("T-WEST", 5, 160, 80, 20, 40, 80, 160),
    PROPERTY("B1", 6, 1, 100, 5, 24, 72, 216, 320, 440, 40, 50),
    PROPERTY("B2", 7, 1, 110, 5, 24, 72, 216, 320, 440, 40, 55),
    PROPERTY("B3", 9, 1, 120, 6, 32, 80, 240, 360, 480, 40, 60),
    UTILITY("U-ENERGY", 10, 120, 60, 4, 10),
    PROPERTY("C1", 11, 2, 150, 8, 40, 120, 360, 500, 600, 80, 75),
    PROPERTY("C2", 13, 2, 160, 8, 40, 120, 360, 500, 600, 80, 80),
    PROPERTY("C3", 14, 2, 180, 10, 48, 144, 400, 560, 720, 80, 90),
    TRANSIT("T-NORTH", 15, 160, 80, 20, 40, 80, 160),
    PROPERTY("D1", 17, 3, 210, 11, 56, 160, 440, 600, 760, 80, 105),
    PROPERTY("D2", 18, 3, 230, 13, 64, 176, 480, 640, 800, 80, 115),
    PROPERTY("E1", 20, 4, 250, 14, 72, 200, 560, 700, 840, 120, 125),
    TRANSIT("T-EAST", 21, 160, 80, 20, 40, 80, 160),
    PROPERTY("E2", 22, 4, 270, 14, 72, 200, 560, 700, 840, 120, 135),
    PROPERTY("E3", 23, 4, 290, 16, 80, 240, 600, 740, 880, 120, 145),
    PROPERTY("F1", 25, 5, 330, 21, 104, 312, 720, 880, 1020, 160, 165),
    UTILITY("U-WATER", 26, 120, 60, 4, 10),
    PROPERTY("F2", 27, 5, 350, 22, 120, 360, 800, 960, 1120, 160, 175),
    PROPERTY("G1", 29, 6, 370, 28, 140, 400, 880, 1040, 1200, 160, 185),
    TRANSIT("T-SOUTH", 30, 160, 80, 20, 40, 80, 160),
    PROPERTY("G2", 31, 6, 420, 40, 160, 480, 1120, 1360, 1600, 160, 210),
};

constexpr TileDefinition kTiles32[] = {
    TILE("CORNER-START", TileKind::Start, kNoAsset, 0), TILE("A1", TileKind::Property, 0, 0),
    TILE("CARD-CF-1", TileKind::CivicFund, kNoAsset, 0), TILE("A2", TileKind::Property, 1, 0),
    TILE("FEE-CITY", TileKind::Fee, kNoAsset, 160), TILE("T-WEST", TileKind::Transit, 2, 0),
    TILE("B1", TileKind::Property, 3, 0), TILE("B2", TileKind::Property, 4, 0),
    TILE("CORNER-HOLD", TileKind::Hold, kNoAsset, 0), TILE("B3", TileKind::Property, 5, 0),
    TILE("U-ENERGY", TileKind::Utility, 6, 0), TILE("C1", TileKind::Property, 7, 0),
    TILE("CARD-CE-1", TileKind::CityEvent, kNoAsset, 0), TILE("C2", TileKind::Property, 8, 0),
    TILE("C3", TileKind::Property, 9, 0), TILE("T-NORTH", TileKind::Transit, 10, 0),
    TILE("CORNER-REST", TileKind::Rest, kNoAsset, 0), TILE("D1", TileKind::Property, 11, 0),
    TILE("D2", TileKind::Property, 12, 0), TILE("CARD-CF-2", TileKind::CivicFund, kNoAsset, 0),
    TILE("E1", TileKind::Property, 13, 0), TILE("T-EAST", TileKind::Transit, 14, 0),
    TILE("E2", TileKind::Property, 15, 0), TILE("E3", TileKind::Property, 16, 0),
    TILE("CORNER-GOTO", TileKind::GoToHold, kNoAsset, 0), TILE("F1", TileKind::Property, 17, 0),
    TILE("U-WATER", TileKind::Utility, 18, 0), TILE("F2", TileKind::Property, 19, 0),
    TILE("CARD-CE-2", TileKind::CityEvent, kNoAsset, 0), TILE("G1", TileKind::Property, 20, 0),
    TILE("T-SOUTH", TileKind::Transit, 21, 0), TILE("G2", TileKind::Property, 22, 0),
};

constexpr AssetDefinition kAssets40[] = {
    PROPERTY("A1", 1, 0, 60, 2, 10, 30, 90, 160, 250, 50, 30),
    PROPERTY("A2", 3, 0, 60, 4, 20, 60, 180, 320, 450, 50, 30),
    TRANSIT("T-WEST", 5, 200, 100, 25, 50, 100, 200),
    PROPERTY("B1", 6, 1, 100, 6, 30, 90, 270, 400, 550, 50, 50),
    PROPERTY("B2", 8, 1, 100, 6, 30, 90, 270, 400, 550, 50, 50),
    PROPERTY("B3", 9, 1, 120, 8, 40, 100, 300, 450, 600, 50, 60),
    PROPERTY("C1", 11, 2, 140, 10, 50, 150, 450, 625, 750, 100, 70),
    UTILITY("U-ENERGY", 12, 150, 75, 4, 10),
    PROPERTY("C2", 13, 2, 140, 10, 50, 150, 450, 625, 750, 100, 70),
    PROPERTY("C3", 14, 2, 160, 12, 60, 180, 500, 700, 900, 100, 80),
    TRANSIT("T-NORTH", 15, 200, 100, 25, 50, 100, 200),
    PROPERTY("D1", 16, 3, 180, 14, 70, 200, 550, 750, 950, 100, 90),
    PROPERTY("D2", 18, 3, 180, 14, 70, 200, 550, 750, 950, 100, 90),
    PROPERTY("D3", 19, 3, 200, 16, 80, 220, 600, 800, 1000, 100, 100),
    PROPERTY("E1", 21, 4, 220, 18, 90, 250, 700, 875, 1050, 150, 110),
    PROPERTY("E2", 23, 4, 220, 18, 90, 250, 700, 875, 1050, 150, 110),
    PROPERTY("E3", 24, 4, 240, 20, 100, 300, 750, 925, 1100, 150, 120),
    TRANSIT("T-EAST", 25, 200, 100, 25, 50, 100, 200),
    PROPERTY("F1", 26, 5, 260, 22, 110, 330, 800, 975, 1150, 150, 130),
    PROPERTY("F2", 27, 5, 260, 22, 110, 330, 800, 975, 1150, 150, 130),
    UTILITY("U-WATER", 28, 150, 75, 4, 10),
    PROPERTY("F3", 29, 5, 280, 24, 120, 360, 850, 1025, 1200, 150, 140),
    PROPERTY("G1", 31, 6, 300, 26, 130, 390, 900, 1100, 1275, 200, 150),
    PROPERTY("G2", 32, 6, 300, 26, 130, 390, 900, 1100, 1275, 200, 150),
    PROPERTY("G3", 34, 6, 320, 28, 150, 450, 1000, 1200, 1400, 200, 160),
    TRANSIT("T-SOUTH", 35, 200, 100, 25, 50, 100, 200),
    PROPERTY("H1", 37, 7, 350, 35, 175, 500, 1100, 1300, 1500, 200, 175),
    PROPERTY("H2", 39, 7, 400, 50, 200, 600, 1400, 1700, 2000, 200, 200),
};

constexpr TileDefinition kTiles40[] = {
    TILE("CORNER-START", TileKind::Start, kNoAsset, 0), TILE("A1", TileKind::Property, 0, 0),
    TILE("CARD-CF-1", TileKind::CivicFund, kNoAsset, 0), TILE("A2", TileKind::Property, 1, 0),
    TILE("FEE-CITY", TileKind::Fee, kNoAsset, 200), TILE("T-WEST", TileKind::Transit, 2, 0),
    TILE("B1", TileKind::Property, 3, 0), TILE("CARD-CE-1", TileKind::CityEvent, kNoAsset, 0),
    TILE("B2", TileKind::Property, 4, 0), TILE("B3", TileKind::Property, 5, 0),
    TILE("CORNER-HOLD", TileKind::Hold, kNoAsset, 0), TILE("C1", TileKind::Property, 6, 0),
    TILE("U-ENERGY", TileKind::Utility, 7, 0), TILE("C2", TileKind::Property, 8, 0),
    TILE("C3", TileKind::Property, 9, 0), TILE("T-NORTH", TileKind::Transit, 10, 0),
    TILE("D1", TileKind::Property, 11, 0), TILE("CARD-CF-2", TileKind::CivicFund, kNoAsset, 0),
    TILE("D2", TileKind::Property, 12, 0), TILE("D3", TileKind::Property, 13, 0),
    TILE("CORNER-REST", TileKind::Rest, kNoAsset, 0), TILE("E1", TileKind::Property, 14, 0),
    TILE("CARD-CE-2", TileKind::CityEvent, kNoAsset, 0), TILE("E2", TileKind::Property, 15, 0),
    TILE("E3", TileKind::Property, 16, 0), TILE("T-EAST", TileKind::Transit, 17, 0),
    TILE("F1", TileKind::Property, 18, 0), TILE("F2", TileKind::Property, 19, 0),
    TILE("U-WATER", TileKind::Utility, 20, 0), TILE("F3", TileKind::Property, 21, 0),
    TILE("CORNER-GOTO", TileKind::GoToHold, kNoAsset, 0), TILE("G1", TileKind::Property, 22, 0),
    TILE("G2", TileKind::Property, 23, 0), TILE("CARD-CF-3", TileKind::CivicFund, kNoAsset, 0),
    TILE("G3", TileKind::Property, 24, 0), TILE("T-SOUTH", TileKind::Transit, 25, 0),
    TILE("CARD-CE-3", TileKind::CityEvent, kNoAsset, 0), TILE("H1", TileKind::Property, 26, 0),
    TILE("FEE-DENSITY", TileKind::Fee, kNoAsset, 100), TILE("H2", TileKind::Property, 27, 0),
};

constexpr BoardDefinition kBoards[] = {
    {"grid-city-16-v1", 16, 9, 2, 3, 500, 80, 20, 12, 4, kTiles16, kAssets16},
    {"grid-city-24-v1", 24, 16, 2, 4, 800, 120, 30, 18, 6, kTiles24, kAssets24},
    {"grid-city-32-v1", 32, 23, 2, 6, 1200, 160, 40, 24, 10, kTiles32, kAssets32},
    {"grid-city-40-v1", 40, 28, 2, 6, 1500, 200, 50, 32, 12, kTiles40, kAssets40},
};

#undef PROPERTY
#undef TRANSIT
#undef UTILITY
#undef TILE

}  // namespace

std::size_t BoardCatalog::count() { return sizeof(kBoards) / sizeof(kBoards[0]); }

const BoardDefinition& BoardCatalog::at(std::size_t index) {
  if (index >= count()) {
    index = 0;
  }
  return kBoards[index];
}

const BoardDefinition* BoardCatalog::find(const char* id) {
  if (id == nullptr) {
    return nullptr;
  }
  for (const auto& board : kBoards) {
    if (std::strcmp(board.id, id) == 0) {
      return &board;
    }
  }
  return nullptr;
}

const BoardDefinition* BoardCatalog::findBySize(std::uint8_t tileCount) {
  for (const auto& board : kBoards) {
    if (board.tileCount == tileCount) {
      return &board;
    }
  }
  return nullptr;
}

}  // namespace gridopoly::core
