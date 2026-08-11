#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t kRemoteTileImageBytes = 128u * 128u * 2u;
constexpr size_t kRemoteTileCacheBudgetBytes = 640u * 1024u;
constexpr uint8_t kRemoteTileCacheCapacity = static_cast<uint8_t>(
    kRemoteTileCacheBudgetBytes / kRemoteTileImageBytes
);

constexpr size_t kRemoteAvatarPreviewBytes = 220u * 300u * 2u;
constexpr size_t kRemoteAvatarFinalBytes = 128u * 128u * 2u;
constexpr size_t kRemoteAvatarCacheBudgetBytes = 384u * 1024u;
// Avatar Setup is the only phase that may borrow a larger transient PSRAM
// pool. All 30 compressed GAVC layers total 1,921,970 bytes; together with
// the preview they fit below this cap. The pool is released when final
// avatars replace the editor preview.
constexpr size_t kRemoteAvatarSetupCacheBudgetBytes = 2u * 1024u * 1024u;
constexpr uint8_t kRemoteAvatarDownloadWorkerCount = 4;
constexpr size_t kRemoteAvatarComponentBudgetBytes =
    kRemoteAvatarSetupCacheBudgetBytes - kRemoteAvatarPreviewBytes;
constexpr size_t kRemoteImageCacheBudgetBytes =
    kRemoteTileCacheBudgetBytes + kRemoteAvatarCacheBudgetBytes;

static_assert(kRemoteTileCacheCapacity == 20,
              "the shared cache budget must retain twenty board tiles");
static_assert(kRemoteAvatarPreviewBytes + kRemoteAvatarComponentBudgetBytes <=
                  kRemoteAvatarSetupCacheBudgetBytes,
              "the transient avatar setup pool must hold all layers and one preview");
static_assert(kRemoteAvatarDownloadWorkerCount == 4,
              "the dedicated avatar preparation page uses four HTTP workers");
static_assert(6u * kRemoteAvatarFinalBytes <= kRemoteAvatarCacheBudgetBytes,
              "the post-setup cache must hold all six final avatars");
static_assert(kRemoteImageCacheBudgetBytes == 1024u * 1024u,
              "all remote RGB565 caches together must remain capped at 1 MiB");

inline int8_t remoteTileCacheSelectLru(const uint64_t *lastUsed,
                                       uint64_t readyMask,
                                       uint8_t count,
                                       int8_t pinnedIndex)
{
    int8_t candidate = -1;
    uint64_t oldest = UINT64_MAX;
    for (uint8_t index = 1; index < count && index < 64; ++index) {
        if ((readyMask & (uint64_t{1} << index)) == 0 ||
            index == static_cast<uint8_t>(pinnedIndex)) {
            continue;
        }
        if (candidate < 0 || lastUsed[index] < oldest) {
            candidate = static_cast<int8_t>(index);
            oldest = lastUsed[index];
        }
    }
    return candidate;
}
