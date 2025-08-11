#define TINY_GSM_MODEM_SIM7000

#include "gps.h"
#include <TinyGsmClient.h>
#include <time.h>
#include "esp_task_wdt.h"  // For watchdog timer
#include "battery.h"  // For getStableBatteryVoltage

// Forward declarations for battery functions
float readBatteryVoltage();
int estimateBatteryPercent(float);

#define SerialMon Serial
#define SerialAT Serial1

extern TinyGsm modem;
extern void ensureModemReady();

// Enable GPS functionality on the SIM7000G module
void gpsBegin() {
  // Ensure modem is powered only when GPS is needed
  ensureModemReady();
  if (!modem.enableGPS()) {
    SerialMon.println("Failed to enable GPS.");
  } else {
    SerialMon.println("GPS enabled.");
  }
}

// Attempt to get a GPS fix within a timeout (in seconds)
GpsFixResult getGpsFix(uint16_t timeoutSec) {
  GpsFixResult result = {};
  result.success = false;
  result.fixTimeEpoch = 0;

  SerialMon.printf("Attempting GPS fix (timeout: %u sec)...\n", timeoutSec);
  gpsBegin();

  uint32_t start = millis();
  while ((millis() - start) < timeoutSec * 1000UL) {
    // Reset watchdog timer every 30 seconds during GPS fix attempt
    if (((millis() - start) / 30000) % 2 == 0) {
      esp_task_wdt_reset();
    }
    
    if (modem.getGPS(&result.latitude, &result.longitude)) {
      result.success = true;
      result.accuracy = 0.0;  // Accuracy is not provided by TinyGSM API

      int year, month, day, hour, minute, second;
      bool timeOk = modem.getGPSTime(&year, &month, &day, &hour, &minute, &second);
      if (timeOk) {
        struct tm t;
        t.tm_year = year - 1900;
        t.tm_mon  = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min  = minute;
        t.tm_sec  = second;
        t.tm_isdst = 0;
        result.fixTimeEpoch = mktime(&t);
      } else {
        result.fixTimeEpoch = 0;
      }

      SerialMon.printf("GPS fix: %.6f, %.6f\n", result.latitude, result.longitude);
      break;
    }
    delay(1000);
  }

  if (!result.success) {
    SerialMon.println("No GPS fix within timeout.");
  }

  return result;
}

// Disable GPS to save power
void gpsEnd() {
  modem.disableGPS();
}
// SerialMon.println("GPS disabled.");

// Determine GPS fix timeout (in seconds) based on battery percentage
uint16_t getGpsFixTimeout(bool isFirstFix) {
  return 10; // 10 seconds for faster troubleshooting
}

// Attempt to get a GPS fix with dynamic timeout
GpsFixResult getGpsFixDynamic(bool isFirstFix) {
  uint16_t timeoutSec = getGpsFixTimeout(isFirstFix);
  SerialMon.printf("GPS timeout: %u seconds (%s fix, battery: %d%%)\n", 
                   timeoutSec, 
                   isFirstFix ? "first" : "subsequent",
                   estimateBatteryPercent(getStableBatteryVoltage()));  // Use stable voltage
  return getGpsFix(timeoutSec);
}