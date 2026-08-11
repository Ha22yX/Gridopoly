#include "remote_tile_cache.h"
#include "remote_tile_cache_policy.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <array>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint16_t kImageSize = 128;
constexpr size_t kImageBytes = kRemoteTileImageBytes;
constexpr uint32_t kRetryDelayMs = 3000;
constexpr uint32_t kWorkerPollMs = 250;
constexpr uint32_t kHttpTimeoutMs = 3500;
constexpr uint8_t kSlotCount = static_cast<uint8_t>(GridCityArtwork::Count);
constexpr char kAssetBaseUrl[] = "http://10.42.0.1/assets/tiles/";

enum class SlotState : uint8_t {
    Empty,
    Requested,
    Loading,
    Ready,
};

struct CacheSlot {
    const char *key = nullptr;
    uint8_t *pixels = nullptr;
    lv_img_dsc_t descriptor{};
    uint32_t retryAfterMs = 0;
    uint64_t lastUsed = 0;
    SlotState state = SlotState::Empty;
};

std::array<CacheSlot, kSlotCount> slots{};
portMUX_TYPE cacheMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t workerTaskHandle = nullptr;
uint32_t publishedGeneration = 0;
uint32_t consumedGeneration = 0;
uint64_t accessOrdinal = 0;
size_t cachedBytes = 0;
uint32_t evictionCount = 0;
int8_t pinnedIndex = -1;
bool cacheStarted = false;

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

const lv_img_dsc_t *requestArtwork(GridCityArtwork artwork, const char *key,
                                   bool pinWhenReady)
{
    if (!cacheStarted || key == nullptr) return nullptr;
    const uint8_t index = static_cast<uint8_t>(artwork);
    if (index == 0 || index >= kSlotCount) return nullptr;

    const lv_img_dsc_t *result = nullptr;
    bool wakeWorker = false;
    portENTER_CRITICAL(&cacheMux);
    CacheSlot &slot = slots[index];
    if (slot.state == SlotState::Ready) {
        slot.lastUsed = ++accessOrdinal;
        if (pinWhenReady) pinnedIndex = static_cast<int8_t>(index);
        result = &slot.descriptor;
    } else if (slot.state == SlotState::Empty) {
        slot.key = key;
        slot.retryAfterMs = 0;
        slot.state = SlotState::Requested;
        wakeWorker = true;
    }
    portEXIT_CRITICAL(&cacheMux);
    if (wakeWorker && workerTaskHandle != nullptr) xTaskNotifyGive(workerTaskHandle);
    return result;
}

bool claimRequest(uint8_t &index, const char *&key)
{
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&cacheMux);
    for (uint8_t candidate = 1; candidate < kSlotCount; ++candidate) {
        CacheSlot &slot = slots[candidate];
        if (slot.state == SlotState::Requested &&
            deadlineReached(nowMs, slot.retryAfterMs)) {
            slot.state = SlotState::Loading;
            index = candidate;
            key = slot.key;
            portEXIT_CRITICAL(&cacheMux);
            return true;
        }
    }
    portEXIT_CRITICAL(&cacheMux);
    return false;
}

void retryRequest(uint8_t index)
{
    portENTER_CRITICAL(&cacheMux);
    CacheSlot &slot = slots[index];
    slot.state = SlotState::Requested;
    slot.retryAfterMs = millis() + kRetryDelayMs;
    portEXIT_CRITICAL(&cacheMux);
}

bool evictLeastRecentlyUsed()
{
    std::array<uint64_t, kSlotCount> lastUsed{};
    uint64_t readyMask = 0;
    uint8_t *pixels = nullptr;
    const char *key = nullptr;

    portENTER_CRITICAL(&cacheMux);
    for (uint8_t index = 1; index < kSlotCount; ++index) {
        const CacheSlot &slot = slots[index];
        lastUsed[index] = slot.lastUsed;
        if (slot.state == SlotState::Ready && slot.pixels != nullptr) {
            readyMask |= uint64_t{1} << index;
        }
    }
    const int8_t candidate = remoteTileCacheSelectLru(
        lastUsed.data(), readyMask, kSlotCount, pinnedIndex
    );
    if (candidate >= 0) {
        CacheSlot &slot = slots[static_cast<uint8_t>(candidate)];
        pixels = slot.pixels;
        key = slot.key;
        slot = CacheSlot{};
        cachedBytes -= kImageBytes;
        ++evictionCount;
    }
    portEXIT_CRITICAL(&cacheMux);

    if (pixels == nullptr) return false;
    heap_caps_free(pixels);
    Serial.printf("GRIDOPOLY_ART evict key=%s cached=%u evictions=%lu\n",
                  key == nullptr ? "?" : key,
                  static_cast<unsigned>(cachedBytes),
                  static_cast<unsigned long>(evictionCount));
    return true;
}

bool reserveImageBudget()
{
    while (true) {
        portENTER_CRITICAL(&cacheMux);
        const bool available = cachedBytes + kImageBytes <=
                               kRemoteTileCacheBudgetBytes;
        portEXIT_CRITICAL(&cacheMux);
        if (available) return true;
        if (!evictLeastRecentlyUsed()) return false;
    }
}

bool downloadAsset(const char *key, uint8_t *pixels)
{
    if (key == nullptr || WiFi.localIP() == IPAddress(0, 0, 0, 0)) return false;

    WiFiClient client;
    client.setTimeout(kHttpTimeoutMs);
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(kHttpTimeoutMs);
    http.useHTTP10(true);
    const String url = String(kAssetBaseUrl) + key + ".rgb565";
    if (!http.begin(client, url)) return false;

    const int status = http.GET();
    const int contentLength = http.getSize();
    bool success = status == HTTP_CODE_OK &&
                   contentLength == static_cast<int>(kImageBytes);
    if (success) {
        success = http.getStream().readBytes(pixels, kImageBytes) == kImageBytes;
    }
    http.end();
    return success;
}

void publishAsset(uint8_t index, uint8_t *pixels)
{
    portENTER_CRITICAL(&cacheMux);
    CacheSlot &slot = slots[index];
    slot.pixels = pixels;
    slot.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    slot.descriptor.header.always_zero = 0;
    slot.descriptor.header.reserved = 0;
    slot.descriptor.header.w = kImageSize;
    slot.descriptor.header.h = kImageSize;
    slot.descriptor.data_size = kImageBytes;
    slot.descriptor.data = pixels;
    slot.lastUsed = ++accessOrdinal;
    slot.state = SlotState::Ready;
    cachedBytes += kImageBytes;
    ++publishedGeneration;
    portEXIT_CRITICAL(&cacheMux);
}

void loaderTask(void *)
{
    while (true) {
        uint8_t index = 0;
        const char *key = nullptr;
        if (!claimRequest(index, key)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWorkerPollMs));
            continue;
        }

        auto *pixels = reserveImageBudget()
            ? static_cast<uint8_t *>(heap_caps_malloc(
                  kImageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
            : nullptr;
        if (pixels != nullptr && downloadAsset(key, pixels)) {
            publishAsset(index, pixels);
            Serial.printf("GRIDOPOLY_ART ready key=%s bytes=%u\n",
                          key, static_cast<unsigned>(kImageBytes));
        } else {
            if (pixels != nullptr) heap_caps_free(pixels);
            retryRequest(index);
            Serial.printf("GRIDOPOLY_ART retry key=%s\n", key == nullptr ? "?" : key);
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

} // namespace

void remoteTileCacheBegin()
{
    if (cacheStarted) return;
    cacheStarted = xTaskCreatePinnedToCore(
        loaderTask, "grid-art", 8192, nullptr, 1, &workerTaskHandle, 0
    ) == pdPASS;
    if (!cacheStarted) Serial.println("GRIDOPOLY_ART task_failed");
}

void remoteTileCachePrefetch(GridCityArtwork artwork, const char *key)
{
    requestArtwork(artwork, key, false);
}

const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork artwork, const char *key)
{
    return requestArtwork(artwork, key, true);
}

bool remoteTileCacheConsumeUpdate()
{
    bool changed = false;
    portENTER_CRITICAL(&cacheMux);
    if (consumedGeneration != publishedGeneration) {
        consumedGeneration = publishedGeneration;
        changed = true;
    }
    portEXIT_CRITICAL(&cacheMux);
    return changed;
}
