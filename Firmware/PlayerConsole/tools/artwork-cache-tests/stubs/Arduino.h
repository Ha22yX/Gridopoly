#pragma once
#include "fakes.h"

// Match the Arduino core API so unqualified-name conflicts fail host compilation.
void randomSeed(unsigned long);
