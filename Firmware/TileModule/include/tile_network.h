#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "tile_model.h"

namespace gridopoly::tile {

enum class TileNetworkLink : std::uint8_t {
  Starting,
  WifiConnecting,
  ServerConnecting,
  OnlineAuto,
  OnlineManual,
  NoFreeTile,
  Fault,
};

inline constexpr std::size_t kMaximumReportedTags = 6U;

enum class TileTagReaderState : std::uint8_t {
  Scanning,
  Stable,
  Fault,
};

inline const char *tileTagReaderStateLabel(TileTagReaderState state) {
  switch (state) {
    case TileTagReaderState::Scanning: return "scanning";
    case TileTagReaderState::Stable: return "stable";
    case TileTagReaderState::Fault: return "fault";
  }
  return "fault";
}

struct TileTagObservation {
  std::uint32_t revision = 0;
  TileTagReaderState state = TileTagReaderState::Scanning;
  std::uint8_t count = 0;
  bool overflow = false;
  char uids[kMaximumReportedTags][9]{};
};

enum class TileMovementCue : std::uint8_t {
  None,
  Departure,
  Destination,
};

inline const char *tileMovementCueLabel(TileMovementCue cue) {
  switch (cue) {
    case TileMovementCue::None: return "none";
    case TileMovementCue::Departure: return "departure";
    case TileMovementCue::Destination: return "destination";
  }
  return "none";
}

inline bool parseTileMovementCue(const char *text, TileMovementCue &cue) {
  if (text == nullptr || std::strcmp(text, "none") == 0) {
    cue = TileMovementCue::None;
    return true;
  }
  if (std::strcmp(text, "departure") == 0) {
    cue = TileMovementCue::Departure;
    return true;
  }
  if (std::strcmp(text, "destination") == 0) {
    cue = TileMovementCue::Destination;
    return true;
  }
  return false;
}

struct TileMovementSignal {
  TileMovementCue mode = TileMovementCue::None;
  std::uint8_t player_id = 0;
  std::uint64_t revision = 0;
};

struct TileNetworkSnapshot {
  std::uint32_t sequence = 0;
  TileNetworkLink link = TileNetworkLink::Starting;
  bool assigned = false;
  TileState tile{};
  std::uint64_t server_revision = 0;
  std::uint64_t assignment_revision = 0;
  TileMovementSignal movement{};
  bool tag_contract_supported = false;
  std::uint32_t lease_ms = 0;
  int http_status = 0;
  std::int8_t rssi = 0;
  char source[8]{};
  char module_id[33]{};
  char device_id[33]{};
};

const char *tileNetworkLabel(TileNetworkLink link);

class TileNetworkClient {
 public:
  void begin();
  bool consume(TileNetworkSnapshot &snapshot);
  // False means the latest local inventory must be retried; it was not queued.
  bool updateTagObservation(TileTagReaderState state,
                            const char uids[][9],
                            std::uint8_t count,
                            bool overflow);

 private:
  static void taskEntry(void *context);
  void taskLoop();

  void startWifiAttempt();

  void publishLink(TileNetworkLink link, int http_status = 0);
  void publish(const TileNetworkSnapshot &snapshot);
  bool heartbeat(TileNetworkSnapshot &snapshot,
                 const TileTagObservation &tags);
  bool legacySnapshot(TileNetworkSnapshot &snapshot);
  bool parsePayload(const char *payload, TileNetworkSnapshot &snapshot,
                    bool legacy);
  TileTagObservation tagObservation();

  void *mutex_ = nullptr;
  void *task_ = nullptr;
  TileNetworkSnapshot shared_{};
  std::uint32_t consumed_sequence_ = 0;
  std::uint32_t last_wifi_attempt_ms_ = 0;
  std::uint32_t dhcp_wait_started_ms_ = 0;

  std::uint8_t consecutive_heartbeat_failures_ = 0;

  bool wifi_was_ready_ = false;
  bool awaiting_dhcp_ = false;

  char module_id_[33]{};
  char device_id_[33]{};
  TileTagObservation observed_tags_{};
};

}  // namespace gridopoly::tile
