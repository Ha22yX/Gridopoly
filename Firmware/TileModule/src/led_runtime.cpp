#include <Arduino.h>
#include <iterator>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "led_runtime.h"
#include "ws2812_esp32.h"

namespace gridopoly::tile {
namespace {
struct CommandMessage { ws2812::Command command{}; std::uint32_t issued_ms = 0; };
QueueHandle_t gScenes = nullptr, gCommands = nullptr;
TaskHandle_t gWorker = nullptr;
portMUX_TYPE gStatusLock = portMUX_INITIALIZER_UNLOCKED;
LedRuntimeStatus gStatus{};
// Only worker() owns the RMT channel, encoder buffer and mutable animator.
ws2812::Transmitter<ws2812::Esp32Driver, board::kWs2812Count> gTransmitter;

void publishStatus(const LedRuntimeStatus &status) {
  portENTER_CRITICAL(&gStatusLock);
  gStatus = status;
  portEXIT_CRITICAL(&gStatusLock);
}

void worker(void *) {
  led_scene::Animator animator;
  LedRuntimeStatus status;
  status.worker = true;
  const bool ready = gTransmitter.begin();
  status.output = gTransmitter.status();
  publishStatus(status);
  Serial.printf("[WS2812] worker=%s core=0 period_ms=%lu count=%u RMT=%s clock_hz=%lu stage=%s\r\n",
      ready ? "READY" : "FAILED", static_cast<unsigned long>(led_scene::kFrameMs),
      static_cast<unsigned>(board::kWs2812Count), ready ? "OK" : "FAIL",
      static_cast<unsigned long>(status.output.clock_hz), status.output.stage);
  if (!ready) { vTaskDelete(nullptr); return; }
  Rgb pixels[board::kWs2812Count]{};
  std::uint32_t previous_ms = millis();
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    led_scene::Input input;
    if (xQueueReceive(gScenes, &input, 0) == pdTRUE) animator.update(input);
    CommandMessage message;
    // Bounded queue: producer activity cannot starve an LED frame.
    for (unsigned i = 0; i < 4 && xQueueReceive(gCommands, &message, 0) == pdTRUE; ++i)
      animator.command(message.command, message.issued_ms);
    const auto now = static_cast<std::uint32_t>(millis());
    if (status.frames != 0) {
      const auto gap = static_cast<std::uint32_t>(now - previous_ms);
      status.max_gap_ms = std::max(status.max_gap_ms, gap);
      if (gap > 2U * led_scene::kFrameMs) ++status.late_frames;
    }
    previous_ms = now;
    status.pattern = animator.render(now, pixels);
    status.mode = animator.mode(now);
    status.remaining_ms = animator.remaining(now);
    const bool sent = led_scene::sendPixels(gTransmitter, pixels, status.pattern);
    status.requested = pixels[0];
    status.peak_index = status.peak_channel = 0;
    for (unsigned i = 0; i < board::kWs2812Count; ++i) {
      status.pixels[i] = pixels[i];
      const auto peak = std::max(pixels[i].red, std::max(pixels[i].green, pixels[i].blue));
      if (peak > status.peak_channel) {
        status.peak_channel = peak;
        status.peak_index = static_cast<std::uint8_t>(i);
      }
    }
    status.output = gTransmitter.status();
    ++status.frames;
    publishStatus(status);
    if (!sent) {
      Serial.printf("[WS2812] ERROR stage=%s driver_error=%d stop_error=%d\r\n",
          status.output.stage, status.output.driver_error, status.output.stop_error);
      vTaskDelete(nullptr); return;  // Never retry an unfinished RMT frame.
    }
    const auto ticks = pdMS_TO_TICKS(led_scene::kFrameMs);
    // No burst of catch-up writes after an actual scheduling overrun.
    if (static_cast<TickType_t>(xTaskGetTickCount() - wake) >= ticks)
      wake = xTaskGetTickCount();
    vTaskDelayUntil(&wake, ticks);
  }
}
}  // namespace

bool beginLedRuntime() {
  if (gWorker != nullptr) return true;
  gScenes = xQueueCreate(1, sizeof(led_scene::Input));
  gCommands = xQueueCreate(4, sizeof(CommandMessage));
  if (gScenes != nullptr && gCommands != nullptr &&
      xTaskCreatePinnedToCore(worker, "tile-led", 4096, nullptr, 2, &gWorker, 0) == pdPASS)
    return true;
  if (gScenes != nullptr) vQueueDelete(gScenes);
  if (gCommands != nullptr) vQueueDelete(gCommands);
  gScenes = gCommands = nullptr;
  gWorker = nullptr;
  return false;
}

bool submitLedScene(const led_scene::Input &input) {
  return gScenes != nullptr && xQueueOverwrite(gScenes, &input) == pdPASS;
}
bool submitLedCommand(const ws2812::Command &command, std::uint32_t issued_ms) {
  if (!command.valid || command.status_only) return false;
  const CommandMessage message{command, issued_ms};
  return gCommands != nullptr && xQueueSend(gCommands, &message, 0) == pdPASS;
}
LedRuntimeStatus ledRuntimeStatus() {
  portENTER_CRITICAL(&gStatusLock);
  const auto status = gStatus;
  portEXIT_CRITICAL(&gStatusLock);
  return status;
}
}  // namespace gridopoly::tile
