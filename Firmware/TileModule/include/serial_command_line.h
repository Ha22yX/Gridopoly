#pragma once
#include <cstddef>

namespace gridopoly::tile {
enum class SerialLineResult { None, Line, TooLong };
template <std::size_t Capacity>
class SerialCommandLine {
 public:
  static_assert(Capacity > 1, "Serial line needs room for text and terminator");
  SerialLineResult feed(char character) {
    if (character == '\r' || character == '\n') {
      if (discard_) {
        discard_ = false;
        length_ = 0;
        return SerialLineResult::TooLong;
      }
      if (length_ == 0) return SerialLineResult::None;
      text_[length_] = '\0';
      length_ = 0;
      return SerialLineResult::Line;
    }
    if (discard_) return SerialLineResult::None;
    if (length_ + 1U >= Capacity) {
      discard_ = true;  // Never execute a silently truncated command prefix.
      length_ = 0;
    } else {
      text_[length_++] = character;
    }
    return SerialLineResult::None;
  }
  char *data() { return text_; }
 private:
  char text_[Capacity]{};
  std::size_t length_ = 0;
  bool discard_ = false;
};
}  // namespace gridopoly::tile
