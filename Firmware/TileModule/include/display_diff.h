#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gridopoly::tile {

struct DisplaySpan {
  std::uint16_t left;
  std::uint16_t right;  // Exclusive; left == right means no changed pixels.
};

inline DisplaySpan changedDisplaySpan(const std::uint16_t *next,
                                      const std::uint16_t *previous,
                                      std::uint16_t width, bool force) {
  if (force) return {0, width};
  std::uint16_t left = 0;
  while (left < width && next[left] == previous[left]) ++left;
  std::uint16_t right = width;
  while (right > left && next[right - 1] == previous[right - 1]) --right;
  return {left, right};
}

struct DisplayTransfer {
  std::uint32_t pixels = 0;
  std::uint32_t rectangles = 0;
  bool complete = true;
};

// Sink writes synchronously. Commit the shadow only after each completed
// rectangle, so a failed submission can be retried from the previous image.
// Equal spans on adjacent rows share one address window and SPI transaction.
template <class Sink>
DisplayTransfer presentDisplayFrame(const std::uint16_t *next,
                                    std::uint16_t *previous,
                                    std::uint16_t width,
                                    std::uint16_t height, bool force,
                                    Sink &sink) {
  DisplayTransfer result;
  for (std::uint16_t y = 0; y < height;) {
    const std::size_t offset = static_cast<std::size_t>(y) * width;
    const DisplaySpan span = changedDisplaySpan(next + offset, previous + offset,
                                                width, force);
    if (span.left == span.right) {
      ++y;
      continue;
    }
    std::uint16_t end = y + 1;
    while (end < height) {
      const std::size_t row = static_cast<std::size_t>(end) * width;
      const DisplaySpan adjacent = changedDisplaySpan(next + row, previous + row,
                                                      width, force);
      if (adjacent.left != span.left || adjacent.right != span.right) break;
      ++end;
    }
    const std::uint16_t count = span.right - span.left;
    if (!sink.write(span.left, y, count, end - y, next, width)) {
      result.complete = false;
      return result;
    }
    for (std::uint16_t row = y; row < end; ++row) {
      const std::size_t start = static_cast<std::size_t>(row) * width + span.left;
      std::memcpy(previous + start, next + start, count * sizeof(*next));
    }
    result.pixels += static_cast<std::uint32_t>(count) * (end - y);
    ++result.rectangles;
    y = end;
  }
  return result;
}

}  // namespace gridopoly::tile
