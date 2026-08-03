#include "demo_data.h"

const PlayerData kPlayers[kMaxPlayerCount] = {
    {"凌汐", "P1", 1860, 17},
    {"砾川", "P2", 1420, 9},
    {"诺瓦", "P3", 980, 12},
    {"岑蓝", "P4", 2040, 23},
    {"弦月", "P5", 760, 5},
    {"北辰", "P6", 1180, 2},
};

const AssetData kAssets[kAssetCount] = {
    {1, "霓虹港湾", "青绿组", 280, 24, 130, 0, false, false},
    {2, "\xE5\x85\x89\xE6\xA0\x85\xE5\x85\xAC\xE5\xAF\x93", "青绿组", 300, 28, 150, 0, false, false},
    {3, "\xE6\x95\xB0\xE6\x8D\xAE\xE9\xAB\x98\xE5\x9C\xB0", "蓝色组", 340, 32, 170, 0, false, false},
    {4, "云轨总站", "交通", 200, 25, 100, 0, true, false},
    {5, "量子电网", "公用事业", 150, 18, 75, 0, false, false},
    {6, "天穹广场", "蓝色组", 350, 35, 175, 3, false, false},
    {7, "\xE6\x97\xA7\xE5\x9F\x8E\xE8\x8A\xAF\xE5\xBB\x8A", "红色组", 240, 20, 120, 0, false, false},
    {8, "\xE6\x9E\x81\xE5\x85\x89\xE7\xA0\x81\xE5\xA4\xB4", "红色组", 320, 30, 160, 0, false, true},
};
