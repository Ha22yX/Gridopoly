#include "demo_engine.h"

#include <cstdio>

namespace gridopoly::tile {
namespace {

constexpr std::size_t kScenarioCount = 9;

TileState baseState(std::uint8_t map_index, const char *id, const char *name,
                    const char *subtitle, TileKind kind, TileActivity activity, ArtworkId artwork,
                    Rgb accent, const char *status, const char *detail) {
  TileState state{};
  state.map_index = map_index;
  copyTileText(state.tile_id, sizeof(state.tile_id), id);
  copyTileText(state.display_name, sizeof(state.display_name), name);
  copyTileText(state.subtitle, sizeof(state.subtitle), subtitle);
  state.kind = kind;
  state.activity = activity;
  state.artwork = artwork;
  state.accent = accent;
  copyTileText(state.status, sizeof(state.status), status);
  copyTileText(state.detail, sizeof(state.detail), detail);
  return state;
}

}  // namespace

std::size_t demoScenarioCount() { return kScenarioCount; }

TileState makeDemoScenario(std::size_t index) {
  TileState state{};
  switch (index % kScenarioCount) {
    case 0:
      state = baseState(1, "A1", "Rivet Row", "FORGE QUARTER", TileKind::Property,
                        TileActivity::Available, ArtworkId::RivetRow, {201, 120, 82}, "AVAILABLE",
                        "BUY OR START AUCTION");
      state.purchase_price = 60;
      state.current_rent = 2;
      state.occupied_players = 0x01;
      state.active_player = 1;
      break;
    case 1:
      state = baseState(1, "A1", "Rivet Row", "FORGE QUARTER", TileKind::Property,
                        TileActivity::RentDue, ArtworkId::RivetRow, {201, 120, 82}, "OWNED BY PLAYER 2",
                        "PLAYER 4 PAYS PLAYER 2");
      state.purchase_price = 60;
      state.current_rent = 30;
      state.owner_player = 2;
      copyTileText(state.owner_display_name, sizeof(state.owner_display_name), "Player 2");
      state.owner_color = {239, 113, 104};
      state.building_level = 2;
      state.occupied_players = 0x08;
      state.active_player = 4;
      break;
    case 2:
      state = baseState(24, "E3", "Nova Avenue", "PULSE QUARTER", TileKind::Property,
                        TileActivity::Owned, ArtworkId::NovaAvenue, {232, 93, 99}, "OWNED BY PLAYER 5",
                        "LEVEL 4 DEVELOPMENT");
      state.purchase_price = 280;
      state.current_rent = 1100;
      state.owner_player = 5;
      copyTileText(state.owner_display_name, sizeof(state.owner_display_name), "Player 5");
      state.owner_color = {194, 138, 232};
      state.building_level = 4;
      state.occupied_players = 0x14;
      break;
    case 3:
      state = baseState(5, "T-WEST", "Westline Terminal", "TRANSIT", TileKind::Transit,
                        TileActivity::Owned, ArtworkId::WestlineTerminal, {170, 184, 189}, "OWNED BY PLAYER 3",
                        "2 HUBS OWNED");
      state.purchase_price = 200;
      state.current_rent = 50;
      state.owner_player = 3;
      copyTileText(state.owner_display_name, sizeof(state.owner_display_name), "Player 3");
      state.owner_color = {82, 220, 183};
      state.occupied_players = 0x04;
      state.active_player = 3;
      break;
    case 4:
      state = baseState(12, "U-ENERGY", "Metro Grid", "UTILITY", TileKind::Utility,
                        TileActivity::RentDue, ArtworkId::MetroGrid, {80, 209, 177}, "OWNED BY PLAYER 1",
                        "DICE X 4");
      state.purchase_price = 150;
      state.current_rent = 32;
      state.owner_player = 1;
      copyTileText(state.owner_display_name, sizeof(state.owner_display_name), "Player 1");
      state.owner_color = {88, 167, 235};
      state.occupied_players = 0x20;
      state.active_player = 6;
      break;
    case 5:
      state = baseState(7, "CARD-CE-1", "Chance", "CITY EVENT", TileKind::Card,
                        TileActivity::DrawCard, ArtworkId::Chance, {239, 140, 74}, "DRAW A CARD",
                        "FOLLOW CONTROLLER EVENT");
      state.occupied_players = 0x02;
      state.active_player = 2;
      break;
    case 6:
      state = baseState(4, "FEE-CITY", "Income Tax", "CITY FEE", TileKind::Fee,
                        TileActivity::PayFee, ArtworkId::IncomeTax, {240, 166, 91}, "PAY 120 CREDITS",
                        "PAYMENT REQUIRED");
      state.purchase_price = 120;
      state.occupied_players = 0x08;
      state.active_player = 4;
      break;
    case 7:
      state = baseState(0, "CORNER-START", "Grid Central", "START", TileKind::Start,
                        TileActivity::Reward, ArtworkId::CentralLaunch, {233, 238, 240}, "+200 CREDITS",
                        "PASS OR LAND REWARD");
      state.purchase_price = 200;
      state.occupied_players = 0x15;
      state.active_player = 1;
      break;
    case 8:
      state = baseState(10, "CORNER-HOLD", "Holding Area", "VISITING", TileKind::Hold,
                        TileActivity::Restricted, ArtworkId::CivicHold, {233, 238, 240}, "JUST VISITING",
                        "NO ACTION REQUIRED");
      state.occupied_players = 0x22;
      state.active_player = 6;
      break;
  }
  normalizeTileState(state);
  return state;
}

DemoEngine::DemoEngine(std::uint32_t interval_ms)
    : interval_ms_(interval_ms == 0 ? 1 : interval_ms),
      state_(makeDemoScenario(0)) {}

bool DemoEngine::update(std::uint32_t now_ms) {
  if (!autoplay_ || static_cast<std::uint32_t>(now_ms - changed_at_ms_) < interval_ms_) {
    return false;
  }
  next(now_ms);
  return true;
}

void DemoEngine::load(std::size_t index, std::uint32_t now_ms) {
  index_ = index % demoScenarioCount();
  state_ = makeDemoScenario(index_);
  changed_at_ms_ = now_ms;
}

void DemoEngine::next(std::uint32_t now_ms) { load(index_ + 1, now_ms); }

void DemoEngine::previous(std::uint32_t now_ms) {
  load(index_ == 0 ? demoScenarioCount() - 1 : index_ - 1, now_ms);
}

void DemoEngine::select(std::size_t index, std::uint32_t now_ms) {
  load(index, now_ms);
}

void DemoEngine::setAutoplay(bool enabled, std::uint32_t now_ms) {
  autoplay_ = enabled;
  changed_at_ms_ = now_ms;
}

}  // namespace gridopoly::tile

