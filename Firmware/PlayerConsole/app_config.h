#pragma once

#include <Arduino.h>

constexpr gpio_num_t kKnobPinA = GPIO_NUM_6;
constexpr gpio_num_t kKnobPinB = GPIO_NUM_5;
constexpr gpio_num_t kButtonPin = GPIO_NUM_0;

constexpr uint16_t kScreenSize = 480;
constexpr uint16_t kSafeDiameter = 384;
constexpr uint32_t kFrameIntervalMs = 33;

// The mating interface is mechanically rotated clockwise by 60 degrees so
// M4 alignment pin 3 sits exactly on the six-o'clock axis.
// Firmware therefore renders at the panel's native zero-degree orientation.
constexpr int16_t kMechanicalCorrectionDeg = 60;
constexpr int16_t kFirmwareRotationDeg = 0;

constexpr uint32_t kShortPressMaxMs = 499;
constexpr uint32_t kBackPressMs = 800;
constexpr uint32_t kHomePressMs = 3000;
constexpr uint32_t kConfirmHoldMs = 1200;

#ifndef GRIDOPOLY_SELF_TEST
#define GRIDOPOLY_SELF_TEST 0
#endif

#define GRIDOPOLY_TRANSPORT_DEMO 0
#define GRIDOPOLY_TRANSPORT_ESPNOW 1
#define GRIDOPOLY_TRANSPORT_WIFI_UDP 2

#ifndef GRIDOPOLY_PLAYER_TRANSPORT
#if GRIDOPOLY_SELF_TEST == 1
#define GRIDOPOLY_PLAYER_TRANSPORT GRIDOPOLY_TRANSPORT_DEMO
#elif defined(GRIDOPOLY_USE_ESPNOW) && GRIDOPOLY_USE_ESPNOW == 1
#define GRIDOPOLY_PLAYER_TRANSPORT GRIDOPOLY_TRANSPORT_ESPNOW
#else
#define GRIDOPOLY_PLAYER_TRANSPORT GRIDOPOLY_TRANSPORT_WIFI_UDP
#endif
#endif
