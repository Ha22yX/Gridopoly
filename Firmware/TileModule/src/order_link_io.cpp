#include "order_link_io.h"
#include "board_config.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace gridopoly::tile {
namespace {
portMUX_TYPE gOrderMux = portMUX_INITIALIZER_UNLOCKED;
order::Link gOrderState;
esp_timer_handle_t gOrderTimer = nullptr;
std::uint64_t gBootId = 0;
bool gTimerReady = false, gOutputPulledLow = false;

void orderTimerTick(void *) {
  const auto now = static_cast<std::uint32_t>(esp_timer_get_time());
  const bool input_high = gpio_get_level(static_cast<gpio_num_t>(board::kOrderInPin)) != 0;
  portENTER_CRITICAL(&gOrderMux);
  const bool pull_low = gOrderState.tick(now, input_high);
  if (pull_low != gOutputPulledLow) {
    gpio_set_level(static_cast<gpio_num_t>(board::kOrderOutPin), pull_low ? 1 : 0);
    gOutputPulledLow = pull_low;
  }
  portEXIT_CRITICAL(&gOrderMux);
}
}  // namespace

bool initializeOrderLink() {
  if (gOrderTimer != nullptr) return gTimerReady;
  // GPIO10 controls Q1's gate, not the inter-module wire directly.
  digitalWrite(board::kOrderOutPin, LOW);
  pinMode(board::kOrderOutPin, OUTPUT);
  pinMode(board::kOrderInPin, INPUT);  // External 10k pull-up and 100nF filter.
  const auto now = static_cast<std::uint32_t>(esp_timer_get_time());
  std::uint64_t boot = 0;
  while (boot == 0) esp_fill_random(&boot, sizeof(boot));
  portENTER_CRITICAL(&gOrderMux);
  gBootId = boot;
  gOrderState.begin(boot, now, digitalRead(board::kOrderInPin) != LOW);
  portEXIT_CRITICAL(&gOrderMux);
  esp_timer_create_args_t args{};
  args.callback = orderTimerTick;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "tile_order";
  args.skip_unhandled_events = true;
  if (esp_timer_create(&args, &gOrderTimer) != ESP_OK) return false;
  if (esp_timer_start_periodic(gOrderTimer, order::kTickUs) != ESP_OK) {
    esp_timer_delete(gOrderTimer);
    gOrderTimer = nullptr;
    return false;
  }
  portENTER_CRITICAL(&gOrderMux);
  gTimerReady = true;
  portEXIT_CRITICAL(&gOrderMux);
  return true;
}

OrderLinkObservation orderLinkObservation() {
  OrderLinkObservation result;
  portENTER_CRITICAL(&gOrderMux);
  const auto now = static_cast<std::uint32_t>(esp_timer_get_time());
  result.boot_id = gBootId;
  const auto &receiver = gOrderState.receiver();
  result.tx_sequence = gOrderState.transmitter().sequence();
  result.upstream = receiver.upstream();
  result.age_ms = receiver.ageMs(now);
  result.valid = gTimerReady && receiver.valid() && result.age_ms < 15000U;
  result.receiving = receiver.receiving();
  result.stuck_low = receiver.stuckLow();
  result.accepted = receiver.accepted();
  result.rejected = receiver.rejected();
  result.timing_drops = gOrderState.timingDrops();
  result.timer_ready = gTimerReady;
  result.output_pulled_low = gOutputPulledLow;
  portEXIT_CRITICAL(&gOrderMux);
  if (!result.valid) { result.upstream = {}; result.age_ms = 0; }
  return result;
}
}  // namespace gridopoly::tile
