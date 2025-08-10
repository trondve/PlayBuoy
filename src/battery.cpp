#include "battery.h"
#include "power.h"
#include "rtc_state.h"
#include <time.h>

#define SerialMon Serial

#define CHARGE_THRESHOLD       3.7f
#define CHARGE_HYSTERESIS      0.03f

#define BATTERY_CRITICAL_VOLTAGE 3.00f
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
    SerialMon.println("Charging detected.");
  } else if (isCharging && voltage < (CHARGE_THRESHOLD - CHARGE_HYSTERESIS)) {
    isCharging = false;
    SerialMon.println("Charging lost.");
    // Start timer or flag for no charge if persists
  }
}

bool handleUndervoltageProtection() {
  float voltage = getStableBatteryVoltage();  // Use stable voltage instead of measuring
  SerialMon.printf("Battery voltage: %.2f V\n", voltage);

  if (voltage < BATTERY_CRITICAL_VOLTAGE) {
    SerialMon.println("Battery undervoltage detected, entering long deep sleep.");
    delay(100);
    esp_sleep_enable_timer_wakeup((uint64_t)BATTERY_UNDERVOLTAGE_SLEEP_HOURS * 3600ULL * 1000000ULL);
    esp_deep_sleep_start();
    return true;
  }
  return false;
}

int estimateBatteryPercent(float voltage) {
  if (voltage >= 4.2f) return 100;
  if (voltage <= 3.0f) return 0;
  return (int)(((voltage - 3.0f) / (4.2f - 3.0f)) * 100);
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
    
    // Winter battery protection strategy
    if (batteryPercent >= 35) {
      // Wake up once daily at noon (battery 35% - 100%)
      int hoursToNoon = 12 - hour;
      if (hoursToNoon <= 0) {
        hoursToNoon += 24;  // Next day at noon
      }
      SerialMon.printf("Winter mode: battery %d%% (35-100%%), waking daily at noon, sleeping %d hours\n", batteryPercent, hoursToNoon);
      return hoursToNoon;
    } else if (batteryPercent >= 30) {
      // Wake up every 2 days
      SerialMon.printf("Winter mode: battery %d%% (30-34%%), sleeping 48 hours (2 days)\n", batteryPercent);
      return 48;
    } else if (batteryPercent >= 25) {
      // Wake up every 3 days
      SerialMon.printf("Winter mode: battery %d%% (25-29%%), sleeping 72 hours (3 days)\n", batteryPercent);
      return 72;
    } else if (batteryPercent >= 20) {
      // Wake up every 7 days
      SerialMon.printf("Winter mode: battery %d%% (20-24%%), sleeping 168 hours (7 days)\n", batteryPercent);
      return 168;
    } else if (batteryPercent >= 15) {
      // Wake up every 14 days
      SerialMon.printf("Winter mode: battery %d%% (15-19%%), sleeping 336 hours (14 days)\n", batteryPercent);
      return 336;
    } else if (batteryPercent >= 10) {
      // Wake up every 30 days
      SerialMon.printf("Winter mode: battery %d%% (10-14%%), sleeping 720 hours (30 days)\n", batteryPercent);
      return 720;
    } else {
      // Wake up every 60 days (battery below 10%)
      SerialMon.printf("Winter mode: battery %d%% (<10%%), sleeping 1440 hours (60 days)\n", batteryPercent);
      return 1440;
    }
  }

  SerialMon.printf("Summer season detected (month %d)\n", month);
  // May–September: use battery-based logic
  if (batteryPercent > 80) {
    SerialMon.printf("Summer mode: battery >80%%, sleeping 3 hours\n");
    return 3;          // minimum 3 hours
  }
  if (batteryPercent > 70) return 6;          // 6 hours
  if (batteryPercent > 60) return 8;          // 8 hours
  if (batteryPercent > 50) return 10;         // 10 hours  
  if (batteryPercent > 40) return 12;         // 12 hours
  if (batteryPercent > 30) return 24;         // 1 day (24 hours)
  if (batteryPercent > 20) return 48;         // 2 days (48 hours)
  if (batteryPercent > 15) return 168;        // 7 days (168 hours)
  if (batteryPercent > 10) return 720;        // 1 month (720 hours)
  return 1460;                                // 2 month (1460 hours)
}

// New function to log battery voltage and estimated percentage
void logBatteryStatus() {
  float voltage = getStableBatteryVoltage(); // Use stable voltage instead of measuring
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.2f V, approx %d%%\n", voltage, percent);
}
// This function logs the current battery voltage and estimated percentage
// It can be called periodically to monitor battery health