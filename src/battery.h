#pragma once
#include <Arduino.h>

void checkBatteryChargeState();
bool handleUndervoltageProtection();
int estimateBatteryPercent(float voltage);
int determineSleepDuration(int batteryPercent);
void logBatteryStatus();
