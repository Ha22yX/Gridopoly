#include <Arduino.h>
#include "board_config.h"
#include "ws2812_led.h"
#include "ws2812_esp32.h"

namespace gridopoly::tile::ws2812 {
namespace {
// TileModule has one RMT owner. ORDER and RFID use GPIO/timers, not RMT.
constexpr rmt_channel_t kChannel = RMT_CHANNEL_0;
}
int Esp32Driver::begin(std::uint32_t &clock_hz) {
  rmt_config_t config{};
  config.rmt_mode = RMT_MODE_TX;
  config.channel = kChannel;
  config.gpio_num = static_cast<gpio_num_t>(board::kWs2812DataPin);
  config.clk_div = 5;  // APB 80 MHz / 5 = 16 MHz (62.5 ns).
  config.mem_block_num = 4;  // Same reserved capacity as the previous HAL path.
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  esp_err_t error = rmt_config(&config);
  if (error != ESP_OK) return error;
  configured_ = true;
  error = rmt_set_source_clk(kChannel, RMT_BASECLK_APB);
  if (error != ESP_OK) return error;
  error = rmt_driver_install(kChannel, 0, 0);
  if (error != ESP_OK) return error;
  return rmt_get_counter_clock(kChannel, &clock_hz);
}
int Esp32Driver::write(const Symbol *symbols, std::size_t count) {
  return rmt_write_items(kChannel, symbols, static_cast<int>(count), false);
}
int Esp32Driver::wait(unsigned timeout_ms) {
  return rmt_wait_tx_done(kChannel, pdMS_TO_TICKS(timeout_ms));
}
int Esp32Driver::stop() {
  if (!configured_) return ESP_OK;
  const esp_err_t stop_error = rmt_tx_stop(kChannel);
  const esp_err_t idle_error = rmt_set_idle_level(kChannel, true, RMT_IDLE_LEVEL_LOW);
  return stop_error != ESP_OK ? stop_error : idle_error;
}
void Esp32Driver::holdLow(unsigned microseconds) {
  // Initialization/completed TX leaves idle output low; no GPIO remapping.
  delayMicroseconds(microseconds);
}
}  // namespace gridopoly::tile::ws2812
