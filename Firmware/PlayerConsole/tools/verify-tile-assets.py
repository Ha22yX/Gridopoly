#!/usr/bin/env python3
"""Verify the Grid City artwork catalog and its player-console integration."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[3]
CONSOLE = PROJECT / "Firmware" / "PlayerConsole"
ASSET_ROOT = PROJECT / "Assets" / "GridCity" / "StreetV3"
ASSET_DIR = ASSET_ROOT / "device"
REMOTE_DIR = ASSET_ROOT / "device-rgb565"
MANIFEST = ASSET_ROOT / "manifests" / "grid-city-street-assets-v3.json"
EMBEDDED_MANIFEST = CONSOLE / "src" / "assets" / "grid_city_tile_assets.manifest.json"
HEADER = CONSOLE / "src" / "assets" / "grid_city_tile_assets.h"
SOURCE = CONSOLE / "src" / "assets" / "grid_city_tile_assets.c"
IMAGE_MAP = CONSOLE / "src" / "assets" / "grid_city_tile_images.cpp"
REMOTE_CACHE = CONSOLE / "remote_tile_cache.cpp"
REMOTE_AVATAR_CACHE = CONSOLE / "remote_avatar_cache.cpp"
AVATAR_COMPONENT_MATH = CONSOLE / "avatar_component_math.h"
REMOTE_POLICY = CONSOLE / "remote_tile_cache_policy.h"
VISUAL_CATALOG = CONSOLE / "grid_city_visual_catalog.cpp"
RENDERER = CONSOLE / "ui_renderer.cpp"
EXPECTED_COUNT = 36
EXPECTED_SIZE = 128
CORNER_IDS = {"CORNER-START", "CORNER-HOLD", "CORNER-REST", "CORNER-GOTO"}


def fail(message: str) -> None:
    print(f"TILE ASSET CHECK FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\bvoid\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    if not match:
        fail(f"{name}() was not found")
    start = source.find("{", match.start())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : index]
    fail(f"{name}() has no closing brace")


def main() -> None:
    catalog = json.loads(MANIFEST.read_text(encoding="utf-8"))
    embedded = json.loads(EMBEDDED_MANIFEST.read_text(encoding="utf-8"))
    assets = catalog.get("assets", [])
    if len(assets) != EXPECTED_COUNT:
        fail(f"expected {EXPECTED_COUNT} catalog assets, found {len(assets)}")
    if embedded.get("asset_count") != EXPECTED_COUNT:
        fail("remote manifest count does not match the V3 catalog")
    if embedded.get("delivery") != "http-rgb565-psram-cache":
        fail("tile delivery must use HTTP RGB565 with a PSRAM cache")
    if embedded.get("embedded_size") != EXPECTED_SIZE:
        fail(f"embedded art must remain {EXPECTED_SIZE}x{EXPECTED_SIZE}")
    expected_bytes = EXPECTED_COUNT * EXPECTED_SIZE * EXPECTED_SIZE * 2
    if embedded.get("embedded_bytes") != expected_bytes:
        fail(f"embedded RGB565 payload is not {expected_bytes} bytes")

    if HEADER.exists() or SOURCE.exists():
        fail("generated tile pixels must not be compiled into PlayerConsole firmware")
    image_map = IMAGE_MAP.read_text(encoding="utf-8")
    visual_catalog = VISUAL_CATALOG.read_text(encoding="utf-8")
    remote_cache = REMOTE_CACHE.read_text(encoding="utf-8")
    remote_avatar_cache = REMOTE_AVATAR_CACHE.read_text(encoding="utf-8")
    avatar_component_math = AVATAR_COMPONENT_MATH.read_text(encoding="utf-8")
    remote_policy = REMOTE_POLICY.read_text(encoding="utf-8")
    for asset in assets:
        key = asset["key"]
        image_path = (
            PROJECT / asset["source"]
            if asset.get("source")
            else ASSET_DIR / f"{key}.png"
        )
        if not image_path.exists():
            fail(f"missing source PNG: {image_path.name}")
        remote_path = REMOTE_DIR / f"{key}.rgb565"
        if not remote_path.exists() or remote_path.stat().st_size != EXPECTED_SIZE * EXPECTED_SIZE * 2:
            fail(f"remote RGB565 asset is missing or malformed: {remote_path.name}")
        if f'"{key}"' not in image_map:
            fail(f"remote asset key is not reachable from the artwork map: {key}")
        if f'"{asset["name"]}"' not in visual_catalog:
            fail(f"visual catalog does not expose name: {asset['name']}")

    corner_assets = {asset["tile_id"] for asset in assets if asset["kind"] == "corner"}
    if corner_assets != CORNER_IDS:
        fail(f"corner artwork coverage mismatch: {sorted(corner_assets)}")
    if "HTTPClient" not in remote_cache or "MALLOC_CAP_SPIRAM" not in remote_cache:
        fail("remote cache must fetch over HTTP and retain pixels in PSRAM")
    if ("kRemoteImageCacheBudgetBytes == 1024u * 1024u" not in remote_policy or
            "kRemoteTileCacheCapacity == 20" not in remote_policy or
            "6u * kRemoteAvatarFinalBytes" not in remote_policy):
        fail("normal gameplay tile and final-avatar caches must share one strict 1 MiB budget")
    if "remoteTileCacheSelectLru" not in remote_cache or "pinnedIndex" not in remote_cache:
        fail("remote cache must evict by LRU without releasing the displayed image")
    avatar_contract = (
        '"assets/avatar-components/v1/%s/%c%u.gavc"',
        '"assets/avatars/%lu/p%u-a%u-%016llx.rgb565"',
        'std::memcmp(file, "GAVC", 4)',
        "kPreviewWidth = 220",
        "kPreviewHeight = 300",
        "kPreviewBackground = 0x061017",
        "avatarTintHairPixel(source, hairColor)",
        "avatarTintSkinPixel(source, skinColor)",
        "avatarSourceOver(destination, source)",
        "slot.descriptor.header.w = 128",
        "slot.descriptor.header.h = 128",
    )
    if any(token not in remote_avatar_cache for token in avatar_contract):
        fail("avatar cache routes or fixed RGB565 dimensions diverge from the UDP contract")
    math_contract = (
        "avatarRoundHalfUp",
        "avatarTintHairPixel",
        "avatarTintSkinPixel",
        "avatarSourceOver",
    )
    if any(token not in avatar_component_math for token in math_contract):
        fail("avatar component tint or source-over math is missing")
    if ("kRemoteAvatarSetupCacheBudgetBytes = 2u * 1024u * 1024u" not in remote_policy or
            "kRemoteAvatarComponentBudgetBytes" not in remote_policy or
            "kRemoteAvatarPreviewBytes" not in remote_policy or
            "kRemoteAvatarPreviewBytes + kRemoteAvatarComponentBudgetBytes" not in remote_policy or
             "kRemoteAvatarSetupCacheBudgetBytes" not in remote_policy or
             "kComponentSlotCount" not in remote_avatar_cache or
             "kWorkerStackBytes = 8192" not in remote_avatar_cache or
             "scheduleDesiredComponentsLocked" not in remote_avatar_cache or
             "scheduleAllComponentsLocked" not in remote_avatar_cache or
             "http.setReuse(true)" not in remote_avatar_cache):
        fail("Avatar Setup must preload all 30 components inside its transient 2 MiB pool")
    if "if (!cacheStarted) remoteAvatarCacheBegin();" not in remote_avatar_cache:
        fail("Avatar Setup must retry a worker that could not start during boot")
    if "WiFi.localIP() == IPAddress(0, 0, 0, 0)" in remote_avatar_cache:
        fail("avatar HTTP worker must not trust Arduino localIP false-negative state")
    if "HTTPClient" not in remote_avatar_cache or "MALLOC_CAP_SPIRAM" not in remote_avatar_cache:
        fail("avatar previews and final portraits must be fetched into PSRAM over HTTP")

    renderer = RENDERER.read_text(encoding="utf-8")
    for function in ("drawHome", "drawMoveGuide", "drawPurchase", "drawAssetDetail", "drawAuction"):
        if "drawArtwork(" not in function_body(renderer, function):
            fail(f"{function}() does not render the mapped tile artwork")

    print(
        f"TILE ASSET CHECK PASS: {EXPECTED_COUNT} remote images including four corners, "
        f"{expected_bytes} server bytes, component avatar HTTP contract fixed, "
        "zero generated firmware pixel arrays"
    )


if __name__ == "__main__":
    main()
