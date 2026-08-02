#pragma once

#include <Arduino.h>

class RomConsoleStream final : public Stream {
public:
    RomConsoleStream(char *buffer, size_t capacity);

    void reset();
    size_t write(uint8_t value) override;
    bool truncated() const;
    const char *data() const;

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    char *buffer_ = nullptr;
    size_t capacity_ = 0;
    size_t length_ = 0;
    bool truncated_ = false;
};
