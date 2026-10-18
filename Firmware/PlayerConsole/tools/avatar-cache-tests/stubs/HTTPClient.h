#pragma once
#include "Arduino.h"
#include "WiFiClient.h"
#include <algorithm>
#include <cstring>
#include <vector>
constexpr int HTTP_CODE_OK = 200;
inline std::vector<uint8_t> avatarHttpBody;
inline int avatarHttpStatus = 200;
inline int avatarHttpLength = 0;
inline String avatarHttpUrl;
struct HTTPClient {
    void setConnectTimeout(int) {}
    void setTimeout(uint32_t) {}
    void useHTTP10(bool) {}
    void setReuse(bool) {}
    bool begin(WiFiClient &, const String &url) { avatarHttpUrl = url; return true; }
    int GET() { return avatarHttpStatus; }
    int getSize() { return avatarHttpLength; }
    HTTPClient &getStream() { return *this; }
    size_t readBytes(uint8_t *bytes, size_t count) {
        count = std::min(count, avatarHttpBody.size());
        if (count) std::memcpy(bytes, avatarHttpBody.data(), count);
        return count;
    }
    void end() {}
};
