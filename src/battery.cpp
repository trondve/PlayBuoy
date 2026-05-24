#include "battery.h"
#include "power.h"
#include "config.h"
#include "rtc_state.h"
#include "utils.h"
#include <time.h>

// Power helpers are declared in power.h

#define SerialMon Serial

#define CHARGE_THRESHOLD       3.7f
#define CHARGE_HYSTERESIS      0.03f

// Thresholds are configured in config.h
#ifndef BATTERY_CRITICAL_VOLTAGE
#define BATTERY_CRITICAL_VOLTAGE 3.70f
#endif

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
    // fastPath=true: NTP hasn't run yet — skip 10s RTC retry loop, fall back to January (winter-safe)
    int sleepMinutes = determineSleepDuration(pct, true);
    rtcState.lastBatteryVoltage = voltage; // persist for next boot's hysteresis
    // Compute next wake epoch from current RTC time
    uint32_t now = (uint32_t)time(NULL);
    uint32_t candidate = (now >= SECONDS_PER_DAY ? now : 0) + (uint32_t)sleepMinutes * 60UL;
    uint32_t nextWake = adjustNextWakeUtcForQuietHours(candidate);
    rtcState.lastSleepMinutes = (uint32_t)sleepMinutes;
    rtcState.lastNextWakeUtc = nextWake;
    // Print local next wake for convenience
    if (rtcState.lastNextWakeUtc >= SECONDS_PER_DAY) {
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
#if DEBUG_NO_DEEP_SLEEP
    SerialMon.println("⚠ DEBUG_NO_DEEP_SLEEP: skipping critical-guard sleep, continuing cycle.");
    return false;
#else
    esp_deep_sleep_start();
    return true; // not reached
#endif
  }
#endif
  return false;
}

int estimateBatteryPercent(float voltage) {
  // Samsung INR18650-35E (3500mAh energy cell) OCV table, 101 points (0-100%).
  // Target cell: LiitoKala Lii-35S which uses the 35E core.
  // Table is at 25°C reference (standard Samsung 35E datasheet values).
  // Runtime temperature correction below handles cold-temperature OCV depression.
  // The 35E has a higher and flatter plateau than the 25R/30Q power cells previously used.
  //
  // Key reference points (at 25°C):
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

  // Temperature compensation: OCV shifts ~1.5mV per °C between -20°C and +25°C.
  // OCV is depressed at cold temps relative to the 25°C table; correct upward so
  // the lookup returns accurate SoC. Correction fades to zero at 25°C (reference).
  // Threshold < 25°C avoids a discontinuity: at exactly 10°C the old threshold
  // caused a 22.5mV step that could cross dump-mode boundaries spuriously.
  float voltageTempCorrected = voltage;
  if (!isnan(rtcState.lastWaterTemp)) {
    float tempC = rtcState.lastWaterTemp;
    if (tempC < 25.0f) {
      voltageTempCorrected = voltage + 0.0015f * (25.0f - tempC);
    }
  }

  if (voltageTempCorrected <= ocvByPercent[0]) return 0;
  if (voltageTempCorrected >= ocvByPercent[100]) return 100;

  int lo = 0, hi = 100;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (ocvByPercent[mid] <= voltageTempCorrected) lo = mid; else hi = mid;
  }
  float vLo = ocvByPercent[lo];
  float vHi = ocvByPercent[hi];
  // Raise epsilon from 1e-6 to 1e-3 (1 mV): prevents div-by-near-zero in tight voltage bands
  float t = (vHi - vLo) > 1e-3f ? (voltageTempCorrected - vLo) / (vHi - vLo) : 0.0f;
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
    while (now < SECONDS_PER_DAY && retry < 10) {
      delay(1000);
      time(&now);
      retry++;
    }
    SerialMon.printf("RTC time check: now=%lu, retry=%d\n", now, retry);
  } else {
    SerialMon.printf("RTC time check (fast): now=%lu\n", now);
  }

  if (now > SECONDS_PER_DAY) {  // Valid time (more than 24 hours since epoch)
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

struct SleepAnchor { int soc; int minutes; };

static const SleepAnchor SUMMER_ANCHORS[] = {
  {100,    120},  // 2h  — actively discharge; frequent reports while solar abundant
  { 80,    180},  // 3h
  { 70,    240},  // 4h
  { 60,    360},  // 6h
  { 50,    720},  // 12h — entering optimal storage range
  { 40,   4320},  // 3d  — more conservative below 50%
  { 30,  10080},  // 1w
  { 25,  20160},  // 2w  — near critical; hibernate and let solar recover
  {  0,  40320},  // 4w
};

static const SleepAnchor SHOULDER_ANCHORS[] = {
  {100,   2880},  // 2d
  { 80,   2880},  // 2d  — flat plateau; solar inconsistent at 59°N
  { 60,   5040},  // 3.5d
  { 50,  10080},  // 1w
  { 40,  20160},  // 2w
  { 35,  30240},  // 3w
  { 30,  43200},  // 1mo
  { 25,  86400},  // 2mo
  {  0, 129600},  // 3mo
};

static const SleepAnchor WINTER_ANCHORS[] = {
  {100,   2880},  // 2d
  { 90,   2880},  // 2d  — flat plateau; minimal solar at 59°N Nov-Mar
  { 80,   5760},  // 4d
  { 70,   7200},  // 5d
  { 60,  10080},  // 1w
  { 50,  15120},  // 1.5w
  { 40,  20160},  // 2w
  { 35,  30240},  // 3w
  { 30,  43200},  // 1mo
  { 25,  86400},  // 2mo
  {  0, 129600},  // 3mo — hibernate; critical guard at 25% handles true emergency
};

static int interpolateSleep(int soc, const SleepAnchor* a, size_t n) {
  if (soc >= a[0].soc) return a[0].minutes;
  if (soc <= a[n - 1].soc) return a[n - 1].minutes;
  for (size_t i = 0; i < n - 1; i++) {
    int socHi = a[i].soc,     socLo = a[i + 1].soc;
    int minHi = a[i].minutes, minLo = a[i + 1].minutes;
    if (soc <= socHi && soc >= socLo) {
      return minHi + (minLo - minHi) * (socHi - soc) / (socHi - socLo);
    }
  }
  return 1440;
}

DumpMode getDumpMode(int rawSoc) {
  if (rawSoc >= 95) return DUMP_TIER4;
  if (rawSoc >= 90) return DUMP_TIER3;
  if (rawSoc >= 85) return DUMP_TIER2;
  if (rawSoc >= 75) return DUMP_TIER1;
  return DUMP_NONE;
}

int determineSleepDuration(int batteryPercent, bool fastPath) {
  int month = getCurrentMonth(fastPath); // 1=Jan, ..., 12=Dec
  Season season = getSeason(month);

  // Dump mode check — must run on raw SoC before hysteresis skews the value.
  // Returns immediately with the dump interval, bypassing the seasonal schedule.
  DumpMode dump = getDumpMode(batteryPercent);
  if (dump != DUMP_NONE) {
    int sleepMin;
    switch (dump) {
      case DUMP_TIER1: sleepMin = (season == SEASON_SUMMER) ? 60 : 360; break;
      case DUMP_TIER2: sleepMin = 60;  break;
      case DUMP_TIER3: sleepMin = 30;  break;
      default:         sleepMin = 15;  break;  // DUMP_TIER4 (≥95%)
    }
    SerialMon.printf("HIGH BATTERY DUMP MODE TIER%d: SoC=%d%%, sleep=%d min (%s season)\n",
                     (int)dump, batteryPercent, sleepMin, seasonName(season));
    return sleepMin;
  }

  // SoC hysteresis: offset batteryPercent by ±2% based on voltage trend
  // to prevent schedule oscillation when sitting near a threshold boundary.
  // Uses the already-persisted lastBatteryVoltage from previous cycle.
  float currentV = getStableBatteryVoltage();
  float prevV = rtcState.lastBatteryVoltage;
  if (prevV > 0.1f && fabsf(currentV - prevV) > 0.005f) {
    // Voltage rising → bias SoC up (stay in shorter sleep)
    // Voltage falling → bias SoC down (stay in longer sleep)
    // Widened to ±5% (from ±2%) to prevent oscillation in 25-50% SoC plateau
    int hysteresis = (currentV > prevV) ? 5 : -5;
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

  const SleepAnchor* anchors;
  size_t n;
  switch (season) {
    case SEASON_WINTER:   anchors = WINTER_ANCHORS;   n = sizeof(WINTER_ANCHORS)   / sizeof(WINTER_ANCHORS[0]);   break;
    case SEASON_SHOULDER: anchors = SHOULDER_ANCHORS; n = sizeof(SHOULDER_ANCHORS) / sizeof(SHOULDER_ANCHORS[0]); break;
    default:              anchors = SUMMER_ANCHORS;   n = sizeof(SUMMER_ANCHORS)   / sizeof(SUMMER_ANCHORS[0]);   break;
  }
  return interpolateSleep(batteryPercent, anchors, n);
}

// Function to log battery voltage and estimated percentage
void logBatteryStatus() {
  float voltage = getStableBatteryVoltage();
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.3f V, approx %d%%\n", voltage, percent);
  // 3.70–3.85V is the at-risk zone: OCV looks safe but under 2A modem load the voltage
  // can sag 150–300mV, pushing VBAT below the SIM7000G minimum (3.55V) and causing
  // registration failure. 3.70V is the critical cutoff; warn above it up to 3.85V.
  if (voltage > BATTERY_CRITICAL_VOLTAGE && voltage <= 3.85f) {
    SerialMon.printf("⚠ MARGINAL VOLTAGE: %.3fV — SIM7000G may fail to register under 2A modem load\n",
                     voltage);
  }
}