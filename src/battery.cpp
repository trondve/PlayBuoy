#include "battery.h"
#include "power.h"
#include "rtc_state.h"

#define SerialMon Serial

#define CHARGE_THRESHOLD       3.7f
#define CHARGE_HYSTERESIS      0.03f

#define BATTERY_CRITICAL_VOLTAGE 3.00f
#define BATTERY_UNDERVOLTAGE_SLEEP_HOURS 168

static bool isCharging = false;

void checkBatteryChargeState() {
  float voltage = readBatteryVoltage();

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
  float voltage = readBatteryVoltage();
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

// Stub implementations for current month and hour.
// Replace with actual RTC logic as needed.
int getCurrentMonth() {
  // TODO: Replace with RTC month (1-12)
  return 1;
}

int getCurrentHour() {
  // TODO: Replace with RTC hour (0-23)
  return 0;
}

// Helper function to determine if current month is between October and April (inclusive)
bool isWinterSeason(int month) {
  return (month >= 10 || month <= 4);
}

int determineSleepDuration(int batteryPercent) {
  int month = getCurrentMonth(); // 1=Jan, ..., 12=Dec
  int hour = getCurrentHour();   // 0-23

  if (isWinterSeason(month)) {
    if (batteryPercent > 20) {
      // Wake up at next 12:00 (noon)
      int hoursToNoon = 12 - hour;
      if (hoursToNoon <= 0) {
        hoursToNoon += 24;
      }
      return hoursToNoon;
    }
    // Battery safety mechanism for low battery
    if (batteryPercent > 15) return 168;        // 7 days (168 hours)
    if (batteryPercent > 10) return 720;        // 1 month (720 hours)
    return 1460;                                // 2 month (1460 hours)
  }

  // May–September: use battery-based logic
  if (batteryPercent > 80) return 3;          // minimum 3 hours
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
  float voltage = readBatteryVoltage();
  int percent = estimateBatteryPercent(voltage);
  SerialMon.printf("Battery voltage: %.2f V, approx %d%%\n", voltage, percent);
}
// This function logs the current battery voltage and estimated percentage
// It can be called periodically to monitor battery health