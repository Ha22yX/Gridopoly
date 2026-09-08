#pragma once

#include <lvgl.h>

#include "transport_types.h"

// Starts four low-priority avatar HTTP loaders. Gameplay artwork keeps the shared
// 1 MiB cache policy; Avatar Setup temporarily owns a separate 2.25 MiB
// component and double-preview pool that is released when identity setup ends.
void remoteAvatarCacheBegin();

struct RemoteAvatarPreloadProgress {
    uint8_t readyCount = 0;
    uint8_t totalCount = 30;
    bool previewReady = false;
    bool complete = false;
};

// Starts one setup-scoped warmup. Four persistent HTTP workers fetch the
// current recipe first, then retain all 30 neutral components through name/ready.
// complete becomes sticky when all 30 component files have been validated; the
// locally composed preview may finish just after the editor opens.
void remoteAvatarCachePreload(const TransportAvatarRecipe &recipe);
RemoteAvatarPreloadProgress remoteAvatarCachePreloadProgress();

// Releases only setup components and the 220x300 preview. Public 128x128
// avatars remain available for gameplay.
void remoteAvatarCacheReleaseSetup();

struct RemoteAvatarPreviewFrame {
    const lv_img_dsc_t *image = nullptr;
    bool exact = false;
};

// Returns a locally composed 220x300 preview. The active three neutral GAVC
// components are fetched first, then all remaining presets are warmed into a
// transient Avatar Setup pool. Hair and skin colors are applied locally. The
// component pool is discarded when the identity flow enters gameplay.
// While a new recipe is composed, image retains the previous complete frame
// and exact is false. This prevents blank or torn frames during fast rotation.
RemoteAvatarPreviewFrame remoteAvatarPreviewFrame(
    const TransportAvatarRecipe &recipe
);

// Compatibility wrapper that only returns an exact frame.
const lv_img_dsc_t *remoteAvatarPreview(const TransportAvatarRecipe &recipe);

// Returns a final 128x128 public avatar, or nullptr while it is downloading.
// Final avatars are only requested after the authority marks the seat final.
const lv_img_dsc_t *remoteAvatarFinal(uint32_t roomId, uint8_t playerId,
                                     uint16_t avatarRevision,
                                     uint64_t avatarContentHash64);

// Consumes a publication edge so LVGL can rebuild spinner/image content.
bool remoteAvatarCacheConsumeUpdate();
