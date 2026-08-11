#include "HttpServer.h"

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <unistd.h>

namespace gridopoly::server {
namespace {

const char* statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 304: return "Not Modified";
    case 404: return "Not Found";
    case 409: return "Conflict";
    default: return "Error";
  }
}

bool asciiPrefixEquals(const char* text, const char* expected) {
  while (*expected != '\0') {
    if (*text == '\0' || std::tolower(static_cast<unsigned char>(*text)) !=
                             std::tolower(static_cast<unsigned char>(*expected))) {
      return false;
    }
    ++text;
    ++expected;
  }
  return true;
}

const char* queryStart(const char* target) {
  return target == nullptr ? nullptr : std::strchr(target, '?');
}

bool findArgument(const char* target, const char* name, const char*& value, std::size_t& length) {
  const auto* cursor = queryStart(target);
  if (cursor == nullptr) return false;
  ++cursor;
  const auto nameLength = std::strlen(name);
  while (*cursor != '\0') {
    const auto* end = std::strchr(cursor, '&');
    if (end == nullptr) end = cursor + std::strlen(cursor);
    const auto* equals = static_cast<const char*>(std::memchr(cursor, '=', static_cast<std::size_t>(end - cursor)));
    if (equals != nullptr && static_cast<std::size_t>(equals - cursor) == nameLength &&
        std::memcmp(cursor, name, nameLength) == 0) {
      value = equals + 1;
      length = static_cast<std::size_t>(end - value);
      return true;
    }
    cursor = *end == '&' ? end + 1 : end;
  }
  return false;
}

}  // namespace

bool HttpRequest::pathEquals(const char* expected) const {
  const auto* query = queryStart(target);
  const auto length = query == nullptr ? std::strlen(target) : static_cast<std::size_t>(query - target);
  return std::strlen(expected) == length && std::memcmp(target, expected, length) == 0;
}

bool HttpRequest::hasArg(const char* name) const {
  const char* value = nullptr;
  std::size_t length = 0;
  return findArgument(target, name, value, length);
}

bool HttpRequest::arg(const char* name, char* output, std::size_t capacity) const {
  if (output == nullptr || capacity == 0) return false;
  const char* value = nullptr;
  std::size_t length = 0;
  if (!findArgument(target, name, value, length)) {
    output[0] = '\0';
    return false;
  }
  if (length >= capacity) length = capacity - 1;
  std::memcpy(output, value, length);
  output[length] = '\0';
  return true;
}

std::uint32_t HttpRequest::argUnsigned(const char* name, std::uint32_t fallback) const {
  char value[24]{};
  if (!arg(name, value, sizeof(value))) return fallback;
  char* end = nullptr;
  const auto parsed = std::strtoul(value, &end, 10);
  return end != value && *end == '\0' ? static_cast<std::uint32_t>(parsed) : fallback;
}

std::int32_t HttpRequest::argSigned(const char* name, std::int32_t fallback) const {
  char value[24]{};
  if (!arg(name, value, sizeof(value))) return fallback;
  char* end = nullptr;
  const auto parsed = std::strtol(value, &end, 10);
  return end != value && *end == '\0' ? static_cast<std::int32_t>(parsed) : fallback;
}

void HttpServer::begin() {
  if (serverSocket_ >= 0) return;
  const int socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket < 0) {
    Serial.printf("GRIDOPOLY_HTTP listening=0 stage=socket error=%d\n", errno);
    return;
  }
  int enabled = 1;
  ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(80);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    const int error = errno;
    ::close(socket);
    Serial.printf("GRIDOPOLY_HTTP listening=0 stage=bind error=%d\n", error);
    return;
  }
  if (::listen(socket, static_cast<int>(slots_.size())) < 0) {
    const int error = errno;
    ::close(socket);
    Serial.printf("GRIDOPOLY_HTTP listening=0 stage=listen error=%d\n", error);
    return;
  }
  const int currentFlags = ::fcntl(socket, F_GETFL, 0);
  if (currentFlags < 0 || ::fcntl(socket, F_SETFL, currentFlags | O_NONBLOCK) < 0) {
    const int error = errno;
    ::close(socket);
    Serial.printf("GRIDOPOLY_HTTP listening=0 stage=nonblock error=%d\n", error);
    return;
  }
  serverSocket_ = socket;
  Serial.printf("GRIDOPOLY_HTTP listening=1 family=ipv4 port=80 slots=%u\n",
                static_cast<unsigned>(slots_.size()));
}

void HttpServer::stop() {
  responseSlot_ = -1;
  for (std::size_t index = 0; index < slots_.size(); ++index) closeSlot(index);
  if (serverSocket_ >= 0) {
    ::close(serverSocket_);
    serverSocket_ = -1;
  }
}

void HttpServer::restart() {
  stop();
  begin();
}

bool HttpServer::poll(HttpRequest& request) {
  acceptClient();
  serviceResponses();
  const auto now = millis();
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    auto& slot = slots_[index];
    if (slot.client.fd() < 0) continue;
    if (!slot.client.connected()) {
      closeSlot(index);
      continue;
    }
    if (slot.responseActive) continue;
    if (readSlot(index, request)) return true;
    const auto idleLimit = slot.totalBytes == 0 ? 10000u : 1500u;
    if (now - slot.lastActivityAt >= idleLimit) {
      ++requestTimeouts_;
      closeSlot(index);
    }
  }
  return false;
}

void HttpServer::acceptClient() {
  if (serverSocket_ < 0) return;
  sockaddr_in peerAddress{};
  socklen_t peerAddressLength = sizeof(peerAddress);
  const int clientSocket = ::accept(serverSocket_, reinterpret_cast<sockaddr*>(&peerAddress),
                                    &peerAddressLength);
  if (clientSocket < 0) return;
  WiFiClient incoming(clientSocket);
  int keepAlive = 1;
  int keepIdleSeconds = 15;
  int keepIntervalSeconds = 5;
  int keepCount = 3;
  ::setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
  ::setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdleSeconds, sizeof(keepIdleSeconds));
  ::setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPINTVL, &keepIntervalSeconds, sizeof(keepIntervalSeconds));
  ::setsockopt(clientSocket, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
  std::int8_t destination = -1;
  std::int8_t oldestIdle = -1;
  std::uint32_t oldestIdleAge = 0;
  const auto now = millis();
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    auto& slot = slots_[index];
    if (slot.client.fd() < 0 || !slot.client.connected()) {
      destination = static_cast<std::int8_t>(index);
      break;
    }
    const bool idle = !slot.responseActive && slot.requestLineLength == 0 && slot.totalBytes == 0;
    const auto idleAge = now - slot.lastActivityAt;
    if (idle && (oldestIdle < 0 || idleAge > oldestIdleAge)) {
      oldestIdle = static_cast<std::int8_t>(index);
      oldestIdleAge = idleAge;
    }
  }
  if (destination < 0) destination = oldestIdle;
  if (destination < 0) {
    ++rejected_;
    incoming.stop();
    return;
  }
  const auto index = static_cast<std::size_t>(destination);
  closeSlot(index);
  auto& slot = slots_[index];
  incoming.setNoDelay(true);
  incoming.setTimeout(2500);
  slot.client = incoming;
  slot.remoteIpv4 = peerAddress.sin_family == AF_INET ? peerAddress.sin_addr.s_addr : 0;
  slot.lastActivityAt = now;
  ++accepted_;
}

void HttpServer::serviceResponses() {
  // One bounded chunk per loop keeps lwIP's dynamic TX buffers available for
  // ESP-NOW priority frames. Previously eight active sockets could enqueue up
  // to 8 KiB in one pass and make esp_now_send() return NO_MEM.
  constexpr std::size_t kMaxChunk = 512;
  // A full UI response can fill lwIP's send window while Wi-Fi retries a lost
  // frame.  Closing after 2.5 s aborted otherwise recoverable TCP transfers.
  constexpr std::uint32_t kResponseStallTimeoutMs = 10000;
  const auto now = millis();
  for (std::size_t offset = 0; offset < slots_.size(); ++offset) {
    const auto index = static_cast<std::size_t>((nextResponseSlot_ + offset) % slots_.size());
    auto& slot = slots_[index];
    if (!slot.responseActive) continue;
    if (slot.nextWriteAt != 0 && static_cast<std::int32_t>(now - slot.nextWriteAt) < 0) continue;
    nextResponseSlot_ = static_cast<std::uint8_t>((index + 1) % slots_.size());
    if (!slot.client || !slot.client.connected()) {
      closeSlot(index);
      continue;
    }
    const auto socket = slot.client.fd();
    if (socket < 0) {
      closeSlot(index);
      continue;
    }
    const std::uint8_t* source = nullptr;
    std::size_t chunk = 0;
    bool sendingHeader = false;
    if (slot.responseHeaderSent < slot.responseHeaderLength) {
      const auto remaining = static_cast<std::size_t>(slot.responseHeaderLength - slot.responseHeaderSent);
      chunk = remaining < kMaxChunk ? remaining : kMaxChunk;
      source = reinterpret_cast<const std::uint8_t*>(slot.responseHeader.data()) + slot.responseHeaderSent;
      sendingHeader = true;
    } else if (slot.responseBodySent < slot.responseBodyLength) {
      const auto remaining = static_cast<std::size_t>(slot.responseBodyLength - slot.responseBodySent);
      chunk = remaining < kMaxChunk ? remaining : kMaxChunk;
      source = slot.responseBodyProgmem
          ? reinterpret_cast<const std::uint8_t*>(slot.responseProgmem + slot.responseBodySent)
          : responseBuffers_[index].data() + slot.responseBodySent;
    }
    std::size_t sent = 0;
    if (source != nullptr && chunk != 0) {
      const int result = ::send(socket, source, chunk, MSG_DONTWAIT);
      if (result > 0) {
        sent = static_cast<std::size_t>(result);
        if (sendingHeader) {
          slot.responseHeaderSent = static_cast<std::uint16_t>(slot.responseHeaderSent + sent);
        } else {
          slot.responseBodySent = static_cast<std::uint16_t>(slot.responseBodySent + sent);
        }
      } else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ++sendErrors_;
        closeSlot(index);
        continue;
      } else if (result < 0) {
        ++writeWouldBlock_;
        slot.nextWriteAt = now + 5;
      }
    }
    if (sent != 0) {
      slot.lastActivityAt = now;
      slot.nextWriteAt = 0;
    }
    if (slot.responseHeaderSent == slot.responseHeaderLength &&
        slot.responseBodySent == slot.responseBodyLength) {
      ++completed_;
      slot.responseActive = false;
      slot.responseBodyProgmem = false;
      slot.responseProgmem = nullptr;
      slot.responseHeaderLength = 0;
      slot.responseHeaderSent = 0;
      slot.responseBodyLength = 0;
      slot.responseBodySent = 0;
      resetRequest(slot);
      slot.lastActivityAt = now;
      // Rotate long-lived browser sockets before a stale lwIP PCB can retain
      // a poisoned send window indefinitely. One visible UI connection now
      // lives about a minute, avoiding a TIME_WAIT storm as well as 8-minute
      // single-PCB sessions.
      if (++slot.servedRequests >= 64) closeSlot(index);
    } else if (sent == 0 && now - slot.lastActivityAt >= kResponseStallTimeoutMs) {
      ++responseTimeouts_;
      closeSlot(index);
    }
    return;
  }
}

bool HttpServer::readSlot(std::size_t index, HttpRequest& request) {
  auto& slot = slots_[index];
  std::uint16_t readThisPass = 0;
  while (slot.client.available() > 0 && readThisPass < 1024) {
    const int next = slot.client.read();
    if (next < 0) break;
    const char character = static_cast<char>(next);
    ++readThisPass;
    ++slot.totalBytes;
    slot.lastActivityAt = millis();
    if (!slot.firstLineComplete) {
      if (character == '\n') {
        slot.firstLineComplete = true;
      } else if (character != '\r') {
        if (slot.requestLineLength + 1 >= slot.requestLine.size()) {
          closeSlot(index);
          return false;
        }
        slot.requestLine[slot.requestLineLength++] = character;
      }
    } else if (character == '\n') {
      slot.headerLine[slot.headerLineLength] = '\0';
      captureHeader(slot);
      slot.headerLineLength = 0;
      slot.headerLine[0] = '\0';
    } else if (character != '\r') {
      if (slot.headerLineLength + 1 < slot.headerLine.size()) {
        slot.headerLine[slot.headerLineLength++] = character;
      }
    }
    switch (slot.headerTerminatorState) {
      case 0: slot.headerTerminatorState = character == '\r' ? 1 : 0; break;
      case 1: slot.headerTerminatorState = character == '\n' ? 2 : (character == '\r' ? 1 : 0); break;
      case 2: slot.headerTerminatorState = character == '\r' ? 3 : 0; break;
      case 3:
        if (character == '\n') {
          slot.requestLine[slot.requestLineLength] = '\0';
          const char* target = nullptr;
          if (std::strncmp(slot.requestLine.data(), "GET ", 4) == 0) {
            request.method = HttpMethod::Get;
            target = slot.requestLine.data() + 4;
          } else if (std::strncmp(slot.requestLine.data(), "POST ", 5) == 0) {
            request.method = HttpMethod::Post;
            target = slot.requestLine.data() + 5;
          } else {
            request.method = HttpMethod::Other;
            target = slot.requestLine.data();
          }
          const auto* space = std::strchr(target, ' ');
          const auto length = space == nullptr ? std::strlen(target) : static_cast<std::size_t>(space - target);
          if (length == 0 || length >= sizeof(request.target)) {
            closeSlot(index);
            return false;
          }
          std::memcpy(request.target, target, length);
          request.target[length] = '\0';
          std::memcpy(request.ifNoneMatch, slot.ifNoneMatch.data(), slot.ifNoneMatch.size());
          request.ifNoneMatch[sizeof(request.ifNoneMatch) - 1] = '\0';
          request.remoteIpv4 = slot.remoteIpv4;
          responseSlot_ = static_cast<std::int8_t>(index);
          return true;
        }
        slot.headerTerminatorState = character == '\r' ? 1 : 0;
        break;
    }
    if (slot.totalBytes >= 4096) {
      closeSlot(index);
      return false;
    }
  }
  return false;
}

void HttpServer::captureHeader(Slot& slot) {
  static constexpr char kIfNoneMatch[] = "If-None-Match:";
  if (!asciiPrefixEquals(slot.headerLine.data(), kIfNoneMatch)) return;
  const char* value = slot.headerLine.data() + sizeof(kIfNoneMatch) - 1;
  while (*value == ' ' || *value == '\t') ++value;
  std::size_t length = std::strlen(value);
  while (length != 0 && (value[length - 1] == ' ' || value[length - 1] == '\t')) --length;
  if (length >= slot.ifNoneMatch.size()) length = slot.ifNoneMatch.size() - 1;
  std::memcpy(slot.ifNoneMatch.data(), value, length);
  slot.ifNoneMatch[length] = '\0';
}

void HttpServer::send(int status, const char* contentType, const char* content, std::size_t length,
                      const char* cacheControl, const char* contentEncoding, bool progmem,
                      const char* etag) {
  if (responseSlot_ < 0 || static_cast<std::size_t>(responseSlot_) >= slots_.size()) return;
  const auto slotIndex = static_cast<std::size_t>(responseSlot_);
  auto& slot = slots_[slotIndex];
  if (length > largestResponseBytes_ && length <= 0xFFFFu) {
    largestResponseBytes_ = static_cast<std::uint16_t>(length);
  }
  if (!slot.client || (!progmem && length > responseBuffers_[slotIndex].size()) || length > 0xFFFFu) {
    if (length > responseBuffers_[slotIndex].size() || length > 0xFFFFu) ++oversizedResponses_;
    const auto index = static_cast<std::size_t>(responseSlot_);
    responseSlot_ = -1;
    closeSlot(index);
    return;
  }
  const int written = snprintf(slot.responseHeader.data(), slot.responseHeader.size(),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: %s\r\nConnection: keep-alive\r\nKeep-Alive: timeout=10, max=64\r\n%s%s%s%s%s%s\r\n",
      status, statusText(status), contentType, static_cast<unsigned>(length), cacheControl,
      contentEncoding == nullptr ? "" : "Content-Encoding: ",
      contentEncoding == nullptr ? "" : contentEncoding,
      contentEncoding == nullptr ? "" : "\r\n",
      etag == nullptr ? "" : "ETag: ",
      etag == nullptr ? "" : etag,
      etag == nullptr ? "" : "\r\n");
  if (written <= 0 || static_cast<std::size_t>(written) >= slot.responseHeader.size()) {
    const auto index = static_cast<std::size_t>(responseSlot_);
    responseSlot_ = -1;
    closeSlot(index);
    return;
  }
  slot.responseHeaderLength = static_cast<std::uint16_t>(written);
  slot.responseHeaderSent = 0;
  slot.responseBodyLength = static_cast<std::uint16_t>(length);
  slot.responseBodySent = 0;
  slot.responseBodyProgmem = progmem;
  slot.responseProgmem = progmem ? content : nullptr;
  if (!progmem && content != nullptr && length != 0) {
    std::memcpy(responseBuffers_[slotIndex].data(), content, length);
  }
  slot.responseActive = true;
  slot.lastActivityAt = millis();
  responseSlot_ = -1;
}

void HttpServer::sendString(int status, const char* contentType, const String& content,
                            const char* cacheControl) {
  send(status, contentType, content.c_str(), content.length(), cacheControl);
}

void HttpServer::sendEmpty(int status, const char* cacheControl) {
  send(status, "text/plain", nullptr, 0, cacheControl);
}

void HttpServer::sendNotModified(const char* etag, const char* cacheControl) {
  send(304, "text/plain", nullptr, 0, cacheControl, nullptr, false, etag);
}

void HttpServer::resetRequest(Slot& slot) {
  slot.requestLineLength = 0;
  slot.headerLineLength = 0;
  slot.totalBytes = 0;
  slot.headerTerminatorState = 0;
  slot.firstLineComplete = false;
  slot.requestLine[0] = '\0';
  slot.headerLine[0] = '\0';
  slot.ifNoneMatch[0] = '\0';
}

void HttpServer::closeSlot(std::size_t index) {
  if (index >= slots_.size()) return;
  auto& slot = slots_[index];
  slot.client.stop();
  slot.client = WiFiClient{};
  resetRequest(slot);
  slot.responseActive = false;
  slot.responseBodyProgmem = false;
  slot.responseProgmem = nullptr;
  slot.responseHeaderLength = 0;
  slot.responseHeaderSent = 0;
  slot.responseBodyLength = 0;
  slot.responseBodySent = 0;
  slot.servedRequests = 0;
  slot.lastActivityAt = 0;
  slot.nextWriteAt = 0;
  slot.remoteIpv4 = 0;
  if (responseSlot_ == static_cast<std::int8_t>(index)) responseSlot_ = -1;
}

HttpServerDiagnostics HttpServer::diagnostics() const {
  HttpServerDiagnostics output{};
  output.accepted = accepted_;
  output.rejected = rejected_;
  output.completed = completed_;
  output.writeWouldBlock = writeWouldBlock_;
  output.sendErrors = sendErrors_;
  output.requestTimeouts = requestTimeouts_;
  output.responseTimeouts = responseTimeouts_;
  output.oversizedResponses = oversizedResponses_;
  output.largestResponseBytes = largestResponseBytes_;
  for (const auto& slot : slots_) {
    if (slot.client.fd() >= 0) ++output.activeConnections;
    if (slot.responseActive) ++output.pendingResponses;
  }
  return output;
}

}  // namespace gridopoly::server
