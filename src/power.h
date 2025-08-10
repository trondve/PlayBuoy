#pragma once
#include <Arduino.h>

bool beginPowerMonitor();
float readBatteryVoltage();
float readBatteryCurrent();
void calibrateBatteryVoltage(float actualVoltage);
