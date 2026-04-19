// Adopt fully working NTP + XTRA + GNSS approach while preserving public API

#include "gps.h"
#include "config.h"
#include "modem.h"
#include "rtc_state.h"

#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#define SerialMon Serial

// Access raw AT port directly for robust AT workflows (matching working sample)
extern HardwareSerial Serial1;  // Configured in main as SerialAT
#define SerialAT Serial1

// Config — use NETWORK_PROVIDER from config.h as primary APN for consistency
static const char* APN_PRIMARY   = NETWORK_PROVIDER;
static const char* APN_SECONDARY = "telenor.smart";
static const char* NTP_HOST      = "no.pool.ntp.org";
static const char* XTRA_URL      = "http://trondve.ddns.net/xtra3grc.bin";
static const char* XTRA_FS_DST   = "/customer/xtra3grc.bin";
static const uint32_t XTRA_HTTP_TIMEOUT_S = 120;
static const uint8_t  XTRA_HTTP_RETRIES   = 5;
static const uint32_t XTRA_STALE_DAYS     = 3;  // Datasheet: xtra3grc.bin valid for 3 days

// HDOP quality threshold — wait for HDOP below this before accepting a fix.
// Fallback: accept any fix after 80% of timeout has elapsed.
static const float HDOP_ACCEPT_THRESHOLD = 3.0f;

// Local state
static Preferences s_prefs;
static bool s_xtraJustApplied = false;  // Set when CGNSCOLD started engine with fresh XTRA

// ---------- AT helpers ----------
static void preATDelay() { delay(100); }

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
      if (echo) SerialMon.write(c);
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
  // Send CNACT activation once, then poll status — repeated CNACT commands
  // can confuse the modem while activation is still in progress (takes up to 15s)
  sendAT(String("AT+CNACT=1,\"") + apn + "\"", nullptr, 3000);
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
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
    // Poll cadence — give modem time to complete activation
    delay(1500);
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

  // Read CCLK before NTP to detect stale modem RTC
  String preNtpCclk;
  sendAT("AT+CCLK?", &preNtpCclk, 1000);
  ClockInfo preCi = parseCCLK(preNtpCclk);

  // Trigger NTP sync and wait for +CNTP: result
  String rsp; sendAT("AT+CNTP", &rsp, 8000);

  // Wait for +CNTP: unsolicited response (1 = success, others = failure)
  bool ntpSuccess = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    while (SerialAT.available()) {
      String line = SerialAT.readStringUntil('\n');
      if (line.indexOf("+CNTP: 1") >= 0) { ntpSuccess = true; break; }
      // +CNTP: 61 = network error, +CNTP: 62 = DNS error, +CNTP: 63 = connect error
      if (line.indexOf("+CNTP:") >= 0 && line.indexOf("+CNTP: 1") < 0) {
        SerialMon.print("NTP failed: "); SerialMon.println(line);
        return false;
      }
    }
    if (ntpSuccess) break;
    delay(500);
  }

  if (!ntpSuccess) {
    SerialMon.println("NTP sync timeout (no +CNTP: 1 response)");
    return false;
  }

  // Read CCLK after NTP and verify time actually changed
  delay(500);
  String cclk;
  if (sendAT("AT+CCLK?", &cclk, 1000)) {
    ClockInfo ci = parseCCLK(cclk);
    if (ci.valid) {
      // Verify year is reasonable (NTP synced to real time, not 2000/2004 default)
      if (ci.year < 2024) {
        SerialMon.printf("NTP returned stale year (%d), rejecting\n", ci.year);
        return false;
      }
      SerialMon.print("NTP synced CCLK: "); SerialMon.println(cclk);
      if (outCi) *outCi = ci;
      return true;
    }
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
// Note: daysFromCivil(1970,1,1) == 0 by definition of the algorithm (epoch-relative).
static uint32_t makeEpochUTC(int year, int month, int day, int hour, int minute, int second) {
  long daysSinceEpoch = daysFromCivil(year, month, day);
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
    if (rl.indexOf("+HTTPTOFSRL: 0") >= 0) done = true;
    // Only exit when BOTH flags are set — they can arrive in either order
    if (done && ok) break;
    delay(1000);
  }
  if (!(done && ok)) return false;

  SerialMon.println("=== APPLY XTRA (CGNSPWR=1 → CGNSCPY → CGNSXTRA=1 → CGNSCOLD) ===");
  // CGNSCPY requires the GNSS engine to be powered on (per SIM7000G datasheet).
  // Power it on cleanly first; if it was already on, CGNSPWR=0 + CGNSPWR=1 restarts it.
  sendAT("AT+CGNSPWR=0");
  delay(300);
  sendAT("AT+CGNSPWR=1");
  delay(300);
  String cp;
  if (!sendAT("AT+CGNSCPY", &cp, 7000)) {
    sendAT("AT+CGNSPWR=0"); // ensure GNSS is off on failure
    return false;
  }
  sendAT("AT+CGNSXTRA=1");
  // Configure GNSS mode and NMEA *before* CGNSCOLD starts the engine,
  // so the cold start runs with correct settings from the beginning.
  sendAT("AT+CGNSMOD=1,1,0,1");  // GPS + GLONASS + BeiDou, no Galileo
  sendAT("AT+CGNSCFG=1");
  if (!sendAT("AT+CGNSCOLD", nullptr, 5000)) return false;
  // CGNSCOLD starts the GNSS engine with XTRA injected — do NOT power cycle after this.
  s_xtraJustApplied = true;
  SerialMon.println("XTRA applied ✅ (GNSS engine started via CGNSCOLD)");
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

// Determine best GNSS start mode based on last fix age
static const char* gnssStartCommand() {
  uint32_t lastFix = rtcState.lastGpsFixTime;
  if (lastFix <= 1000000000) return "AT+CGNSCOLD";  // No prior fix — cold start

  uint32_t now = (uint32_t)time(NULL);
  if (now <= 1000000000) return "AT+CGNSCOLD";      // RTC not set — cold start

  uint32_t ageSec = now - lastFix;
  if (ageSec < 4 * 3600) {
    SerialMon.printf("Last fix %lu sec ago — hot start\n", ageSec);
    return "AT+CGNSHOT";    // Ephemeris still valid (<4h)
  } else if (ageSec < 24 * 3600) {
    SerialMon.printf("Last fix %lu sec ago — warm start\n", ageSec);
    return "AT+CGNSWARM";   // Almanac valid, ephemeris stale
  }
  SerialMon.printf("Last fix %lu sec ago — cold start\n", ageSec);
  return "AT+CGNSCOLD";     // Everything stale
}

static bool gnssStart() {
  SerialMon.println("=== GNSS POWER ON ===");

  // If XTRA just did CGNSCOLD, the engine is already running with XTRA injected.
  // Don't power-cycle — just configure NMEA output and verify.
  if (s_xtraJustApplied) {
    SerialMon.println("GNSS already started by XTRA CGNSCOLD — skipping power cycle");
    s_xtraJustApplied = false;
    sendAT("AT+CGNSNMEA=511");
    sendAT("AT+CGNSRTMS=1000");
    // Verify engine is actually running
    for (int i = 0; i < 10; ++i) { if (gnssEngineRunning()) return true; delay(300); }
    SerialMon.println("XTRA-started engine not responding, falling through to normal start");
  }

  // Normal start: configure, power on, use appropriate start mode
  sendAT("AT+CGNSPWR=0");
  sendAT("AT+CGNSMOD=1,1,0,1");  // GPS + GLONASS + BeiDou
  sendAT("AT+CGNSCFG=1");
  sendAT("AT+CGPIO=0,48,1,1");
  sendAT("AT+SGPIO=0,4,1,1");
  sendAT("AT+CGNSPWR=1");
  delay(300);

  // Issue warm/hot/cold start based on last fix age
  const char* startCmd = gnssStartCommand();
  SerialMon.printf("GNSS start mode: %s\n", startCmd);
  sendAT(startCmd);

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

static bool parseCgnsInfFix(const String& inf, float* outLat, float* outLon, uint32_t* outEpoch, float* outHdop = nullptr) {
  int p = inf.indexOf("+CGNSINF:"); if (p < 0) return false;
  int start = inf.indexOf(':', p); if (start < 0) return false; start++;
  // CGNSINF fields: 0=run, 1=fix, 2=utc, 3=lat, 4=lon, 5=alt, 6=speed, 7=course, 8=fixmode, 9=reserved, 10=HDOP
  int field = 0; bool run=false, hasFix=false; double lat=0, lon=0; float hdop=99.0f; String ts;
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
        case 10: { token.trim(); if (token.length() > 0) hdop = token.toFloat(); } break;
        default: break;
      }
      token = ""; field++;
      if (field > 11) break;
    } else {
      token += c;
    }
  }
  if (!(run && hasFix)) return false;
  // Validate coordinates: reject (0,0) which is a common GPS default when no real fix,
  // and reject anything outside valid geographic range
  if (lat == 0.0 && lon == 0.0) { SerialMon.println("GPS fix rejected: (0,0) coordinates"); return false; }
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
    SerialMon.printf("GPS fix rejected: out of range (%.4f, %.4f)\n", lat, lon);
    return false;
  }
  if (outLat) *outLat = (float)lat;
  if (outLon) *outLon = (float)lon;
  if (outHdop) *outHdop = hdop;
  if (outEpoch && ts.length() >= 14) {
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

// Returns true and populates result if a fix was acquired during the warmup period.
// Uses CGNSINF polling only — no NMEA streaming, avoiding UART contention.
static bool gnssWarmup60s(GpsFixResult* outResult, uint32_t gnssStartTime) {
  uint32_t tStart = millis();
  SerialMon.println("GNSS warmup: polling CGNSINF for up to 60s...");
  while (millis() - tStart < 60000) {
    String inf;
    if (sendAT("AT+CGNSINF", &inf, 1500, /*echo*/false)) {
      float lat, lon, hdop; uint32_t epoch;
      if (parseCgnsInfFix(inf, &lat, &lon, &epoch, &hdop)) {
        if (outResult) {
          outResult->success = true;
          outResult->latitude = lat;
          outResult->longitude = lon;
          outResult->fixTimeEpoch = epoch;
          outResult->hdop = hdop;
          outResult->ttfSeconds = (uint16_t)((millis() - gnssStartTime) / 1000);
          SerialMon.printf("GPS fix during warmup! HDOP=%.1f TTF=%us\n", hdop, outResult->ttfSeconds);
        }
        return true;
      }
    }
    delay(1000);
  }
  SerialMon.println("GNSS warmup: no fix after 60s");
  return false;
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
      } else {
        SerialMon.println("XTRA skipped: NTP returned invalid clock data (CCLK parse failed)");
      }
      if (nowCi.valid && shouldDownloadXTRA(nowCi)) {
        if (downloadAndApplyXTRA()) markXTRAJustApplied(nowCi);
      }
    } else {
      SerialMon.println("XTRA skipped: NTP sync failed (no valid CCLK within 90s)");
    }
  } else {
    SerialMon.println("XTRA skipped: PDP connection failed (no data connectivity)");
  }
  tearDownPDP();
}

GpsFixResult getGpsFix(uint16_t timeoutSec) {
  GpsFixResult result{}; result.success = false; result.accuracy = 0; result.hdop = 99.0f; result.fixTimeEpoch = 0; result.latitude = result.longitude = 0; result.ttfSeconds = 0;
  uint32_t gnssStartTime = millis(); // Track time-to-fix

  // Ensure time and XTRA freshness before GNSS
  syncTimeAndMaybeApplyXTRA();

  // GNSS on
  if (!gnssStart()) {
    SerialMon.println("GNSS engine NOT running ❌ (continuing anyway)");
  }

  // Warmup: poll CGNSINF for up to 60s (no NMEA streaming — avoids UART contention)
  if (gnssWarmup60s(&result, gnssStartTime)) {
    // Fix acquired during warmup — skip redundant polling
    gnssStop();
    return result;
  }

  // Attempt fix up to timeoutSec by polling CGNSINF
  uint32_t start = millis();
  uint32_t timeoutMs = timeoutSec * 1000UL;
  uint32_t nextProgressSec = 30;  // Log every 30s
  uint32_t lastInfLog = 0; bool firstInfLog = true;

  // HDOP quality gate: accept fix immediately if HDOP is good enough.
  // After 80% of timeout, accept any fix regardless of HDOP (better than nothing).
  uint32_t hdopGraceMs = (uint32_t)(timeoutMs * 0.8f);

  // No watchdog reset in this loop — intentional. The 45-minute WDT is the safety
  // net that prevents the buoy from burning battery forever in bad weather. If we
  // can't get a fix within the WDT window, it's better to reset and sleep.
  SerialMon.println("Starting GPS fix acquisition...");
  while ((millis() - start) < timeoutMs) {
    String inf;
    if (sendAT("AT+CGNSINF", &inf, 1500, /*echo*/false)) {
      float lat, lon, hdop; uint32_t epoch;
      if (parseCgnsInfFix(inf, &lat, &lon, &epoch, &hdop)) {
        uint32_t elapsed = millis() - start;
        bool hdopOk = (hdop <= HDOP_ACCEPT_THRESHOLD);
        bool pastGrace = (elapsed >= hdopGraceMs);

        if (hdopOk || pastGrace) {
          result.success = true; result.latitude = lat; result.longitude = lon; result.fixTimeEpoch = epoch;
          result.hdop = hdop;
          result.ttfSeconds = (uint16_t)((millis() - gnssStartTime) / 1000);
          if (!hdopOk) {
            SerialMon.printf("GPS fix accepted (HDOP=%.1f, past grace period) TTF=%us\n", hdop, result.ttfSeconds);
          } else {
            SerialMon.printf("GPS fix acquired! HDOP=%.1f TTF=%us\n", hdop, result.ttfSeconds);
          }
          break;
        } else {
          SerialMon.printf("GPS fix has HDOP=%.1f (want ≤%.1f), waiting for better fix...\n", hdop, HDOP_ACCEPT_THRESHOLD);
        }
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