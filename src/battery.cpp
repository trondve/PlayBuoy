#include "battery.h"
#include "power.h"
#include "config.h"
#include "rtc_state.h"
#include <time.h>

#define SerialMon Serial

#define CHARGE_THRESHOLD       3.7f
#define CHARGE_HYSTERESIS      0.03f

#define BATTERY_CRITICAL_VOLTAGE 3.00f  // Re-enabled but will only print voltage, not sleep
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
#ifdef BATTERY_CRITICAL_VOLTAGE
  if (voltage < BATTERY_CRITICAL_VOLTAGE) {
    SerialMon.println("WARNING: Battery undervoltage detected!");
    SerialMon.printf("Current voltage: %.3f V (threshold: %.2f V)\n", voltage, BATTERY_CRITICAL_VOLTAGE);
    SerialMon.println("Continuing operation - no deep sleep triggered.");
    return true;
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

int getCurrentHour() {
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
  
  if (now > 24 * 3600) {  // Valid time (more than 24 hours since epoch)
    localtime_r(&now, &timeinfo);
    int hour = timeinfo.tm_hour;  // 0-23
    bool isDST = timeinfo.tm_isdst > 0;
    SerialMon.printf("RTC hour: %d, DST: %s (from epoch %lu)\n", hour, isDST ? "YES" : "NO", now);
    return hour;
  } else {
    // Fallback: assume current hour (10 AM)
    SerialMon.println("RTC not valid, using fallback hour: 10");
    return 10;  // 10 AM
  }
}

// Helper function to determine if current month is between October and April (inclusive)
bool isWinterSeason(int month) {
  return (month >= 10 || month <= 4);
}

int determineSleepDuration(int batteryPercent) {
  int month = getCurrentMonth(); // 1=Jan, ..., 12=Dec
  int hour = getCurrentHour();   // 0-23
  
  // Get timezone info for debug output
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  bool isDST = timeinfo.tm_isdst > 0;
  const char* tzName = isDST ? "CEST" : "CET";
  
  SerialMon.printf("Sleep calculation: month=%d, hour=%d, battery=%d%%, timezone=%s\n", 
                   month, hour, batteryPercent, tzName);

  if (isWinterSeason(month)) {
    SerialMon.printf("Winter season detected (month %d)\n", month);
    
    // More conservative winter strategy as requested
    if (batteryPercent >= 70) {
      // Wake up once daily at noon
      int hoursToNoon = 12 - hour;
      if (hoursToNoon <= 0) {
        hoursToNoon += 24;  // Next day at noon
      }
      SerialMon.printf("Winter mode: battery %d%% (>=70%%), waking daily at noon, sleeping %d hours\n", batteryPercent, hoursToNoon);
      return hoursToNoon;
    } else if (batteryPercent >= 60) {
      SerialMon.printf("Winter mode: battery %d%% (60-69%%), sleeping 48 hours (2 days)\n", batteryPercent);
      return 48;   // 2 days
    } else if (batteryPercent >= 50) {
      SerialMon.printf("Winter mode: battery %d%% (50-59%%), sleeping 168 hours (7 days)\n", batteryPercent);
      return 168;  // 7 days
    } else if (batteryPercent >= 40) {
      SerialMon.printf("Winter mode: battery %d%% (40-49%%), sleeping 336 hours (14 days)\n", batteryPercent);
      return 336;  // 14 days
    } else if (batteryPercent >= 30) {
      SerialMon.printf("Winter mode: battery %d%% (30-39%%), sleeping 720 hours (30 days)\n", batteryPercent);
      return 720;  // 30 days
    } else if (batteryPercent >= 20) {
      SerialMon.printf("Winter mode: battery %d%% (20-29%%), sleeping 1440 hours (60 days)\n", batteryPercent);
      return 1440; // 60 days
    } else {
      SerialMon.printf("Winter mode: battery %d%% (<20%%), sleeping 2160 hours (90 days)\n", batteryPercent);
      return 2160; // 90 days (hibernate-like)
    }
  }

  SerialMon.printf("Summer season detected (month %d)\n", month);
  // May–September: use battery-based logic
  if (batteryPercent > 80) {
    SerialMon.printf("Summer mode: battery >80%%, sleeping 3 hours\n");
    return 3;          // minimum 3 hours
  }
  if (batteryPercent > 70) return 6;          // 6 hours
  if (batteryPercent > 60) return 12;         // 12 hours
  if (batteryPercent > 50) return 24;         // 24 hours  
  if (batteryPercent > 40) return 48;         // 2 day (48 hours)
  if (batteryPercent > 30) return 168;        // 7 days (168 hours)
  if (batteryPercent > 20) return 720;        // 1 month (720 hours)
  if (batteryPercent > 15) return 1460;       // 2 month (1460 hours)
  if (batteryPercent > 10) return 2180;       // 3 month (2180 hours)
  return 2;                               
}

// Function to log battery voltage and estimated percentage
void logBatteryStatus() {
  float voltage = getStableBatteryVoltage(); 
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.2f V, approx %d%%\n", voltage, percent);
}
// This function logs the current battery voltage and estimated percentage
// It can be called periodically to monitor battery health