#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "AuthorityService.h"
#include "UdpPlayerServer.h"

namespace gridopoly::pi {

class HttpServer {
 public:
  struct Diagnostics {
    std::uint64_t accepted{};
    std::uint64_t rejected{};
    std::uint64_t completed{};
    std::uint64_t parseErrors{};
    std::uint64_t readTimeouts{};
    std::uint64_t sendErrors{};
    std::uint64_t notModified{};
    std::uint64_t keepAlivePreemptions{};
    std::uint32_t active{};
    std::uint32_t queued{};
  };

  HttpServer(AuthorityService& authority, UdpPlayerServer& udp,
             std::uint16_t port = 80, std::string serviceIp = "10.42.0.1",
             std::size_t workerCount = 32,
             std::filesystem::path assetDirectory = "/usr/local/share/gridopoly/tiles");
  ~HttpServer();

  bool start();
  void stop();
  std::uint16_t port() const { return port_; }
  Diagnostics diagnostics() const;

 private:
  struct Request {
    std::string method{};
    std::string path{};
    std::unordered_map<std::string, std::string> query{};
    std::unordered_map<std::string, std::string> headers{};
    bool keepAlive{};
  };

  struct Response {
    int status{200};
    std::string contentType{"application/json; charset=utf-8"};
    std::string body{};
    std::string cacheControl{"no-store"};
    std::string contentEncoding{};
    std::string etag{};
  };

  struct PendingClient {
    int descriptor{-1};
    std::chrono::steady_clock::time_point enqueuedAt{};
  };

  AuthorityService& authority_;
  UdpPlayerServer& udp_;
  std::uint16_t port_{};
  std::string serviceIp_;
  std::size_t workerCount_{};
  std::filesystem::path assetDirectory_{};
  int listenSocket_{-1};
  std::atomic<bool> running_{};
  std::thread acceptThread_{};
  std::vector<std::thread> workers_{};
  std::deque<PendingClient> queue_{};
  mutable std::mutex queueMutex_;
  std::condition_variable queueReady_;
  std::atomic<bool> keepAlivePreemptionClaimed_{};
  mutable std::mutex diagnosticsMutex_;
  Diagnostics diagnostics_{};
  std::chrono::steady_clock::time_point startedAt_{};

  bool openSocket();
  void closeSocket();
  void acceptLoop();
  void workerLoop();
  void handleClient(int descriptor);
  bool readRequest(int descriptor, Request& request, bool countTimeout);
  bool claimStarvedClient();
  Response route(const Request& request);
  bool sendResponse(int descriptor, const Response& response, bool keepAlive,
                    std::size_t remainingRequests);

  static bool parseRequest(const std::string& raw, Request& request);
  static std::string urlDecode(const std::string& value);
  static std::uint32_t unsignedQuery(const Request& request, const char* name,
                                     std::uint32_t fallback = 0);
  static std::int32_t signedQuery(const Request& request, const char* name,
                                  std::int32_t fallback = 0);
};

}  // namespace gridopoly::pi
