#include "rom_console_stream.h"

RomConsoleStream::RomConsoleStream(char *buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity)
{
    reset();
}

void RomConsoleStream::reset()
{
    length_ = 0;
    truncated_ = false;
    if (buffer_ != nullptr && capacity_ != 0) buffer_[0] = '\0';
}

size_t RomConsoleStream::write(uint8_t value)
{
    if (buffer_ == nullptr || length_ + 1 >= capacity_) {
        truncated_ = true;
        return 0;
    }
    buffer_[length_++] = static_cast<char>(value);
    buffer_[length_] = '\0';
    return 1;
}

bool RomConsoleStream::truncated() const
{
    return truncated_;
}

const char *RomConsoleStream::data() const
{
    return buffer_ == nullptr ? "" : buffer_;
}
