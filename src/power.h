#pragma once
#include <Arduino.h>

bool beginPowerMonitor();
float readBatteryVoltage();

// Expose power control helpers for other modules (defined in main.cpp)
void powerOff3V3Rail();
void powerOffModem();
void preparePinsAndSubsystemsForDeepSleep();

// Quiet hours helper: adjust candidate next wake UTC to avoid 00:00-05:59 local
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);