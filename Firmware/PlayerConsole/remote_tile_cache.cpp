#include "remote_tile_cache.h"
#include "remote_tile_cache_policy.h"
#include <gridopoly/assets/RemoteArtworkPolicy.h>
#include <Arduino.h>
#include <ESP.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <algorithm>
#include <array>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#endif
#ifndef GRIDOPOLY_ARTWORK_HOST
#define GRIDOPOLY_ARTWORK_HOST "10.42.0.1"
#endif

namespace {
using namespace gridopoly::assets;
constexpr uint32_t kHttpTimeoutMs = 3500;
constexpr uint32_t kWorkerPollMs = 100;
enum class SlotState : uint8_t { Empty, Loading, Ready };
struct CacheSlot {
    RemoteArtworkRequest request{};
    lv_img_dsc_t descriptor{};
    uint8_t *pixels = nullptr;
    uint64_t lastUsed = 0;
    uint16_t leases = 0;
    SlotState state = SlotState::Empty;
};
struct LoadTicket {
    RemoteArtworkRequest request{};
    uint32_t context = 0;
    uint8_t slot = 0;
};
std::array<CacheSlot, kRemoteTileCacheCapacity> slots{};
std::array<RemoteArtworkRequest, 2> wanted{};
portMUX_TYPE cacheMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t workerTaskHandle = nullptr;
bool cacheStarted = false;
uint32_t contextGeneration = 0, contextRoom = 0;
uint8_t contextBoard = 0;
uint32_t publishedGeneration = 0, consumedGeneration = 0;
uint64_t accessOrdinal = 0;
// Only the single worker writes retry state after startup.
uint32_t nextAttemptMs = 0, artworkRandomSeed = 0, attemptOrdinal = 0;
uint8_t failures = 0, nextWanted = 0;

bool wantedLocked(const RemoteArtworkRequest &request)
{
    return request.valid() && (sameArtwork(wanted[0], request) || sameArtwork(wanted[1], request));
}
int readySlotLocked(const RemoteArtworkRequest &request)
{
    for (uint8_t i = 0; i < slots.size(); ++i)
        if (slots[i].state == SlotState::Ready && sameArtwork(slots[i].request, request)) return i;
    return -1;
}

// Only the worker replaces pixels. Allocation, free and HTTP are outside the
// lock; acquiring an LVGL lease and choosing an eviction victim are atomic.
bool claimRequest(LoadTicket &ticket, uint8_t *&evictedPixels)
{
    if (!artworkDeadlineReached(millis(), nextAttemptMs)) return false;
    portENTER_CRITICAL(&cacheMux);
    int requestIndex = -1;
    for (uint8_t n = 0; n < wanted.size(); ++n) {
        const uint8_t i = (nextWanted + n) % wanted.size();
        if (wanted[i].valid() && readySlotLocked(wanted[i]) < 0) { requestIndex = i; break; }
    }
    int candidate = -1;
    uint64_t oldest = UINT64_MAX;
    if (requestIndex >= 0) for (uint8_t i = 0; i < slots.size(); ++i) {
        const auto &slot = slots[i];
        if (slot.state == SlotState::Empty) { candidate = i; break; }
        if (slot.state == SlotState::Ready && slot.leases == 0 &&
            !wantedLocked(slot.request) && slot.lastUsed < oldest) {
            candidate = i; oldest = slot.lastUsed;
        }
    }
    if (candidate < 0) { portEXIT_CRITICAL(&cacheMux); return false; }
    auto &slot = slots[candidate];
    evictedPixels = slot.pixels;
    slot = CacheSlot{};
    slot.request = wanted[requestIndex];
    slot.state = SlotState::Loading;
    ticket = {slot.request, contextGeneration, static_cast<uint8_t>(candidate)};
    nextWanted = static_cast<uint8_t>((requestIndex + 1) % wanted.size());
    portEXIT_CRITICAL(&cacheMux);
    return true;
}

bool downloadAsset(const RemoteArtworkRequest &request, uint8_t *pixels,
                   uint32_t &retryAfterSeconds)
{
    char path[128]{};
    if (!artworkPath(request, path, sizeof(path))) return false;
    WiFiClient client;
    client.setTimeout(kHttpTimeoutMs);
    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(kHttpTimeoutMs);
    http.setReuse(false);
    http.useHTTP10(true);
    const String url = String("http://") + GRIDOPOLY_ARTWORK_HOST + path;
    if (!http.begin(client, url)) return false;
    const char *headers[] = {"Retry-After", "Content-Encoding", "Transfer-Encoding"};
    http.collectHeaders(headers, 3);
    const int status = http.GET();
    if (status == 429 || status == 503)
        retryAfterSeconds = artworkRetryAfterSeconds(http.header("Retry-After").c_str());
    bool success = status == HTTP_CODE_OK && http.getSize() == static_cast<int>(kRemoteTileImageBytes) &&
                   http.header("Content-Encoding").length() == 0 &&
                   http.header("Transfer-Encoding").length() == 0;
    size_t received = 0;
    uint32_t crc = kArtworkCrc32Initial;
    const uint32_t deadline = millis() + kHttpTimeoutMs;
    auto &stream = http.getStream();
    while (success && received < kRemoteTileImageBytes) {
        if (artworkDeadlineReached(millis(), deadline)) { success = false; break; }
        const int available = stream.available();
        if (available <= 0) {
            if (!http.connected()) { success = false; break; }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        const size_t chunk = std::min<size_t>(1024, std::min<size_t>(available, kRemoteTileImageBytes - received));
        const int count = stream.read(pixels + received, chunk);
        if (count <= 0) { success = false; break; }
        crc = artworkCrc32Update(crc, pixels + received, static_cast<size_t>(count));
        received += static_cast<size_t>(count);
    }
    success = success && received == kRemoteTileImageBytes && artworkCrc32Finish(crc) == request.crc;
    http.end();
    return success;
}

bool completeRequest(const LoadTicket &ticket, uint8_t *pixels, bool downloaded)
{
    bool published = false;
    portENTER_CRITICAL(&cacheMux);
    auto &slot = slots[ticket.slot];
    if (downloaded && ticket.context == contextGeneration && wantedLocked(ticket.request)) {
        slot.pixels = pixels;
        slot.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
        slot.descriptor.header.w = slot.descriptor.header.h = 128;
        slot.descriptor.data_size = kRemoteTileImageBytes;
        slot.descriptor.data = pixels;
        slot.lastUsed = ++accessOrdinal;
        slot.state = SlotState::Ready;
        // Prefetch completion must not rebuild an unrelated visible page.
        if (sameArtwork(wanted[0], ticket.request)) ++publishedGeneration;
        published = true;
    } else slot = CacheSlot{};
    portEXIT_CRITICAL(&cacheMux);
    if (!published && pixels) heap_caps_free(pixels);
    return published;
}

bool loadOne()
{
    LoadTicket ticket{};
    uint8_t *evicted = nullptr;
    if (!claimRequest(ticket, evicted)) return false;
    // Free before allocating: resident plus in-flight payload never exceeds 640 KiB.
    if (evicted) heap_caps_free(evicted);
    auto *pixels = static_cast<uint8_t *>(heap_caps_malloc(
        kRemoteTileImageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint32_t retryAfter = 0;
    const bool success = pixels && downloadAsset(ticket.request, pixels, retryAfter);
    completeRequest(ticket, pixels, success);
    if (success) {
        failures = 0;
        nextAttemptMs = millis() + 80;
    } else {
        if (failures < 255) ++failures;
        const uint32_t entropy = artworkEntropy(artworkRandomSeed ^ esp_random(), ticket.request.key,
                                                 ticket.request.level, ++attemptOrdinal);
        nextAttemptMs = millis() + retryArtworkDelayMs(failures, entropy, retryAfter);
    }
    return true;
}
void loaderTask(void *)
{
    while (true) {
        loadOne();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWorkerPollMs));
    }
}
} // namespace

void remoteTileCacheBegin()
{
    if (cacheStarted) return;
    const uint64_t mac = ESP.getEfuseMac();
    artworkRandomSeed = static_cast<uint32_t>(mac) ^ static_cast<uint32_t>(mac >> 32) ^ esp_random();
    nextAttemptMs = millis() + initialArtworkDelayMs(artworkEntropy(artworkRandomSeed, "player", 0));
    cacheStarted = xTaskCreatePinnedToCore(loaderTask, "grid-art", 8192, nullptr, 1,
                                         &workerTaskHandle, 0) == pdPASS;
    if (!cacheStarted) Serial.println("GRIDOPOLY_ART task_failed");
}
void remoteTileCacheSetView(uint32_t room, uint8_t boardSize, GridCityArtwork artwork,
                           const char *key, uint8_t level)
{
    const auto request = artworkRequest(artwork, key, level);
    portENTER_CRITICAL(&cacheMux);
    if (contextRoom != room || contextBoard != boardSize) {
        ++contextGeneration;
        contextRoom = room; contextBoard = boardSize;
        wanted[1] = {};
    }
    wanted[0] = request;
    portEXIT_CRITICAL(&cacheMux);
}
void remoteTileCachePrefetch(GridCityArtwork artwork, const char *key, uint8_t level)
{
    const auto request = artworkRequest(artwork, key, level);
    portENTER_CRITICAL(&cacheMux);
    wanted[1] = request;
    portEXIT_CRITICAL(&cacheMux);
}
const lv_img_dsc_t *remoteTileCacheImage(GridCityArtwork artwork, const char *key, uint8_t level)
{
    if (!cacheStarted) return nullptr;
    const auto request = artworkRequest(artwork, key, level);
    const lv_img_dsc_t *result = nullptr;
    portENTER_CRITICAL(&cacheMux);
    const int i = readySlotLocked(request);
    if (i >= 0 && slots[i].leases < UINT16_MAX) {
        ++slots[i].leases;
        slots[i].lastUsed = ++accessOrdinal;
        result = &slots[i].descriptor;
    }
    portEXIT_CRITICAL(&cacheMux);
    return result;
}
void remoteTileCacheReleaseImage(const lv_img_dsc_t *image)
{
    if (!image) return;
    for (auto &slot : slots) if (&slot.descriptor == image) {
        // Decoder cache may retain the descriptor after object deletion. Drop
        // it on the LVGL thread BEFORE the lease permits worker-side eviction.
        lv_img_cache_invalidate_src(image);
        portENTER_CRITICAL(&cacheMux);
        if (slot.leases != 0) --slot.leases;
        portEXIT_CRITICAL(&cacheMux);
        return;
    }
}
bool remoteTileCacheConsumeUpdate()
{
    portENTER_CRITICAL(&cacheMux);
    const bool changed = consumedGeneration != publishedGeneration;
    consumedGeneration = publishedGeneration;
    portEXIT_CRITICAL(&cacheMux);
    return changed;
}
