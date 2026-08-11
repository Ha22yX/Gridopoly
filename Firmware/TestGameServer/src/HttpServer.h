#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <array>

namespace gridopoly::server {

enum class HttpMethod : std::uint8_t { Get, Post, Other };

struct HttpRequest {
  HttpMethod method{HttpMethod::Other};
  char target[384]{};
  char ifNoneMatch[48]{};
  std::uint32_t remoteIpv4{};

  bool pathEquals(const char* expected) const;
  bool hasArg(const char* name) const;
  bool arg(const char* name, char* output, std::size_t capacity) const;
  std::uint32_t argUnsigned(const char* name, std::uint32_t fallback = 0) const;
  std::int32_t argSigned(const char* name, std::int32_t fallback = 0) const;
};

struct HttpServerDiagnostics {
  std::uint32_t accepted{};
  std::uint32_t rejected{};
  std::uint32_t completed{};
  std::uint32_t writeWouldBlock{};
  std::uint32_t sendErrors{};
  std::uint32_t requestTimeouts{};
  std::uint32_t responseTimeouts{};
  std::uint32_t oversizedResponses{};
  std::uint16_t largestResponseBytes{};
  std::uint8_t activeConnections{};
  std::uint8_t pendingResponses{};
};

class HttpServer {
 public:
  void begin();
  void stop();
  void restart();
  bool poll(HttpRequest& request);
  void send(int status, const char* contentType, const char* content, std::size_t length,
            const char* cacheControl = "no-store", const char* contentEncoding = nullptr,
            bool progmem = false, const char* etag = nullptr);
  void sendString(int status, const char* contentType, const String& content,
                  const char* cacheControl = "no-store");
  void sendEmpty(int status, const char* cacheControl = "no-store");
  void sendNotModified(const char* etag, const char* cacheControl = "no-cache");
  HttpServerDiagnostics diagnostics() const;

 private:
  struct Slot {
    WiFiClient client{};
    std::array<char, 512> requestLine{};
    std::array<char, 160> headerLine{};
    std::array<char, 48> ifNoneMatch{};
    std::array<char, 320> responseHeader{};
    const char* responseProgmem{};
    std::uint16_t requestLineLength{};
    std::uint16_t headerLineLength{};
    std::uint16_t totalBytes{};
    std::uint16_t responseHeaderLength{};
    std::uint16_t responseHeaderSent{};
    std::uint16_t responseBodyLength{};
    std::uint16_t responseBodySent{};
    std::uint32_t lastActivityAt{};
    std::uint32_t nextWriteAt{};
    std::uint32_t remoteIpv4{};
    std::uint8_t headerTerminatorState{};
    std::uint16_t servedRequests{};
    bool firstLineComplete{};
    bool responseActive{};
    bool responseBodyProgmem{};
  };

  int serverSocket_{-1};
  std::array<Slot, 8> slots_{};
  // Each connection owns its response body. This deliberately spends about
  // 64 KiB of the S3's DRAM to prevent one slow browser from monopolising a
  // shared buffer and starving health/action requests.
  std::array<std::array<std::uint8_t, 8192>, 8> responseBuffers_{};
  std::int8_t responseSlot_{-1};
  std::uint32_t accepted_{};
  std::uint32_t rejected_{};
  std::uint32_t completed_{};
  std::uint32_t writeWouldBlock_{};
  std::uint32_t sendErrors_{};
  std::uint32_t requestTimeouts_{};
  std::uint32_t responseTimeouts_{};
  std::uint32_t oversizedResponses_{};
  std::uint16_t largestResponseBytes_{};
  std::uint8_t nextResponseSlot_{};

  void acceptClient();
  void serviceResponses();
  bool readSlot(std::size_t index, HttpRequest& request);
  void captureHeader(Slot& slot);
  void resetRequest(Slot& slot);
  void closeSlot(std::size_t index);
};

}  // namespace gridopoly::server
