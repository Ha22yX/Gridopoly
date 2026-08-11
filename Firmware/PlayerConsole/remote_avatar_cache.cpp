#include "remote_avatar_cache.h"

#include "avatar_component_math.h"
#include "remote_tile_cache_policy.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <array>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t kComponentKindCount = 3;
constexpr uint8_t kComponentPresetsPerKind = 10;
constexpr uint8_t kComponentSlotCount =
    kComponentKindCount * kComponentPresetsPerKind;
constexpr uint8_t kFinalSlotCount = 6;
constexpr uint16_t kPreviewWidth = 220;
constexpr uint16_t kPreviewHeight = 300;
constexpr uint32_t kRetryDelayMs = 2500;
constexpr uint32_t kWorkerPollMs = 100;
constexpr uint32_t kHttpTimeoutMs = 5000;
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr size_t kGavcHeaderBytes = 32;
constexpr size_t kMaxComponentFileBytes = 128u * 1024u;
// LVGL's RGB565 chroma key keeps the empty preview canvas transparent without
// adding a 66 KiB alpha plane. Partially transparent edge pixels are blended
// against the actual console background so antialiasing remains clean.
constexpr uint16_t kPreviewChromaKey = 0x07E0;
constexpr uint32_t kPreviewBackground = 0x061017;
constexpr uint32_t kPreviewEdgeBackground = kPreviewBackground;
constexpr char kServerBaseUrl[] = "http://10.42.0.1/";

enum class CacheMode : uint8_t { None, Preview, Finals, PreviewAndFinals };
enum class SlotState : uint8_t { Empty, Requested, Loading, Ready };
enum class RequestKind : uint8_t { None, Component, Final };

constexpr std::array<AvatarComponentRgb, 20> kHairPalette{{
    {104, 116, 124}, {176, 83, 43}, {40, 48, 58}, {80, 55, 47},
    {142, 90, 60}, {209, 164, 79}, {216, 204, 176}, {45, 132, 138},
    {112, 92, 82}, {139, 54, 42}, {104, 42, 44}, {200, 151, 78},
    {213, 139, 92}, {166, 178, 188}, {235, 238, 234}, {117, 37, 63},
    {194, 85, 119}, {111, 78, 160}, {55, 93, 168}, {48, 125, 91},
}};

constexpr std::array<AvatarComponentRgb, 8> kSkinPalette{{
    {239, 202, 173}, {232, 181, 139}, {224, 158, 100}, {202, 141, 82},
    {173, 128, 83}, {161, 96, 62}, {137, 83, 56}, {91, 53, 42},
}};

struct ComponentHeader {
    uint8_t kind = 0;
    uint8_t preset = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t decodedBytes = 0;
    uint32_t encodedBytes = 0;
    uint32_t crc32 = 0;
};

struct ComponentSlot {
    char path[96]{};
    uint8_t *file = nullptr;
    size_t fileBytes = 0;
    uint32_t retryAfterMs = 0;
    uint32_t serial = 0;
    uint8_t expectedKind = 0;
    uint8_t preset = 0;
    ComponentHeader header{};
    SlotState state = SlotState::Empty;
};

struct FinalSlot {
    char path[176]{};
    uint8_t *pixels = nullptr;
    lv_img_dsc_t descriptor{};
    uint32_t retryAfterMs = 0;
    uint32_t serial = 0;
    SlotState state = SlotState::Empty;
};

struct ClaimedRequest {
    RequestKind kind = RequestKind::None;
    uint8_t index = 0;
    uint32_t serial = 0;
    char path[176]{};
};

struct RleCursor {
    const uint8_t *cursor = nullptr;
    const uint8_t *end = nullptr;
    uint32_t producedPixels = 0;
    uint32_t expectedPixels = 0;
    uint16_t remaining = 0;
    bool repeat = false;
    uint8_t repeatedPixel[4]{};

    bool next(uint8_t pixel[4])
    {
        if (remaining == 0) {
            if (cursor + 2 > end) return false;
            const uint16_t token = static_cast<uint16_t>(cursor[0]) |
                                   (static_cast<uint16_t>(cursor[1]) << 8);
            cursor += 2;
            remaining = static_cast<uint16_t>(token & 0x7FFFu);
            repeat = (token & 0x8000u) != 0;
            if (remaining == 0 || producedPixels + remaining > expectedPixels) return false;
            if (repeat) {
                if (cursor + 4 > end) return false;
                std::memcpy(repeatedPixel, cursor, 4);
                cursor += 4;
            }
        }
        if (repeat) {
            std::memcpy(pixel, repeatedPixel, 4);
        } else {
            if (cursor + 4 > end) return false;
            std::memcpy(pixel, cursor, 4);
            cursor += 4;
        }
        --remaining;
        ++producedPixels;
        return true;
    }

    bool complete() const
    {
        return producedPixels == expectedPixels && remaining == 0 && cursor == end;
    }
};

std::array<ComponentSlot, kComponentSlotCount> components{};
std::array<FinalSlot, kFinalSlotCount> finals{};
SemaphoreHandle_t cacheMutex = nullptr;
std::array<TaskHandle_t, kRemoteAvatarDownloadWorkerCount> workerTaskHandles{};
CacheMode cacheMode = CacheMode::None;
TransportAvatarRecipe desiredRecipe{};
TransportAvatarRecipe composedRecipe{};
uint8_t *previewPixels = nullptr;
lv_img_dsc_t previewDescriptor{};
bool previewReady = false;
bool composeRequested = false;
uint32_t recipeGeneration = 0;
uint32_t publishedGeneration = 0;
uint32_t consumedGeneration = 0;
size_t setupCachedBytes = 0;
size_t finalCachedBytes = 0;
bool cacheStarted = false;
bool preloadScheduled = false;
bool preloadComplete = false;
bool previewRecoveryAttempted = false;

struct WorkerContext {
    WiFiClient httpClient;
    uint8_t index = 0;
};

std::array<WorkerContext, kRemoteAvatarDownloadWorkerCount> workerContexts{};

uint8_t componentIndex(uint8_t kind, uint8_t preset)
{
    return static_cast<uint8_t>((kind - 1u) * kComponentPresetsPerKind +
                                (preset - 1u));
}

uint8_t desiredPresetForKind(uint8_t kind)
{
    if (kind == 1) return desiredRecipe.facePresetId;
    if (kind == 2) return desiredRecipe.outfitPresetId;
    return desiredRecipe.hairPresetId;
}

ComponentSlot &desiredComponent(uint8_t kind)
{
    return components[componentIndex(kind, desiredPresetForKind(kind))];
}

uint16_t get16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t get32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

bool sameRecipe(const TransportAvatarRecipe &a, const TransportAvatarRecipe &b)
{
    return a.catalogVersion == b.catalogVersion &&
           a.hairPresetId == b.hairPresetId && a.hairColorId == b.hairColorId &&
           a.facePresetId == b.facePresetId && a.skinToneId == b.skinToneId &&
           a.outfitPresetId == b.outfitPresetId;
}

uint32_t crc32Update(uint32_t crc, const uint8_t *bytes, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u &
                  static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u)));
        }
    }
    return crc;
}

bool parseComponent(const uint8_t *file, size_t fileBytes,
                    uint8_t expectedKind, uint8_t expectedPreset,
                    ComponentHeader &header)
{
    if (file == nullptr || fileBytes < kGavcHeaderBytes ||
        std::memcmp(file, "GAVC", 4) != 0 || file[4] != 1 ||
        file[5] != expectedKind || file[6] != expectedPreset || file[7] != 1 ||
        get16(file + 8) != kPreviewWidth || get16(file + 10) != kPreviewHeight) {
        return false;
    }
    header.kind = file[5];
    header.preset = file[6];
    header.x = get16(file + 12);
    header.y = get16(file + 14);
    header.width = get16(file + 16);
    header.height = get16(file + 18);
    header.decodedBytes = get32(file + 20);
    header.encodedBytes = get32(file + 24);
    header.crc32 = get32(file + 28);
    if (header.width == 0 || header.height == 0 ||
        header.x + header.width > kPreviewWidth ||
        header.y + header.height > kPreviewHeight ||
        header.decodedBytes != static_cast<uint32_t>(header.width) * header.height * 4u ||
        header.encodedBytes != fileBytes - kGavcHeaderBytes) return false;

    RleCursor decoder{file + kGavcHeaderBytes, file + fileBytes, 0,
                      header.decodedBytes / 4u};
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t pixel[4];
    while (decoder.producedPixels < decoder.expectedPixels) {
        if (!decoder.next(pixel)) return false;
        crc = crc32Update(crc, pixel, sizeof(pixel));
    }
    return decoder.complete() && (crc ^ 0xFFFFFFFFu) == header.crc32;
}

void freeComponentLocked(ComponentSlot &slot)
{
    if (slot.file != nullptr) {
        setupCachedBytes -= slot.fileBytes;
        heap_caps_free(slot.file);
    }
    slot = ComponentSlot{};
}

void freeFinalLocked(FinalSlot &slot)
{
    if (slot.pixels != nullptr) {
        finalCachedBytes -= kRemoteAvatarFinalBytes;
        heap_caps_free(slot.pixels);
    }
    slot = FinalSlot{};
}

void clearPreviewLocked()
{
    for (ComponentSlot &slot : components) freeComponentLocked(slot);
    if (previewPixels != nullptr) {
        setupCachedBytes -= kRemoteAvatarPreviewBytes;
        heap_caps_free(previewPixels);
        previewPixels = nullptr;
    }
    previewDescriptor = lv_img_dsc_t{};
    previewReady = false;
    composeRequested = false;
    desiredRecipe = TransportAvatarRecipe{};
    composedRecipe = TransportAvatarRecipe{};
    preloadScheduled = false;
    preloadComplete = false;
    previewRecoveryAttempted = false;
}

void clearFinalsLocked()
{
    for (FinalSlot &slot : finals) freeFinalLocked(slot);
}

void enterModeLocked(CacheMode mode)
{
    if (mode == CacheMode::Preview) {
        if (cacheMode == CacheMode::Preview ||
            cacheMode == CacheMode::PreviewAndFinals) return;
        if (cacheMode == CacheMode::Finals) clearFinalsLocked();
        cacheMode = CacheMode::Preview;
        return;
    }
    if (mode == CacheMode::Finals) {
        if (cacheMode == CacheMode::Finals ||
            cacheMode == CacheMode::PreviewAndFinals) return;
        cacheMode = cacheMode == CacheMode::Preview
            ? CacheMode::PreviewAndFinals : CacheMode::Finals;
    }
}

bool previewModeLocked()
{
    return cacheMode == CacheMode::Preview ||
           cacheMode == CacheMode::PreviewAndFinals;
}

bool finalModeLocked()
{
    return cacheMode == CacheMode::Finals ||
           cacheMode == CacheMode::PreviewAndFinals;
}

bool allComponentsReadyLocked()
{
    for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
        if (desiredComponent(kind).state != SlotState::Ready) return false;
    }
    return true;
}

bool allLibraryComponentsReadyLocked()
{
    for (const ComponentSlot &slot : components) {
        if (slot.state != SlotState::Ready) return false;
    }
    return true;
}

bool ensurePreviewBufferLocked()
{
    if (previewPixels != nullptr) return true;
    previewPixels = static_cast<uint8_t *>(heap_caps_malloc(
        kRemoteAvatarPreviewBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (previewPixels == nullptr) return false;
    setupCachedBytes += kRemoteAvatarPreviewBytes;
    return true;
}

bool requestComponentLocked(uint8_t kind, uint8_t preset, const char *path)
{
    const uint8_t index = componentIndex(kind, preset);
    ComponentSlot &slot = components[index];
    if (slot.state != SlotState::Empty && slot.expectedKind == kind &&
        slot.preset == preset && std::strcmp(slot.path, path) == 0) return false;
    freeComponentLocked(slot);
    std::strncpy(slot.path, path, sizeof(slot.path) - 1);
    slot.expectedKind = kind;
    slot.preset = preset;
    slot.serial = ++recipeGeneration;
    if (slot.serial == 0) slot.serial = ++recipeGeneration;
    slot.state = SlotState::Requested;
    return true;
}

bool scheduleDesiredComponentsLocked()
{
    bool scheduled = false;
    char path[96];
    for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
        const uint8_t preset = desiredPresetForKind(kind);
        const char prefix = kind == 1 ? 'f' : (kind == 2 ? 'o' : 'h');
        const char *folder = kind == 1 ? "face" : (kind == 2 ? "outfit" : "hair");
        snprintf(path, sizeof(path), "assets/avatar-components/v1/%s/%c%u.gavc",
                 folder, prefix, preset);
        scheduled |= requestComponentLocked(kind, preset, path);
    }
    return scheduled;
}

void scheduleAllComponentsLocked()
{
    if (preloadScheduled) return;
    char path[96];
    for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
        const char prefix = kind == 1 ? 'f' : (kind == 2 ? 'o' : 'h');
        const char *folder = kind == 1 ? "face" : (kind == 2 ? "outfit" : "hair");
        for (uint8_t preset = 1; preset <= kComponentPresetsPerKind; ++preset) {
            snprintf(path, sizeof(path), "assets/avatar-components/v1/%s/%c%u.gavc",
                     folder, prefix, preset);
            requestComponentLocked(kind, preset, path);
        }
    }
    preloadScheduled = true;
}

bool recoverPreviewBufferLocked()
{
    if (ensurePreviewBufferLocked()) return true;
    if (previewRecoveryAttempted || !preloadComplete) return false;

    // Thirty differently sized GAVC allocations can leave enough total PSRAM
    // but no contiguous 132 KiB block for the composed preview. Keep the three
    // visible layers, release background presets, reserve the preview, then
    // refill the library behind the editor. The sticky preloadComplete flag
    // prevents this allocator recovery from sending the UI back to 3/30.
    previewRecoveryAttempted = true;
    for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
        const uint8_t desired = desiredPresetForKind(kind);
        for (uint8_t preset = 1; preset <= kComponentPresetsPerKind; ++preset) {
            if (preset == desired) continue;
            ComponentSlot &slot = components[componentIndex(kind, preset)];
            if (slot.state == SlotState::Ready) freeComponentLocked(slot);
        }
    }

    const bool allocated = ensurePreviewBufferLocked();
    preloadScheduled = false;
    scheduleAllComponentsLocked();
    return allocated;
}

bool claimRequestLocked(ClaimedRequest &request)
{
    const uint32_t nowMs = millis();
    if (previewModeLocked()) {
        // Always let the three visible layers jump ahead of background warmup.
        for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
            const uint8_t index = componentIndex(kind, desiredPresetForKind(kind));
            ComponentSlot &slot = components[index];
            if (slot.state != SlotState::Requested ||
                !deadlineReached(nowMs, slot.retryAfterMs)) continue;
            slot.state = SlotState::Loading;
            request.kind = RequestKind::Component;
            request.index = index;
            request.serial = slot.serial;
            std::strncpy(request.path, slot.path, sizeof(request.path) - 1);
            return true;
        }
        for (uint8_t index = 0; index < kComponentSlotCount; ++index) {
            ComponentSlot &slot = components[index];
            if (slot.state != SlotState::Requested ||
                !deadlineReached(nowMs, slot.retryAfterMs)) continue;
            slot.state = SlotState::Loading;
            request.kind = RequestKind::Component;
            request.index = index;
            request.serial = slot.serial;
            std::strncpy(request.path, slot.path, sizeof(request.path) - 1);
            return true;
        }
    }
    if (finalModeLocked()) {
        for (uint8_t index = 0; index < kFinalSlotCount; ++index) {
            FinalSlot &slot = finals[index];
            if (slot.state != SlotState::Requested ||
                !deadlineReached(nowMs, slot.retryAfterMs)) continue;
            slot.state = SlotState::Loading;
            request.kind = RequestKind::Final;
            request.index = index;
            request.serial = slot.serial;
            std::strncpy(request.path, slot.path, sizeof(request.path) - 1);
            return true;
        }
    }
    return false;
}

bool downloadAsset(WiFiClient &client, const ClaimedRequest &request,
                   uint8_t *&bytes, size_t &length)
{
    bytes = nullptr;
    length = 0;
    // Arduino's station facade can transiently report a zero localIP from this
    // worker even while the authenticated UDP session is healthy. Let the TCP
    // connect establish network truth; an actual outage follows the normal
    // bounded timeout/retry path instead of becoming an endless local retry.
    client.setTimeout(kHttpTimeoutMs);
    client.setNoDelay(true);
    HTTPClient http;
    http.setConnectTimeout(1800);
    http.setTimeout(kHttpTimeoutMs);
    http.useHTTP10(false);
    http.setReuse(true);
    const String url = String(kServerBaseUrl) + request.path;
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    const int contentLength = http.getSize();
    const size_t maxBytes = request.kind == RequestKind::Component
        ? kMaxComponentFileBytes : kRemoteAvatarFinalBytes;
    bool success = status == HTTP_CODE_OK && contentLength > 0 &&
                   static_cast<size_t>(contentLength) <= maxBytes &&
                   (request.kind != RequestKind::Final ||
                    static_cast<size_t>(contentLength) == kRemoteAvatarFinalBytes);
    if (success) {
        length = static_cast<size_t>(contentLength);
        bytes = static_cast<uint8_t *>(heap_caps_malloc(
            length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        success = bytes != nullptr && http.getStream().readBytes(bytes, length) == length;
    }
    http.end();
    if (!success) client.stop();
    if (!success && bytes != nullptr) {
        heap_caps_free(bytes);
        bytes = nullptr;
        length = 0;
    }
    return success;
}

void completeRequest(const ClaimedRequest &request, uint8_t *bytes,
                     size_t length, bool success)
{
    ComponentHeader parsedHeader{};
    bool parsedComponent = false;
    if (request.kind == RequestKind::Component && request.index < kComponentSlotCount) {
        const uint8_t expectedKind = static_cast<uint8_t>(
            request.index / kComponentPresetsPerKind + 1u);
        const uint8_t expectedPreset = static_cast<uint8_t>(
            request.index % kComponentPresetsPerKind + 1u);
        // CRC validation walks the entire decoded layer. Keep that work outside
        // the cache mutex so background warmup cannot stall LVGL rendering.
        parsedComponent = success && parseComponent(
            bytes, length, expectedKind, expectedPreset, parsedHeader
        );
    }
    bool accepted = false;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    if (request.kind == RequestKind::Component && request.index < kComponentSlotCount) {
        ComponentSlot &slot = components[request.index];
        const bool affectsDesired = slot.expectedKind >= 1 &&
            slot.expectedKind <= kComponentKindCount &&
            slot.preset == desiredPresetForKind(slot.expectedKind);
        const bool valid = parsedComponent && slot.serial == request.serial &&
            slot.state == SlotState::Loading &&
            setupCachedBytes + length +
                    (previewPixels == nullptr ? kRemoteAvatarPreviewBytes : 0) <=
                kRemoteAvatarSetupCacheBudgetBytes;
        if (valid) {
            slot.file = bytes;
            slot.fileBytes = length;
            slot.header = parsedHeader;
            slot.state = SlotState::Ready;
            setupCachedBytes += length;
            if (affectsDesired && allComponentsReadyLocked() &&
                (!previewReady || !sameRecipe(composedRecipe, desiredRecipe))) {
                composeRequested = true;
            }
            accepted = true;
        } else if (slot.serial == request.serial && slot.state == SlotState::Loading) {
            slot.state = SlotState::Requested;
            slot.retryAfterMs = millis() + kRetryDelayMs;
        }
    } else if (request.kind == RequestKind::Final && request.index < kFinalSlotCount) {
        FinalSlot &slot = finals[request.index];
        const bool valid = success && slot.serial == request.serial &&
            slot.state == SlotState::Loading && length == kRemoteAvatarFinalBytes &&
            finalCachedBytes + length <= kRemoteAvatarCacheBudgetBytes;
        if (valid) {
            slot.pixels = bytes;
            slot.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
            slot.descriptor.header.always_zero = 0;
            slot.descriptor.header.reserved = 0;
            slot.descriptor.header.w = 128;
            slot.descriptor.header.h = 128;
            slot.descriptor.data_size = length;
            slot.descriptor.data = bytes;
            slot.state = SlotState::Ready;
            finalCachedBytes += length;
            ++publishedGeneration;
            accepted = true;
        } else if (slot.serial == request.serial && slot.state == SlotState::Loading) {
            slot.state = SlotState::Requested;
            slot.retryAfterMs = millis() + kRetryDelayMs;
        }
    }
    xSemaphoreGive(cacheMutex);
    if (!accepted && bytes != nullptr) heap_caps_free(bytes);
}

bool composePreviewLocked()
{
    if (!allComponentsReadyLocked()) return false;
    if (!ensurePreviewBufferLocked()) return false;

    std::array<RleCursor, kComponentKindCount> decoders{};
    for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
        const ComponentSlot &slot = desiredComponent(kind);
        decoders[kind - 1] = RleCursor{slot.file + kGavcHeaderBytes,
                                      slot.file + slot.fileBytes, 0,
                                      slot.header.decodedBytes / 4u};
    }
    const AvatarComponentRgb hairColor = kHairPalette[desiredRecipe.hairColorId - 1];
    const AvatarComponentRgb skinColor = kSkinPalette[desiredRecipe.skinToneId - 1];
    const uint8_t background[3] = {
        static_cast<uint8_t>((kPreviewEdgeBackground >> 16) & 0xFF),
        static_cast<uint8_t>((kPreviewEdgeBackground >> 8) & 0xFF),
        static_cast<uint8_t>(kPreviewEdgeBackground & 0xFF),
    };

    for (uint16_t y = 0; y < kPreviewHeight; ++y) {
        for (uint16_t x = 0; x < kPreviewWidth; ++x) {
            uint8_t destination[4]{};
            for (uint8_t kind = 1; kind <= kComponentKindCount; ++kind) {
                const uint8_t index = kind - 1;
                const ComponentSlot &slot = desiredComponent(kind);
                if (x < slot.header.x || y < slot.header.y ||
                    x >= slot.header.x + slot.header.width ||
                    y >= slot.header.y + slot.header.height) continue;
                uint8_t source[4];
                if (!decoders[index].next(source)) return false;
                if (slot.expectedKind == 1) avatarTintSkinPixel(source, skinColor);
                if (slot.expectedKind == 3) avatarTintHairPixel(source, hairColor);
                avatarSourceOver(destination, source);
            }
            uint16_t rgb565 = kPreviewChromaKey;
            if (destination[3] != 0) {
                uint8_t rgb[3];
                for (uint8_t channel = 0; channel < 3; ++channel) {
                    rgb[channel] = avatarRoundHalfUp(
                        static_cast<uint64_t>(destination[channel]) * destination[3] +
                        static_cast<uint64_t>(background[channel]) *
                            (255u - destination[3]),
                        255u);
                }
                rgb565 = static_cast<uint16_t>(
                    ((rgb[0] & 0xF8u) << 8) | ((rgb[1] & 0xFCu) << 3) |
                    (rgb[2] >> 3));
            }
            const size_t offset = (static_cast<size_t>(y) * kPreviewWidth + x) * 2u;
            previewPixels[offset] = static_cast<uint8_t>(rgb565 & 0xFFu);
            previewPixels[offset + 1] = static_cast<uint8_t>(rgb565 >> 8);
        }
    }
    for (const RleCursor &decoder : decoders) {
        if (!decoder.complete()) return false;
    }

    previewDescriptor.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
    previewDescriptor.header.always_zero = 0;
    previewDescriptor.header.reserved = 0;
    previewDescriptor.header.w = kPreviewWidth;
    previewDescriptor.header.h = kPreviewHeight;
    previewDescriptor.data_size = kRemoteAvatarPreviewBytes;
    previewDescriptor.data = previewPixels;
    composedRecipe = desiredRecipe;
    previewReady = true;
    composeRequested = false;
    ++publishedGeneration;
    return true;
}

void loaderTask(void *parameter)
{
    WorkerContext &worker = *static_cast<WorkerContext *>(parameter);
    while (true) {
        ClaimedRequest request{};
        bool shouldCompose = false;
        xSemaphoreTake(cacheMutex, portMAX_DELAY);
        shouldCompose = previewModeLocked() && composeRequested &&
                        allComponentsReadyLocked();
        if (shouldCompose) {
            if (composePreviewLocked()) {
                scheduleAllComponentsLocked();
            }
            xSemaphoreGive(cacheMutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const bool claimed = claimRequestLocked(request);
        xSemaphoreGive(cacheMutex);
        if (!claimed) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWorkerPollMs));
            continue;
        }
        uint8_t *bytes = nullptr;
        size_t length = 0;
        const bool success = downloadAsset(worker.httpClient, request, bytes, length);
        completeRequest(request, bytes, length, success);
        Serial.printf("GRIDOPOLY_AVATAR_COMPONENT worker=%u %s path=%s bytes=%u\n",
                      worker.index, success ? "ready" : "retry", request.path,
                      static_cast<unsigned>(length));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

} // namespace

void remoteAvatarCacheBegin()
{
    if (cacheStarted) return;
    if (cacheMutex == nullptr) cacheMutex = xSemaphoreCreateMutex();
    if (cacheMutex == nullptr) {
        Serial.println("GRIDOPOLY_AVATAR mutex_failed");
        return;
    }
    uint8_t createdWorkers = 0;
    for (uint8_t index = 0; index < kRemoteAvatarDownloadWorkerCount; ++index) {
        workerTaskHandles[index] = nullptr;
        workerContexts[index].index = index;
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "grid-avatar-%u", index);
        const BaseType_t taskCreateResult = xTaskCreatePinnedToCore(
            loaderTask, taskName, kWorkerStackBytes, &workerContexts[index], 1,
            &workerTaskHandles[index], 0
        );
        if (taskCreateResult == pdPASS) {
            ++createdWorkers;
        } else {
            workerTaskHandles[index] = nullptr;
        }
    }
    cacheStarted = createdWorkers > 0;
    if (createdWorkers != kRemoteAvatarDownloadWorkerCount) {
        Serial.printf("GRIDOPOLY_AVATAR workers=%u/%u\n", createdWorkers,
                      kRemoteAvatarDownloadWorkerCount);
    }
}

void wakeWorkers()
{
    for (TaskHandle_t handle : workerTaskHandles) {
        if (handle != nullptr) xTaskNotifyGive(handle);
    }
}

void remoteAvatarCachePreload(const TransportAvatarRecipe &recipe)
{
    if (!cacheStarted) remoteAvatarCacheBegin();
    if (!cacheStarted || cacheMutex == nullptr) return;
    const TransportAvatarRecipe normalized = normalizedTransportAvatarRecipe(recipe);
    bool wakeWorker = false;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    enterModeLocked(CacheMode::Preview);
    // Reserve the only large contiguous allocation before four workers split
    // PSRAM into thirty component blocks. Without this reservation all files
    // can reach Ready while the final 220x300 preview allocation still fails.
    bool previewBufferReady = ensurePreviewBufferLocked();
    if (!sameRecipe(desiredRecipe, normalized)) {
        desiredRecipe = normalized;
        previewReady = false;
        ++recipeGeneration;
    }
    wakeWorker |= scheduleDesiredComponentsLocked();
    scheduleAllComponentsLocked();
    wakeWorker = true;
    if (allLibraryComponentsReadyLocked()) preloadComplete = true;
    if (!previewBufferReady) {
        previewBufferReady = recoverPreviewBufferLocked();
    }
    if (previewBufferReady && allComponentsReadyLocked() &&
        (!previewReady || !sameRecipe(composedRecipe, normalized))) {
        composeRequested = true;
        wakeWorker = true;
    }
    xSemaphoreGive(cacheMutex);
    if (wakeWorker) wakeWorkers();
}

RemoteAvatarPreloadProgress remoteAvatarCachePreloadProgress()
{
    RemoteAvatarPreloadProgress progress{};
    if (!cacheStarted || cacheMutex == nullptr) return progress;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    for (const ComponentSlot &slot : components) {
        if (slot.state == SlotState::Ready) ++progress.readyCount;
    }
    if (allLibraryComponentsReadyLocked()) preloadComplete = true;
    if (preloadComplete) progress.readyCount = progress.totalCount;
    progress.previewReady = previewReady && sameRecipe(composedRecipe, desiredRecipe);
    progress.complete = preloadComplete;
    xSemaphoreGive(cacheMutex);
    return progress;
}

void remoteAvatarCacheReleaseSetup()
{
    if (!cacheStarted || cacheMutex == nullptr) return;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    if (previewModeLocked()) {
        clearPreviewLocked();
        cacheMode = cacheMode == CacheMode::PreviewAndFinals
            ? CacheMode::Finals : CacheMode::None;
    }
    xSemaphoreGive(cacheMutex);
}

const lv_img_dsc_t *remoteAvatarPreview(const TransportAvatarRecipe &recipe)
{
    // A temporary internal-heap shortage during boot must not make Avatar
    // Setup permanently blank. Retry once the page is actually visible.
    if (!cacheStarted) remoteAvatarCacheBegin();
    if (!cacheStarted || cacheMutex == nullptr) return nullptr;
    const TransportAvatarRecipe normalized = normalizedTransportAvatarRecipe(recipe);
    bool wakeWorker = false;
    const lv_img_dsc_t *result = nullptr;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    enterModeLocked(CacheMode::Preview);
    if (!sameRecipe(desiredRecipe, normalized)) {
        desiredRecipe = normalized;
        previewReady = false;
        ++recipeGeneration;
    }
    wakeWorker |= scheduleDesiredComponentsLocked();
    if (!ensurePreviewBufferLocked()) {
        (void)recoverPreviewBufferLocked();
    }
    if (allComponentsReadyLocked() &&
        (!previewReady || !sameRecipe(composedRecipe, normalized))) {
        composeRequested = true;
        wakeWorker = true;
    }
    if (previewReady && sameRecipe(composedRecipe, normalized)) result = &previewDescriptor;
    xSemaphoreGive(cacheMutex);
    if (wakeWorker) wakeWorkers();
    return result;
}

const lv_img_dsc_t *remoteAvatarFinal(uint32_t roomId, uint8_t playerId,
                                     uint16_t avatarRevision,
                                     uint64_t avatarContentHash64)
{
    if (!cacheStarted || cacheMutex == nullptr || playerId == 0 || playerId > 6 ||
        avatarRevision == 0 || avatarContentHash64 == 0) return nullptr;
    char path[176];
    snprintf(path, sizeof(path), "assets/avatars/%lu/p%u-a%u-%016llx.rgb565",
             static_cast<unsigned long>(roomId), playerId, avatarRevision,
             static_cast<unsigned long long>(avatarContentHash64));
    const uint8_t index = static_cast<uint8_t>(playerId - 1);
    bool wakeWorker = false;
    const lv_img_dsc_t *result = nullptr;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    enterModeLocked(CacheMode::Finals);
    FinalSlot &slot = finals[index];
    if (std::strcmp(slot.path, path) == 0) {
        if (slot.state == SlotState::Ready) result = &slot.descriptor;
    } else {
        freeFinalLocked(slot);
        std::strncpy(slot.path, path, sizeof(slot.path) - 1);
        slot.serial = ++recipeGeneration;
        if (slot.serial == 0) slot.serial = ++recipeGeneration;
        slot.state = SlotState::Requested;
        wakeWorker = true;
    }
    xSemaphoreGive(cacheMutex);
    if (wakeWorker) wakeWorkers();
    return result;
}

bool remoteAvatarCacheConsumeUpdate()
{
    if (!cacheStarted || cacheMutex == nullptr) return false;
    bool changed = false;
    xSemaphoreTake(cacheMutex, portMAX_DELAY);
    if (consumedGeneration != publishedGeneration) {
        consumedGeneration = publishedGeneration;
        changed = true;
    }
    xSemaphoreGive(cacheMutex);
    return changed;
}
