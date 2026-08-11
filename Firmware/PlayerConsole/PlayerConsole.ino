#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <lvgl.h>
#include <new>

#include "app_config.h"
#include "app_state.h"
#include "demo_transport.h"
#include "espnow_player_transport.h"
#include "hardware_input.h"
#include "logic_tests.h"
#include "lvgl_v8_port.h"
#include "rom_console_stream.h"
#include "remote_avatar_cache.h"
#include "remote_tile_cache.h"
#include "ui_renderer.h"
#include "wifi_udp_player_transport.h"

#if GRIDOPOLY_SELF_TEST == 1
// The complete renderer and reducer suites intentionally share one Arduino test task.
// Keep their large protocol fixtures away from the production task budget.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#endif

#ifndef BOARD_HAS_PSRAM
#error "Gridopoly Player Console requires PSRAM"
#endif

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 1
#error "Use ESP32-S3 Hardware CDC/JTAG USB mode"
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

AppState app;
#if GRIDOPOLY_PLAYER_TRANSPORT == GRIDOPOLY_TRANSPORT_WIFI_UDP
WifiUdpPlayerTransport transport;
#elif GRIDOPOLY_PLAYER_TRANSPORT == GRIDOPOLY_TRANSPORT_ESPNOW
EspNowPlayerTransport transport;
#else
DemoTransport transport;
#endif
bool ready = false;
bool lvglReady = false;
bool avatarSetupCacheHeld = false;
constexpr uint16_t kRgbBounceBufferRows = 20;
static_assert(480 % kRgbBounceBufferRows == 0 &&
              (480 / kRgbBounceBufferRows) % 2 == 0,
              "RGB bounce rows must divide the panel into an even number of blocks");

bool pageUsesRemoteTileArtwork(ScreenPage page)
{
    return page == ScreenPage::Home || page == ScreenPage::AssetDetail ||
           page == ScreenPage::MoveGuide || page == ScreenPage::TileEvent ||
           page == ScreenPage::CardReveal || page == ScreenPage::Purchase ||
           page == ScreenPage::Auction;
}

bool pageUsesRemoteAvatarArtwork(ScreenPage page)
{
    return page == ScreenPage::AvatarSetup || page == ScreenPage::PlayerReady ||
           page == ScreenPage::PlayerDetail || page == ScreenPage::Trade;
}

#if GRIDOPOLY_SELF_TEST == 1
constexpr size_t kTestCapacity = 16384;
constexpr uint32_t kCarouselPerfTransitionMs = 220;
constexpr uint32_t kCarouselPerfMinFrames = 6;
constexpr uint32_t kCarouselPerfMinFps = 24;
constexpr uint32_t kCarouselPerfMaxGapMs = 42;
constexpr uint32_t kPerfMaxFirstFrameMs = 80;
constexpr uint32_t kCarouselPerfObserveMs = 320;
constexpr uint32_t kCarouselPerfRetargetMs = 80;
constexpr uint32_t kCenterListPerfTransitionMs = 200;
constexpr uint32_t kCenterListPerfRetargetMs = 160;
constexpr uint32_t kCenterListPerfRequiredCoverageMs =
    kCenterListPerfTransitionMs + 2 * kCenterListPerfRetargetMs;
constexpr uint32_t kCenterListPerfMinSteadyIntervals = 10;
constexpr uint32_t kCenterListPerfColdMaxGapMs = 60;
// Anti-tearing can finish a second framebuffer sync after the first flush callback.
constexpr uint32_t kPerfSceneQuietMs = 250;
constexpr uint32_t kPerfSceneTimeoutMs = 2500;
constexpr uint32_t kPerfRevisionStride = 16;
constexpr uint8_t kPerfTraceCapacity = 16;
constexpr uint32_t kSelfTestCheckpointMagic = 0x47525034;
char testBuffer[kTestCapacity] = {};
RomConsoleStream testOutput(testBuffer, sizeof(testBuffer));

struct SelfTestCheckpoint {
    uint32_t magic;
    uint32_t inverseMagic;
    uint32_t componentPassed;
    uint32_t inverseComponentPassed;
};

RTC_NOINIT_ATTR SelfTestCheckpoint selfTestCheckpoint;

struct CarouselPerfResult {
    uint32_t frames = 0;
    uint32_t fps = 0;
    uint32_t firstFrameMs = 0;
    uint32_t maxGapMs = 0;
    uint32_t coverageMs = 0;
    uint32_t steadyIntervals = 0;
    uint32_t steadyFps = 0;
    uint32_t steadyCoverageMs = 0;
    uint16_t gaps[kPerfTraceCapacity]{};
    uint16_t renderTimes[kPerfTraceCapacity]{};
    uint32_t pixelCounts[kPerfTraceCapacity]{};
    uint8_t traceCount = 0;
    uint32_t incrementalRenders = 0;
    uint32_t rebuildRenders = 0;
    bool passed = false;
};

enum class PerfScenario : uint8_t {
    WaitingForward,
    WaitingReverseWrap,
    MyTurnFive,
    Retarget,
    SwipeEvent,
    AssetsListCold,
    AssetsListWarm,
};

struct CarouselPerfProbe {
    volatile bool armed = false;
    volatile uint32_t startedMs = 0;
    volatile uint32_t requiredCoverageMs = 0;
    volatile uint32_t previousFrameMs = 0;
    volatile uint32_t lastFrameMs = 0;
    volatile uint32_t frames = 0;
    volatile uint32_t firstFrameMs = 0;
    volatile uint32_t maxGapMs = 0;
    volatile uint16_t gaps[kPerfTraceCapacity]{};
    volatile uint16_t renderTimes[kPerfTraceCapacity]{};
    volatile uint32_t pixelCounts[kPerfTraceCapacity]{};
    void (*previousMonitor)(lv_disp_drv_t *, uint32_t, uint32_t) = nullptr;
};

struct SceneSettleProbe {
    volatile bool watching = false;
    volatile uint32_t frames = 0;
    volatile uint32_t lastFrameMs = 0;
    void (*previousMonitor)(lv_disp_drv_t *, uint32_t, uint32_t) = nullptr;
};

CarouselPerfProbe carouselPerfProbe{};
SceneSettleProbe sceneSettleProbe{};
uint32_t perfRevisionSeed = 1000;

constexpr bool carouselPerfMeetsGate(uint32_t frames, uint32_t fps,
                                     uint32_t maxGapMs, uint32_t coverageMs,
                                     uint32_t requiredCoverageMs = kCarouselPerfTransitionMs,
                                     uint32_t firstFrameMs = 0)
{
    return frames >= kCarouselPerfMinFrames && fps >= kCarouselPerfMinFps &&
           firstFrameMs <= kPerfMaxFirstFrameMs &&
           maxGapMs <= kCarouselPerfMaxGapMs &&
           requiredCoverageMs >= kCarouselPerfTransitionMs &&
           coverageMs >= requiredCoverageMs;
}

constexpr bool centerListPerfMeetsGate(uint32_t steadyIntervals, uint32_t steadyFps,
                                       uint32_t maxGapMs, uint32_t coverageMs,
                                       uint32_t firstFrameMs = 0,
                                       uint32_t maxAllowedGapMs = kCarouselPerfMaxGapMs)
{
    return steadyIntervals >= kCenterListPerfMinSteadyIntervals &&
           steadyFps >= kCarouselPerfMinFps &&
           firstFrameMs <= kPerfMaxFirstFrameMs &&
           maxGapMs <= maxAllowedGapMs &&
           coverageMs >= kCenterListPerfRequiredCoverageMs;
}

constexpr bool perfCoverageReached(uint32_t nowMs, uint32_t startedMs,
                                   uint32_t requiredCoverageMs)
{
    return nowMs - startedMs >= requiredCoverageMs;
}

static_assert(carouselPerfMeetsGate(6, 24, 42, 220), "carousel performance boundary must pass");
static_assert(!carouselPerfMeetsGate(5, 24, 42, 220), "carousel frame count must span the transition");
static_assert(!carouselPerfMeetsGate(6, 23, 42, 220), "carousel FPS floor must be enforced");
static_assert(!carouselPerfMeetsGate(6, 24, 43, 220), "carousel frame gap ceiling must be enforced");
static_assert(!carouselPerfMeetsGate(6, 24, 42, 220, 220, 81),
              "carousel first-frame latency ceiling must be enforced");
static_assert(!carouselPerfMeetsGate(6, 24, 42, 219), "carousel measurement must span 220ms");
static_assert(carouselPerfMeetsGate(8, 24, 42, 300, 300),
              "retarget performance boundary must span both visible transitions");
static_assert(centerListPerfMeetsGate(10, 24, 42, 520),
              "center-list performance boundary must pass");
static_assert(!centerListPerfMeetsGate(9, 24, 42, 520),
              "center-list steady interval floor must be enforced");
static_assert(!centerListPerfMeetsGate(10, 23, 42, 520),
              "center-list steady FPS floor must be enforced");
static_assert(centerListPerfMeetsGate(10, 24, 60, 520, 80,
                                     kCenterListPerfColdMaxGapMs),
              "center-list cold exposure has an explicit bounded gap");
static_assert(!centerListPerfMeetsGate(10, 24, 61, 520, 80,
                                      kCenterListPerfColdMaxGapMs),
              "center-list cold exposure gap ceiling must be enforced");
static_assert(!centerListPerfMeetsGate(10, 24, 42, 520, 81),
              "center-list first-frame latency ceiling must be enforced");
static_assert(kPerfRevisionStride > 2,
              "each performance scene needs revisions beyond its input sequence");
static_assert(!perfCoverageReached(219, 0, 220),
              "performance probe must remain armed before full transition coverage");
static_assert(perfCoverageReached(220, 0, 220),
              "performance probe must stop at exact transition coverage");
static_assert(perfCoverageReached(0x00000020U, 0xfffffff0U, 48),
              "performance probe coverage must remain wrap safe");

void carouselPerfMonitor(lv_disp_drv_t *driver, uint32_t renderMs, uint32_t pixelCount)
{
    if (carouselPerfProbe.armed && pixelCount > 0) {
        const uint32_t nowMs = millis();
        const uint32_t gapMs = nowMs - carouselPerfProbe.previousFrameMs;
        carouselPerfProbe.previousFrameMs = nowMs;
        carouselPerfProbe.lastFrameMs = nowMs;
        if (carouselPerfProbe.frames == 0) {
            carouselPerfProbe.firstFrameMs = gapMs;
        } else if (gapMs > carouselPerfProbe.maxGapMs) {
            carouselPerfProbe.maxGapMs = gapMs;
        }
        if (carouselPerfProbe.frames < kPerfTraceCapacity) {
            const uint8_t index = static_cast<uint8_t>(carouselPerfProbe.frames);
            carouselPerfProbe.gaps[index] = static_cast<uint16_t>(gapMs);
            carouselPerfProbe.renderTimes[index] = static_cast<uint16_t>(renderMs);
            carouselPerfProbe.pixelCounts[index] = pixelCount;
        }
        ++carouselPerfProbe.frames;
        if (perfCoverageReached(nowMs, carouselPerfProbe.startedMs,
                                carouselPerfProbe.requiredCoverageMs)) {
            carouselPerfProbe.armed = false;
        }
    }
    if (carouselPerfProbe.previousMonitor != nullptr) {
        carouselPerfProbe.previousMonitor(driver, renderMs, pixelCount);
    }
}

class CarouselPerfMonitorGuard {
public:
    bool install(lv_disp_t *target)
    {
        if (installed_ || target == nullptr || target->driver == nullptr ||
            target->driver->monitor_cb == carouselPerfMonitor) {
            return false;
        }
        display = target;
        carouselPerfProbe.previousMonitor = display->driver->monitor_cb;
        carouselPerfProbe.armed = true;
        installed_ = true;
        display->driver->monitor_cb = carouselPerfMonitor;
        return true;
    }

    bool restore()
    {
        carouselPerfProbe.armed = false;
        if (!installed_) return true;
        const bool ownsMonitor = display != nullptr && display->driver != nullptr &&
                                 display->driver->monitor_cb == carouselPerfMonitor;
        if (ownsMonitor) {
            display->driver->monitor_cb = carouselPerfProbe.previousMonitor;
        }
        display = nullptr;
        installed_ = false;
        return ownsMonitor;
    }

    ~CarouselPerfMonitorGuard() { restore(); }

private:
    lv_disp_t *display = nullptr;
    bool installed_ = false;
};

void sceneSettleMonitor(lv_disp_drv_t *driver, uint32_t renderMs, uint32_t pixelCount)
{
    if (sceneSettleProbe.watching && pixelCount > 0) {
        sceneSettleProbe.lastFrameMs = millis();
        ++sceneSettleProbe.frames;
    }
    if (sceneSettleProbe.previousMonitor != nullptr) {
        sceneSettleProbe.previousMonitor(driver, renderMs, pixelCount);
    }
}

bool waitForStablePerfScene(lv_disp_t *display)
{
    const uint32_t startedMs = millis();
    bool stable = false;
    while (millis() - startedMs < kPerfSceneTimeoutMs) {
        bool animationsRunning = true;
        if (lvgl_port_lock(100)) {
            animationsRunning = lv_anim_count_running() != 0;
            lvgl_port_unlock();
        }

        const uint32_t nowMs = millis();
        const uint32_t completedFrames = sceneSettleProbe.frames;
        const uint32_t lastFrameMs = sceneSettleProbe.lastFrameMs;
        if (completedFrames > 0 && !animationsRunning &&
            nowMs - lastFrameMs >= kPerfSceneQuietMs) {
            stable = true;
            break;
        }
        delay(5);
    }

    if (!lvgl_port_lock(-1)) return false;
    sceneSettleProbe.watching = false;
    if (display != nullptr && display->driver != nullptr &&
        display->driver->monitor_cb == sceneSettleMonitor) {
        display->driver->monitor_cb = sceneSettleProbe.previousMonitor;
    } else {
        stable = false;
    }
    lvgl_port_unlock();
    return stable;
}

bool preparePerfScenario(AppState &state, PerfScenario scenario)
{
    if (!lvgl_port_lock(1000)) return false;
    lv_disp_t *display = lv_disp_get_default();
    if (display == nullptr || display->driver == nullptr ||
        display->refr_timer == nullptr ||
        display->driver->monitor_cb == carouselPerfMonitor ||
        display->driver->monitor_cb == sceneSettleMonitor) {
        lvgl_port_unlock();
        return false;
    }

    const uint32_t nowMs = millis();
    sceneSettleProbe.previousMonitor = display->driver->monitor_cb;
    sceneSettleProbe.frames = 0;
    sceneSettleProbe.lastFrameMs = nowMs;
    sceneSettleProbe.watching = true;
    display->driver->monitor_cb = sceneSettleMonitor;
    appInit(state, nowMs);
    state.toastUntilMs = 0;
    state.nav = NavigationState{};
    if (scenario == PerfScenario::AssetsListCold ||
        scenario == PerfScenario::AssetsListWarm) {
        state.nav.current = NavigationEntry{ScreenPage::Assets, 0, 0};
        state.page = ScreenPage::Assets;
        state.focus = 0;
        state.assetListIndex = 0;
    } else {
        state.homePhase = scenario == PerfScenario::MyTurnFive ||
                                  scenario == PerfScenario::Retarget
                              ? HomePhase::MyTurn
                              : HomePhase::Waiting;
        state.turnsUntilYou = state.homePhase == HomePhase::MyTurn ? 0 : 3;
        state.nav.current = NavigationEntry{ScreenPage::Home, 0, 0};
        state.page = ScreenPage::Home;
        state.focus = 0;
    }
    perfRevisionSeed += kPerfRevisionStride;
    state.revision = perfRevisionSeed;
    uiRendererRender(state, nowMs);
    lv_timer_ready(display->refr_timer);
    lvgl_port_unlock();
    return waitForStablePerfScene(display);
}

bool retargetCarouselFixture(AppState &state, int8_t delta = 1)
{
    if (!lvgl_port_lock(1000)) return false;
    const uint32_t nowMs = millis();
    appHandleInput(state, InputEvent{InputKind::Rotate, delta, nowMs}, nowMs);
    uiRendererRender(state, nowMs);
    lvgl_port_unlock();
    return true;
}

CarouselPerfResult runCarouselPerfFixture(AppState &state, PerfScenario scenario)
{
    CarouselPerfResult result{};
    CarouselPerfMonitorGuard monitorGuard;
    if (!preparePerfScenario(state, scenario)) return result;
    if (!lvgl_port_lock(1000)) return result;
    lv_disp_t *display = lv_disp_get_default();
    if (display == nullptr || display->driver == nullptr ||
        display->driver->monitor_cb == carouselPerfMonitor) {
        lvgl_port_unlock();
        return result;
    }

    carouselPerfProbe.frames = 0;
    carouselPerfProbe.firstFrameMs = 0;
    carouselPerfProbe.maxGapMs = 0;
    uiRendererResetTestStats();
    carouselPerfProbe.startedMs = millis();
    carouselPerfProbe.requiredCoverageMs = scenario == PerfScenario::Retarget
        ? kCarouselPerfTransitionMs + kCarouselPerfRetargetMs
        : (scenario == PerfScenario::AssetsListCold ||
           scenario == PerfScenario::AssetsListWarm
               ? kCenterListPerfRequiredCoverageMs
               : kCarouselPerfTransitionMs);
    carouselPerfProbe.previousFrameMs = carouselPerfProbe.startedMs;
    carouselPerfProbe.lastFrameMs = carouselPerfProbe.startedMs;
    if (!monitorGuard.install(display)) {
        lvgl_port_unlock();
        return result;
    }
    if (scenario == PerfScenario::WaitingReverseWrap) {
        appHandleInput(
            state,
            InputEvent{InputKind::Rotate, -1, carouselPerfProbe.startedMs},
            carouselPerfProbe.startedMs
        );
    } else if (scenario == PerfScenario::SwipeEvent) {
        appHandleUiEvent(
            state, UiEvent{UiEventKind::ListNext, 0}, carouselPerfProbe.startedMs
        );
    } else {
        appHandleInput(
            state,
            InputEvent{InputKind::Rotate, 1, carouselPerfProbe.startedMs},
            carouselPerfProbe.startedMs
        );
    }
    uiRendererRender(state, carouselPerfProbe.startedMs);
    lvgl_port_unlock();

    bool scenarioCompleted = true;
    if (scenario == PerfScenario::Retarget) {
        delay(kCarouselPerfRetargetMs);
        scenarioCompleted = retargetCarouselFixture(state);
    } else if (scenario == PerfScenario::AssetsListCold ||
               scenario == PerfScenario::AssetsListWarm) {
        delay(kCenterListPerfRetargetMs);
        scenarioCompleted = retargetCarouselFixture(state);
        delay(kCenterListPerfRetargetMs);
        scenarioCompleted = retargetCarouselFixture(state) && scenarioCompleted;
    }
    delay(kCarouselPerfObserveMs);

    if (!lvgl_port_lock(1000)) return result;
    scenarioCompleted = monitorGuard.restore() && scenarioCompleted;
    result.frames = carouselPerfProbe.frames;
    result.firstFrameMs = carouselPerfProbe.firstFrameMs;
    result.maxGapMs = carouselPerfProbe.maxGapMs;
    result.coverageMs = carouselPerfProbe.lastFrameMs - carouselPerfProbe.startedMs;
    result.traceCount = static_cast<uint8_t>(
        result.frames < kPerfTraceCapacity ? result.frames : kPerfTraceCapacity
    );
    for (uint8_t index = 0; index < result.traceCount; ++index) {
        result.gaps[index] = carouselPerfProbe.gaps[index];
        result.renderTimes[index] = carouselPerfProbe.renderTimes[index];
        result.pixelCounts[index] = carouselPerfProbe.pixelCounts[index];
    }
    const UiRendererTestStats rendererStats = uiRendererGetTestStats();
    result.incrementalRenders = rendererStats.incrementalRenders;
    result.rebuildRenders = rendererStats.rebuildRenders;
    lvgl_port_unlock();

    if (result.coverageMs > 0) {
        result.fps = result.frames * 1000U / result.coverageMs;
    }
    if (result.frames > 1 && result.coverageMs > result.firstFrameMs) {
        result.steadyIntervals = result.frames - 1;
        result.steadyCoverageMs = result.coverageMs - result.firstFrameMs;
        result.steadyFps = result.steadyIntervals * 1000U / result.steadyCoverageMs;
    }
    const bool incrementalOnly = result.incrementalRenders > 0 &&
                                 result.rebuildRenders == 0;
    if (scenario == PerfScenario::AssetsListCold ||
        scenario == PerfScenario::AssetsListWarm) {
        const uint32_t maxAllowedGap = scenario == PerfScenario::AssetsListCold
            ? kCenterListPerfColdMaxGapMs : kCarouselPerfMaxGapMs;
        result.passed = scenarioCompleted && incrementalOnly && centerListPerfMeetsGate(
            result.steadyIntervals, result.steadyFps,
            result.maxGapMs, result.coverageMs,
            result.firstFrameMs, maxAllowedGap
        );
    } else {
        const uint32_t requiredCoverage = scenario == PerfScenario::Retarget
            ? kCarouselPerfTransitionMs + kCarouselPerfRetargetMs
            : kCarouselPerfTransitionMs;
        result.passed = scenarioCompleted && incrementalOnly && carouselPerfMeetsGate(
            result.frames, result.fps, result.maxGapMs, result.coverageMs,
            requiredCoverage, result.firstFrameMs
        );
    }
    return result;
}

void printPerfResult(const char *marker, const CarouselPerfResult &result)
{
    esp_rom_printf(
        "%s frames=%u fps=%u first_frame_ms=%u max_gap_ms=%u coverage_ms=%u incremental=%u rebuild=%u %s\n",
        marker,
        static_cast<unsigned>(result.frames),
        static_cast<unsigned>(result.fps),
        static_cast<unsigned>(result.firstFrameMs),
        static_cast<unsigned>(result.maxGapMs),
        static_cast<unsigned>(result.coverageMs),
        static_cast<unsigned>(result.incrementalRenders),
        static_cast<unsigned>(result.rebuildRenders),
        result.passed ? "PASS" : "FAIL"
    );
    esp_rom_printf("%s DIAG", marker);
    for (uint8_t index = 0; index < result.traceCount; ++index) {
        esp_rom_printf(
            " [%u:%u/%u]",
            static_cast<unsigned>(result.gaps[index]),
            static_cast<unsigned>(result.renderTimes[index]),
            static_cast<unsigned>(result.pixelCounts[index])
        );
    }
    esp_rom_printf("\n");
    if (result.steadyIntervals > 0) {
        esp_rom_printf(
            "%s STEADY intervals=%u fps=%u coverage_ms=%u\n",
            marker,
            static_cast<unsigned>(result.steadyIntervals),
            static_cast<unsigned>(result.steadyFps),
            static_cast<unsigned>(result.steadyCoverageMs)
        );
    }
}
#endif

void fault(const char *code)
{
    ready = false;
    Serial.printf("Gridopoly fault: %s\n", code);
    if (!lvglReady || !lvgl_port_lock(-1)) return;
    uiRendererShowFault(code);
    lvgl_port_unlock();
}

} // namespace

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(200);

#if GRIDOPOLY_SELF_TEST == 1
    const bool resumedComponentPhase =
        esp_reset_reason() == ESP_RST_SW &&
        selfTestCheckpoint.magic == kSelfTestCheckpointMagic &&
        selfTestCheckpoint.inverseMagic == ~kSelfTestCheckpointMagic &&
        selfTestCheckpoint.componentPassed <= 1 &&
        selfTestCheckpoint.inverseComponentPassed == ~selfTestCheckpoint.componentPassed;
    if (!resumedComponentPhase) {
        // The uploader releases native USB after this boot has already started.
        delay(3000);
        testOutput.reset();
        const bool cleanStart = !lv_is_initialized();
        const bool componentTestsPassed = runLvglComponentTests(testOutput);
        const bool cleanObjects = lv_disp_get_next(nullptr) == nullptr &&
                                  lv_indev_get_next(nullptr) == nullptr;
        const bool componentPhasePassed = cleanStart && componentTestsPassed && cleanObjects;
        testOutput.printf(
            "[%s] fake-display component phase releases every display and input\n",
            cleanObjects ? "PASS" : "FAIL"
        );
        selfTestCheckpoint.componentPassed = componentPhasePassed ? 1U : 0U;
        selfTestCheckpoint.inverseComponentPassed = ~selfTestCheckpoint.componentPassed;
        selfTestCheckpoint.inverseMagic = ~kSelfTestCheckpointMagic;
        selfTestCheckpoint.magic = kSelfTestCheckpointMagic;
        esp_rom_printf("%s\n", testOutput.data());
        esp_rom_printf("COMPONENT TESTS %s\n", componentPhasePassed ? "PASS" : "FAIL");
        delay(100);
        esp_restart();
        while (true) delay(1000);
    }

    const bool componentPassed = selfTestCheckpoint.componentPassed == 1U;
    selfTestCheckpoint.magic = 0;
    selfTestCheckpoint.inverseMagic = 0;
    selfTestCheckpoint.componentPassed = 0;
    selfTestCheckpoint.inverseComponentPassed = 0;
    testOutput.reset();
    resetLogicTestFailure();
    const bool cleanBeforePure = !lv_is_initialized();
    const bool pureTestsPassed = runPureLogicTests(testOutput);
    const bool cleanAfterPure = !lv_is_initialized();
    const bool purePassed = cleanBeforePure && pureTestsPassed && cleanAfterPure;
    testOutput.printf(
        "[%s] pure logic leaves LVGL uninitialized before physical port setup\n",
        cleanBeforePure && cleanAfterPure ? "PASS" : "FAIL"
    );
#endif

    Board *board = new (std::nothrow) Board();
    if (board == nullptr || !board->init()) { fault("BOARD_INIT"); return; }
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // The board default is ten rows. Twenty rows is the vendor-prescribed
        // mitigation when intermittent horizontal scan-line drift remains.
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(
            lcd->getFrameWidth() * kRgbBounceBufferRows
        );
    }
#endif
#endif
    if (!board->begin()) { fault("BOARD_BEGIN"); return; }

    // Touch and pixels share the same mechanically corrected coordinate system.
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) { fault("LVGL_INIT"); return; }
    lvglReady = true;
    if (!hardwareInputBegin()) { fault("INPUT_INIT"); return; }

    const uint32_t startedMs = millis();
    appInit(app, startedMs);
    transport.begin(startedMs);
#if GRIDOPOLY_PLAYER_TRANSPORT == GRIDOPOLY_TRANSPORT_WIFI_UDP
    remoteTileCacheBegin();
    remoteAvatarCacheBegin();
#endif
    if (!lvgl_port_lock(-1)) { fault("LVGL_LOCK"); return; }
    const bool rendererReady = uiRendererBegin();
    if (rendererReady) {
        const uint32_t renderedMs = millis();
        uiRendererRender(app, renderedMs);
        appNotifyFramePresented(app, renderedMs);
    }
    lvgl_port_unlock();
    if (!rendererReady) { fault("UI_INIT"); return; }

#if GRIDOPOLY_SELF_TEST == 1
    // Leave enough time to attach a monitor after the uploader resets native USB.
    delay(12000);
    static CarouselPerfResult waitingForward;
    static CarouselPerfResult waitingReverseWrap;
    static CarouselPerfResult myTurnFive;
    static CarouselPerfResult retarget;
    static CarouselPerfResult swipeEvent;
    static CarouselPerfResult assetsListCold;
    static CarouselPerfResult assetsListWarm;
    waitingForward = runCarouselPerfFixture(
        app, PerfScenario::WaitingForward
    );
    waitingReverseWrap = runCarouselPerfFixture(
        app, PerfScenario::WaitingReverseWrap
    );
    myTurnFive = runCarouselPerfFixture(
        app, PerfScenario::MyTurnFive
    );
    retarget = runCarouselPerfFixture(
        app, PerfScenario::Retarget
    );
    swipeEvent = runCarouselPerfFixture(
        app, PerfScenario::SwipeEvent
    );
    assetsListCold = runCarouselPerfFixture(
        app, PerfScenario::AssetsListCold
    );
    assetsListWarm = runCarouselPerfFixture(
        app, PerfScenario::AssetsListWarm
    );
    const bool livePerfPassed = waitingForward.passed && waitingReverseWrap.passed &&
                                myTurnFive.passed && retarget.passed &&
                                swipeEvent.passed && assetsListCold.passed &&
                                assetsListWarm.passed;
    const bool passed = purePassed && componentPassed && livePerfPassed;
    esp_rom_printf("%s\n", testOutput.data());
    printPerfResult("CAROUSEL PERF WAIT_FWD", waitingForward);
    printPerfResult("CAROUSEL PERF WAIT_WRAP_REV", waitingReverseWrap);
    printPerfResult("CAROUSEL PERF MYTURN_5", myTurnFive);
    printPerfResult("CAROUSEL PERF RETARGET", retarget);
    printPerfResult("CAROUSEL PERF SWIPE_EVENT", swipeEvent);
    printPerfResult("LIST PERF ASSETS_SCROLL_COLD", assetsListCold);
    printPerfResult("LIST PERF ASSETS_SCROLL_WARM", assetsListWarm);
    esp_rom_printf("SELFTEST SUMMARY pure=%u component=%u perf=%u clean_before=%u clean_after=%u\n",
                   purePassed ? 1U : 0U, componentPassed ? 1U : 0U,
                   livePerfPassed ? 1U : 0U, cleanBeforePure ? 1U : 0U,
                   cleanAfterPure ? 1U : 0U);
    esp_rom_printf("SELFTEST FIRST FAILURE %s\n", firstLogicTestFailure());
    esp_rom_printf("%s\n", passed ? "SELFTEST PASS" : "SELFTEST FAILED");
    if (!passed) while (true) delay(1000);
#endif

    ready = true;
    Serial.printf("Gridopoly Player Console ready; mechanical=%d firmware=%d\n",
                  kMechanicalCorrectionDeg, kFirmwareRotationDeg);
}

void loop()
{
    if (!ready) { delay(5); return; }
    const uint32_t nowMs = millis();
    InputEvent input{};
    TransportCommand command{};
    TransportEvent transportEvent{};
    while (hardwareInputPoll(input)) {
        const uint8_t beforeCommands = app.commandCount;
        appHandleInput(app, input, nowMs);
        Serial.printf(
            "GRIDOPOLY_INPUT kind=%u delta=%d page=%u focus=%u queued=%u auth=%u catalog=%u pending=%04x\n",
            static_cast<unsigned>(input.kind), input.delta, static_cast<unsigned>(app.page),
            app.focus, static_cast<unsigned>(app.commandCount - beforeCommands),
            app.authorityOnline ? 1u : 0u, app.boardCatalogCompatible ? 1u : 0u,
            app.pendingCommandMask
        );
    }

    if (lvgl_port_lock(-1)) {
        char recognizedCharacter = '\0';
        if (uiRendererPollHandwriting(recognizedCharacter, nowMs)) {
            appIdentityAppendCharacter(app, recognizedCharacter);
            Serial.printf("GRIDOPOLY_NAME recognized=%c\n", recognizedCharacter);
        }
        TouchAction action = TouchAction::None;
        while (uiRendererPollTouch(action)) {
            const uint8_t beforeCommands = app.commandCount;
            appHandleTouch(app, action, nowMs);
            Serial.printf(
                "GRIDOPOLY_TOUCH action=%u page=%u focus=%u queued=%u auth=%u catalog=%u pending=%04x\n",
                static_cast<unsigned>(action), static_cast<unsigned>(app.page), app.focus,
                static_cast<unsigned>(app.commandCount - beforeCommands),
                app.authorityOnline ? 1u : 0u, app.boardCatalogCompatible ? 1u : 0u,
                app.pendingCommandMask
            );
        }
        lvgl_port_unlock();
    }

    while (appPollCommand(app, command)) {
        Serial.printf(
            "GRIDOPOLY_COMMAND dequeue kind=%u request=%lu app_version=%lu\n",
            static_cast<unsigned>(command.kind), static_cast<unsigned long>(command.requestId),
            static_cast<unsigned long>(command.stateVersion)
        );
        transport.send(command, nowMs);
    }
    transport.tick(nowMs);
    while (transport.poll(transportEvent)) appHandleTransportEvent(app, transportEvent, nowMs);
    appTick(app, nowMs);
#if GRIDOPOLY_PLAYER_TRANSPORT == GRIDOPOLY_TRANSPORT_WIFI_UDP
    if (appIdentityActive(app)) {
        avatarSetupCacheHeld = true;
    } else if (avatarSetupCacheHeld) {
        remoteAvatarCacheReleaseSetup();
        avatarSetupCacheHeld = false;
    }
    if (app.page == ScreenPage::AvatarLoading) {
        remoteAvatarCachePreload(app.identity.draftRecipe);
        const RemoteAvatarPreloadProgress progress = remoteAvatarCachePreloadProgress();
        appUpdateAvatarPreloadProgress(app, progress.readyCount,
                                       progress.totalCount, progress.complete);
    } else if (app.page == ScreenPage::AvatarSetup) {
        (void)remoteAvatarPreview(app.identity.draftRecipe);
    }
    const bool tileArtworkChanged = remoteTileCacheConsumeUpdate();
    const bool avatarArtworkChanged = remoteAvatarCacheConsumeUpdate();
    if ((tileArtworkChanged && pageUsesRemoteTileArtwork(app.page)) ||
        (avatarArtworkChanged && pageUsesRemoteAvatarArtwork(app.page))) {
        uiRendererInvalidateArtwork();
    }
#endif

    if (lvgl_port_lock(-1)) {
        uiRendererRender(app, nowMs);
        lvgl_port_unlock();
        appNotifyFramePresented(app, nowMs);
    }
    delay(2);
}
