// Production cache with deterministic HTTP, RAM and scheduler faults.
#include "../../remote_tile_cache.cpp"
#include <fstream>
#include <iterator>
int failed = 0, checked = 0;
void check(bool ok, const char *name) {
    ++checked; std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); if (!ok) ++failed;
}
void resetFixture() {
    for (const auto &p : allocations) std::free(p.first);
    allocations.clear(); liveBytes = peakBytes = 0;
    slots = {}; wanted = {}; response = {};
    contextGeneration = contextRoom = contextBoard = 0;
    publishedGeneration = consumedGeneration = 0; accessOrdinal = 0;
    failures = nextWanted = 0; nextAttemptMs = 0; fakeNow = 0;
    cacheStarted = false; workerTaskHandle = nullptr; createdTasks = 0;
    httpGets = invalidations = 0; allocationFails = false;
}
std::vector<uint8_t> bytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("Missing fixture %s\n", path.c_str()); std::exit(2); }
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void view(uint8_t level, uint32_t room = 100, uint8_t board = 16) {
    remoteTileCacheSetView(room, board, GridCityArtwork::RivetRow, "a1-rivet-row", level);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string assets = argv[1];
    using namespace gridopoly::assets;
    const auto l1 = bytes(assets + "/development/a1-rivet-row-l1.rgb565");
    const auto l5 = bytes(assets + "/development/a1-rivet-row-l5.rgb565");
    bool catalogOk = true;
    for (const auto &p : kDevelopmentProperties) for (uint8_t level = 1; level <= 5; ++level) {
        const auto payload = bytes(assets + "/development/" + p.key + "-l" + std::to_string(level) + ".rgb565");
        auto req = artworkRequest(GridCityArtwork::RivetRow, p.key, level);
        char path[128]{};
        catalogOk &= payload.size() == 32768 && artworkCrc32(payload.data(), payload.size()) == req.crc &&
            req.catalog == 1 && artworkPath(req, path, sizeof(path)) &&
            std::string(path) == "/assets/development/v1/player/" + std::string(p.key) + "-l" + std::to_string(level) + ".rgb565";
    }
    check(catalogOk, "all 110 development files match routes and catalog CRC");
    bool legacyOk = true;
    for (const auto &p : kLegacyTileChecksums) {
        const auto payload = bytes(assets + "/legacy/" + p.key + ".rgb565");
        legacyOk &= payload.size() == 32768 && artworkCrc32(payload.data(), payload.size()) == p.crc;
    }
    check(legacyOk, "all 36 legacy files match frozen CRCs");
    auto unknown = artworkRequest(GridCityArtwork::RivetRow, "a1-rivet-row", 255);
    auto zero = artworkRequest(GridCityArtwork::RivetRow, "a1-rivet-row", 0);
    auto transit = artworkRequest(GridCityArtwork::WestlineTerminal, "transit-westline-terminal", 5);
    char path[128]{};
    check(artworkPath(zero, path, sizeof(path)) && std::string(path) == "/assets/tiles/a1-rivet-row.rgb565" &&
          unknown.catalog == 0 && !sameArtwork(zero, unknown) && transit.catalog == 0,
          "L0 unknown and non-property retain old source without guessing");
    check(!artworkRequest(GridCityArtwork::RivetRow, "../x", 1).valid() && !artworkPath(unknown, path, 4),
          "unknown keys and truncated paths rejected");
    resetFixture(); remoteTileCacheBegin(); remoteTileCacheBegin(); view(1); response.body = l1;
    check(createdTasks == 1 && nextAttemptMs >= 250 && nextAttemptMs <= 2000 && !loadOne() && httpGets == 0,
          "single worker and initial boot/device jitter gate downloads");
    fakeNow = nextAttemptMs;
    bool atomic = false;
    response.onGet = [&] { atomic = remoteTileCacheImage(GridCityArtwork::RivetRow, "a1-rivet-row", 1) == nullptr; };
    check(loadOne() && atomic && remoteTileCacheConsumeUpdate(), "publish only after full length and CRC");
    response.onGet = {};
    auto *first = remoteTileCacheImage(GridCityArtwork::RivetRow, "a1-rivet-row", 1);
    auto *second = remoteTileCacheImage(GridCityArtwork::RivetRow, "a1-rivet-row", 1);
    check(first && second == first && first->data_size == 32768 && slots[0].leases == 2 &&
          std::memcmp(first->data, l1.data(), l1.size()) == 0, "two LVGL owners hold immutable image");
    check(response.url == "http://configured-pi:8080/assets/development/v1/player/a1-rivet-row-l1.rgb565",
          "configured Pi host and versioned URL");
    remoteTileCacheReleaseImage(first);
    check(slots[0].leases == 1 && invalidations == 1, "one release cannot unpin other owner");
    remoteTileCacheReleaseImage(second);
    check(slots[0].leases == 0 && invalidations == 2, "last release invalidates decoder cache before eviction");
    check(!loadOne(), "ready key causes no extra fetch");
    for (int kind = 0; kind < 3; ++kind) {
        resetFixture(); remoteTileCacheBegin(); fakeNow = nextAttemptMs; view(1); response.body = l1;
        response.onGet = [kind] { view(kind == 0 ? 5 : 1, kind == 1 ? 101 : 100, kind == 2 ? 24 : 16); };
        loadOne();
        check(!remoteTileCacheConsumeUpdate() && liveBytes == 0 && slots[0].state == SlotState::Empty,
              kind == 0 ? "late old level rejected" : kind == 1 ? "new room rejects old result" : "remap rejects old result");
    }
    resetFixture(); remoteTileCacheBegin(); fakeNow = nextAttemptMs;
    remoteTileCacheSetView(1, 16, GridCityArtwork::Fallback, nullptr, 255);
    remoteTileCachePrefetch(GridCityArtwork::RivetRow, "a1-rivet-row", 5); response.body = l5;
    check(loadOne() && !remoteTileCacheConsumeUpdate() && liveBytes == 32768, "prefetch does not invalidate unrelated page");
    remoteTileCachePrefetch(GridCityArtwork::Fallback, nullptr); fakeNow = nextAttemptMs;
    check(!loadOne() && httpGets == 1, "no demand means no all-level prefetch");
    const char *faults[] = {"CRC corruption", "truncated body", "wrong length", "gzip", "chunked", "stalled body", "slow body", "allocation failure"};
    for (int fault = 0; fault < 8; ++fault) {
        resetFixture(); remoteTileCacheBegin(); fakeNow = nextAttemptMs; view(1); response.body = l1;
        if (fault == 0) response.body[123] ^= 1;
        if (fault == 1) response.body.resize(1024);
        if (fault == 2) response.length = 32769;
        if (fault == 3) response.encoding = "gzip";
        if (fault == 4) response.transfer = "chunked";
        if (fault == 5) response.stall = true;
        if (fault == 6) response.readDelay = 500;
        if (fault == 7) allocationFails = true;
        loadOne();
        check(liveBytes == 0 && !remoteTileCacheConsumeUpdate() && failures == 1 &&
              nextAttemptMs - fakeNow >= 1000 && nextAttemptMs - fakeNow <= 2000, faults[fault]);
    }
    for (const int status : {429, 503}) {
        resetFixture(); remoteTileCacheBegin(); fakeNow = nextAttemptMs; view(1);
        response.status = status; response.retry = "17"; loadOne();
        const uint32_t until = nextAttemptMs;
        check(until - fakeNow >= 17000 && until - fakeNow <= 18000 && !loadOne(), "429/503 Retry-After respected");
        view(5); check(!loadOne() && httpGets == 1, "page change cannot bypass throttle");
        fakeNow = until; response.status = 200; response.body = l5; response.retry.clear();
        check(loadOne() && failures == 0 && remoteTileCacheConsumeUpdate(), "valid retry resets failures");
    }
    resetFixture(); remoteTileCacheBegin(); fakeNow = nextAttemptMs;
    std::vector<const lv_img_dsc_t *> pins;
    for (size_t i = 0; i < 20; ++i) {
        const auto &p = kDevelopmentProperties[i];
        remoteTileCacheSetView(1, 16, GridCityArtwork::RivetRow, p.key, 1);
        response.body = bytes(assets + "/development/" + p.key + "-l1.rgb565");
        fakeNow = nextAttemptMs; loadOne();
        pins.push_back(remoteTileCacheImage(GridCityArtwork::RivetRow, p.key, 1));
    }
    const auto &extra = kDevelopmentProperties[20];
    remoteTileCacheSetView(1, 16, GridCityArtwork::RivetRow, extra.key, 1); fakeNow = nextAttemptMs;
    check(std::all_of(pins.begin(), pins.end(), [](auto p) {return p != nullptr;}) &&
          liveBytes == 640 * 1024 && !loadOne() && httpGets == 20, "20 pinned images block download within 640KiB");
    remoteTileCacheReleaseImage(pins[4]);
    response.body = bytes(assets + "/development/" + extra.key + "-l1.rgb565");
    check(loadOne() && liveBytes == 640 * 1024 && peakBytes == 640 * 1024 &&
          sameArtwork(slots[4].request, artworkRequest(GridCityArtwork::RivetRow, extra.key, 1)),
          "only released slot evicted; staging included in peak 640KiB");
    for (size_t i = 0; i < pins.size(); ++i) if (i != 4) remoteTileCacheReleaseImage(pins[i]);
    check(artworkDeadlineReached(0x10, 0xfffffff0) && !artworkDeadlineReached(0xfffffff0, 0x10) &&
          retryArtworkDelayMs(10, 0) == 30000 && artworkRetryAfterSeconds("999999999999") == 120,
          "millis rollover exponential cap and Retry-After overflow");
    resetFixture();
    check(liveBytes == 0 && !cacheStarted && wanted[0].key == nullptr, "fresh boot has no persistent cache");
    std::printf("%d checks, %d failed\n", checked, failed);
    return failed ? 1 : 0;
}
