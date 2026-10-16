#pragma once
#include <cctype>
#include <cstdio>
#include <cstring>
#include "development_artwork_cache.h"

namespace gridopoly::tile::development {
// Local selection override only. No assignment, network DTO or game mutation.
class Probe {
 public:
  bool start(const char *key, unsigned level, std::uint32_t now) {
    if (level < 1 || level > 5) return false;
    const Key candidate = Key::from(key, static_cast<std::uint8_t>(level), true);
    if (!candidate.valid()) return false;
    key_ = candidate; active_ = true; deadline_ = now + 30000U; return true;
  }
  bool stop() { const bool changed = active_; active_ = false; return changed; }
  bool expire(std::uint32_t now) {
    return active_ && policy::artworkDeadlineReached(now, deadline_) ? stop() : false;
  }
  bool active() const { return active_; }
  Key key() const { return active_ ? key_ : Key{}; }
 private:
  Key key_{};
  bool active_ = false;
  std::uint32_t deadline_ = 0;
};
enum class CommandKind { Invalid, Status, Stop, Probe };
struct Command { CommandKind kind = CommandKind::Invalid; char key[40]{}; unsigned level = 0; };
inline Command parseCommand(const char *text) {
  Command result;
  if (std::strcmp(text, "ART STATUS") == 0) result.kind = CommandKind::Status;
  else if (std::strcmp(text, "ART STOP") == 0) result.kind = CommandKind::Stop;
  else if (std::strncmp(text, "ART PROBE ", 10) == 0) {
    char extra = 0, level[8]{};
    if (std::sscanf(text + 10, "%39s %7s %c", result.key, level, &extra) == 2 &&
        level[0] >= '1' && level[0] <= '5' && level[1] == '\0') {
      result.level = static_cast<unsigned>(level[0] - '0');
      for (char *p = result.key; *p; ++p)
        *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
      if (Key::from(result.key, static_cast<std::uint8_t>(result.level), true).valid())
        result.kind = CommandKind::Probe;
    }
  }
  return result;
}
}  // namespace gridopoly::tile::development
