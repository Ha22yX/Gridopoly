#pragma once

// Selecting a board is deliberate: the existing player screen has no battery
// base. A default build must not initialize another UART or display telemetry.
#define GRIDOPOLY_BATTERY_NONE 0
#define GRIDOPOLY_BATTERY_VIEWE_MD80ET_V10 1
#ifndef GRIDOPOLY_BATTERY_BOARD
#define GRIDOPOLY_BATTERY_BOARD GRIDOPOLY_BATTERY_NONE
#endif

#if GRIDOPOLY_BATTERY_BOARD != 0 && GRIDOPOLY_BATTERY_BOARD != 1
#error "Unknown battery board; verify the actual screen wiring before adding a profile"
#endif
#define GRIDOPOLY_BATTERY_ENABLED (GRIDOPOLY_BATTERY_BOARD != GRIDOPOLY_BATTERY_NONE)

#if GRIDOPOLY_BATTERY_ENABLED
// VIEWESMART MD80ET V1.0 schematic: external UARTRX/UARTTX -> S3 RXD/TXD.
// ESP32-S3 IO MUX Table 2-4: U0RXD = GPIO44, U0TXD = GPIO43.
// These are ESP32 GPIO numbers, not FPC positions 6/7. Physical revision and
// end-to-end continuity still require first-board verification; see the report.
constexpr int kBatteryUartRxGpio = 44;
constexpr int kBatteryUartTxGpio = 43;
#endif
