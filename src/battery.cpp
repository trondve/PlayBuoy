#include "battery.h"
#include "power.h"
#include "config.h"
#include "rtc_state.h"
#include <time.h>

// Power helpers are declared in power.h

#define SerialMon Serial

#define CHARGE_THRESHOLD       3.7f
#define CHARGE_HYSTERESIS      0.03f

// Thresholds are configured in config.h
#ifndef BATTERY_CRITICAL_VOLTAGE
#define BATTERY_CRITICAL_VOLTAGE 3.70f
#endif
#define BATTERY_UNDERVOLTAGE_SLEEP_HOURS 168

static bool isCharging = false;
static float stableBatteryVoltage = 0.0f;  // Store stable battery voltage measured at startup

void setStableBatteryVoltage(float voltage) {
  stableBatteryVoltage = voltage;
}

float getStableBatteryVoltage() {
  return stableBatteryVoltage;
}

void checkBatteryChargeState() {
  float voltage = getStableBatteryVoltage();  // Use stable voltage instead of measuring

  if (!isCharging && voltage > (CHARGE_THRESHOLD + CHARGE_HYSTERESIS)) {
    isCharging = true;
    rtcState.chargingProblemDetected = false;
  } else if (isCharging && voltage < (CHARGE_THRESHOLD - CHARGE_HYSTERESIS)) {
    isCharging = false;
    SerialMon.println("Charging lost.");
    // Start timer or flag for no charge if persists
  }
}

bool handleUndervoltageProtection() {
  float voltage = getStableBatteryVoltage();  // Use stable voltage instead of measuring

  // Undervoltage protection (compile-time optional)
#if ENABLE_CRITICAL_GUARD
  int pct = estimateBatteryPercent(voltage);
  if (pct <= BATTERY_CRITICAL_PERCENT || voltage <= BATTERY_CRITICAL_VOLTAGE) {
    SerialMon.println("CRITICAL: Battery too low — entering deep sleep.");
    SerialMon.printf("Current: %d%% / %.3f V (<= %d%% or <= %.3f V)\n",
                     pct, voltage, BATTERY_CRITICAL_PERCENT, BATTERY_CRITICAL_VOLTAGE);
    // Decide a conservative sleep window using existing policy
    int sleepHours = determineSleepDuration(pct);
    // Compute next wake epoch from current RTC time
    uint32_t now = (uint32_t)time(NULL);
    uint32_t candidate = (now >= 24 * 3600 ? now : 0) + (uint32_t)sleepHours * 3600UL;
    uint32_t nextWake = adjustNextWakeUtcForQuietHours(candidate);
    rtcState.lastSleepHours = (uint16_t)sleepHours;
    rtcState.lastNextWakeUtc = nextWake;
    // Print local next wake for convenience
    if (rtcState.lastNextWakeUtc >= 24 * 3600) {
      struct tm tm_local;
      time_t t = (time_t)rtcState.lastNextWakeUtc;
      localtime_r(&t, &tm_local);
      char whenBuf[32];
      strftime(whenBuf, sizeof(whenBuf), "%d/%m/%y - %H:%M", &tm_local);
      SerialMon.printf("Sleeping for %d hour(s)...\n", sleepHours);
      SerialMon.printf("Next wake (Europe/Oslo): %s\n", whenBuf);
    }
    // Ensure rails and modem are off
    powerOff3V3Rail();
    powerOffModem();
    // Configure pins/subsystems for minimum leakage
    preparePinsAndSubsystemsForDeepSleep();
    // Sleep for the exact number of seconds until nextWake (minimum 5 minutes)
    uint32_t sleepSec = 300;
    now = (uint32_t)time(NULL);
    if (nextWake > now) sleepSec = nextWake - now;
    if (sleepSec < 300) sleepSec = 300; // enforce minimum sleep floor
    esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
    esp_deep_sleep_start();
    return true; // not reached
  }
#endif
  return false;
}

int estimateBatteryPercent(float voltage) {
  // User-provided OCV table: voltage at each integer percent 0..100
  static const float ocvByPercent[101] = {
    3.000f, 3.081f, 3.161f, 3.242f, 3.322f, 3.403f, 3.423f, 3.443f, 3.463f, 3.483f,
    3.503f, 3.519f, 3.535f, 3.551f, 3.567f, 3.583f, 3.593f, 3.603f, 3.613f, 3.623f,
    3.633f, 3.641f, 3.649f, 3.657f, 3.665f, 3.673f, 3.679f, 3.685f, 3.691f, 3.697f,
    3.703f, 3.709f, 3.715f, 3.721f, 3.727f, 3.733f, 3.737f, 3.741f, 3.745f, 3.749f,
    3.753f, 3.759f, 3.765f, 3.771f, 3.777f, 3.783f, 3.787f, 3.791f, 3.795f, 3.799f,
    3.803f, 3.807f, 3.811f, 3.815f, 3.819f, 3.823f, 3.829f, 3.835f, 3.841f, 3.847f,
    3.853f, 3.859f, 3.865f, 3.871f, 3.877f, 3.883f, 3.889f, 3.895f, 3.901f, 3.907f,
    3.913f, 3.921f, 3.929f, 3.937f, 3.945f, 3.953f, 3.959f, 3.965f, 3.971f, 3.977f,
    3.983f, 3.995f, 4.007f, 4.019f, 4.031f, 4.043f, 4.055f, 4.067f, 4.079f, 4.091f,
    4.103f, 4.119f, 4.136f, 4.153f, 4.168f, 4.183f, 4.186f, 4.190f, 4.193f, 4.197f,
    4.200f
  };

  if (voltage <= ocvByPercent[0]) return 0;
  if (voltage >= ocvByPercent[100]) return 100;

  int lo = 0, hi = 100;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (ocvByPercent[mid] <= voltage) lo = mid; else hi = mid;
  }
  float vLo = ocvByPercent[lo];
  float vHi = ocvByPercent[hi];
  float t = (vHi - vLo) > 1e-6f ? (voltage - vLo) / (vHi - vLo) : 0.0f;
  int pct = (int)roundf(lo + t * (hi - lo));
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}


//  RTC logic
int getCurrentMonth() {
  // Get current time from ESP32 RTC
  time_t now;
  struct tm timeinfo;
  
  // Wait for time to be set (this can take a few seconds after configTime)
  int retry = 0;
  do {
    time(&now);
    retry++;
    if (now < 24 * 3600) {  // If time is less than 24 hours since epoch
      delay(1000);
    }
  } while (now < 24 * 3600 && retry < 10);
  
  SerialMon.printf("RTC time check: now=%lu, retry=%d\n", now, retry);
  
  if (now > 24 * 3600) {  // Valid time (more than 24 hours since epoch)
    localtime_r(&now, &timeinfo);
    int month = timeinfo.tm_mon + 1;  // tm_mon is 0-11, we want 1-12
    bool isDST = timeinfo.tm_isdst > 0;
    SerialMon.printf("RTC month: %d, DST: %s (from epoch %lu)\n", month, isDST ? "YES" : "NO", now);
    return month;
  } else {
    // Fallback: assume current month (August 2025)
    SerialMon.println("RTC not valid, using fallback month: 8 (August)");
    return 8;  // August
  }
}

// Helper function to determine if current month is between October and April (inclusive)
bool isWinterSeason(int month) {
  return (month >= 10 || month <= 4);
}

int determineSleepDuration(int batteryPercent) {
  int month = getCurrentMonth(); // 1=Jan, ..., 12=Dec

  // Get timezone info for debug output
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  bool isDST = timeinfo.tm_isdst > 0;
  const char* tzName = isDST ? "CEST" : "CET";

  SerialMon.printf("Sleep calculation: month=%d, battery=%d%%, timezone=%s\n",
                   month, batteryPercent, tzName);

  // Sleep schedule designed around 18650 lithium-ion battery health:
  // - Optimal storage range: 40-60% (preserve this range when possible)
  // - Daily charge should not exceed 80% (discharge actively above 80%)
  // - Never discharge below 20% to prevent battery damage
  // - Critical guard at 25% provides safety margin for aged cells
  //
  // Portable: works in sunny southern Europe AND far-north long winters.
  // Water temperature can change significantly in 3-6 hours in peak summer.

  if (isWinterSeason(month)) {
    SerialMon.printf("Winter season detected (month %d)\n", month);
    // Winter: minimal solar harvest, conserve battery.
    // Even above 80% we report every 12h to slowly discharge toward healthy range.
    if (batteryPercent > 80) return 12;         // 12 hours — discharge toward healthy range
    if (batteryPercent > 70) return 24;         // 24 hours
    if (batteryPercent > 60) return 24;         // 24 hours — still some margin
    if (batteryPercent > 50) return 48;         // 2 days
    if (batteryPercent > 40) return 72;         // 3 days — entering optimal storage range
    if (batteryPercent > 35) return 168;        // 1 week
    if (batteryPercent > 30) return 336;        // 2 weeks
    if (batteryPercent > 25) return 720;        // 1 month
    return 2160;                                 // 3 months — near critical, hibernate
  }

  SerialMon.printf("Summer season detected (month %d)\n", month);
  // Summer: solar harvest available, more frequent reporting.
  // Above 80%: report aggressively to discharge toward healthy range AND
  //   capture temperature changes (can shift 2-3C in a few hours).
  // 60-80%: active reporting zone, sustainable with solar.
  // 40-60%: optimal storage — moderate reporting, let solar maintain this range.
  // Below 40%: conservation — battery needs to recharge.
  if (batteryPercent > 80) {
    SerialMon.printf("Summer mode: battery >80%%, sleeping 2 hours (discharge toward healthy range)\n");
    return 2;                                   // 2 hours — actively discharge + frequent temp updates
  }
  if (batteryPercent > 70) return 3;          // 3 hours — good solar, capture temp changes
  if (batteryPercent > 60) return 6;          // 6 hours — sustainable equilibrium for most climates
  if (batteryPercent > 50) return 9;          // 9 hours — in optimal storage range, moderate reporting
  if (batteryPercent > 40) return 12;         // 12 hours — bottom of optimal range, conserve
  if (batteryPercent > 35) return 24;         // 24 hours — below optimal, needs recharge
  if (batteryPercent > 30) return 48;         // 2 days
  if (batteryPercent > 25) return 72;         // 3 days
  return 168;                                  // 1 week — near critical, let solar recover
}

// Function to log battery voltage and estimated percentage
void logBatteryStatus() {
  float voltage = getStableBatteryVoltage(); 
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.2f V, approx %d%%\n", voltage, percent);
}
// This function logs the current battery voltage and estimated percentage
// It can be called periodically to monitor battery health