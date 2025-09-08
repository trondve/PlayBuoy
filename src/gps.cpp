// Adopt fully working NTP + XTRA + GNSS approach while preserving public API

#include "gps.h"
#include "config.h"
#include "modem.h"

#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#define SerialMon Serial

// Access raw AT port directly for robust AT workflows (matching working sample)
extern HardwareSerial Serial1;  // Configured in main as SerialAT
#define SerialAT Serial1

// Config
static const char* APN_PRIMARY   = "telenor.smart";
static const char* APN_SECONDARY = "telenor";
static const char* NTP_HOST      = "no.pool.ntp.org";
static const char* XTRA_URL      = "http://trondve.ddns.net/xtra3grc.bin";
static const char* XTRA_FS_DST   = "/customer/xtra3grc.bin";
static const uint32_t XTRA_HTTP_TIMEOUT_S = 120;
static const uint8_t  XTRA_HTTP_RETRIES   = 5;
static const uint32_t XTRA_STALE_DAYS     = 7;

// Local state
static Preferences s_prefs;
static bool s_muteEcho = false;  // Mute raw modem bytes while streaming NMEA

// ---------- AT helpers ----------
static void preATDelay() { delay(20); }

static bool sendAT(const String& cmd,
                   String* rspOut = nullptr,
                   uint32_t timeoutMs = 1500,
                   bool echo = true) {
  preATDelay();
  SerialAT.println(cmd);
  String rsp;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      if (echo && !s_muteEcho) SerialMon.write(c);
      rsp += c;
      if (rsp.indexOf("\r\nOK\r\n") >= 0 || rsp.indexOf("\r\nERROR\r\n") >= 0 || rsp.indexOf("+CME ERROR:") >= 0) {
        if (rspOut) *rspOut = rsp;
        return (rsp.indexOf("\r\nOK\r\n") >= 0);
      }
    }
  }
  if (rspOut) *rspOut = rsp;
  return false;
}

// ---------- PDP helpers ----------
static bool bringUpPDP(const char* apn) {
  SerialMon.printf("=== PDP with APN \"%s\" ===\n", apn);
  sendAT(String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"");
  // CNACT is preferred on SIM7000
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    sendAT(String("AT+CNACT=1,\"") + apn + "\"", nullptr, 3000);
    String r;
    if (sendAT("AT+CNACT?", &r)) {
      int ipIdx = r.indexOf("+CNACT: 1,\"");
      if (ipIdx >= 0) {
        int firstQ = r.indexOf('"', ipIdx);
        int secondQ = (firstQ >= 0) ? r.indexOf('"', firstQ + 1) : -1;
        String ip = (firstQ >= 0 && secondQ > firstQ) ? r.substring(firstQ + 1, secondQ) : String("");
        if (ip.length() && ip != "0.0.0.0") {
          SerialMon.print("PDP ACTIVE ✅  IP: "); SerialMon.println(ip);
          return true;
        }
      }
    }
    // Conservative cadence between PDP attempts
    delay(1200);
  }
  return false;
}

static void tearDownPDP() {
  // Proper order: CNACT -> CGACT -> CGATT -> CIPSHUT
  String dummy;
  if (!sendAT("AT+CNACT=0,0", &dummy, 5000)) {
    sendAT("AT+CNACT=0", nullptr, 5000);
  }
  delay(400);
  sendAT("AT+CGACT=0,1", nullptr, 5000);
  delay(400);
  sendAT("AT+CGATT=0",   nullptr, 5000);
  delay(400);
  sendAT("AT+CIPSHUT",   nullptr, 8000);
  delay(400);
}

// ---------- Time helpers ----------
struct ClockInfo { int year, month, day, hour, min, sec, tz_q; bool valid; };

static ClockInfo parseCCLK(const String& cclk) {
  ClockInfo ci{0,0,0,0,0,0,0,false};
  int q1=cclk.indexOf('"'), q2=cclk.indexOf('"', q1+1); if (q1<0||q2<0) return ci;
  String s=cclk.substring(q1+1,q2); int comma=s.indexOf(','); if (comma<0) return ci;
  String d=s.substring(0,comma), t=s.substring(comma+1);
  int p1=d.indexOf('/'), p2=d.indexOf('/', p1+1); if (p1<0||p2<0) return ci;
  int yy=d.substring(0,p1).toInt(), MM=d.substring(p1+1,p2).toInt(), dd=d.substring(p2+1).toInt();
  int tzpos=-1; for (int i = (int)t.length()-1;i>=0;--i){char c=t[i]; if(c=='+'||c=='-'){tzpos=i;break;}}
  if (tzpos<0) return ci;
  String times=t.substring(0,tzpos), tzs=t.substring(tzpos);
  int c1=times.indexOf(':'), c2=times.indexOf(':',c1+1); if(c1<0||c2<0) return ci;
  int hh=times.substring(0,c1).toInt(), mm=times.substring(c1+1,c2).toInt(), ss=times.substring(c2+1).toInt();
  int sign=(tzs[0]=='-')?-1:+1, q=tzs.substring(1).toInt();
  ci.year=2000+yy; ci.month=MM; ci.day=dd; ci.hour=hh; ci.min=mm; ci.sec=ss; ci.tz_q=sign*q;
  ci.valid=(ci.year>=2000 && MM>=1 && MM<=12 && dd>=1 && dd<=31);
  return ci;
}

static bool doNTPSync(ClockInfo* outCi = nullptr) {
  SerialMon.println("=== NTP SYNC (no.pool.ntp.org) ===");
  sendAT("AT+CNTPCID=1");
  sendAT(String("AT+CNTP=\"") + NTP_HOST + "\",0");
  String rsp; sendAT("AT+CNTP", &rsp, 8000);
  uint32_t t0 = millis();
  while (millis() - t0 < 90000) {
    String cclk;
    if (sendAT("AT+CCLK?", &cclk, 1000)) {
      ClockInfo ci = parseCCLK(cclk);
      if (ci.valid) {
        SerialMon.print("CCLK raw: \n"); SerialMon.println(cclk);
        if (outCi) *outCi = ci;
        return true;
      }
    }
    delay(1000);
  }
  return false;
}

// ---------- XTRA helpers ----------
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2; const int era = (y >= 0 ? y : y-399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153*(m + (m > 2 ? -3 : 9)) + 2)/5 + d-1;
  const unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
  return era * 146097L + (long)doe - 10957L;
}

static bool shouldDownloadXTRA(const ClockInfo& nowCi) {
  s_prefs.begin("xtra", false);
  long lastDay = s_prefs.getLong("last_day", -1);
  long today   = daysFromCivil(nowCi.year, nowCi.month, nowCi.day);
  bool due = (lastDay < 0) || ((today - lastDay) >= (long)XTRA_STALE_DAYS);
  s_prefs.end();
  if (due) {
    SerialMon.printf("XTRA is due (last=%ld, today=%ld, Δ=%ld days). Will download.\n",
                     lastDay, today, (lastDay<0? -1 : (today-lastDay)));
  } else {
    SerialMon.println("XTRA is fresh enough; skipping download.");
  }
  return due;
}

// Portable UTC epoch builder (replacement for timegm)
static uint32_t makeEpochUTC(int year, int month, int day, int hour, int minute, int second) {
  long daysSinceEpoch = daysFromCivil(year, month, day) - daysFromCivil(1970, 1, 1);
  if (daysSinceEpoch < 0) return 0;
  uint32_t secondsInDay = (uint32_t)(hour * 3600L + minute * 60L + second);
  return (uint32_t)(daysSinceEpoch * 86400L) + secondsInDay;
}

static void markXTRAJustApplied(const ClockInfo& nowCi) {
  long today = daysFromCivil(nowCi.year, nowCi.month, nowCi.day);
  s_prefs.begin("xtra", false);
  s_prefs.putLong("last_day", today);
  s_prefs.end();
}

static bool downloadAndApplyXTRA() {
  SerialMon.println("=== XTRA DOWNLOAD to /customer/ via HTTPTOFS ===");
  String cmd = String("AT+HTTPTOFS=\"") + XTRA_URL + "\",\"" + XTRA_FS_DST + "\"," +
               XTRA_HTTP_TIMEOUT_S + "," + XTRA_HTTP_RETRIES;
  if (!sendAT(cmd, nullptr, 5000)) return false;
  uint32_t t0 = millis();
  bool done = false, ok = false;
  while (millis() - t0 < 60000) {
    String rl;
    if (!sendAT("AT+HTTPTOFSRL?", &rl, 2000)) { delay(500); continue; }
    if (rl.indexOf("+HTTPTOFS: 200") >= 0) ok = true;
    if (rl.indexOf("+HTTPTOFSRL: 0") >= 0) { done = true; break; }
    delay(1000);
  }
  if (!(done && ok)) return false;

  SerialMon.println("=== APPLY XTRA (CGNSCPY → CGNSXTRA=1 → CGNSCOLD) ===");
  String cp;
  if (!sendAT("AT+CGNSCPY", &cp, 7000)) return false;
  sendAT("AT+CGNSXTRA=1");
  if (!sendAT("AT+CGNSCOLD", nullptr, 5000)) return false;
  SerialMon.println("XTRA applied ✅");
  return true;
}

// ---------- GNSS helpers ----------
static bool gnssEngineRunning() {
  String inf;
  if (!sendAT("AT+CGNSINF", &inf, 1200, /*echo*/true)) return false;
  int p = inf.indexOf("+CGNSINF:"); if (p < 0) return false;
  for (int i = p + 9; i < (int)inf.length(); ++i) {
    char c = inf[i]; if (c==' '||c=='\t'||c==':') continue; return c=='1';
  }
  return false;
}

static void gnssConfigure() {
  sendAT("AT+CGNSPWR=0");
  // Enable GPS, GLONASS, BeiDou; disable Galileo
  sendAT("AT+CGNSMOD=1,1,0,1");
  sendAT("AT+CGNSCFG=1");
  sendAT("AT+CGPIO=0,48,1,1");
  sendAT("AT+SGPIO=0,4,1,1");
}

static bool gnssStart() {
  SerialMon.println("=== GNSS POWER ON ===");
  // Ensure GPS power pin is set just before GNSS start to avoid early power-on
  extern void powerOnGPS();
  powerOnGPS();
  delay(5000);
  gnssConfigure();
  sendAT("AT+CGNSPWR=1");
  delay(300);
  for (int i = 0; i < 10; ++i) { if (gnssEngineRunning()) goto configured_nmea; delay(300); }
  SerialMon.println("GNSS not running; trying opposite SGPIO polarity...");
  sendAT("AT+CGNSPWR=0"); delay(150);
  sendAT("AT+SGPIO=0,4,1,0"); delay(150);
  sendAT("AT+CGNSPWR=1");
  for (int i = 0; i < 10; ++i) { if (gnssEngineRunning()) goto configured_nmea; delay(300); }
  SerialMon.println("Still not running; trying CGPIO control...");
  sendAT("AT+CGNSPWR=0"); delay(150);
  sendAT("AT+CGPIO=4,1,1"); delay(150);
  sendAT("AT+CGNSPWR=1");
  for (int i = 0; i < 10; ++i) { if (gnssEngineRunning()) goto configured_nmea; delay(300); }
configured_nmea:
  sendAT("AT+CGNSNMEA=511");
  sendAT("AT+CGNSRTMS=1000");
  return gnssEngineRunning();
}

static void gnssStop() {
  sendAT("AT+CGNSPWR=0");
}

static bool parseCgnsInfFix(const String& inf, float* outLat, float* outLon, uint32_t* outEpoch) {
  int p = inf.indexOf("+CGNSINF:"); if (p < 0) return false;
  int start = inf.indexOf(':', p); if (start < 0) return false; start++;
  // Split by commas
  int field = 0; bool run=false, hasFix=false; double lat=0, lon=0; String ts;
  String token; token.reserve(32);
  for (int i = start; i <= (int)inf.length(); ++i) {
    char c = (i == (int)inf.length()) ? ',' : inf[i];
    if (c == ',') {
      switch (field) {
        case 0: run = (token.trim(), (token == "1")); break;
        case 1: hasFix = (token.trim(), (token == "1")); break;
        case 2: ts = token; break; // YYYYMMDDhhmmss.sss
        case 3: lat = token.toDouble(); break;
        case 4: lon = token.toDouble(); break;
        default: break;
      }
      token = ""; field++;
      if (field > 8) break;
    } else {
      token += c;
    }
  }
  if (!(run && hasFix)) return false;
  if (outLat) *outLat = (float)lat;
  if (outLon) *outLon = (float)lon;
  if (outEpoch && ts.length() >= 14) {
    // Parse UTC time: YYYYMMDDhhmmss
    struct tm t{};
    t.tm_year = ts.substring(0,4).toInt() - 1900;
    t.tm_mon  = ts.substring(4,6).toInt() - 1;
    t.tm_mday = ts.substring(6,8).toInt();
    t.tm_hour = ts.substring(8,10).toInt();
    t.tm_min  = ts.substring(10,12).toInt();
    t.tm_sec  = ts.substring(12,14).toInt();
    *outEpoch = makeEpochUTC(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
  return true;
}

static void gnssSmoke60s() {
  // Stream NMEA for up to 60s while polling CGNSINF each second; stop early on fix
  s_muteEcho = true;
  sendAT("AT+CGNSTST=1");
  uint32_t tStart = millis();
  uint32_t lastInf = 0;
  bool gotFix = false;
  while (millis() - tStart < 60000 && !gotFix) {
    // Drain streaming NMEA quickly
    while (SerialAT.available()) {
      String line = SerialAT.readStringUntil('\n');
      line.trim();
      if (!line.startsWith("$")) continue;
      if (line.startsWith("$GA")) continue; // drop Galileo
      SerialMon.println(line);
    }
    if (millis() - lastInf > 1000) {
      lastInf = millis();
      String inf;
      if (sendAT("AT+CGNSINF", &inf, 1200, /*echo*/false)) {
        float lat, lon; uint32_t epoch;
        if (parseCgnsInfFix(inf, &lat, &lon, &epoch)) {
          gotFix = true;
        }
      }
    }
  }
  sendAT("AT+CGNSTST=0", nullptr, 1200, /*echo*/false);
  s_muteEcho = false;
}

// ---------- Public API ----------
// (gpsBegin removed; not needed)

void gpsEnd() { gnssStop(); }

static void syncTimeAndMaybeApplyXTRA() {
  // In minimal mode we may skip time/XTRA later via a guard in main; this function remains unchanged otherwise
  ClockInfo nowCi{};
  // Try primary, then secondary APN
  bool pdp = bringUpPDP(APN_PRIMARY) || bringUpPDP(APN_SECONDARY);
  if (pdp) {
    // Conservative idle after PDP up before CNTP
    delay(1500);
    if (doNTPSync(&nowCi)) {
      // Set ESP32 RTC from modem time (convert local time + tz to UTC epoch)
      if (nowCi.valid) {
        uint32_t epochLocal = makeEpochUTC(nowCi.year, nowCi.month, nowCi.day, nowCi.hour, nowCi.min, nowCi.sec);
        long tzSeconds = (long)nowCi.tz_q * 15L * 60L;
        uint32_t epochUtc = (tzSeconds >= 0 && (uint32_t)tzSeconds > epochLocal) ? 0 : (uint32_t)((long)epochLocal - tzSeconds);
        struct timeval tv; tv.tv_sec = epochUtc; tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        SerialMon.printf("RTC set from NTP via modem: %lu (UTC)\n", (unsigned long)epochUtc);
      }
      if (nowCi.valid && shouldDownloadXTRA(nowCi)) {
        if (downloadAndApplyXTRA()) markXTRAJustApplied(nowCi);
      }
    }
  }
  tearDownPDP();
}

GpsFixResult getGpsFix(uint16_t timeoutSec) {
  GpsFixResult result{}; result.success = false; result.accuracy = 0; result.fixTimeEpoch = 0; result.latitude = result.longitude = 0;

  // Ensure time and XTRA freshness before GNSS
  syncTimeAndMaybeApplyXTRA();

  // GNSS on
  if (!gnssStart()) {
    SerialMon.println("GNSS engine NOT running ❌ (continuing anyway)");
  }

  // Optional priming smoketest: 60s or until fix
  gnssSmoke60s();

  // Attempt fix up to timeoutSec by polling CGNSINF
  uint32_t start = millis();
  uint32_t nextProgressSec = 30;  // Log every 30s
  uint32_t lastInfLog = 0; bool firstInfLog = true;

  SerialMon.println("Starting GPS fix acquisition...");
  while ((millis() - start) < timeoutSec * 1000UL) {
    String inf;
    if (sendAT("AT+CGNSINF", &inf, 1500, /*echo*/false)) {
      float lat, lon; uint32_t epoch;
      if (parseCgnsInfFix(inf, &lat, &lon, &epoch)) {
        result.success = true; result.latitude = lat; result.longitude = lon; result.fixTimeEpoch = epoch;
        SerialMon.println("GPS Fix acquired!");
        break;
      }
    }

    if (millis() - lastInfLog >= 30000) {
      String inf2;
      if (sendAT("AT+CGNSINF", &inf2, 1500, /*echo*/true)) {
        if (firstInfLog) { SerialMon.println("=== GPS Status Monitoring ==="); firstInfLog = false; }
        SerialMon.printf("CGNSINF: %s\n", inf2.c_str());
      }
      lastInfLog = millis();
    }

    uint32_t elapsedSec = (millis() - start) / 1000UL;
    if (elapsedSec >= nextProgressSec) {
      if (elapsedSec >= 60) {
        uint32_t minutes = elapsedSec / 60; uint32_t seconds = elapsedSec % 60;
        if (seconds == 0) SerialMon.printf("Searched for GPS fix for %lu minute%s\n", minutes, minutes == 1 ? "" : "s");
        else SerialMon.printf("Searched for GPS fix for %lu minute%s and %lu second%s\n", minutes, minutes == 1 ? "" : "s", seconds, seconds == 1 ? "" : "s");
      } else {
        SerialMon.printf("Searched for GPS fix for %lu seconds\n", elapsedSec);
      }
      nextProgressSec += 30;
    }
    delay(1000);
  }

  // GNSS off here; main will bring PDP up to upload
  gnssStop();
  return result;
}

uint16_t getGpsFixTimeout(bool isFirstFix) {
  extern float getStableBatteryVoltage();
  extern int estimateBatteryPercent(float);
  float voltage = getStableBatteryVoltage();
  int percent = estimateBatteryPercent(voltage);
  if (isFirstFix) {
    if (percent > 60) return 1200;  // 20 minutes
    if (percent > 40) return 900;   // 15 minutes
    return 600;                     // 10 minutes
  } else {
    if (percent > 60) return 600;   // 10 minutes
    if (percent > 40) return 450;   // 7.5 minutes
    return 300;                     // 5 minutes
  }
}

GpsFixResult getGpsFixDynamic(bool isFirstFix) {
  return getGpsFix(getGpsFixTimeout(isFirstFix));
}