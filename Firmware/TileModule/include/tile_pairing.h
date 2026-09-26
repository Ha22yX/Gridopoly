#pragma once
#include <cstdint>
#include <cstring>

namespace gridopoly::tile {
enum class TilePairingState : std::uint8_t {
  None, WaitingReader, ClearRequired, AwaitingTag, SuccessRemove,
  AlreadyBound, MultipleTags, ReaderOffline, TimedOut, Cancelled, Complete
};
inline const char *tilePairingStateLabel(TilePairingState state) {
  switch (state) {
    case TilePairingState::WaitingReader: return "waiting_reader";
    case TilePairingState::ClearRequired: return "clear_required";
    case TilePairingState::AwaitingTag: return "awaiting_tag";
    case TilePairingState::SuccessRemove: return "success_remove";
    case TilePairingState::AlreadyBound: return "already_bound";
    case TilePairingState::MultipleTags: return "multiple_tags";
    case TilePairingState::ReaderOffline: return "reader_offline";
    case TilePairingState::TimedOut: return "timed_out";
    case TilePairingState::Cancelled: return "cancelled";
    case TilePairingState::Complete: return "complete";
    default: return "none";
  }
}
inline bool parseTilePairingState(const char *text, TilePairingState &state) {
  if (text == nullptr) return false;
  for (unsigned i = 1; i <= static_cast<unsigned>(TilePairingState::Complete); ++i) {
    const auto candidate = static_cast<TilePairingState>(i);
    if (std::strcmp(text, tilePairingStateLabel(candidate)) == 0) {
      state = candidate; return true;
    }
  }
  return false;
}
struct TilePairingSignal {
  bool active = false;
  TilePairingState state = TilePairingState::None;
  std::uint32_t room_id = 0, session_id = 0, revision = 0;
  std::uint32_t lease_ms = 0, received_ms = 0;
  std::uint8_t player_id = 0;
  char player_name[49]{};
};
inline bool pairingVisible(const TilePairingSignal &signal, std::uint32_t now) {
  return signal.active && signal.room_id != 0 && signal.session_id != 0 &&
         signal.state != TilePairingState::None && signal.lease_ms != 0 &&
         static_cast<std::uint32_t>(now - signal.received_ms) < signal.lease_ms;
}
inline bool pairingChanged(const TilePairingSignal &a, const TilePairingSignal &b) {
  return a.active != b.active || a.state != b.state || a.room_id != b.room_id ||
         a.session_id != b.session_id || a.revision != b.revision ||
         a.player_id != b.player_id || std::strcmp(a.player_name, b.player_name) != 0;
}
inline const char *pairingInstruction(TilePairingState state) {
  switch (state) {
    case TilePairingState::AwaitingTag: return "PLACE YOUR TOKEN";
    case TilePairingState::SuccessRemove: return "PAIRED! TAKE IT BACK";
    case TilePairingState::AlreadyBound: return "BOUND - USE ANOTHER";
    case TilePairingState::MultipleTags: return "ONE TOKEN AT A TIME";
    case TilePairingState::ClearRequired: return "REMOVE ALL TOKENS";
    case TilePairingState::ReaderOffline: return "READER UNAVAILABLE";
    case TilePairingState::TimedOut: return "PAIRING TIMED OUT";
    case TilePairingState::Cancelled: return "PAIRING CANCELLED";
    case TilePairingState::Complete: return "ALL TOKENS PAIRED";
    default: return "WAITING FOR READER";
  }
}
struct TagScanEvidence {
  std::uint32_t sequence = 0, completed_ms = 0;
  std::uint32_t pairing_room_id = 0, pairing_session_id = 0;
  bool complete = false;
};
inline constexpr std::uint32_t kTagFreshMs = 5000;
inline bool tagScanFresh(const TagScanEvidence &scan, std::uint32_t now) {
  return scan.complete && scan.sequence != 0 &&
         static_cast<std::uint32_t>(now - scan.completed_ms) < kTagFreshMs;
}
}  // namespace gridopoly::tile
