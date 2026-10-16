#pragma once

// Heartbeats and artwork must resolve the same ignored local configuration.
#if __has_include("config/secrets.local.h")
#include "config/secrets.local.h"
#elif __has_include("../../PlayerConsole/config/secrets.local.h")
#include "../../PlayerConsole/config/secrets.local.h"
#endif
#ifndef GRIDOPOLY_TILE_HTTP_HOST
#define GRIDOPOLY_TILE_HTTP_HOST "10.42.0.1"
#endif
#ifndef GRIDOPOLY_TILE_HTTP_PORT
#define GRIDOPOLY_TILE_HTTP_PORT 80
#endif
