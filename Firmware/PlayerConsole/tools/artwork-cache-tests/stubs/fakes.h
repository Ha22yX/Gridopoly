#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
using String = std::string;
inline uint32_t fakeNow = 0, rng = 1234567;
inline int criticalDepth = 0, createdTasks = 0, httpGets = 0, invalidations = 0;
inline size_t liveBytes = 0, peakBytes = 0;
inline bool allocationFails = false;
inline std::unordered_map<void *, size_t> allocations;
inline uint32_t millis() { return fakeNow; }
inline uint32_t esp_random() { rng = rng * 1664525u + 1013904223u; return rng; }
struct FakeEsp { uint64_t getEfuseMac() { return 0x206ef1bac904ull; } };
inline FakeEsp ESP;
struct FakeSerial { void println(const char *) {} };
inline FakeSerial Serial;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) do { (void)(x); assert(criticalDepth++ == 0); } while(0)
#define portEXIT_CRITICAL(x) do { (void)(x); assert(--criticalDepth == 0); } while(0)
using TaskHandle_t = void *;
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, int,
                                  TaskHandle_t *out, int) { ++createdTasks; *out = (void *)1; return pdPASS; }
inline void vTaskDelay(uint32_t ms) { assert(criticalDepth == 0); fakeNow += ms; }
inline void ulTaskNotifyTake(int, uint32_t ms) { vTaskDelay(ms); }
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void *heap_caps_malloc(size_t bytes, int) {
    assert(criticalDepth == 0);
    if (allocationFails) return nullptr;
    void *p = std::malloc(bytes); allocations[p] = bytes;
    liveBytes += bytes; peakBytes = std::max(peakBytes, liveBytes); return p;
}
inline void heap_caps_free(void *p) {
    assert(criticalDepth == 0 && allocations.count(p) == 1);
    liveBytes -= allocations.at(p); allocations.erase(p); std::free(p);
}
struct FakeResponse {
    int status = 200, length = 32768;
    bool stall = false;
    uint32_t readDelay = 0;
    std::string retry, encoding, transfer, url;
    std::vector<uint8_t> body;
    size_t offset = 0;
    std::function<void()> onGet;
};
inline FakeResponse response;
class WiFiClient {
public:
    void setTimeout(uint32_t) {}
    int available() { return response.stall ? 0 : int(response.body.size() - response.offset); }
    int read(uint8_t *out, size_t n) {
        assert(criticalDepth == 0);
        n = std::min(n, response.body.size() - response.offset);
        std::memcpy(out, response.body.data() + response.offset, n);
        response.offset += n; fakeNow += response.readDelay; return int(n);
    }
};
constexpr int HTTP_CODE_OK = 200;
class HTTPClient {
    WiFiClient *stream = nullptr;
public:
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    void setReuse(bool) {}
    void useHTTP10(bool) {}
    bool begin(WiFiClient &s, const String &url) { stream = &s; response.url = url; return true; }
    void collectHeaders(const char **, size_t) {}
    int GET() {
        assert(criticalDepth == 0); ++httpGets; response.offset = 0;
        if (response.onGet) response.onGet();
        return response.status;
    }
    int getSize() { return response.length; }
    String header(const char *key) {
        if (std::strcmp(key, "Retry-After") == 0) return response.retry;
        if (std::strcmp(key, "Content-Encoding") == 0) return response.encoding;
        return response.transfer;
    }
    WiFiClient &getStream() { return *stream; }
    bool connected() { return response.stall || response.offset < response.body.size(); }
    void end() {}
};
