#pragma once
#include <Arduino.h>

void checkBatteryChargeState();
bool handleUndervoltageProtection();
int estimateBatteryPercent(float voltage);
int determineSleepDuration(int batteryPercent);
void logBatteryStatus();
void setStableBatteryVoltage(float voltage);  // Set stable battery voltage measured at startup
float getStableBatteryVoltage();  // Get battery voltage measured at startup

// Power controls provided by the main power module
void powerOff3V3Rail();
void powerOffModem();