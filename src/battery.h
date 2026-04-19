#pragma once
#include <Arduino.h>

void checkBatteryChargeState();
bool handleUndervoltageProtection();
int estimateBatteryPercent(float voltage);
int determineSleepDuration(int batteryPercent, bool fastPath = false);
void logBatteryStatus();
void setStableBatteryVoltage(float voltage);  // Set stable battery voltage measured at startup
float getStableBatteryVoltage();  // Get battery voltage measured at startup

// Power controls and sleep helpers provided by the main module (main.cpp)
void powerOff3V3Rail();
void powerOffModem();
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);
void preparePinsAndSubsystemsForDeepSleep();