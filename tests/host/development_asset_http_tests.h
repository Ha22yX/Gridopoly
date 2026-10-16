#pragma once

// Included in http_asset_integration_tests.cpp, sharing its actual socket
// helpers. All requests are loopback and all files/game state are temporary.
void verifyDevelopmentAdmission() {
  using Limiter = gridopoly::pi::DevelopmentAssetLimiter;
  const auto start = Limiter::Clock::time_point{};
  Limiter limiter(32, start);
  std::vector<std::shared_ptr<Limiter::Permit>> permits;
  for (unsigned i = 0; i < 4; ++i) permits.push_back(limiter.acquire(start));
  for (const auto& permit : permits) assert(permit);
  assert(!limiter.acquire(start + std::chrono::seconds(1)));
  assert(limiter.stats().active == 4 && limiter.stats().peak == 4);
  permits.clear();
  for (unsigned i = 0; i < 4; ++i) assert(limiter.acquire(start + std::chrono::seconds(1)));
  assert(!limiter.acquire(start + std::chrono::milliseconds(1124)));
  assert(limiter.acquire(start + std::chrono::milliseconds(1125)));
  assert(!limiter.acquire(start + std::chrono::milliseconds(1000)));
  assert(limiter.stats().active == 0);
  Limiter small(4, start);
  auto a = small.acquire(start), b = small.acquire(start);
  assert(a && b && !small.acquire(start) && small.stats().limit == 2);
}

void verifyDevelopmentSlowClients(gridopoly::pi::HttpServer& http, unsigned limit) {
  const std::string route = "/assets/development/v1/tile/a1-rivet-row-l1.rgb565";
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  std::vector<int> sockets;
  for (unsigned i = 0; i < limit; ++i) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    const int window = 1024;
    assert(::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &window, sizeof(window)) == 0);
    assert(::setsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &window, sizeof(window)) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(http.port());
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const std::string requestText = "GET " + route + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
    assert(::send(fd, requestText.data(), requestText.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(requestText.size()));
    sockets.push_back(fd);
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (http.developmentAssetStats().active != limit && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(http.developmentAssetStats().active == limit);
  const auto begin = std::chrono::steady_clock::now();
  const auto busy = get(http.port(), route);
  assert(busy.status == 503 && busy.headers.at("retry-after") == "1");
  assert(get(http.port(), "/health").status == 200);
  assert(post(http.port(), "/api/tile-modules/heartbeat?moduleId=slow-load&deviceId=slow-load-device").status == 200);
  assert(get(http.port(), "/assets/avatar-components/v1/hair/h1.gavc").status == 200);
  const auto latency = std::chrono::steady_clock::now() - begin;
  assert(latency < std::chrono::milliseconds(1500));
  // Abort in-flight sends. Every permit must be released on the error path.
  for (int fd : sockets) {
    const linger reset{1, 0};
    assert(::setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0);
    ::close(fd);
  }
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (http.developmentAssetStats().active != 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  assert(http.developmentAssetStats().active == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  assert(get(http.port(), route).status == 200);
  std::cout << "DEVELOPMENT_HTTP_SLOW_SEND slots=" << limit
            << " controls_and_avatar_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(latency).count()
            << " abort_releases=yes\n";
}

void verifyDevelopmentLevelWire(const std::filesystem::path& source,
                                const std::filesystem::path& temporary) {
  using namespace gridopoly::core;
  const auto root = temporary / "development-wire";
  std::filesystem::create_directories(root);
  GameEngine engine;
  assert(engine.reset(*BoardCatalog::findBySize(32), 127));
  assert(engine.addPlayer("Artwork Player", ControllerKind::RealConsole, true));
  assert(engine.addPlayer("Artwork Bot", ControllerKind::Bot, true));
  assert(engine.start());
  auto state = engine.state();
  const auto asset = state.board->tiles[1].assetIndex;
  assert(std::string(state.board->tiles[1].id) == "A1" && asset != kNoAsset);
  state.assets[asset].ownerId = 1;
  state.assets[asset].buildingLevel = 5;
  gridopoly::pi::FileStateStore store(root / "state.bin");
  assert(store.save(state));
  gridopoly::pi::AuthorityIdentityOptions options;
  options.identityPath = root / "identity.bin";
  options.avatarAssetRoot = root / "avatars";
  options.avatarComponentRoot = source / "Assets/GridCity/Avatars/V1/runtime/components-v1";
  gridopoly::pi::AuthorityService authority(root / "state.bin", root / "authority.meta", 0x55443322,
      std::chrono::milliseconds(1200), options);
  assert(authority.initialize());
  gridopoly::pi::UdpPlayerServer udp(authority, "development-wire-isolated-only",
      root / "registry.bin", "127.0.0.1", "127.255.255.255", 0);
  gridopoly::pi::HttpServer http(authority, udp, 0, "127.0.0.1", 4, temporary / "tiles");
  assert(http.start());
  const auto version = authority.stateVersion();
  const std::string heartbeat = "/api/tile-modules/heartbeat?moduleId=level-wire&deviceId=level-wire-device";
  assert(post(http.port(), heartbeat).status == 200);
  const auto assignment = post(http.port(), "/api/tile-debug/assignment?moduleId=level-wire&deviceId=level-wire-device&tileId=A1&buildingLevel=1");
  assert(assignment.status == 200 && bodyText(assignment).find("\"buildingLevel\":5") != std::string::npos);
  const auto response = post(http.port(), heartbeat);
  assert(response.status == 200 && bodyText(response).find("\"buildingLevel\":5") != std::string::npos);
  assert(bodyText(response).find("\"artworkKey\":\"a1-rivet-row\"") != std::string::npos);
  assert(get(http.port(), "/assets/development/v1/tile/a1-rivet-row-l5.rgb565").status == 200);
  assert(authority.stateVersion() == version && authority.stateCopy().assets[asset].buildingLevel == 5);
  http.stop();
}

void verifyDevelopmentAssets(const std::filesystem::path& source,
                             const std::filesystem::path& temporary,
                             gridopoly::pi::AuthorityService& authority,
                             gridopoly::pi::UdpPlayerServer& udp) {
  using namespace gridopoly::assets;
  verifyDevelopmentAdmission();
  const auto assets = source / "Assets/GridCity/DevelopmentV1";
  const auto target = temporary / "tiles/development/v1";
  for (const auto* directory : {"tile", "player", "web"}) std::filesystem::create_directories(target / directory);
  // Verify the server's allow-list and exact reader against every staged file,
  // without stretching the HTTP test by 330 rate-limited sequential downloads.
  std::size_t checked = 0;
  for (const auto& property : kDevelopmentProperties) {
    for (unsigned level = 1; level <= 5; ++level) {
      for (const auto* directory : {"tile", "player", "web"}) {
        const bool png = std::string(directory) == "web";
        const auto name = std::string(property.key) + "-l" + std::to_string(level) + (png ? ".png" : ".rgb565");
        const auto origin = assets / (png ? "device" : std::string("rgb565-") + directory) / name;
        std::filesystem::copy_file(origin, target / directory / name);
        gridopoly::pi::DevelopmentAssetFile file;
        assert(gridopoly::pi::developmentAssetFile(std::string(kDevelopmentAssetPrefix) + directory + '/' + name, file));
        std::string body;
        assert(gridopoly::pi::readDevelopmentAsset(temporary / "tiles", file, body));
        const auto truth = readBytes(origin);
        assert(body == std::string(truth.begin(), truth.end()));
        ++checked;
      }
    }
  }
  assert(checked == 330);
  gridopoly::pi::HttpServer http(authority, udp, 0, "127.0.0.1", 32, temporary / "tiles");
  assert(http.start() && http.port() != 80 && http.port() != 4242);
  const auto room = authority.roomId(), version = authority.stateVersion();
  const std::string route = "/assets/development/v1/tile/a1-rivet-row-l1.rgb565";
  const auto truth = readBytes(assets / "rgb565-tile/a1-rivet-row-l1.rgb565");
  auto retry = [&](const std::string& path, const std::string& headers = {}) {
    auto response = getWithHeader(http.port(), path, headers);
    if (response.status == 503) {
      assert(response.headers.at("retry-after") == "1");
      std::this_thread::sleep_for(std::chrono::seconds(1));
      response = getWithHeader(http.port(), path, headers);
    }
    return response;
  };
  const auto tile = retry(route);
  assert(tile.status == 200 && tile.body == truth);
  assert(tile.headers.at("content-length") == "51200");
  assert(tile.headers.at("content-type") == "application/octet-stream");
  assert(tile.headers.count("content-encoding") == 0 && tile.headers.count("transfer-encoding") == 0);
  assert(tile.headers.at("connection") == "close");
  assert(tile.headers.at("cache-control") == "public, max-age=31536000, immutable");
  const auto player = retry("/assets/development/v1/player/a1-rivet-row-l5.rgb565");
  assert(player.status == 200 && player.body == readBytes(assets / "rgb565-player/a1-rivet-row-l5.rgb565"));
  assert(player.headers.at("content-length") == "32768");
  const auto web = retry("/assets/development/v1/web/a1-rivet-row-l1.png");
  assert(web.status == 200 && web.body == readBytes(assets / "device/a1-rivet-row-l1.png"));
  assert(web.headers.at("content-type") == "image/png");
  const auto cached = retry(route, "If-None-Match: \"other\", W/" + tile.headers.at("etag") + "\r\n");
  assert(cached.status == 304 && cached.body.empty() && cached.headers.at("etag") == tile.headers.at("etag"));
  assert(cached.headers.count("content-length") == 0);
  assert(retry(route, "If-None-Match: \"prefix" + tile.headers.at("etag") + "suffix\"\r\n").status == 200);
  for (const auto* bad : {"tile/a1-rivet-row-l0.rgb565", "tile/a1-rivet-row-l6.rgb565",
       "tile/a1-rivet-row-l01.rgb565", "tile/A1-rivet-row-l1.rgb565", "tile/not-a-property-l1.rgb565",
       "tile/../authority.meta", "tile/%2e%2e%2fauthority.meta", "tile/a1-rivet-row-l1.rgb565%00.png",
       "tile/a1-rivet-row-l1.png", "web/a1-rivet-row-l1.rgb565", "player//a1-rivet-row-l1.rgb565",
       "tile/a1-rivet-row-l1.rgb565/extra", "other/a1-rivet-row-l1.rgb565"}) {
    const auto response = get(http.port(), std::string(kDevelopmentAssetPrefix) + bad);
    assert(response.status == 404 && response.headers.at("cache-control") == "no-store");
  }
  assert(get(http.port(), "/assets/development/v2/tile/a1-rivet-row-l1.rgb565").status == 404);
  assert(post(http.port(), route).status == 405);
  const auto file = target / "tile/a1-rivet-row-l1.rgb565";
  // Truncated, oversized and same-size corrupted files must never produce 200
  // or a misleading 304, even when the client holds the formerly valid ETag.
  auto malformed = truth; malformed.pop_back();
  writeBytes(file, malformed);
  assert(retry(route, "If-None-Match: " + tile.headers.at("etag") + "\r\n").status == 404);
  malformed = truth; malformed.push_back(0); writeBytes(file, malformed);
  assert(retry(route).status == 404);
  malformed = truth; malformed[120] ^= 1; writeBytes(file, malformed);
  assert(retry(route).status == 404);
  std::filesystem::remove(file);
  assert(retry(route).status == 404);
  std::filesystem::create_symlink(assets / "rgb565-tile/a1-rivet-row-l1.rgb565", file);
  assert(retry(route).status == 404);
  std::filesystem::remove(file); writeBytes(file, truth);
  const auto webFile = target / "web/a1-rivet-row-l1.png";
  const auto pngTruth = readBytes(webFile);
  malformed = pngTruth; malformed[40] ^= 1; writeBytes(webFile, malformed);
  assert(retry("/assets/development/v1/web/a1-rivet-row-l1.png").status == 404);
  writeBytes(webFile, pngTruth);
  for (const unsigned count : {32u, 40u}) {
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::vector<HttpResponse> responses(count);
    std::vector<std::thread> clients;
    for (unsigned i = 0; i < count; ++i) clients.emplace_back([&, i] {
      ++ready;
      while (!start.load()) std::this_thread::yield();
      responses[i] = get(http.port(), route);
    });
    std::chrono::steady_clock::duration maxControl{};
    std::thread control([&] {
      ++ready;
      while (!start.load()) std::this_thread::yield();
      for (unsigned i = 0; i < 8; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        assert(get(http.port(), "/health").status == 200);
        assert(get(http.port(), "/api/state").status == 200);
        const auto heartbeat = post(http.port(), "/api/tile-modules/heartbeat?moduleId=load-test&deviceId=load-test-device");
        assert(heartbeat.status == 200 && bodyText(heartbeat).find("\"buildingLevel\":0") != std::string::npos);
        maxControl = std::max(maxControl, std::chrono::steady_clock::now() - begin);
      }
    });
    while (ready.load() != count + 1) std::this_thread::yield();
    const auto begin = std::chrono::steady_clock::now();
    start = true;
    for (auto& client : clients) client.join();
    control.join();
    unsigned ok = 0, busy = 0;
    for (const auto& response : responses) {
      assert(response.headers.at("connection") == "close");
      if (response.status == 200) { ++ok; assert(response.body == truth); }
      else { ++busy; assert(response.status == 503 && response.headers.at("retry-after") == "1"); }
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    const auto budget = 4 + static_cast<unsigned>(std::chrono::duration<double>(elapsed).count() * 8);
    assert(ok > 0 && busy > 0 && ok <= budget);
    assert(maxControl < std::chrono::milliseconds(1500));
    const auto stats = http.developmentAssetStats();
    assert(stats.active == 0 && stats.peak <= 4 && stats.limit == 4);
    std::cout << "DEVELOPMENT_HTTP_BURST clients=" << count << " ok=" << ok << " busy=" << busy
              << " control_batch_max_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(maxControl).count()
              << " active_peak=" << stats.peak << '\n';
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    assert(get(http.port(), route).status == 200);
  }
  verifyDevelopmentSlowClients(http, 4);
  assert(authority.roomId() == room && authority.stateVersion() == version);
  http.stop();
  gridopoly::pi::HttpServer small(authority, udp, 0, "127.0.0.1", 4, temporary / "tiles");
  assert(small.start());
  verifyDevelopmentSlowClients(small, 2);
  small.stop();
  verifyDevelopmentLevelWire(source, temporary);
  std::cout << "DEVELOPMENT_ASSETS_HTTP_PASS exact_files=" << checked << '\n';
}
