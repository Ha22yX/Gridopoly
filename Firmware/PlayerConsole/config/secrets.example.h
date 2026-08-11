#pragma once

// Copy this file to secrets.local.h. The local file is ignored by Git and none of these
// values may be printed to serial output.
#define GRIDOPOLY_WIFI_UDP_SSID "gridopoly"
#define GRIDOPOLY_WIFI_UDP_PASSWORD "replace-with-the-local-ap-password"
#define GRIDOPOLY_WIFI_UDP_CHANNEL 1
#define GRIDOPOLY_WIFI_UDP_PSK "replace-with-at-least-16-random-characters"

// Optional ESP-NOW fallback credential.
#define GRIDOPOLY_ESPNOW_TEST_PSK "replace-with-at-least-16-random-characters"
