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

// Seed modem clock from the ESP32 RTC to help GNSS TTFF
static void seedModemClockFromRtc() {
  time_t now = time(NULL);
  if (now < 24 * 3600) return; // skip if RTC not valid
  struct tm tloc, tgm;
  localtime_r(&now, &tloc);
  gmtime_r(&now, &tgm);
  long offsetSec = mktime(&tloc) - mktime(&tgm); // local - UTC
  int quarters = (int)(offsetSec / 900); // 15-minute units
  if (quarters > 48) quarters = 48; if (quarters < -48) quarters = -48;
  char sign = quarters >= 0 ? '+' : '-';
  int qabs = quarters >= 0 ? quarters : -quarters;
  char buf[40];
  // yy/MM/dd,hh:mm:ss±zz  (zz = number of 15-minute units)
  snprintf(buf, sizeof(buf), "\"%02d/%02d/%02d,%02d:%02d:%02d%c%02d\"",
           (tloc.tm_year % 100), tloc.tm_mon + 1, tloc.tm_mday,
           tloc.tm_hour, tloc.tm_min, tloc.tm_sec,
           sign, qabs);
  modem.sendAT(String("+CCLK=") + buf);
  modem.waitResponse();
}

// Enable GPS functionality on the SIM7000G module
void gpsBegin() {
  // Ensure modem is powered only when GPS is needed
  ensureModemReady();
  seedModemClockFromRtc();
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
  float v = getStableBatteryVoltage();
  int pct = estimateBatteryPercent(v);
  if (pct > 60) return 600; // 10 min
  if (pct > 40) return 300; // 5 min
  if (pct > 30) return 120; // 2 min
  if (pct > 20) return 60;  // 1 min
  return 0;                 // skip fix below 20%
}

// Attempt to get a GPS fix with dynamic timeout
GpsFixResult getGpsFixDynamic(bool isFirstFix) {
  uint16_t timeoutSec = getGpsFixTimeout(isFirstFix);
  SerialMon.printf("GPS timeout: %u seconds (%s fix, battery: %d%%)\n", 
                   timeoutSec, 
                   isFirstFix ? "first" : "subsequent",
                   estimateBatteryPercent(getStableBatteryVoltage()));  // Use stable voltage
  if (timeoutSec == 0) {
    GpsFixResult r = {};
    r.success = false;
    r.latitude = r.longitude = 0;
    r.accuracy = 0;
    r.fixTimeEpoch = 0;
    SerialMon.println("Battery low: skipping GPS fix");
    return r;
  }
  return getGpsFix(timeoutSec);
}