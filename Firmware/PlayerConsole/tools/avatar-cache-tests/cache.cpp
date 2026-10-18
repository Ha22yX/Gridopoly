// Include the actual cache/decoder/compositor so tests exercise its lifecycle,
// allocations and publication rules, not a second implementation of them.
#include "remote_avatar_cache.cpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
unsigned checks = 0;
void expect(bool condition, const char *name)
{
    ++checks;
    if (!condition) { std::printf("FAIL %s\n", name); std::exit(1); }
}
using Bytes = std::vector<uint8_t>;
using Pixel = std::array<uint8_t, 5>;
void put16(Bytes &bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}
void put32(Bytes &bytes, size_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
// Independent bit-by-bit fixture CRC (including mask), plus standard check vector.
uint32_t fixtureCrc(const Bytes &bytes)
{
    uint32_t crc = UINT32_MAX;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ UINT32_MAX;
}
Bytes component(uint16_t version, uint8_t kind, uint8_t preset,
                const std::vector<Pixel> &pixels, bool repeat = false)
{
    const size_t sample = version == 2 ? 5 : 4;
    Bytes raw;
    for (const auto &pixel : pixels) raw.insert(raw.end(), pixel.begin(), pixel.begin() + sample);
    Bytes bytes(34, 0);
    std::memcpy(bytes.data(), "GAVC", 4);
    bytes[4] = bytes[7] = static_cast<uint8_t>(version);
    bytes[5] = kind; bytes[6] = preset;
    put16(bytes, 8, 220); put16(bytes, 10, 300);
    put16(bytes, 16, static_cast<uint16_t>(pixels.size())); put16(bytes, 18, 1);
    put32(bytes, 20, static_cast<uint32_t>(raw.size()));
    put32(bytes, 28, fixtureCrc(raw));
    put16(bytes, 32, static_cast<uint16_t>(pixels.size() | (repeat ? 0x8000u : 0u)));
    if (repeat) bytes.insert(bytes.end(), raw.begin(), raw.begin() + sample);
    else bytes.insert(bytes.end(), raw.begin(), raw.end());
    put32(bytes, 24, static_cast<uint32_t>(bytes.size() - 32));
    return bytes;
}
Bytes solid(uint16_t version, uint8_t kind, uint8_t preset = 1)
{
    return component(version, kind, preset, {{178, 178, 178, 255, 255}});
}
bool parses(const Bytes &bytes, uint16_t version, uint8_t kind = 1, uint8_t preset = 1)
{
    ComponentHeader header;
    return parseComponent(bytes.data(), bytes.size(), version, kind, preset, header);
}
uint8_t *allocatedCopy(const Bytes &bytes)
{
    auto *result = static_cast<uint8_t *>(heap_caps_malloc(bytes.size(), 0));
    expect(result != nullptr, "test response allocation");
    std::memcpy(result, bytes.data(), bytes.size());
    return result;
}
TransportAvatarRecipe recipe(uint16_t version)
{
    TransportAvatarRecipe value{}; value.catalogVersion = version; return value;
}
void resetCache()
{
    expect(!previewBuffers.composing(), "test cleanup never interrupts compositor");
    remoteAvatarCacheReleaseSetup(); remoteAvatarCacheReleaseFinals();
    expect(avatarLiveAllocations.empty() && setupCachedBytes == 0 && finalCachedBytes == 0,
           "release returns all accepted/download allocations");
    avatarAllocationLimit = static_cast<size_t>(-1);
}
void deliver(const ClaimedRequest &request, const Bytes &bytes)
{
    completeRequest(request, allocatedCopy(bytes), bytes.size(), true);
}
void fillLibrary(uint16_t version)
{
    ClaimedRequest request;
    unsigned count = 0;
    while (claimRequestLocked(request)) {
        expect(request.kind == RequestKind::Component && request.catalogVersion == version,
               "only requested catalog is downloaded");
        deliver(request, solid(version, static_cast<uint8_t>(request.index / 10 + 1),
                               static_cast<uint8_t>(request.index % 10 + 1)));
        expect(++count <= avatarCatalogComponentCount(version), "bounded library warmup");
    }
    expect(allLibraryComponentsReadyLocked(), "full catalog ready");
}
void publish(const TransportAvatarRecipe &value)
{
    ComposeSnapshot snapshot;
    expect(beginComposeLocked(snapshot), "compose begins with validated files");
    expect(composePreview(snapshot), "production compositor succeeds");
    expect(finishComposeLocked(snapshot, true), "worker publishes current generation");
    expect(remoteAvatarPreviewFrame(value).exact, "UI acquires exact requested catalog");
}

void codecChecks()
{
    expect(fixtureCrc(Bytes{'1','2','3','4','5','6','7','8','9'}) == 0xCBF43926,
           "fixture CRC standard check vector");
    expect(TransportAvatarRecipe{}.catalogVersion == kAvatarDefaultCatalogVersion,
           "compile-time default used");
    expect(normalizedTransportAvatarRecipe(recipe(1)).catalogVersion == 1 &&
           normalizedTransportAvatarRecipe(recipe(2)).catalogVersion == 2,
           "explicit versions survive either default");
    auto invalid = recipe(99); invalid.facePresetId = 11; invalid.skinToneId = 9;
    const auto normalized = normalizedTransportAvatarRecipe(invalid);
    expect(normalized.catalogVersion == kAvatarDefaultCatalogVersion &&
           normalized.facePresetId == 1 && normalized.skinToneId == 1,
           "unsupported default/IDs normalize safely");
    for (uint16_t version : std::array<uint16_t, 2>{1, 2}) {
        const std::vector<Pixel> pixels(3, Pixel{178, 178, 178, 128, 213});
        for (bool repeat : {false, true}) {
            const auto valid = component(version, 1, 1, pixels, repeat);
            expect(parses(valid, version), "literal and repeat parse in both formats");
            expect(!parses(valid, version == 1 ? 2 : 1), "path version must match schema and encoding");
            auto bad = valid; bad.back() ^= 1;
            expect(!parses(bad, version), "CRC covers every sample byte including mask");
            for (size_t length = 0; length < valid.size(); ++length) {
                ComponentHeader header;
                expect(!parseComponent(valid.data(), length, version, 1, 1, header), "truncated file rejected");
            }
            bad = valid; bad.push_back(0); put32(bad, 24, static_cast<uint32_t>(bad.size() - 32));
            expect(!parses(bad, version), "trailing payload rejected");
            bad = valid; put16(bad, 32, 0);
            expect(!parses(bad, version), "zero run rejected");
            bad = valid; put16(bad, 32, 0xFFFF);
            expect(!parses(bad, version), "overlong run rejected before reading pixel");
            bad = valid; put16(bad, 12, 219);
            expect(!parses(bad, version), "rectangle cannot extend past canvas");
            bad = valid; put32(bad, 20, 0xFFFFFFFF);
            expect(!parses(bad, version), "decoded byte count checked");
            bad = valid; bad[7] = version == 1 ? 2 : 1;
            expect(!parses(bad, version), "mixed schema and encoding rejected");
        }
    }
    expect(parses(solid(2, 4), 2, 4), "V2 shared neck accepted");
    expect(!parses(solid(1, 4), 1, 4), "V1 cannot contain a neck");
    expect(!parses(solid(2, 4, 2), 2, 4, 2), "only one shared neck preset");
    auto oversized = solid(2, 1); oversized.resize(128 * 1024 + 1);
    expect(!parses(oversized, 2), "128 KiB per-file gate");
    // Mixed repeat + literal tokens with a nonzero mask in the final pixel.
    auto mixed = component(2, 1, 1, {{178,178,178,255,255},{178,178,178,255,255},{10,20,30,128,7}});
    Bytes encoded{2,128,178,178,178,255,255,1,0,10,20,30,128,7};
    mixed.resize(32); mixed.insert(mixed.end(), encoded.begin(), encoded.end());
    put32(mixed, 24, static_cast<uint32_t>(encoded.size()));
    expect(parses(mixed, 2), "mixed run types include five-byte samples");
}

void tintChecks()
{
    const AvatarComponentRgb ebony{91,53,42};
    Pixel pixel{178,178,178,97,255}; avatarTintSkinMaskedPixel(pixel.data(), ebony);
    expect(pixel == Pixel{91,53,42,97,255}, "neutral material maps to exact Ebony and preserves alpha");
    pixel = {178,178,178,97,128}; avatarTintSkinMaskedPixel(pixel.data(), ebony);
    expect(pixel == Pixel{134,115,110,97,128}, "partial mask blends with half-up rounding");
    pixel = {255,255,255,1,255}; avatarTintSkinMaskedPixel(pixel.data(), ebony);
    expect(pixel == Pixel{130,76,60,1,255}, "bright antialias pixel is skin even at alpha one");
    pixel = {250,240,230,255,0}; avatarTintSkinMaskedPixel(pixel.data(), ebony);
    expect(pixel == Pixel{250,240,230,255,0}, "unmasked eye white retains RGB");
    pixel = {255,255,255,255,255}; avatarTintSkinMaskedPixel(pixel.data(), {239,202,173});
    expect(pixel == Pixel{255,255,248,255,255}, "tinted highlight clamps before blending");
    pixel = {178,178,178,0,255}; avatarTintSkinMaskedPixel(pixel.data(), ebony);
    expect(pixel == Pixel{178,178,178,0,255}, "transparent pixels remain unchanged");
}

void compositionChecks()
{
    resetCache(); auto value = recipe(2); value.skinToneId = 8;
    remoteAvatarCacheRequestPreview(value);
    const Pixel clear{0,0,0,0,0}, skin{178,178,178,255,255};
    const std::vector<Pixel> neck(5, skin);
    const std::vector<Pixel> face{clear,skin,Pixel{250,250,250,255,0},skin,skin};
    const std::vector<Pixel> outfit{clear,clear,clear,Pixel{10,20,30,255,255},Pixel{10,20,30,255,255}};
    const std::vector<Pixel> hair{clear,clear,clear,clear,Pixel{0,0,0,255,255}};
    ClaimedRequest request;
    while (claimRequestLocked(request)) {
        const uint8_t kind = static_cast<uint8_t>(request.index / 10 + 1);
        const auto &pixels = kind == 1 ? face : kind == 2 ? outfit : kind == 3 ? hair : neck;
        deliver(request, component(2, kind, 1, pixels));
    }
    ComposeSnapshot snapshot;
    expect(beginComposeLocked(snapshot) && snapshot.layerCount == 4, "V2 has four layers");
    expect(snapshot.layers[0].expectedKind == 4 && snapshot.layers[1].expectedKind == 1 &&
           snapshot.layers[2].expectedKind == 2 && snapshot.layers[3].expectedKind == 3,
           "neck face outfit hair order");
    expect(composePreview(snapshot) && finishComposeLocked(snapshot, true), "V2 layered fixture composed");
    const auto frame = remoteAvatarPreviewFrame(value);
    expect(frame.exact && frame.image != nullptr, "V2 fixture published");
    const uint16_t expected[] = {0x59A5,0x59A5,0xFFDF,0x08A3,0x0000,0x07E0};
    for (unsigned i = 0; i < 6; ++i) expect(get16(frame.image->data + 2 * i) == expected[i],
        "RGB565 golden: neck/face skin, eye white, untinted outfit, hair and transparency");
    expect(get16(frame.image->data + kRemoteAvatarPreviewBytes - 2) == 0x07E0, "remaining canvas is transparent");
    resetCache();
}

void lifecycleChecks()
{
    resetCache(); const auto v1 = recipe(1), v2 = recipe(2);
    remoteAvatarCachePreload(v1); fillLibrary(1); publish(v1);
    auto progress = remoteAvatarCachePreloadProgress();
    expect(progress.readyCount == 30 && progress.totalCount == 30 && progress.complete,
           "V1 complete at 30 files without neck");
    const auto oldFrame = remoteAvatarPreviewFrame(v1);
    const auto *oldPixels = oldFrame.image->data;
    const Bytes front(oldPixels, oldPixels + kRemoteAvatarPreviewBytes);
    auto changed = v1; changed.skinToneId = 8;
    remoteAvatarCacheRequestPreview(changed);
    ComposeSnapshot oldCompose; expect(beginComposeLocked(oldCompose), "old catalog compose in flight");
    const auto *oldFile = oldCompose.layers[0].file;
    remoteAvatarCachePreload(v2);
    progress = remoteAvatarCachePreloadProgress();
    expect(progress.totalCount == 31 && progress.readyCount == 0 && !progress.complete,
           "switch resets progress even while old files are retained");
    expect(avatarLiveAllocations.count(const_cast<uint8_t *>(oldFile)) == 1,
           "active snapshot component remains allocated");
    ClaimedRequest request;
    expect(!claimRequestLocked(request), "old catalog downloads blocked while switch waits");
    auto interim = remoteAvatarPreviewFrame(v2);
    expect(interim.image && !interim.exact && interim.image->data == oldPixels,
           "old complete frame remains visible during switch");
    expect(composePreview(oldCompose), "old snapshot still decodes safely");
    expect(!finishComposeLocked(oldCompose, true), "late old composition cannot publish");
    expect(std::memcmp(oldPixels, front.data(), front.size()) == 0,
           "worker did not mutate LVGL front");
    // Same scheduling step used by the worker after the old compose finishes.
    scheduleDesiredComponentsLocked(); scheduleAllComponentsLocked();
    expect(componentCatalogVersion == 2 && setupCachedBytes == 2 * kRemoteAvatarPreviewBytes,
           "catalog replacement frees components but keeps both previews");
    fillLibrary(2); publish(v2);
    progress = remoteAvatarCachePreloadProgress();
    expect(progress.readyCount == 31 && progress.totalCount == 31 && progress.complete,
           "V2 complete requires all 31 files");
    remoteAvatarCachePreload(v1); fillLibrary(1); publish(v1);
    expect(components[30].state == SlotState::Empty, "switch back drops V2 neck");
    resetCache();

    remoteAvatarCachePreload(v1);
    expect(claimRequestLocked(request), "old HTTP request claimed");
    const auto late = request;
    remoteAvatarCachePreload(v2);
    expect(claimRequestLocked(request), "new HTTP request claimed");
    auto *lateBytes = allocatedCopy(solid(1, 1));
    completeRequest(late, lateBytes, solid(1, 1).size(), true);
    expect(avatarLiveAllocations.count(lateBytes) == 0 &&
           components[request.index].state == SlotState::Loading,
           "late V1 response freed without changing V2 request");
    // A V1 body at a V2 URL must not become a valid new cache entry.
    deliver(request, solid(1, 1));
    expect(components[request.index].state == SlotState::Requested, "wrong-schema body retries");
    avatarTestNow += kRetryDelayMs;
    fillLibrary(2); publish(v2);
    ComposeSnapshot releasing;
    expect(beginComposeLocked(releasing), "compose before setup release");
    remoteAvatarCacheReleaseSetup();
    expect(releaseRequested && !avatarLiveAllocations.empty(), "release defers until snapshot finishes");
    expect(composePreview(releasing) && !finishComposeLocked(releasing, true),
           "released compose never publishes");
    expect(avatarLiveAllocations.empty() && setupCachedBytes == 0, "deferred release frees all setup memory");

    remoteAvatarCachePreload(v1); fillLibrary(1);
    expect(beginComposeLocked(releasing), "compose before quick release/reentry");
    remoteAvatarCacheReleaseSetup(); remoteAvatarCachePreload(v2);
    expect(!releaseRequested && previewBuffers.composing(), "reentry retains active allocations");
    expect(composePreview(releasing) && !finishComposeLocked(releasing, true), "reentry rejects old generation");
    scheduleDesiredComponentsLocked(); scheduleAllComponentsLocked(); fillLibrary(2); publish(v2);
    resetCache();
}

void downloadAndBudgetChecks()
{
    resetCache(); remoteAvatarCachePreload(recipe(2));
    ClaimedRequest request; expect(claimRequestLocked(request), "download request claimed");
    WiFiClient client; uint8_t *bytes = nullptr; size_t length = 0;
    avatarHttpBody = solid(2, 1); avatarHttpLength = static_cast<int>(avatarHttpBody.size());
    expect(downloadAsset(client, request, bytes, length) &&
           avatarHttpUrl == "http://10.42.0.1/assets/avatar-components/v2/face/f1.gavc",
           "HTTP uses requested catalog URL");
    completeRequest(request, bytes, length, true);
    avatarHttpLength = 128 * 1024 + 1;
    bytes = nullptr; length = 0;
    expect(!downloadAsset(client, request, bytes, length) && bytes == nullptr, "oversized HTTP rejected before allocation");
    avatarHttpLength = 100; bytes = nullptr; length = 0;
    expect(!downloadAsset(client, request, bytes, length) && bytes == nullptr && length == 0,
           "short HTTP body releases temporary allocation");
    expect(claimRequestLocked(request), "budget request claimed");
    const auto body = solid(2, static_cast<uint8_t>(request.index / 10 + 1));
    const size_t saved = setupCachedBytes;
    setupCachedBytes = kRemoteAvatarSetupCacheBudgetBytes - body.size() + 1;
    deliver(request, body);
    expect(components[request.index].state == SlotState::Requested, "one byte over setup budget rejected");
    setupCachedBytes = saved;
    avatarTestNow += kRetryDelayMs;
    fillLibrary(2);
    size_t accounted = 2 * kRemoteAvatarPreviewBytes;
    for (const auto &slot : components) accounted += slot.fileBytes;
    expect(accounted == setupCachedBytes && setupCachedBytes == avatarLiveBytes,
           "compressed five-byte-format files accounted once");
    resetCache();
}

void finalPathChecks()
{
    remoteAvatarFinal(42, 1, 7, 0x123, 1);
    expect(std::strcmp(finals[0].path, "assets/avatars/42/p1-a7-0000000000000123.rgb565") == 0,
           "V1 final URL unchanged");
    ClaimedRequest oldRequest; expect(claimRequestLocked(oldRequest), "claim V1 final");
    remoteAvatarFinal(42, 1, 7, 0x123, 2);
    expect(std::strcmp(finals[0].path, "assets/avatars/v2/42/p1-a7-0000000000000123.rgb565") == 0,
           "same identifiers use distinct V2 final key");
    deliver(oldRequest, Bytes(kRemoteAvatarFinalBytes, 1));
    expect(finals[0].pixels == nullptr, "old final response cannot fill V2 slot");
    ClaimedRequest request; expect(claimRequestLocked(request), "claim V2 final");
    deliver(request, Bytes(kRemoteAvatarFinalBytes, 2));
    expect(remoteAvatarFinal(42, 1, 7, 0x123, 2) != nullptr, "correct V2 final available");
    expect(remoteAvatarFinal(42, 1, 7, 0x123, 3) == nullptr, "unsupported final version not silently downgraded");
    remoteAvatarCachePreload(recipe(1));
    expect(finals[0].pixels && finals[0].pixels[0] == 2, "preloading retains old page's final until UI releases it");
    remoteAvatarCacheReleaseFinals();
    expect(finalCachedBytes == 0 && previewModeLocked(), "final release preserves setup");
    resetCache();
}

void actualPackChecks(const std::filesystem::path &root, uint16_t version)
{
    resetCache();
    size_t total = 0;
    std::array<Bytes, 31> files;
    for (uint8_t kind = 1; kind <= (version == 2 ? 4 : 3); ++kind) {
        const char *folder = kind == 1 ? "face" : kind == 2 ? "outfit" : kind == 3 ? "hair" : "neck";
        for (uint8_t preset = 1; preset <= (kind == 4 ? 1 : 10); ++preset) {
            const std::string name = std::string(1, folder[0]) + std::to_string(preset) + ".gavc";
            std::ifstream file(root / folder / name, std::ios::binary);
            expect(file.good(), "actual component fixture exists");
            Bytes bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            expect(parses(bytes, version, kind, preset), "actual component passes format and CRC");
            total += bytes.size();
            files[componentIndex(kind, preset)] = bytes;
            if (version == 2) {
                bytes.back() ^= 1;
                expect(!parses(bytes, version, kind, preset), "actual pack CRC detects changed skin mask byte");
            }
        }
    }
    expect(total <= kRemoteAvatarComponentBudgetBytes, "actual compressed pack within setup budget");
    remoteAvatarCachePreload(recipe(version));
    ClaimedRequest request;
    while (claimRequestLocked(request)) {
        deliver(request, files[request.index]);
        expect(components[request.index].state == SlotState::Ready, "actual pack accepted by cache budget gate");
    }
    expect(allLibraryComponentsReadyLocked(), "all actual catalog files ready together");
    expect(setupCachedBytes == total + 2 * kRemoteAvatarPreviewBytes, "actual pack plus both previews accounted");
    const TransportAvatarRecipe recipes[] = {
        {version, 8, 1, 7, 8, 2}, {version, 1, 1, 10, 1, 7}
    };
    // Captured with the unmodified V1 production compositor at c09f0e25.
    const uint32_t legacyCrc[] = {0x97C5C388, 0x3DE5A2FF};
    unsigned recipeIndex = 0;
    for (const auto &value : recipes) {
        remoteAvatarCacheRequestPreview(value);
        publish(value);
        const auto frame = remoteAvatarPreviewFrame(value);
        char name[96];
        std::snprintf(name, sizeof(name), "v%u-h%u-c%u-f%u-s%u-o%u.rgb565", version,
                      value.hairPresetId, value.hairColorId, value.facePresetId,
                      value.skinToneId, value.outfitPresetId);
        std::ofstream output(name, std::ios::binary);
        output.write(reinterpret_cast<const char *>(frame.image->data), frame.image->data_size);
        output.close();
        expect(output.good(), "actual preview exported for independent server byte comparison");
        const uint32_t crc = crc32Update(UINT32_MAX, frame.image->data,
                                        frame.image->data_size) ^ UINT32_MAX;
        if (version == 1) expect(crc == legacyCrc[recipeIndex], "V1 preview matches pre-V2 compositor golden");
        ++recipeIndex;
        std::printf("PREVIEW %s crc32=%08x bytes=%u\n", name,
                    crc, frame.image->data_size);
    }
    std::printf("V%u actual pack: %u files, %zu bytes\n", version, avatarCatalogComponentCount(version), total);
    resetCache();
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    remoteAvatarCacheBegin();
    std::puts("RUN codec"); codecChecks();
    std::puts("RUN tint"); tintChecks();
    std::puts("RUN composition"); compositionChecks();
    std::puts("RUN lifecycle"); lifecycleChecks();
    std::puts("RUN download/budget"); downloadAndBudgetChecks();
    std::puts("RUN final paths"); finalPathChecks();
    expect(argc == 2 || argc == 3, "V1 path and optional V2 component path supplied");
    std::puts("RUN actual V1 pack"); actualPackChecks(std::filesystem::path(argv[1]), 1);
    if (argc == 3) {
        std::puts("RUN actual V2 pack"); actualPackChecks(std::filesystem::path(argv[2]), 2);
    } else {
        std::puts("V2 artwork pack not supplied: synthetic V2 codec/compositor checks only");
    }
    std::printf("PASS %u avatar codec/cache/composition checks (default catalog %u)\n",
                checks, kAvatarDefaultCatalogVersion);
    return 0;
}
