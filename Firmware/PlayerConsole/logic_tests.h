#pragma once

#include <Arduino.h>

bool runPureLogicTests(Stream &out);
bool runLvglComponentTests(Stream &out);
bool runLogicTests(Stream &out);
void resetLogicTestFailure();
const char *firstLogicTestFailure();
