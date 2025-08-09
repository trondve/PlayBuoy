#pragma once
#include <Arduino.h>

// Checks if an OTA update is available and performs it.
// Returns true if an update was attempted and caused a reboot.
bool checkAndPerformOTA(const char* updateUrl);
