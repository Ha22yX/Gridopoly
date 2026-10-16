// Included by http_asset_integration_tests.cpp inside its anonymous namespace.
class DeadlineTestPeer {
 public:
  explicit DeadlineTestPeer(std::uint16_t port) {
    descriptor_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(descriptor_ >= 0);
    const timeval timeout{5, 0};
    const int enabled = 1;
    assert(::setsockopt(descriptor_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    assert(::setsockopt(descriptor_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
    assert(::setsockopt(descriptor_, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(descriptor_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
  }
  ~DeadlineTestPeer() { ::close(descriptor_); }
  DeadlineTestPeer(const DeadlineTestPeer&) = delete;
  DeadlineTestPeer& operator=(const DeadlineTestPeer&) = delete;

  void send(const std::string& text) const {
    std::size_t sent = 0;
    while (sent < text.size()) {
      const auto count = ::send(descriptor_, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
      if (count < 0 && errno == EINTR) continue;
      assert(count > 0);
      sent += static_cast<std::size_t>(count);
    }
  }

  HttpResponse receive() const {
    std::string raw;
    char buffer[8192];
    auto append = [&] {
      const auto count = ::recv(descriptor_, buffer, sizeof(buffer), 0);
      assert(count > 0);
      raw.append(buffer, static_cast<std::size_t>(count));
    };
    while (raw.find("\r\n\r\n") == std::string::npos) append();
    const auto headerEnd = raw.find("\r\n\r\n");
    std::istringstream lines(raw.substr(0, headerEnd));
    std::string line;
    assert(static_cast<bool>(std::getline(lines, line)));
    HttpResponse result{};
    result.status = std::stoi(line.substr(line.find(' ') + 1, 3));
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const auto colon = line.find(':');
      assert(colon != std::string::npos);
      auto start = colon + 1;
      while (start < line.size() && line[start] == ' ') ++start;
      result.headers[lower(line.substr(0, colon))] = line.substr(start);
    }
    const auto end = headerEnd + 4 + std::stoull(result.headers.at("content-length"));
    while (raw.size() < end) append();
    assert(raw.size() == end);
    result.body.assign(raw.begin() + static_cast<std::ptrdiff_t>(headerEnd + 4), raw.end());
    return result;
  }

  HttpResponse health() const {
    send("GET /health HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
    auto result = receive();
    assert(result.status == 200);
    return result;
  }

  void expectClosed() const {
    char byte{};
    const auto count = ::recv(descriptor_, &byte, 1, 0);
    assert(count == 0 || (count < 0 && errno == ECONNRESET));
  }

 private:
  int descriptor_{-1};
};

void verifyHttpRequestDeadlines(gridopoly::pi::AuthorityService& authority,
                                gridopoly::pi::UdpPlayerServer& udp,
                                const std::filesystem::path& assets) {
  using Clock = std::chrono::steady_clock;
  using namespace std::chrono_literals;
  gridopoly::pi::HttpServer http(authority, udp, 0, "127.0.0.1", 4, assets);
  assert(http.start());
  const std::string body =
      "{\"tagReaderState\":\"stable\",\"tagRevision\":1,\"tags\":[],\"overflow\":false}";
  const std::string headers =
      "POST /api/tile-modules/heartbeat?moduleId=deadline-probe&deviceId=deadline-probe HTTP/1.1\r\n"
      "Host: localhost\r\nConnection: keep-alive\r\nContent-Type: application/json\r\n"
      "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  auto sendDelayedBody = [&](const DeadlineTestPeer& peer) {
    peer.send(headers);
    std::this_thread::sleep_for(1400ms);
    peer.send(body);
    const auto response = peer.receive();
    assert(response.status == 200);
    assert(bodyText(response).find("\"ok\":true") != std::string::npos);
    assert(response.headers.at("connection") == "keep-alive");
  };

  // The request budget starts with bytes, for both fresh and reused connections.
  for (const auto idle : {0ms, 2000ms}) {
    DeadlineTestPeer peer(http.port());
    std::this_thread::sleep_for(idle);
    sendDelayedBody(peer);
  }
  {
    DeadlineTestPeer peer(http.port());
    peer.health();
    for (int attempt = 0; attempt < 2; ++attempt) {
      std::this_thread::sleep_for(2000ms);
      sendDelayedBody(peer);
    }
  }
  assert(http.diagnostics().readTimeouts == 0);
  std::cout << "HTTP_DEADLINE_DELAYED_BODY first=2 reused=2 status=200\n";

  // Idle keep-alive close remains normal; a silent first request is a timeout.
  {
    DeadlineTestPeer peer(http.port());
    peer.health();
    const auto started = Clock::now();
    peer.expectClosed();
    const auto elapsed = Clock::now() - started;
    assert(elapsed >= 2700ms && elapsed < 4000ms);
    assert(http.diagnostics().readTimeouts == 0);
  }
  {
    DeadlineTestPeer peer(http.port());
    const auto started = Clock::now();
    peer.expectClosed();
    const auto elapsed = Clock::now() - started;
    assert(elapsed >= 2700ms && elapsed < 4000ms);
    assert(http.diagnostics().readTimeouts == 1);
  }

  // Keep sending valid fragments: neither completing the header nor later body
  // bytes may renew the single fixed request budget that began with the "P".
  {
    DeadlineTestPeer peer(http.port());
    peer.health();
    std::this_thread::sleep_for(2000ms);
    const auto started = Clock::now();
    peer.send(headers.substr(0, 1));
    std::this_thread::sleep_for(1000ms);
    peer.send(headers.substr(1));
    std::this_thread::sleep_for(1000ms);
    peer.send(body.substr(0, 1));
    std::this_thread::sleep_for(500ms);
    peer.send(body.substr(1, 1));
    peer.expectClosed();
    const auto elapsed = Clock::now() - started;
    assert(elapsed >= 2700ms && elapsed < 4000ms);
    assert(http.diagnostics().readTimeouts == 2);
    std::cout << "HTTP_DEADLINE_FRAGMENT_LIMIT elapsed_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
  }

  {
    DeadlineTestPeer peer(http.port());
    for (unsigned index = 1; index <= 100; ++index) {
      const auto response = peer.health();
      assert(response.headers.at("connection") == (index == 100 ? "close" : "keep-alive"));
      if (index < 100) {
        assert(response.headers.at("keep-alive") == "timeout=3, max=" + std::to_string(100 - index));
      } else {
        assert(response.headers.count("keep-alive") == 0);
      }
    }
    peer.expectClosed();
  }
  assert(get(http.port(), "/health").status == 200);
  assert(http.diagnostics().readTimeouts == 2);
  assert(http.diagnostics().sendErrors == 0);
  assert(http.developmentAssetStats().admitted == 0);
  http.stop();
  std::cout << "HTTP_DEADLINE_TESTS_PASS idle_close=yes first_timeout=yes fixed_budget=yes hundred_close=yes\n";
}
