#pragma once

#include <algorithm>
#include <cstdio>
#include "development_artwork_cache.h"

namespace gridopoly::tile::development {
struct HttpReply {
  int status = 0, content_length = -1;
  std::uint32_t retry_after_seconds = 0;
  bool identity_encoding = true;
};

// The same streaming/validation routine is used by the ESP worker and host
// fault tests. Transport::read is nonblocking; only the worker opens HTTP.
template <typename Transport, typename Cancelled>
TransferResult download(const Request &request, Transport &transport, Cancelled cancelled) {
  struct Close { Transport &transport; ~Close() { transport.close(); } } close{transport};
  TransferResult result;
  if (cancelled()) { result.state = TransferState::Cancelled; return result; }
  if (!request.key.valid() || !request.bytes) return result;
  char path[128];
  const int size = std::snprintf(path, sizeof(path), "%stile/%s-l%u.rgb565",
      policy::kDevelopmentAssetPrefix, request.key.property->key,
      static_cast<unsigned>(request.key.level));
  if (size < 0 || static_cast<std::size_t>(size) >= sizeof(path)) return result;
  const HttpReply reply = transport.open(path);
  result.http_status = reply.status;
  result.content_length = reply.content_length;
  result.retry_after_seconds = reply.retry_after_seconds;
  if (reply.status != 200) return result;
  if (cancelled()) { result.state = TransferState::Cancelled; return result; }
  if (reply.content_length != static_cast<int>(kImageBytes)) {
    result.state = TransferState::LengthError; return result;
  }
  if (!reply.identity_encoding) { result.state = TransferState::EncodingError; return result; }
  const std::uint32_t started = transport.now();
  std::uint32_t last_progress = started, crc = policy::kArtworkCrc32Initial;
  while (result.received < kImageBytes) {
    if (cancelled()) { result.state = TransferState::Cancelled; return result; }
    const std::uint32_t now = transport.now();
    if (static_cast<std::uint32_t>(now - started) >= 8000U ||
        static_cast<std::uint32_t>(now - last_progress) >= 1500U) {
      result.state = TransferState::Timeout; return result;
    }
    const std::size_t capacity = std::min<std::size_t>(1024, kImageBytes - result.received);
    const int count = transport.read(request.bytes + result.received, capacity);
    if (count < 0 || static_cast<std::size_t>(count) > capacity) {
      result.state = TransferState::Truncated; return result;
    }
    if (count > 0) {
      crc = policy::artworkCrc32Update(crc, request.bytes + result.received,
                                     static_cast<std::size_t>(count));
      result.received += static_cast<std::size_t>(count);
      last_progress = now;
    } else if (!transport.connected()) {
      result.state = TransferState::Truncated; return result;
    }
    transport.yield();  // Let the network/heartbeat task and idle task run.
  }
  result.crc = policy::artworkCrc32Finish(crc);  // Raw little-endian bytes.
  result.state = result.crc == request.key.crc() ? TransferState::Complete : TransferState::CrcError;
  return result;
}
}  // namespace gridopoly::tile::development
