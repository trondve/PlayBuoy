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
  // Standard 18650 Li-ion OCV table based on typical discharge curves.
  // Key characteristics: steep drop at 0-10%, flat plateau 3.6-3.8V (30-70%),
  // steep rise at 90-100%. At 3.733V this gives ~41% (accurate for standard 18650).
  // Previous table overestimated SoC in 5-60% range vs standard curves.
  // Source: composite of Samsung INR18650-25R/30Q and generic 18650 discharge data.
  static const float ocvByPercent[101] = {
    3.000f, 3.050f, 3.100f, 3.150f, 3.200f, 3.270f, 3.310f, 3.340f, 3.370f, 3.400f,  //  0-9%
    3.430f, 3.455f, 3.475f, 3.495f, 3.510f, 3.525f, 3.540f, 3.555f, 3.568f, 3.580f,  // 10-19%
    3.590f, 3.600f, 3.608f, 3.616f, 3.624f, 3.632f, 3.639f, 3.646f, 3.653f, 3.660f,  // 20-29%
    3.666f, 3.672f, 3.678f, 3.684f, 3.690f, 3.696f, 3.702f, 3.708f, 3.714f, 3.720f,  // 30-39%
    3.726f, 3.732f, 3.738f, 3.744f, 3.750f, 3.756f, 3.762f, 3.768f, 3.774f, 3.780f,  // 40-49%
    3.786f, 3.792f, 3.798f, 3.804f, 3.810f, 3.817f, 3.824f, 3.831f, 3.838f, 3.846f,  // 50-59%
    3.854f, 3.862f, 3.870f, 3.879f, 3.888f, 3.897f, 3.907f, 3.917f, 3.928f, 3.939f,  // 60-69%
    3.950f, 3.962f, 3.974f, 3.987f, 4.000f, 4.013f, 4.027f, 4.041f, 4.055f, 4.070f,  // 70-79%
    4.085f, 4.095f, 4.105f, 4.114f, 4.123f, 4.132f, 4.140f, 4.148f, 4.155f, 4.162f,  // 80-89%
    4.168f, 4.173f, 4.178f, 4.182f, 4.186f, 4.189f, 4.192f, 4.195f, 4.197f, 4.199f,  // 90-99%
    4.200f                                                                               // 100%
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
    // Fallback: assume winter (January) for conservative power management.
    // Using summer schedule without valid time could drain the battery fatally.
    SerialMon.println("RTC not valid, using fallback month: 1 (January/winter-safe)");
    return 1;  // January — triggers winter sleep schedule
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