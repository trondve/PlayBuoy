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

// 1) PDP up for time/XTRA
static bool pdpUp(const char* apn) {
  sendAT("+CGATT=1", 10000);
  sendAT(String("+CGDCONT=1,\"IP\",\"") + apn + "\"", 3000);
  if (!sendAT(String("+CNACT=1,\"") + apn + "\"", 20000)) {
    if (!sendAT("+CGACT=1,1", 20000)) return false;
  }
  return modem.isGprsConnected();
}

// 2) Enable XTRA, download to /customer, import and report validity
static bool fetchXtra() {
  sendAT("+CGNSXTRA=1", 3000);
  if (!sendAT("+HTTPINIT", 5000)) return false;
  const char* urls[] = {
    "http://xtrapath1.izatcloud.net/xtra3grc.bin",
    "http://xtrapath2.izatcloud.net/xtra3grc.bin",
    "http://xtrapath3.izatcloud.net/xtra3grc.bin"
  };
  bool ok = false;
  for (int i = 0; i < 3 && !ok; i++) {
    String cmd = String("+HTTPTOFS=\"") + urls[i] + "\",\"/customer/xtra3grc.bin\"";
    if (sendAT(cmd, 70000)) {
      modem.sendAT("+HTTPTOFSRL?");
      if (modem.waitResponse(3000) == 1) {
        String r = modem.stream.readString();
        if (r.indexOf("200") != -1) ok = true;
      }
    }
  }
  sendAT("+HTTPTERM", 3000);
  if (!ok) return false;
  if (!sendAT("+CGNSCPY", 10000)) return false; // import
  sendAT("+CGNSXTRA", 3000); // execution: prints validity window per manual
  return true;
}

// 3) Disconnect data fully before GNSS
static void pdpDown() {
  sendAT("+CIPSHUT", 5000);
  sendAT("+CGACT=0,1", 5000);
  sendAT("+CNACT=0,0", 5000);
  sendAT("+CGATT=0", 5000);
}

// 4) Configure constellations/URCs (best-effort)
static void gnssConfig() {
  sendAT("+CGNSMOD=1,1,1,1", 5000); // GPS, GLONASS, BEIDOU, GALILEO
  sendAT("+CGNSCFG=1", 3000);       // optional routing
  sendAT("+CGNSNMEA=1", 3000);      // optional sentences
  sendAT("+CGNSURC=1", 3000);       // URC on fix
}

// 5) GNSS on
static bool gnssOn() { return sendAT("+CGNSPWR=1", 3000); }

// Public minimal API
void gpsBegin() {}
void gpsEnd() { gnssOff(); }

GpsFixResult getGpsFix(uint16_t timeoutSec) {
  GpsFixResult r{}; r.success = false; r.accuracy = 0; r.fixTimeEpoch = 0; r.latitude = r.longitude = 0;

  // 0
  gnssOff();

  // 1 time + PDP
  if (pdpUp(NETWORK_PROVIDER)) {
    setModemClockFromRtc();
    // 2 XTRA
    fetchXtra();
  }

  // 3 disconnect PDP for GNSS
  pdpDown();

  // 4 config
  gnssConfig();

  // 5 power GNSS
  if (!gnssOn()) return r;

  // 6 wait for fix (poll +CGNSINF; use TinyGSM quick parse via getGPS)
  uint32_t start = millis();
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
    delay(1000);
  }

  // 7 GNSS off here; main will bring PDP up to upload
  gnssOff();
  return r;
}

uint16_t getGpsFixTimeout(bool isFirstFix) {
  // Fixed 30 min per request (could adapt later using RTC age)
  (void)isFirstFix; return 1800;
}

GpsFixResult getGpsFixDynamic(bool isFirstFix) {
  return getGpsFix(getGpsFixTimeout(isFirstFix));
}
