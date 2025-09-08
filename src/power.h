#pragma once
#include <Arduino.h>

bool beginPowerMonitor();
float readBatteryVoltage();

// Expose power control helpers for other modules (defined in main.cpp)
void powerOff3V3Rail();
void powerOffModem();