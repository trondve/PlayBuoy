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
    int sleepMinutes = determineSleepDuration(pct);
    rtcState.lastBatteryVoltage = voltage; // persist for next boot's hysteresis
    // Compute next wake epoch from current RTC time
    uint32_t now = (uint32_t)time(NULL);
    uint32_t candidate = (now >= 24 * 3600 ? now : 0) + (uint32_t)sleepMinutes * 60UL;
    uint32_t nextWake = adjustNextWakeUtcForQuietHours(candidate);
    rtcState.lastSleepMinutes = (uint32_t)sleepMinutes;
    rtcState.lastNextWakeUtc = nextWake;
    // Print local next wake for convenience
    if (rtcState.lastNextWakeUtc >= 24 * 3600) {
      struct tm tm_local;
      time_t t = (time_t)rtcState.lastNextWakeUtc;
      localtime_r(&t, &tm_local);
      char whenBuf[32];
      strftime(whenBuf, sizeof(whenBuf), "%d/%m/%y - %H:%M", &tm_local);
      if (sleepMinutes >= 60)
        SerialMon.printf("Sleeping for %dh %dm...\n", sleepMinutes / 60, sleepMinutes % 60);
      else
        SerialMon.printf("Sleeping for %d minute(s)...\n", sleepMinutes);
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
  // Samsung INR18650-35E (3500mAh energy cell) OCV table, 101 points (0-100%).
  // Target cell: LiitoKala Lii-35S which uses the 35E core.
  // Adjusted ~20mV downward from 25°C datasheet values for typical 5-15°C
  // operating temperature (Norwegian lake water). The 35E has a higher and
  // flatter plateau than the 25R/30Q power cells previously used.
  //
  // Key reference points (at ~10°C):
  //   0% = 2.950V (conservative floor, real cutoff is 2.50V)
  //   10% = 3.350V    25% = 3.555V
  //   50% = 3.720V    75% = 4.002V
  //   90% = 4.153V   100% = 4.200V
  //   3.70V = ~47% (voltage guard catches well before 25% SoC guard)
  //
  // Flat plateau region: 3.55-3.75V covers roughly 20-55% SoC.
  // Small voltage changes in this region = large SoC swings — this is normal
  // for high-capacity energy cells. The binary search + interpolation gives
  // sub-1% resolution despite the flat curve.
  //
  // Source: Samsung INR18650-35E datasheet discharge curves (0.2C, 0.5C)
  // with cold-temperature offset derived from published -10°C/0°C/25°C data.
  static const float ocvByPercent[101] = {
    2.950f, 3.020f, 3.080f, 3.130f, 3.175f, 3.215f, 3.250f, 3.280f, 3.305f, 3.330f,  //  0-9%
    3.350f, 3.370f, 3.388f, 3.405f, 3.420f, 3.435f, 3.450f, 3.464f, 3.478f, 3.490f,  // 10-19%
    3.502f, 3.514f, 3.525f, 3.535f, 3.545f, 3.555f, 3.564f, 3.573f, 3.581f, 3.589f,  // 20-29%
    3.596f, 3.603f, 3.610f, 3.616f, 3.622f, 3.628f, 3.634f, 3.640f, 3.646f, 3.652f,  // 30-39%
    3.658f, 3.664f, 3.670f, 3.676f, 3.682f, 3.688f, 3.694f, 3.700f, 3.706f, 3.713f,  // 40-49%
    3.720f, 3.727f, 3.734f, 3.742f, 3.750f, 3.758f, 3.767f, 3.776f, 3.786f, 3.796f,  // 50-59%
    3.806f, 3.817f, 3.828f, 3.840f, 3.852f, 3.864f, 3.877f, 3.890f, 3.904f, 3.918f,  // 60-69%
    3.932f, 3.946f, 3.960f, 3.974f, 3.988f, 4.002f, 4.016f, 4.030f, 4.044f, 4.058f,  // 70-79%
    4.071f, 4.082f, 4.092f, 4.101f, 4.110f, 4.118f, 4.126f, 4.133f, 4.140f, 4.147f,  // 80-89%
    4.153f, 4.159f, 4.164f, 4.169f, 4.174f, 4.179f, 4.184f, 4.189f, 4.194f, 4.197f,  // 90-99%
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
// fastPath: skip retry loop when NTP hasn't run (brownout recovery)
int getCurrentMonth(bool fastPath) {
  time_t now;
  struct tm timeinfo;

  time(&now);

  if (!fastPath) {
    // Wait for time to be set (this can take a few seconds after configTime)
    int retry = 0;
    while (now < 24 * 3600 && retry < 10) {
      delay(1000);
      time(&now);
      retry++;
    }
    SerialMon.printf("RTC time check: now=%lu, retry=%d\n", now, retry);
  } else {
    SerialMon.printf("RTC time check (fast): now=%lu\n", now);
  }

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

// Three-season model: winter (Nov-Mar), shoulder (Apr-May, Sep-Oct), summer (Jun-Aug)
enum Season { SEASON_WINTER, SEASON_SHOULDER, SEASON_SUMMER };

static Season getSeason(int month) {
  // Winter: November through March (darkest months at 59°N)
  if (month >= 11 || month <= 3) return SEASON_WINTER;
  // Shoulder: April-May (spring transition) and September-October (autumn transition)
  if (month == 4 || month == 5 || month == 9 || month == 10) return SEASON_SHOULDER;
  // Summer: June-August (peak solar at 59°N)
  return SEASON_SUMMER;
}

static const char* seasonName(Season s) {
  switch (s) {
    case SEASON_WINTER:   return "winter";
    case SEASON_SHOULDER: return "shoulder";
    case SEASON_SUMMER:   return "summer";
  }
  return "unknown";
}

int determineSleepDuration(int batteryPercent, bool fastPath) {
  int month = getCurrentMonth(fastPath); // 1=Jan, ..., 12=Dec
  Season season = getSeason(month);

  // SoC hysteresis: offset batteryPercent by ±2% based on voltage trend
  // to prevent schedule oscillation when sitting near a threshold boundary.
  // Uses the already-persisted lastBatteryVoltage from previous cycle.
  float currentV = getStableBatteryVoltage();
  float prevV = rtcState.lastBatteryVoltage;
  if (prevV > 0.1f && fabsf(currentV - prevV) > 0.005f) {
    // Voltage rising → bias SoC up (stay in shorter sleep)
    // Voltage falling → bias SoC down (stay in longer sleep)
    int hysteresis = (currentV > prevV) ? 2 : -2;
    batteryPercent += hysteresis;
    if (batteryPercent < 0) batteryPercent = 0;
    if (batteryPercent > 100) batteryPercent = 100;
    SerialMon.printf("SoC hysteresis: V %.3f→%.3f, offset %+d%%, effective SoC %d%%\n",
                     prevV, currentV, hysteresis, batteryPercent);
  }

  // Get timezone info for debug output
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  bool isDST = timeinfo.tm_isdst > 0;
  const char* tzName = isDST ? "CEST" : "CET";

  SerialMon.printf("Sleep calculation: month=%d (%s), battery=%d%%, timezone=%s\n",
                   month, seasonName(season), batteryPercent, tzName);

  // Sleep schedule designed around 18650 lithium-ion battery health:
  // - Optimal storage range: 40-60% (preserve this range when possible)
  // - Daily charge should not exceed 80% (discharge actively above 80%)
  // - Never discharge below 20% to prevent battery damage
  // - Critical guard at 25% provides safety margin for aged cells
  //
  // Returns minutes for finer-grained control.
  // Portable: works in sunny southern Europe AND far-north long winters.

  if (season == SEASON_WINTER) {
    // Winter: minimal solar harvest, conserve battery.
    if (batteryPercent > 80) return 720;          // 12 hours — discharge toward healthy range
    if (batteryPercent > 70) return 1440;         // 24 hours
    if (batteryPercent > 60) return 1440;         // 24 hours — still some margin
    if (batteryPercent > 50) return 2880;         // 2 days
    if (batteryPercent > 40) return 4320;         // 3 days — entering optimal storage range
    if (batteryPercent > 35) return 10080;        // 1 week
    if (batteryPercent > 30) return 20160;        // 2 weeks
    if (batteryPercent > 25) return 43200;        // 1 month
    return 129600;                                 // 3 months — near critical, hibernate
  }

  if (season == SEASON_SHOULDER) {
    // Shoulder: intermediate schedule between winter and summer.
    // Solar is available but weak/inconsistent (Apr-May, Sep-Oct at 59°N).
    if (batteryPercent > 80) return 360;          // 6 hours — some discharge, moderate reporting
    if (batteryPercent > 70) return 540;          // 9 hours
    if (batteryPercent > 60) return 720;          // 12 hours
    if (batteryPercent > 50) return 1080;         // 18 hours
    if (batteryPercent > 40) return 1440;         // 24 hours
    if (batteryPercent > 35) return 2880;         // 2 days
    if (batteryPercent > 30) return 4320;         // 3 days
    if (batteryPercent > 25) return 10080;        // 1 week
    return 20160;                                  // 2 weeks — conserve, solar should recover
  }

  // Summer: solar harvest available, more frequent reporting.
  if (batteryPercent > 80) {
    SerialMon.printf("Summer mode: battery >80%%, sleeping 2 hours (discharge toward healthy range)\n");
    return 120;                                    // 2 hours — actively discharge + frequent temp updates
  }
  if (batteryPercent > 70) return 180;           // 3 hours — good solar, capture temp changes
  if (batteryPercent > 60) return 360;           // 6 hours — sustainable equilibrium
  if (batteryPercent > 50) return 540;           // 9 hours — in optimal storage range
  if (batteryPercent > 40) return 720;           // 12 hours — bottom of optimal range, conserve
  if (batteryPercent > 35) return 1440;          // 24 hours — below optimal, needs recharge
  if (batteryPercent > 30) return 2880;          // 2 days
  if (batteryPercent > 25) return 4320;          // 3 days
  return 10080;                                    // 1 week — near critical, let solar recover
}

// Function to log battery voltage and estimated percentage
void logBatteryStatus() {
  float voltage = getStableBatteryVoltage(); 
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.2f V, approx %d%%\n", voltage, percent);
}
// This function logs the current battery voltage and estimated percentage
// It can be called periodically to monitor battery health