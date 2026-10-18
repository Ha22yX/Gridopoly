#pragma once

// Included by the real-socket HTTP integration harness; every mutation targets
// a private temporary AuthorityService, never a running device or game.
void verifyCoreDebugEdits() {
  using namespace gridopoly::core;
  using namespace gridopoly::protocol;
  for (const auto size : {16, 24, 32, 40}) {
    GameEngine engine;
    assert(engine.reset(*BoardCatalog::findBySize(size), 123));
    assert(engine.addPlayer("First", ControllerKind::Web));
    assert(engine.addPlayer("Second", ControllerKind::Web));
    assert(engine.start());
    const auto initial = engine.state();
    assert(engine.debugSetPlayerCash(1, 0));
    assert(engine.state().players[0].cash == 0);
    assert(engine.state().players[1].cash == initial.players[1].cash);
    assert(engine.state().stateVersion == initial.stateVersion + 1);
    const auto& history = engine.state().financialHistory[0];
    const auto& entry = history.entries[(history.head + kPlayerFinancialHistory - 1) % kPlayerFinancialHistory];
    assert(entry.kind == EventKind::DebugCashChanged && entry.amount == -initial.players[0].cash);
    assert(engine.debugSetPlayerCash(1, GameEngine::kMaximumDebugCash));
    const auto version = engine.state().stateVersion;
    const auto sequence = engine.state().nextEventSequence;
    assert(engine.debugSetPlayerCash(1, GameEngine::kMaximumDebugCash));
    assert(engine.state().stateVersion == version && engine.state().nextEventSequence == sequence);
    assert(!engine.debugSetPlayerCash(0, 1) && !engine.debugSetPlayerCash(3, 1));
    assert(!engine.debugSetPlayerCash(1, -1) && !engine.debugSetPlayerCash(1, 1000001));
    assert(!engine.debugSetAssetOwner(kNoAsset, 1) && !engine.debugSetAssetOwner(0, 3));
    const auto safe = engine.state();
    for (std::uint8_t i = 0; i < safe.board->assetCount; ++i) {
      assert(engine.debugSetAssetOwner(i, 1));
      assert(engine.state().assets[i].ownerId == 1);
      const auto before = engine.state().stateVersion;
      assert(engine.debugSetAssetOwner(i, 1));
      assert(engine.state().stateVersion == before);
      assert(engine.debugSetAssetOwner(i, 2));
      assert(engine.debugSetAssetOwner(i, 0));
      assert(engine.state().assets[i].ownerId == 0);
      assert(engine.state().players[0].cash == safe.players[0].cash);
      assert(engine.state().players[1].cash == safe.players[1].cash);
    }
    // Every pending workflow and active trade independently blocks both edits,
    // even when a restored state's phase alone appears permissive.
    const auto clean = engine.state();
    for (unsigned guard = 0; guard < 9; ++guard) {
      engine.mutableStateForRestore() = clean;
      auto& state = engine.mutableStateForRestore();
      if (guard == 0) state.pendingMove.active = true;
      if (guard == 1) state.pendingPurchase.active = true;
      if (guard == 2) state.pendingCard.active = true;
      if (guard == 3) state.pendingDebt.active = true;
      if (guard == 4) state.auction.active = true;
      if (guard == 5) state.trades[0].status = TradeWorkflowStatus::Offered;
      if (guard == 6) state.trades[1].status = TradeWorkflowStatus::Countered;
      if (guard == 7) state.phase = GamePhase::Lobby;
      if (guard == 8) state.phase = GamePhase::GameOver;
      assert(!engine.debugSetPlayerCash(1, 10) && !engine.debugSetAssetOwner(0, 1));
      assert(state.stateVersion == clean.stateVersion && state.nextEventSequence == clean.nextEventSequence);
      assert(state.players[0].cash == clean.players[0].cash && state.assets[0].ownerId == 0);
    }
    engine.mutableStateForRestore() = clean;
    engine.mutableStateForRestore().players[1].bankrupt = true;
    assert(!engine.debugSetPlayerCash(2, 1) && !engine.debugSetAssetOwner(0, 2));
    engine.mutableStateForRestore() = clean;
    engine.mutableStateForRestore().assets[0].mortgaged = true;
    assert(!engine.debugSetAssetOwner(0, 1));
    engine.mutableStateForRestore() = clean;
    bool testedSibling = false;
    for (std::uint8_t i = 0; i < clean.board->assetCount; ++i) {
      for (std::uint8_t j = 0; j < clean.board->assetCount; ++j) {
        if (i == j || clean.board->assets[i].kind != TileKind::Property ||
            clean.board->assets[j].kind != TileKind::Property ||
            clean.board->assets[i].group != clean.board->assets[j].group) continue;
        engine.mutableStateForRestore().assets[j].buildingLevel = 1;
        assert(!engine.debugSetAssetOwner(i, 1));
        engine.mutableStateForRestore() = clean;
        testedSibling = true;
      }
    }
    assert(testedSibling);
    engine.mutableStateForRestore().phase = GamePhase::TurnEnd;
    assert(engine.debugSetPlayerCash(1, 555));
    assert(engine.debugSetAssetOwner(0, 2));
  }
  std::cout << "GRIDOPOLY_DEBUG_CORE_GUARDS_PASS boards=4\n";
}

void verifyDebugHttpEdits(const std::filesystem::path& directory,
                         const std::filesystem::path& components) {
  using namespace gridopoly::core;
  using namespace gridopoly::pi;
  using namespace gridopoly::protocol;
  std::filesystem::create_directories(directory);
  AuthorityIdentityOptions options;
  options.identityPath = directory / "identity.bin";
  options.avatarComponentRoot = components;
  options.avatarAssetRoot = directory / "avatars";
  const auto statePath = directory / "state.bin";
  AuthorityService authority(statePath, directory / "authority.meta", 0x445566u,
                             std::chrono::milliseconds(1200), options);
  assert(authority.initialize() && authority.flush());
  UdpPlayerServer udp(authority, "isolated-debug-test", directory / "registry.bin",
                      "127.0.0.1", "127.255.255.255", 0);
  HttpServer http(authority, udp, 0, "127.0.0.1", 4, directory / "tiles");
  assert(http.start());
  const auto room = authority.roomId();
  auto context = [&] { return "roomId=" + std::to_string(room) + "&expectedVersion=" +
      std::to_string(authority.stateVersion()); };
  auto cashUrl = [&](const std::string& value) { return "/api/debug/player-cash?" + context() +
      "&playerId=1&cash=" + value; };
  const auto before = authority.stateCopy();
  const auto initialCashUrl = cashUrl("5000");
  const auto updated = post(http.port(), initialCashUrl);
  assert(updated.status == 200 && bodyText(updated).find("\"changed\":true") != std::string::npos);
  assert(authority.stateCopy().players[0].cash == 5000);
  assert(authority.stateVersion() == before.stateVersion + 1);
  assert(post(http.port(), initialCashUrl).status == 409);
  const auto version = authority.stateVersion();
  assert(bodyText(post(http.port(), cashUrl("5000"))).find("\"changed\":false") != std::string::npos);
  assert(authority.stateVersion() == version);
  for (const auto* value : {"-1", "1000001", "4294967296", "1.5", "1e3", "12x", "", "%2B1", "%201"})
    assert(post(http.port(), cashUrl(value)).status == 400);
  assert(get(http.port(), cashUrl("100")).status == 405);
  assert(postJson(http.port(), cashUrl("100"), "{}").status == 400);
  assert(post(http.port(), cashUrl("100") + "&cash=200").status == 400);
  assert(post(http.port(), "/api/debug/player-cash?playerId=1&cash=100").status == 400);
  assert(post(http.port(), "/api/debug/player-cash?" + context() + "&playerId=257&cash=100").status == 400);
  const auto* board = before.board;
  const std::string tile = board->tiles[board->assets[0].tileIndex].id;
  auto ownerUrl = [&](const std::string& owner) { return "/api/debug/asset-owner?" + context() +
      "&tileId=" + tile + "&ownerId=" + owner; };
  assert(post(http.port(), ownerUrl("2")).status == 200);
  assert(authority.stateCopy().assets[0].ownerId == 2 && authority.stateCopy().players[1].cash == before.players[1].cash);
  assert(post(http.port(), ownerUrl("0")).status == 200);
  assert(authority.stateCopy().assets[0].ownerId == 0);
  for (const auto* owner : {"-1", "256", "9", "1x"}) assert(post(http.port(), ownerUrl(owner)).status == 400);
  assert(post(http.port(), "/api/debug/asset-owner?" + context() + "&tileId=does-not-exist&ownerId=1").status == 400);
  assert(post(http.port(), "/api/debug/asset-owner?" + context() + "&tileId=" + board->tiles[0].id + "&ownerId=1").status == 400);

  // A saved edit is immediately recoverable, including its ledger and event.
  GameEngine restored;
  assert(FileStateStore(statePath).restore(restored));
  assert(restored.state().players[0].cash == 5000 && restored.state().assets[0].ownerId == 0);
  assert(restored.state().stateVersion == authority.stateVersion());
  PlayerDetailResponse detail;
  assert(authority.makePlayerDetail(1, 1, authority.stateVersion(), detail));
  assert(detail.ledgerCount && detail.ledger[0].kind == static_cast<std::uint8_t>(EventKind::DebugCashChanged));
  assert(detail.ledger[0].amount == 5000 - before.players[0].cash);
  std::array<std::uint8_t, 1024> wire{}; std::size_t written = 0;
  assert(encodePlayerDetailResponse(detail, wire.data(), wire.size(), written));
  PlayerDetailResponse decoded;
  assert(decodePlayerDetailResponse(wire.data(), written, decoded) && decoded.ledger[0].kind == detail.ledger[0].kind);

  // Force the atomic-save temp file to fail without changing the saved state.
  const auto savedBytes = readBytes(statePath);
  const auto unchanged = authority.stateCopy();
  assert(std::filesystem::create_directory(directory / "state.bin.tmp"));
  assert(post(http.port(), cashUrl("123")).status == 500);
  assert(authority.stateVersion() == unchanged.stateVersion && authority.latestEventSequence() == unchanged.nextEventSequence - 1);
  assert(authority.stateCopy().players[0].cash == unchanged.players[0].cash && readBytes(statePath) == savedBytes);
  assert(std::filesystem::remove(directory / "state.bin.tmp"));

  // Two editors with the same version cannot both overwrite the state.
  const auto firstUrl = cashUrl("6000"), secondUrl = cashUrl("7000");
  HttpResponse first, second;
  std::thread one([&] { first = post(http.port(), firstUrl); });
  std::thread two([&] { second = post(http.port(), secondUrl); });
  one.join(); two.join();
  assert((first.status == 200 && second.status == 409) || (first.status == 409 && second.status == 200));
  assert(authority.stateVersion() == unchanged.stateVersion + 1);
  const auto staleRoomUrl = cashUrl("8000");
  assert(authority.newGame(16, 1, 3));
  assert(authority.roomId() != room && post(http.port(), staleRoomUrl).status == 409);
  http.stop();
  std::cout << "GRIDOPOLY_DEBUG_HTTP_PERSISTENCE_CONCURRENCY_PASS\n";
}
