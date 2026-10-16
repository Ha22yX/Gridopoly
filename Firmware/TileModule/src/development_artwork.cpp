#include "development_artwork.h"
#include "development_artwork_probe.h"
#include "development_artwork_transfer.h"
#include "tile_http_endpoint.h"
#include "tile_artwork.h"
#include "tile_assignment.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>

namespace gridopoly::tile {
namespace {
using namespace development;
std::uint8_t *allocateImage(std::size_t size) {
  return static_cast<std::uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
Cache gCache(allocateImage, heap_caps_free);
Probe gProbe;
QueueHandle_t gRequests = nullptr, gCompletions = nullptr;
std::atomic<std::uint32_t> gGeneration{0};
std::uint32_t gSeed = 0, gAttempt = 0, gCacheHits = 0;
Key gLastSelection{};
bool gWorkerReady = false;
struct Completion { Request request; TransferResult result; };

std::uint32_t entropy(Key key) {
  return policy::artworkEntropy(gSeed ^ esp_random(), key.valid() ? key.property->key : "",
                                key.level, ++gAttempt);
}
Key stateKey(const TileState *state) {
  return state ? Key::from(artworkKey(state->artwork), state->building_level,
                          state->kind == TileKind::Property) : Key{};
}
class HttpTransport {
 public:
  HttpReply open(const char *path) {
    HttpReply reply;
    if (WiFi.status() != WL_CONNECTED) return reply;
    http_.setConnectTimeout(1200);
    http_.setTimeout(1200);
    http_.useHTTP10(true);
    http_.setReuse(false);
    http_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const String url = String("http://") + GRIDOPOLY_TILE_HTTP_HOST + ":" +
                       String(static_cast<unsigned>(GRIDOPOLY_TILE_HTTP_PORT)) + path;
    if (!http_.begin(client_, url)) return reply;
    const char *headers[]{"Retry-After", "Content-Encoding", "Transfer-Encoding"};
    http_.collectHeaders(headers, 3);
    reply.status = http_.GET();
    reply.content_length = http_.getSize();
    reply.retry_after_seconds = policy::artworkRetryAfterSeconds(http_.header("Retry-After").c_str());
    const String encoding = http_.header("Content-Encoding");
    reply.identity_encoding = (encoding.isEmpty() || encoding.equalsIgnoreCase("identity")) &&
                              http_.header("Transfer-Encoding").isEmpty();
    return reply;
  }
  int read(std::uint8_t *destination, std::size_t capacity) {
    const int available = client_.available();
    if (available <= 0) return 0;
    return client_.read(destination, std::min<std::size_t>(capacity, static_cast<std::size_t>(available)));
  }
  bool connected() { return client_.connected() || client_.available() > 0; }
  std::uint32_t now() { return millis(); }
  void yield() { vTaskDelay(pdMS_TO_TICKS(1)); }
  void close() { http_.end(); client_.stop(); }
 private:
  WiFiClient client_;
  HTTPClient http_;
};

void downloadWorker(void *) {
  Request request;
  for (;;) {
    if (xQueueReceive(gRequests, &request, portMAX_DELAY) != pdTRUE) continue;
    HttpTransport transport;
    Completion completion{request, download(request, transport, [&] {
      return gGeneration.load(std::memory_order_acquire) != request.generation;
    })};
    // Exactly one outstanding request; this bounded queue is empty until its
    // corresponding completion. Waiting here cannot block the display owner.
    xQueueSend(gCompletions, &completion, portMAX_DELAY);
  }
}
}  // namespace

void beginDevelopmentArtwork() {
  if (gWorkerReady) return;
  gSeed = esp_random();
  std::uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
    for (auto byte : mac) gSeed = (gSeed ^ byte) * 16777619U;
  gRequests = xQueueCreate(1, sizeof(Request));
  gCompletions = xQueueCreate(1, sizeof(Completion));
  if (gRequests && gCompletions &&
      xTaskCreatePinnedToCore(downloadWorker, "tile-artwork", 8192, nullptr, 1, nullptr, 0) == pdPASS) {
    gWorkerReady = true;
  } else {
    if (gRequests) vQueueDelete(gRequests);
    if (gCompletions) vQueueDelete(gCompletions);
    gRequests = gCompletions = nullptr;
  }
  Serial.printf("[ART] worker=%s ready_limit=5 staging=1 ram_limit=307200 endpoint=%s:%u cache=RAM_ONLY\r\n",
                gWorkerReady ? "READY" : "DISABLED", GRIDOPOLY_TILE_HTTP_HOST,
                static_cast<unsigned>(GRIDOPOLY_TILE_HTTP_PORT));
}

bool serviceDevelopmentArtwork(const TileState *authoritative, std::uint32_t now) {
  bool redraw = gProbe.expire(now);
  if (redraw) Serial.println(F("[ART-PROBE] expired restore=AUTHORITATIVE"));
  const Key selected = gProbe.active() ? gProbe.key() : stateKey(authoritative);
  if (selected != gLastSelection) {
    if (!gProbe.active() && gCache.acquire(selected).pixels()) ++gCacheHits;
    gLastSelection = selected;
  }
  gCache.select(selected, now, entropy(selected));
  gGeneration.store(gCache.generation(), std::memory_order_release);
  gCache.online(WiFi.status() == WL_CONNECTED, now, entropy(selected));
  if (!gWorkerReady) return redraw;
  Completion completion;
  if (xQueueReceive(gCompletions, &completion, 0) == pdTRUE) {
    const bool published = gCache.finish(completion.request, completion.result, now, entropy(selected));
    redraw = redraw || published;
    Serial.printf("[ART%s] result=%u http=%d key=%s level=%u bytes=%u crc=%08X published=%s ready=%u ram=%u\r\n",
        gProbe.active() ? "-PROBE" : "", static_cast<unsigned>(completion.result.state),
        completion.result.http_status, completion.request.key.property->key, completion.request.key.level,
        static_cast<unsigned>(completion.result.received), completion.result.crc,
        published ? "YES" : "NO", gCache.readyCount(), static_cast<unsigned>(gCache.allocatedBytes()));
  }
  Request request;
  if (gCache.next(now, entropy(selected), request)) {
    if (xQueueSend(gRequests, &request, 0) != pdTRUE) {
      TransferResult failure; failure.state = TransferState::QueueError;
      gCache.finish(request, failure, now, entropy(selected));
    } else {
      Serial.printf("[ART%s] request=%u key=%s level=%u generation=%u\r\n",
                    gProbe.active() ? "-PROBE" : "", gCache.requests(), request.key.property->key,
                    request.key.level, request.generation);
    }
  }
  return redraw;
}

development::Cache::Lease acquireDevelopmentArtwork(const TileState &state) {
  return gCache.acquire(stateKey(&state));
}
bool developmentArtworkPreview(TileState &state) {
  if (!gProbe.active()) return false;
  state = {};
  state.kind = TileKind::Property;
  state.artwork = artworkFromServerKey(gProbe.key().property->key);
  state.building_level = gProbe.key().level;
  copyTileText(state.tile_id, sizeof(state.tile_id), gProbe.key().property->tileId);
  copyTileText(state.display_name, sizeof(state.display_name), gProbe.key().property->key);
  return true;
}
bool developmentArtworkProbeActive() { return gProbe.active(); }
void printDevelopmentArtworkStatus() {
  Serial.printf("[ART%s] worker=%s requests=%u published=%u hits=%u stale=%u failures=%u alloc_failures=%u ready=%u busy=%s ram=%u peak_ram=%u limit=307200\r\n",
      gProbe.active() ? "-PROBE" : "", gWorkerReady ? "READY" : "DISABLED", gCache.requests(),
      gCache.published(), gCacheHits, gCache.discarded(), gCache.failures(), gCache.allocationFailures(),
      gCache.readyCount(), gCache.busy() ? "YES" : "NO", static_cast<unsigned>(gCache.allocatedBytes()),
      static_cast<unsigned>(gCache.allocatedBytes()));
}
bool developmentArtworkCommand(const char *command, std::uint32_t now) {
  const auto parsed = development::parseCommand(command);
  switch (parsed.kind) {
    case CommandKind::Probe:
      if (gProbe.start(parsed.key, parsed.level, now)) {
        const bool cached = gCache.acquire(gProbe.key()).pixels() != nullptr;
        if (cached) ++gCacheHits;
        Serial.printf("[ART-PROBE] start key=%s level=%u cached=%s duration_ms=30000 game_mutation=NONE\r\n",
                      parsed.key, parsed.level, cached ? "YES" : "NO");
        printDevelopmentArtworkStatus(); return true;
      }
      break;
    case CommandKind::Stop:
      gProbe.stop();
      Serial.println(F("[ART-PROBE] stopped restore=AUTHORITATIVE"));
      return true;
    case CommandKind::Status: printDevelopmentArtworkStatus(); return false;
    case CommandKind::Invalid: break;
  }
  Serial.println(F("ERR: ART PROBE <property-artwork-key> <1..5>, ART STOP, ART STATUS"));
  return false;
}
}  // namespace gridopoly::tile
