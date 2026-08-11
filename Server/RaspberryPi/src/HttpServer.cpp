#include "HttpServer.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <netinet/tcp.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include "../../../Firmware/TestGameServer/src/WebUiGzip.h"

namespace gridopoly::pi {
namespace {

using namespace gridopoly::core;
using namespace gridopoly::protocol;

constexpr auto kQueuedClientFairnessDeadline = std::chrono::milliseconds(500);

const char* reasonPhrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

ActionCode parseAction(const std::string& value) {
  if (value == "roll") return ActionCode::Roll;
  if (value == "confirm") return ActionCode::ConfirmPosition;
  if (value == "buy") return ActionCode::Buy;
  if (value == "decline") return ActionCode::Decline;
  if (value == "end") return ActionCode::EndTurn;
  if (value == "holdfee") return ActionCode::PayHoldFee;
  if (value == "mortgage") return ActionCode::Mortgage;
  if (value == "unmortgage") return ActionCode::Unmortgage;
  if (value == "build") return ActionCode::Build;
  if (value == "sell") return ActionCode::SellBuilding;
  if (value == "paydebt") return ActionCode::PayDebt;
  if (value == "bankrupt") return ActionCode::DeclareBankruptcy;
  if (value == "bid") return ActionCode::AuctionBid;
  if (value == "passbid") return ActionCode::AuctionPass;
  if (value == "auctionready") return ActionCode::AuctionReady;
  if (value == "cardcontinue") return ActionCode::CardContinue;
  return static_cast<ActionCode>(0);
}

bool validTileAssetName(const std::string& value) {
  constexpr std::string_view pngSuffix = ".png";
  constexpr std::string_view rgb565Suffix = ".rgb565";
  const std::string_view suffix =
      value.size() > rgb565Suffix.size() &&
              value.compare(value.size() - rgb565Suffix.size(), rgb565Suffix.size(),
                            rgb565Suffix) == 0
          ? rgb565Suffix
          : pngSuffix;
  if (value.size() <= suffix.size() || value.size() > 80 ||
      value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0) {
    return false;
  }
  for (std::size_t index = 0; index < value.size() - suffix.size(); ++index) {
    const auto character = static_cast<unsigned char>(value[index]);
    if (!(std::islower(character) || std::isdigit(character) || character == '-')) return false;
  }
  return true;
}

bool parseDecimal(const std::string& value, std::size_t begin, std::size_t end,
                  std::uint32_t& output) {
  if (begin >= end || (end - begin > 1 && value[begin] == '0')) return false;
  const auto* first = value.data() + begin;
  const auto* last = value.data() + end;
  const auto result = std::from_chars(first, last, output);
  return result.ec == std::errc{} && result.ptr == last;
}

bool parseAvatarPreviewName(const std::string& value, AvatarRecipe& recipe) {
  constexpr std::string_view suffix = ".rgb565";
  if (value.size() <= suffix.size() ||
      value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0 ||
      value[0] != 'h') return false;
  const auto c = value.find("-c", 1);
  const auto f = value.find("-f", c == std::string::npos ? 0 : c + 2);
  const auto s = value.find("-s", f == std::string::npos ? 0 : f + 2);
  const auto o = value.find("-o", s == std::string::npos ? 0 : s + 2);
  const auto end = value.size() - suffix.size();
  if (c == std::string::npos || f == std::string::npos || s == std::string::npos ||
      o == std::string::npos || o >= end) return false;
  std::uint32_t hair = 0;
  std::uint32_t color = 0;
  std::uint32_t face = 0;
  std::uint32_t skin = 0;
  std::uint32_t outfit = 0;
  if (!parseDecimal(value, 1, c, hair) || !parseDecimal(value, c + 2, f, color) ||
      !parseDecimal(value, f + 2, s, face) || !parseDecimal(value, s + 2, o, skin) ||
      !parseDecimal(value, o + 2, end, outfit) || hair > 255 || color > 255 ||
      face > 255 || skin > 255 || outfit > 255) return false;
  recipe = {kAvatarCatalogVersionV1, static_cast<std::uint8_t>(hair),
            static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(face),
            static_cast<std::uint8_t>(skin), static_cast<std::uint8_t>(outfit)};
  return gridopoly::pi::validAvatarRecipe(recipe);
}

bool validFinalAvatarRelative(const std::string& value) {
  const auto slash = value.find('/');
  if (slash == std::string::npos || slash == 0 || slash != value.rfind('/') ||
      slash + 1 >= value.size()) return false;
  for (std::size_t index = 0; index < slash; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
  }
  const auto file = value.substr(slash + 1);
  const bool png = file.size() > 4 && file.compare(file.size() - 4, 4, ".png") == 0;
  const bool rgb = file.size() > 7 && file.compare(file.size() - 7, 7, ".rgb565") == 0;
  if (!png && !rgb) return false;
  const auto stem = file.substr(0, file.size() - (png ? 4 : 7));
  const auto avatar = stem.find("-a");
  const auto hash = stem.find('-', avatar == std::string::npos ? 0 : avatar + 2);
  if (stem.empty() || stem[0] != 'p' || avatar == std::string::npos ||
      hash == std::string::npos || hash + 17 != stem.size()) return false;
  std::uint32_t player = 0;
  std::uint32_t revision = 0;
  if (!parseDecimal(stem, 1, avatar, player) ||
      !parseDecimal(stem, avatar + 2, hash, revision) || player < 1 || player > 6 ||
      revision == 0) return false;
  for (std::size_t index = hash + 1; index < stem.size(); ++index) {
    const auto c = stem[index];
    if (!std::isdigit(static_cast<unsigned char>(c)) && (c < 'a' || c > 'f')) return false;
  }
  return true;
}

std::string strongEtag(std::uint64_t hash) {
  std::ostringstream output;
  output << '"' << std::hex << std::setfill('0') << std::setw(16) << hash << '"';
  return output.str();
}

std::uint64_t fnv64(const std::uint8_t* bytes, std::size_t length) {
  std::uint64_t hash = 1469598103934665603ull;
  for (std::size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash == 0 ? 1 : hash;
}

}  // namespace

HttpServer::HttpServer(AuthorityService& authority, UdpPlayerServer& udp,
                       std::uint16_t port, std::string serviceIp, std::size_t workerCount,
                       std::filesystem::path assetDirectory)
    : authority_(authority), udp_(udp), port_(port), serviceIp_(std::move(serviceIp)),
      workerCount_(std::max<std::size_t>(2, workerCount)),
      assetDirectory_(std::move(assetDirectory)) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
  if (running_) return true;
  if (!openSocket()) return false;
  startedAt_ = std::chrono::steady_clock::now();
  running_ = true;
  for (std::size_t index = 0; index < workerCount_; ++index) {
    workers_.emplace_back(&HttpServer::workerLoop, this);
  }
  acceptThread_ = std::thread(&HttpServer::acceptLoop, this);
  return true;
}

void HttpServer::stop() {
  running_ = false;
  closeSocket();
  queueReady_.notify_all();
  if (acceptThread_.joinable()) acceptThread_.join();
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
  workers_.clear();
  std::lock_guard<std::mutex> lock(queueMutex_);
  while (!queue_.empty()) {
    ::close(queue_.front().descriptor);
    queue_.pop_front();
  }
}

HttpServer::Diagnostics HttpServer::diagnostics() const {
  std::scoped_lock lock(queueMutex_, diagnosticsMutex_);
  auto copy = diagnostics_;
  copy.queued = static_cast<std::uint32_t>(queue_.size());
  return copy;
}

bool HttpServer::openSocket() {
  listenSocket_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listenSocket_ < 0) return false;
  const int enabled = 1;
  ::setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  const int flags = ::fcntl(listenSocket_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(listenSocket_, F_SETFL, flags | O_NONBLOCK) != 0) {
    closeSocket();
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(listenSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(listenSocket_, 512) != 0) {
    closeSocket();
    return false;
  }
  if (port_ == 0) {
    sockaddr_in bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0) {
      closeSocket();
      return false;
    }
    port_ = ntohs(bound.sin_port);
  }
  return true;
}

void HttpServer::closeSocket() {
  const auto descriptor = listenSocket_;
  listenSocket_ = -1;
  if (descriptor >= 0) {
    ::shutdown(descriptor, SHUT_RDWR);
    ::close(descriptor);
  }
}

void HttpServer::acceptLoop() {
  while (running_) {
    pollfd pollDescriptor{listenSocket_, POLLIN, 0};
    if (::poll(&pollDescriptor, 1, 100) <= 0) continue;
    for (int accepted = 0; accepted < 64; ++accepted) {
      sockaddr_in peer{};
      socklen_t peerLength = sizeof(peer);
      const auto client = ::accept4(listenSocket_, reinterpret_cast<sockaddr*>(&peer),
                                    &peerLength, SOCK_CLOEXEC | SOCK_NONBLOCK);
      if (client < 0) break;
      const int enabled = 1;
      ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
      bool queued = false;
      {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() < 1024) {
          queue_.push_back({client, std::chrono::steady_clock::now()});
          queued = true;
        }
      }
      {
        std::lock_guard<std::mutex> lock(diagnosticsMutex_);
        if (queued) ++diagnostics_.accepted;
        else ++diagnostics_.rejected;
      }
      if (queued) queueReady_.notify_one();
      else ::close(client);
    }
  }
}

void HttpServer::workerLoop() {
  while (running_) {
    int descriptor = -1;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueReady_.wait_for(lock, std::chrono::milliseconds(200),
                           [this] { return !running_ || !queue_.empty(); });
      if (!queue_.empty()) {
        descriptor = queue_.front().descriptor;
        queue_.pop_front();
        keepAlivePreemptionClaimed_ = false;
      }
    }
    if (descriptor < 0) continue;
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.active;
    }
    handleClient(descriptor);
    ::shutdown(descriptor, SHUT_RDWR);
    ::close(descriptor);
    {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      --diagnostics_.active;
      ++diagnostics_.completed;
    }
  }
}

void HttpServer::handleClient(int descriptor) {
  constexpr std::size_t kMaxRequestsPerConnection = 100;
  for (std::size_t handled = 0; handled < kMaxRequestsPerConnection && running_; ++handled) {
    Request request{};
    if (!readRequest(descriptor, request, handled == 0)) return;
    const auto remaining = kMaxRequestsPerConnection - handled - 1;
    const bool keepAlive = request.keepAlive && remaining != 0;
    if (!sendResponse(descriptor, route(request), keepAlive, remaining)) return;
    if (!keepAlive) return;
  }
}

bool HttpServer::readRequest(int descriptor, Request& request, bool countTimeout) {
  std::string raw;
  raw.reserve(2048);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 16384) {
    // A completed keep-alive connection must not monopolize a fixed worker
    // while newly accepted clients wait in the bounded queue. A request that
    // has already started remains atomic; only a completely idle connection
    // yields, and the client can transparently open another TCP connection.
    if (!countTimeout && raw.empty() && claimStarvedClient()) {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.keepAlivePreemptions;
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      if (countTimeout) {
        std::lock_guard<std::mutex> lock(diagnosticsMutex_);
        ++diagnostics_.readTimeouts;
      }
      return false;
    }
    pollfd pollDescriptor{descriptor, POLLIN, 0};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const auto ready = ::poll(&pollDescriptor, 1, static_cast<int>(std::min<std::int64_t>(remaining, 250)));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) continue;
    char buffer[2048];
    const auto count = ::recv(descriptor, buffer, sizeof(buffer), 0);
    if (count <= 0) return false;
    raw.append(buffer, static_cast<std::size_t>(count));
  }
  if (!parseRequest(raw, request)) {
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    ++diagnostics_.parseErrors;
    return false;
  }
  return true;
}

bool HttpServer::claimStarvedClient() {
  std::lock_guard<std::mutex> lock(queueMutex_);
  if (queue_.empty() ||
      std::chrono::steady_clock::now() - queue_.front().enqueuedAt <
          kQueuedClientFairnessDeadline) {
    return false;
  }
  bool expected = false;
  return keepAlivePreemptionClaimed_.compare_exchange_strong(expected, true);
}

HttpServer::Response HttpServer::route(const Request& request) {
  if (request.method == "GET" && request.path == "/") {
    Response response{};
    const auto found = request.headers.find("if-none-match");
    if (found != request.headers.end() && found->second.find(gridopoly::server::kWebUiEtagToken) != std::string::npos) {
      response.status = 304;
      response.etag = gridopoly::server::kWebUiEtag;
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.notModified;
      return response;
    }
    response.contentType = "text/html; charset=utf-8";
    response.body.assign(reinterpret_cast<const char*>(gridopoly::server::kWebUiGzip),
                         gridopoly::server::kWebUiGzipSize);
    response.cacheControl = "no-cache";
    response.contentEncoding = "gzip";
    response.etag = gridopoly::server::kWebUiEtag;
    return response;
  }
  if (request.method == "GET" && request.path == "/health") {
    const auto http = diagnostics();
    const auto udp = udp_.diagnostics();
    const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt_).count();
    std::ostringstream body;
    body << "{\"ok\":true,\"platform\":\"raspberry-pi\",\"roomId\":" << authority_.roomId()
         << ",\"version\":" << authority_.stateVersion() << ",\"peers\":"
         << static_cast<unsigned>(authority_.peerCount()) << ",\"botActionIntervalMs\":"
         << authority_.botActionIntervalMs() << ",\"controlVersion\":"
         << authority_.controlVersion() << ",\"identityRevision\":"
         << authority_.identityRevision() << ",\"identityPhase\":"
         << static_cast<unsigned>(authority_.identityPhase()) << ",\"uptimeMs\":" << uptime
         << ",\"http\":{\"accepted\":" << http.accepted << ",\"rejected\":" << http.rejected
         << ",\"completed\":" << http.completed << ",\"active\":" << http.active
         << ",\"queued\":" << http.queued << ",\"timeouts\":" << http.readTimeouts
         << ",\"sendErrors\":" << http.sendErrors
         << ",\"keepAlivePreemptions\":" << http.keepAlivePreemptions
         << "},\"udp\":{\"rx\":" << udp.rxDatagrams
         << ",\"valid\":" << udp.validDatagrams << ",\"authFailures\":" << udp.authFailures
         << ",\"replayDrops\":" << udp.replayDrops << ",\"tx\":" << udp.txDatagrams
         << ",\"txErrors\":" << udp.txErrors << ",\"heartbeats\":" << udp.heartbeats
         << ",\"acks\":" << udp.heartbeatAcks << ",\"resyncs\":" << udp.resyncs
         << ",\"peerSilenceMs\":" << udp.connectedPeerSilenceMs
         << ",\"maxPeerSilenceMs\":" << udp.maxPeerSilenceMs
         << ",\"detail\":{\"requests\":" << udp.detailRequests
         << ",\"responses\":" << udp.detailResponses
         << ",\"replays\":" << udp.detailReplays
         << ",\"errors\":" << udp.detailErrors
         << ",\"lastRequestId\":" << udp.lastDetailRequestId
         << ",\"lastTarget\":" << static_cast<unsigned>(udp.lastDetailTargetId)
         << ",\"lastExpectedVersion\":" << udp.lastDetailExpectedVersion
         << ",\"lastResponseBytes\":" << udp.lastDetailResponseBytes
         << "},\"trade\":{\"requests\":" << udp.tradeRequests
         << ",\"responses\":" << udp.tradeResponses
         << ",\"replays\":" << udp.tradeReplays
         << ",\"errors\":" << udp.tradeErrors
         << ",\"lastRequestId\":" << udp.lastTradeRequestId
         << ",\"lastTradeId\":" << udp.lastTradeId
         << ",\"lastRevision\":" << udp.lastTradeRevision
         << ",\"lastOperation\":" << static_cast<unsigned>(udp.lastTradeOperation)
         << ",\"lastResult\":" << static_cast<unsigned>(udp.lastTradeResult)
         << ",\"lastResponseBytes\":" << udp.lastTradeResponseBytes
         << "},\"identity\":{\"requests\":" << udp.identityRequests
         << ",\"responses\":" << udp.identityResponses
         << ",\"errors\":" << udp.identityErrors
         << ",\"lastRequestId\":" << udp.lastIdentityRequestId
         << ",\"lastRevision\":" << udp.lastIdentityRevision
         << ",\"lastOperation\":" << static_cast<unsigned>(udp.lastIdentityOperation)
         << ",\"lastResult\":" << static_cast<unsigned>(udp.lastIdentityResult)
         << ",\"lastResponseBytes\":" << udp.lastIdentityResponseBytes << "}}}";
    return {200, "application/json; charset=utf-8", body.str(), "no-store", {}, {}};
  }
  if (request.path == "/api/settings" && request.method == "GET") {
    return {200, "application/json; charset=utf-8",
            "{\"ok\":true,\"botActionIntervalMs\":" +
                std::to_string(authority_.botActionIntervalMs()) +
                ",\"minimumMs\":" +
                std::to_string(AuthorityService::kMinimumBotActionIntervalMs) +
                ",\"maximumMs\":" +
                std::to_string(AuthorityService::kMaximumBotActionIntervalMs) + "}",
            "no-store", {}, {}};
  }
  if (request.path == "/api/settings" && request.method == "POST") {
    const auto intervalMs = unsignedQuery(request, "botIntervalMs");
    if (intervalMs < AuthorityService::kMinimumBotActionIntervalMs ||
        intervalMs > AuthorityService::kMaximumBotActionIntervalMs) {
      return {400, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"bot_interval_out_of_range\",\"minimumMs\":" +
                  std::to_string(AuthorityService::kMinimumBotActionIntervalMs) +
                  ",\"maximumMs\":" +
                  std::to_string(AuthorityService::kMaximumBotActionIntervalMs) + "}",
              "no-store", {}, {}};
    }
    if (!authority_.setBotActionIntervalMs(intervalMs)) {
      return {500, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"settings_persist_failed\"}",
              "no-store", {}, {}};
    }
    return {200, "application/json; charset=utf-8",
            "{\"ok\":true,\"botActionIntervalMs\":" +
                std::to_string(authority_.botActionIntervalMs()) + "}",
            "no-store", {}, {}};
  }
  if (request.path == "/api/forced-roll" && request.method == "POST") {
    if (unsignedQuery(request, "cancel") != 0) {
      if (!authority_.clearForcedRollTarget()) {
        return {500, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"forced_roll_persist_failed\"}",
                "no-store", {}, {}};
      }
    } else {
      const auto rawPlayer = unsignedQuery(request, "player", 0);
      const auto rawTarget = unsignedQuery(request, "target", 0xFFFFFFFFu);
      if (rawPlayer > 0xFFu || rawTarget > 0xFFu) {
        return {400, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"invalid_forced_roll_request\"}",
                "no-store", {}, {}};
      }
      const auto result = authority_.setForcedRollTarget(
          static_cast<std::uint8_t>(rawPlayer), static_cast<std::uint8_t>(rawTarget),
          unsignedQuery(request, "expected"));
      if (!result) {
        std::ostringstream body;
        body << "{\"ok\":false,\"code\":" << static_cast<unsigned>(result.code)
             << ",\"message\":\"" << result.message << "\",\"version\":"
             << authority_.stateVersion() << ",\"controlVersion\":"
             << authority_.controlVersion() << '}';
        return {409, "application/json; charset=utf-8", body.str(), "no-store", {}, {}};
      }
    }
    const auto forced = authority_.forcedRollState();
    std::ostringstream body;
    body << "{\"ok\":true,\"version\":" << authority_.stateVersion()
         << ",\"controlVersion\":" << authority_.controlVersion()
         << ",\"forcedRoll\":{\"active\":" << (forced.active ? "true" : "false")
         << ",\"player\":" << static_cast<unsigned>(forced.playerId)
         << ",\"target\":" << static_cast<unsigned>(forced.targetTile)
         << ",\"steps\":" << static_cast<unsigned>(forced.steps)
         << ",\"origin\":" << static_cast<unsigned>(forced.originTile) << "}}";
    return {200, "application/json; charset=utf-8", body.str(), "no-store", {}, {}};
  }
  constexpr std::string_view avatarComponentPrefix = "/assets/avatar-components/v1/";
  if (request.method == "GET" && request.path.rfind(avatarComponentPrefix, 0) == 0) {
    AvatarComponentKind kind{};
    std::uint8_t presetId = 0;
    const auto relative = request.path.substr(avatarComponentPrefix.size());
    AvatarComponent decoded{};
    std::vector<std::uint8_t> bytes;
    const auto root = authority_.avatarComponentRoot();
    if (root.empty() || !parseAvatarComponentRelative(relative, kind, presetId) ||
        !loadAvatarComponent(root, kind, presetId, decoded, &bytes)) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_component_not_found\"}",
              "no-store", {}, {}};
    }
    const auto etag = strongEtag(fnv64(bytes.data(), bytes.size()));
    const auto found = request.headers.find("if-none-match");
    if (found != request.headers.end() && found->second.find(etag) != std::string::npos) {
      Response response{};
      response.status = 304;
      response.cacheControl = "public, max-age=31536000, immutable";
      response.etag = etag;
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.notModified;
      return response;
    }
    return {200, "application/octet-stream",
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            "public, max-age=31536000, immutable", {}, etag};
  }
  constexpr std::string_view avatarPreviewPrefix = "/assets/avatar-previews/v1/";
  if (request.method == "GET" && request.path.rfind(avatarPreviewPrefix, 0) == 0) {
    const auto name = request.path.substr(avatarPreviewPrefix.size());
    AvatarRecipe recipe{};
    AvatarPreviewResult rendered{};
    if (!parseAvatarPreviewName(name, recipe) ||
        !authority_.renderAvatarPreview(recipe, rendered)) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_preview_not_found\"}",
              "no-store", {}, {}};
    }
    const auto etag = strongEtag(rendered.contentHash64);
    const auto found = request.headers.find("if-none-match");
    if (found != request.headers.end() && found->second.find(etag) != std::string::npos) {
      Response response{};
      response.status = 304;
      response.cacheControl = "public, max-age=31536000, immutable";
      response.etag = etag;
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.notModified;
      return response;
    }
    std::ifstream stream(rendered.path, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // istreambuf_iterator reaching its sentinel does not portably set eofbit
    // (notably with libstdc++ on ARM64). Only badbit denotes an I/O failure;
    // the exact byte count below detects truncation.
    if (stream.bad() || body.size() != 220u * 300u * 2u) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_preview_not_found\"}",
              "no-store", {}, {}};
    }
    return {200, "application/octet-stream", std::move(body),
            "public, max-age=31536000, immutable", {}, etag};
  }
  constexpr std::string_view avatarAssetPrefix = "/assets/avatars/";
  if (request.method == "GET" && request.path.rfind(avatarAssetPrefix, 0) == 0) {
    const auto relative = request.path.substr(avatarAssetPrefix.size());
    const auto root = authority_.avatarAssetRoot();
    if (root.empty() || !validFinalAvatarRelative(relative)) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_not_found\"}", "no-store", {}, {}};
    }
    std::ifstream stream(root / "avatars" / relative, std::ios::binary);
    if (!stream) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_not_found\"}", "no-store", {}, {}};
    }
    std::string body((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const bool rgb565 = relative.size() > 7 &&
        relative.compare(relative.size() - 7, 7, ".rgb565") == 0;
    if ((rgb565 && body.size() != 128u * 128u * 2u) ||
        (!rgb565 && (body.size() < 24 ||
                     std::memcmp(body.data(), "\x89PNG\r\n\x1a\n", 8) != 0))) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"avatar_not_found\"}", "no-store", {}, {}};
    }
    const auto dot = relative.rfind('.');
    const auto hashStart = relative.rfind('-', dot);
    const auto etag = '"' + relative.substr(hashStart + 1, dot - hashStart - 1) + '"';
    const auto found = request.headers.find("if-none-match");
    if (found != request.headers.end() && found->second.find(etag) != std::string::npos) {
      Response response{};
      response.status = 304;
      response.cacheControl = "public, max-age=31536000, immutable";
      response.etag = etag;
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.notModified;
      return response;
    }
    return {200, rgb565 ? "application/octet-stream" : "image/png", std::move(body),
            "public, max-age=31536000, immutable", {}, etag};
  }
  constexpr std::string_view tileAssetPrefix = "/assets/tiles/";
  if (request.method == "GET" && request.path.rfind(tileAssetPrefix, 0) == 0) {
    const auto name = request.path.substr(tileAssetPrefix.size());
    if (!validTileAssetName(name)) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"asset_not_found\"}", "no-store", {}, {}};
    }
    std::ifstream stream(assetDirectory_ / name, std::ios::binary);
    if (!stream) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"asset_not_found\"}", "no-store", {}, {}};
    }
    std::string body((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const bool rgb565 = name.size() > 7 && name.compare(name.size() - 7, 7, ".rgb565") == 0;
    if (rgb565 && body.size() != 128u * 128u * 2u) {
      return {404, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"asset_not_found\"}", "no-store", {}, {}};
    }
    return {200, rgb565 ? "application/octet-stream" : "image/png", std::move(body),
            "public, max-age=31536000, immutable", {}, {}};
  }
  if (request.method == "GET" && (request.path == "/api/sync" || request.path == "/api/state")) {
    const bool unchanged = request.query.count("since") != 0 &&
        unsignedQuery(request, "since") == authority_.stateVersion() &&
        unsignedQuery(request, "peers", 0xFFFFFFFFu) == authority_.peerCount() &&
        unsignedQuery(request, "room", 0) == authority_.roomId() &&
        unsignedQuery(request, "network", 0) == authority_.networkId() &&
        unsignedQuery(request, "control", 0) == authority_.controlVersion() &&
        unsignedQuery(request, "identity", 0xFFFFFFFFu) == authority_.identityRevision();
    if (unchanged) return {204, {}, {}, "no-store", {}, {}};
    return {200, "application/json; charset=utf-8",
            request.path == "/api/sync" ? authority_.syncJson(serviceIp_)
                                         : authority_.stateJson(serviceIp_),
            "no-store", {}, {}};
  }
  if (request.method == "GET" && request.path == "/api/board") {
    const auto expectedRoom = unsignedQuery(request, "room", authority_.roomId());
    if (expectedRoom != authority_.roomId()) {
      return {409, "application/json; charset=utf-8",
              "{\"ok\":false,\"error\":\"room_changed\",\"roomId\":" +
                  std::to_string(authority_.roomId()) + "}", "no-store", {}, {}};
    }
    return {200, "application/json; charset=utf-8", authority_.boardJson(),
            "private, max-age=300", {}, {}};
  }
  if (request.method == "POST" && request.path == "/api/action") {
    const auto found = request.query.find("action");
    const auto action = found == request.query.end() ? static_cast<ActionCode>(0) : parseAction(found->second);
    const auto player = static_cast<std::uint8_t>(unsignedQuery(request, "player"));
    const auto asset = static_cast<std::uint8_t>(unsignedQuery(request, "asset", 0xFF));
    const auto argument = signedQuery(request, "arg", -1);
    const auto result = authority_.execute(action, player, asset, argument,
                                           unsignedQuery(request, "expected"));
    std::ostringstream body;
    body << "{\"ok\":" << (result ? "true" : "false") << ",\"code\":"
         << static_cast<unsigned>(result.code) << ",\"message\":\"" << result.message
         << "\",\"version\":" << authority_.stateVersion() << '}';
    return {result ? 200 : 409, "application/json; charset=utf-8", body.str(),
            "no-store", {}, {}};
  }
  if (request.method == "POST" && request.path == "/api/new") {
    const auto boardSize = unsignedQuery(request, "size", 32);
    const auto humanCount = unsignedQuery(request, "humans", 1);
    const auto botCount = unsignedQuery(request, "bots", 3);
    const auto result = boardSize > 0xFFu || humanCount > 0xFFu || botCount > 0xFFu
        ? Result{ErrorCode::InvalidArgument, "unsupported board or player counts"}
        : authority_.newGame(static_cast<std::uint8_t>(boardSize),
                             static_cast<std::uint8_t>(humanCount),
                             static_cast<std::uint8_t>(botCount));
    std::ostringstream body;
    body << "{\"ok\":" << (result ? "true" : "false") << ",\"code\":"
         << static_cast<unsigned>(result.code) << ",\"message\":\"" << result.message
         << "\",\"version\":" << authority_.stateVersion()
         << ",\"roomId\":" << authority_.roomId()
         << ",\"identityRevision\":" << authority_.identityRevision() << '}';
    return {result ? 200 : 409, "application/json; charset=utf-8", body.str(),
            "no-store", {}, {}};
  }
  if (request.method == "POST" && request.path == "/api/web-detach") {
    return {204, {}, {}, "no-store", {}, {}};
  }
  return {404, "application/json; charset=utf-8",
          "{\"ok\":false,\"error\":\"not_found\"}", "no-store", {}, {}};
}

bool HttpServer::sendResponse(int descriptor, const Response& response, bool keepAlive,
                              std::size_t remainingRequests) {
  std::ostringstream header;
  header << "HTTP/1.1 " << response.status << ' ' << reasonPhrase(response.status) << "\r\n"
         << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
  if (keepAlive) {
    header << "Keep-Alive: timeout=3, max=" << remainingRequests << "\r\n";
  }
  header
         << "X-Content-Type-Options: nosniff\r\n";
  if (!response.contentType.empty()) header << "Content-Type: " << response.contentType << "\r\n";
  if (!response.cacheControl.empty()) header << "Cache-Control: " << response.cacheControl << "\r\n";
  if (!response.contentEncoding.empty()) header << "Content-Encoding: " << response.contentEncoding << "\r\n";
  if (!response.etag.empty()) header << "ETag: " << response.etag << "\r\n";
  header << "Content-Length: " << response.body.size() << "\r\n\r\n";
  const auto headerText = header.str();
  std::array<std::pair<const char*, std::size_t>, 2> parts{{
      {headerText.data(), headerText.size()}, {response.body.data(), response.body.size()}}};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (const auto& part : parts) {
    std::size_t sent = 0;
    while (sent < part.second) {
      const auto count = ::send(descriptor, part.first + sent, part.second - sent, MSG_NOSIGNAL);
      if (count > 0) {
        sent += static_cast<std::size_t>(count);
        continue;
      }
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        pollfd pollDescriptor{descriptor, POLLOUT, 0};
        ::poll(&pollDescriptor, 1, 100);
        continue;
      }
      break;
    }
    if (sent != part.second) {
      std::lock_guard<std::mutex> lock(diagnosticsMutex_);
      ++diagnostics_.sendErrors;
      return false;
    }
  }
  return true;
}

bool HttpServer::parseRequest(const std::string& raw, Request& request) {
  const auto firstLineEnd = raw.find("\r\n");
  if (firstLineEnd == std::string::npos) return false;
  std::istringstream firstLine(raw.substr(0, firstLineEnd));
  std::string target;
  std::string version;
  if (!(firstLine >> request.method >> target >> version) || version.rfind("HTTP/", 0) != 0) return false;
  const auto queryAt = target.find('?');
  request.path = urlDecode(target.substr(0, queryAt));
  if (queryAt != std::string::npos) {
    std::size_t start = queryAt + 1;
    while (start <= target.size()) {
      const auto end = target.find('&', start);
      const auto item = target.substr(start, end == std::string::npos ? std::string::npos : end - start);
      const auto equals = item.find('=');
      request.query[urlDecode(item.substr(0, equals))] =
          equals == std::string::npos ? "" : urlDecode(item.substr(equals + 1));
      if (end == std::string::npos) break;
      start = end + 1;
    }
  }
  std::size_t cursor = firstLineEnd + 2;
  while (cursor < raw.size()) {
    const auto end = raw.find("\r\n", cursor);
    if (end == std::string::npos || end == cursor) break;
    const auto colon = raw.find(':', cursor);
    if (colon == std::string::npos || colon > end) return false;
    auto name = lower(raw.substr(cursor, colon - cursor));
    auto valueStart = colon + 1;
    while (valueStart < end && std::isspace(static_cast<unsigned char>(raw[valueStart]))) ++valueStart;
    request.headers[std::move(name)] = raw.substr(valueStart, end - valueStart);
    cursor = end + 2;
  }
  const auto connection = request.headers.find("connection");
  const auto connectionValue = connection == request.headers.end()
      ? std::string{} : lower(connection->second);
  request.keepAlive = version == "HTTP/1.1"
      ? connectionValue != "close"
      : connectionValue == "keep-alive";
  return request.method == "GET" || request.method == "POST" || request.method == "HEAD";
}

std::string HttpServer::urlDecode(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '+' ) output.push_back(' ');
    else if (value[index] == '%' && index + 2 < value.size()) {
      unsigned number = 0;
      const auto result = std::from_chars(value.data() + index + 1, value.data() + index + 3,
                                          number, 16);
      if (result.ec == std::errc{}) {
        output.push_back(static_cast<char>(number));
        index += 2;
      } else output.push_back(value[index]);
    } else output.push_back(value[index]);
  }
  return output;
}

std::uint32_t HttpServer::unsignedQuery(const Request& request, const char* name,
                                        std::uint32_t fallback) {
  const auto found = request.query.find(name);
  if (found == request.query.end()) return fallback;
  std::uint32_t value = 0;
  const auto result = std::from_chars(found->second.data(),
                                      found->second.data() + found->second.size(), value);
  return result.ec == std::errc{} && result.ptr == found->second.data() + found->second.size()
      ? value : fallback;
}

std::int32_t HttpServer::signedQuery(const Request& request, const char* name,
                                     std::int32_t fallback) {
  const auto found = request.query.find(name);
  if (found == request.query.end()) return fallback;
  std::int32_t value = 0;
  const auto result = std::from_chars(found->second.data(),
                                      found->second.data() + found->second.size(), value);
  return result.ec == std::errc{} && result.ptr == found->second.data() + found->second.size()
      ? value : fallback;
}

}  // namespace gridopoly::pi
