#include "demo_data.h"

const PlayerData kPlayers[kMaxPlayerCount] = {
    {"Lynn", "P1", 1860, 17},
    {"Flint", "P2", 1420, 9},
    {"Nova", "P3", 980, 12},
    {"Cyan", "P4", 2040, 23},
    {"Luna", "P5", 760, 5},
    {"Polaris", "P6", 1180, 2},
};

const AssetData kAssets[kAssetCount] = {
    {1, "Neon Harbor", "TEAL GROUP", 280, 24, 130, 0, false, false},
    {2, "Raster Flats", "TEAL GROUP", 300, 28, 150, 0, false, false},
    {3, "Data Heights", "BLUE GROUP", 340, 32, 170, 0, false, false},
    {4, "Skyrail Central", "TRANSIT", 200, 25, 100, 0, true, false},
    {5, "Quantum Grid", "UTILITY", 150, 18, 75, 0, false, false},
    {6, "Sky Plaza", "BLUE GROUP", 350, 35, 175, 3, false, false},
    {7, "Old Chip Row", "RED GROUP", 240, 20, 120, 0, false, false},
    {8, "Aurora Wharf", "RED GROUP", 320, 30, 160, 0, false, true},
};
