#pragma once

// Copy to display.local.h only for a specific physically validated module.
// The unconfigured/default build stays at 8 MHz. 40 MHz passed the image and
// page-transition check on tile-288485ba9fe8 with internal-RAM row transfers;
// it exceeds the generic ST7789V datasheet guarantee. Revalidate other boards.
#ifndef GRIDOPOLY_TILE_DISPLAY_SPI_HZ
#define GRIDOPOLY_TILE_DISPLAY_SPI_HZ 8000000
#endif
