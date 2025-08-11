#pragma once
#include <Arduino.h>

bool beginPowerMonitor();
float readBatteryVoltage();
void calibrateBatteryVoltage(float actualVoltage);
// Single quiet ADC sample converted to battery voltage (no logging). Returns NAN if invalid
float readBatteryVoltageSample();
