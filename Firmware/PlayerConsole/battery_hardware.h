#pragma once
#include "battery_board_config.h"
#include "battery_telemetry.h"

void batteryHardwareBegin(uint32_t nowMs);
void batteryHardwareTick(uint32_t nowMs);
gridopoly::player_console::BatteryStatus batteryHardwareStatus(uint32_t nowMs);
