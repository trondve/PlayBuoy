#define TINY_GSM_MODEM_SIM7000

#include "gps.h"
#include "config.h"
#include "modem.h"
#include <TinyGsmClient.h>
#include <time.h>

#define SerialMon Serial

extern TinyGsm modem;

// Minimal helpers
static bool sendAT(const String& cmd, unsigned long timeoutMs = 2000) {
  modem.sendAT(cmd); 
  return modem.waitResponse(timeoutMs) == 1;
}

static bool setModemClockFromRtc() {
  time_t now = time(NULL);
  if (now < 24 * 3600) return false;
  struct tm tloc, tgm; localtime_r(&now, &tloc); gmtime_r(&now, &tgm);
  long off = mktime(&tloc) - mktime(&tgm); int q = (int)(off / 900); if (q > 48) q = 48; if (q < -48) q = -48;
  char buf[40]; snprintf(buf, sizeof(buf), "\"%02d/%02d/%02d,%02d:%02d:%02d%+03d\"",
                        (tloc.tm_year % 100), tloc.tm_mon + 1, tloc.tm_mday,
                        tloc.tm_hour, tloc.tm_min, tloc.tm_sec, q);
  return sendAT(String("+CCLK=") + buf);
}

// 0) GNSS off
static bool gnssOff() { return sendAT("+CGNSPWR=0"); }

// 1) PDP up for time sync only
static bool pdpUp(const char* apn) {
  sendAT("+CGATT=1", 10000);
  sendAT(String("+CGDCONT=1,\"IP\",\"") + apn + "\"", 3000);
  if (!sendAT(String("+CNACT=1,\"") + apn + "\"", 20000)) {
    if (!sendAT("+CGACT=1,1", 20000)) return false;
  }
  return modem.isGprsConnected();
}

// 2) Disconnect data fully before GNSS
static void pdpDown() {
  sendAT("+CIPSHUT", 5000);
  sendAT("+CGACT=0,1", 5000);
  sendAT("+CNACT=0,0", 5000);
  sendAT("+CGATT=0", 5000);
}

// 3) Configure constellations/URCs (best-effort)
static void gnssConfig() {
  sendAT("+CGNSMOD=1,1,1,1", 5000); // GPS, GLONASS, BEIDOU, GALILEO
  sendAT("+CGNSCFG=1", 3000);       // optional routing
  sendAT("+CGNSNMEA=1", 3000);      // optional sentences
  sendAT("+CGNSURC=1", 3000);       // URC on fix
}

// 4) GNSS on
static bool gnssOn() { return sendAT("+CGNSPWR=1", 3000); }

// Public minimal API
void gpsBegin() {}
void gpsEnd() { gnssOff(); }

GpsFixResult getGpsFix(uint16_t timeoutSec) {
  GpsFixResult r{}; r.success = false; r.accuracy = 0; r.fixTimeEpoch = 0; r.latitude = r.longitude = 0;

  // 0
  gnssOff();

  // 1 time sync only
  if (pdpUp(NETWORK_PROVIDER)) {
    setModemClockFromRtc();
  }

  // 2 disconnect PDP for GNSS
  pdpDown();

  // 3 config
  gnssConfig();

  // 4 power GNSS
  if (!gnssOn()) return r;

  // 5 wait for fix (poll +CGNSINF; use TinyGSM quick parse via getGPS)
  uint32_t start = millis();
  uint32_t nextProgressSec = 30;
  while ((millis() - start) < timeoutSec * 1000UL) {
    float lat, lon;
    if (modem.getGPS(&lat, &lon)) {
      r.success = true; r.latitude = lat; r.longitude = lon;
      int y, mo, d, h, mi, s;
      if (modem.getGPSTime(&y, &mo, &d, &h, &mi, &s)) {
        struct tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
        r.fixTimeEpoch = mktime(&t);
      }
      break;
          }
      
      uint32_t elapsedSec = (millis() - start) / 1000UL;
      if (elapsedSec >= nextProgressSec) {
        if (elapsedSec >= 60) {
          uint32_t minutes = elapsedSec / 60;
          uint32_t seconds = elapsedSec % 60;
          if (seconds == 0) {
            SerialMon.printf("Searched for GPS fix for %lu minute%s\n", minutes, minutes == 1 ? "" : "s");
          } else {
            SerialMon.printf("Searched for GPS fix for %lu minute%s and %lu second%s\n", minutes, minutes == 1 ? "" : "s", seconds, seconds == 1 ? "" : "s");
          }
        } else {
          SerialMon.printf("Searched for GPS fix for %lu seconds\n", elapsedSec);
        }
        nextProgressSec += 30;
      }
      
      delay(1000);
    }

  // 6 GNSS off here; main will bring PDP up to upload
  gnssOff();
  return r;
}

uint16_t getGpsFixTimeout(bool isFirstFix) {
  // Set to 30 minutes for robust fix attempts in tough conditions
  (void)isFirstFix; return 1800;
}

GpsFixResult getGpsFixDynamic(bool isFirstFix) {
  return getGpsFix(getGpsFixTimeout(isFirstFix));
}