/*
================================================================================
  Key Findings & Essential Information
================================================================================

HARDWARE:
- Board: LilyGO T-SIM7000G (ESP32-D0WD-V3, 240MHz, 4MB flash)
- Modem: SIMCom SIM7000G
- GNSS: Integrated (GPS + GLONASS, Galileo disabled)
- Antennas: LTE + GNSS external antennas required

PINS:
- Modem Power Rail: controlled via GPIO (defined in code)
- Modem Reset: GPIO (defined in code)
- Modem PWRKEY: GPIO (defined in code)
- UART: 57600 baud (stable rate, stored in SIM7000G with AT&W)
- FTDI cable for debug: 
    • Black → GND
    • Orange → TXD (goes to RX on LilyGO)
    • Yellow → RXD (goes to TX on LilyGO)

NETWORK:
- APN: "telenor.smart" (Telenor NB-IoT/LTE-M)
- RAT: LTE Cat-M1 only (NB-IoT disabled in this build)
- Band configuration: "CAT-M", bands 3 and 20 (EU carriers)
- PDP context: configured with AT+CGDCONT
- Operator: automatically selected (AT+COPS=0)

GNSS SETTINGS:
- GNSS enabled via AT+CGNSPWR=1
- Constellations: GPS + GLONASS only (Galileo disabled)
- NMEA output: AT+CGNSNMEA=511, AT+CGNSRTMS=1000 (1Hz streaming)
- Cold start with XTRA applied if available

XTRA (ASSISTED-GNSS):
- XTRA file: http://trondve.ddns.net/xtra3grc.bin
- Download to: /customer/xtra3grc.bin
- Validity: 7 days (tracked via Preferences in flash)
- Downloaded only if expired or missing
- Applied using AT+CGNSCPY + AT+CGNSXTRA=1 + AT+CGNSCOLD

TIMINGS:
- Initial ESP32 boot: 10s settling delay before touching modem
- Modem boot: ~5–10s until UART is stable ("RDY")
- GNSS warm start: usually 20–60s outdoors, depending on sky view
- GNSS smoketest: 30s or until fix acquired
- NTP sync: pool.ntp.org used each boot for clock sync

NOTES:
- Shutdown sequence corrected: CNACT → CGACT → CGATT → CIPSHUT
- NTP sync survives across resets (clock persists)
- XTRA not downloaded every boot — only when expired
- Typical flash usage: ~22% (285 KB of 1.3 MB)
- Typical RAM usage: ~7% (21 KB of 320 KB)

================================================================================
*/

#include <Arduino.h>
#include <Preferences.h>

// ---------- Board Pins ----------
#define MODEM_RX        26
#define MODEM_TX        27
#define MODEM_PWRKEY     4
#define MODEM_RST        5
#define MODEM_POWER_ON  23
#define MODEM_DTR       32
#define MODEM_RI        33

// ---------- UART ----------
#define UART_BAUD       57600
HardwareSerial SerialAT(1);

// ---------- APN / NTP / XTRA ----------
static const char* APN_PRIMARY = "telenor.smart";
static const char* NTP_HOST    = "no.pool.ntp.org";
static const char* XTRA_URL    = "http://trondve.ddns.net/xtra3grc.bin";
static const char* XTRA_FS_DST = "/customer/xtra3grc.bin";
static const uint32_t XTRA_HTTP_TIMEOUT_S = 120;
static const uint8_t  XTRA_HTTP_RETRIES   = 5;
static const uint32_t XTRA_STALE_DAYS     = 7;

// ---------- Timings ----------
static const uint32_t AT_RSP_TIMEOUT_MS    = 1200;
static const uint32_t UART_READY_WAIT_MS   = 5000;
static const uint32_t PWRKEY_LOW_MS        = 1000;
static const uint32_t NET_REG_TIMEOUT_MS   = 30000;
static const uint32_t PDP_WAIT_MS          = 20000;
static const uint32_t NTP_POLL_MAX_MS      = 90000;

// ---------- Globals ----------
Preferences prefs;
static bool gMuteEcho = false;  // mute raw modem bytes while NMEA streaming

// ---------- Small helpers ----------
static void preATDelay() { delay(1000); }

static bool sendAT(const String& cmd,
                   String* rspOut = nullptr,
                   uint32_t tmo = AT_RSP_TIMEOUT_MS,
                   bool echo = true) {
  preATDelay();
  Serial.print(">> "); Serial.println(cmd);
  SerialAT.println(cmd);

  String rsp;
  uint32_t t0 = millis();
  while (millis() - t0 < tmo) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      if (echo && !gMuteEcho) Serial.write(c);  // DO NOT echo when muted
      rsp += c;
      if (rsp.indexOf("\r\nOK\r\n") >= 0 || rsp.indexOf("\r\nERROR\r\n") >= 0 || rsp.indexOf("+CME ERROR:") >= 0) {
        if (rspOut) *rspOut = rsp;
        return (rsp.indexOf("\r\nOK\r\n") >= 0);
      }
    }
  }
  Serial.println("!! TIMEOUT");
  if (rspOut) *rspOut = rsp;
  return false;
}

static void railsEnable() {
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_DTR, OUTPUT);
  pinMode(MODEM_RI, INPUT);

  digitalWrite(MODEM_POWER_ON, LOW);
  digitalWrite(MODEM_RST, LOW);
  digitalWrite(MODEM_PWRKEY, HIGH);
  digitalWrite(MODEM_DTR, HIGH);
  delay(100);

  digitalWrite(MODEM_POWER_ON, HIGH);
  delay(1000);

  digitalWrite(MODEM_RST, HIGH); delay(100);
  digitalWrite(MODEM_RST, LOW);  delay(100);
  digitalWrite(MODEM_RST, HIGH); delay(3000);

  digitalWrite(MODEM_PWRKEY, LOW); delay(PWRKEY_LOW_MS);
  digitalWrite(MODEM_PWRKEY, HIGH);

  digitalWrite(MODEM_DTR, LOW);
}

static bool modemPowerOn() {
  Serial.println("=== MODEM POWER SEQUENCE (rail + RST + PWRKEY LOW; UART 57600) ===");
  railsEnable();
  SerialAT.begin(UART_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  Serial.println("Power sequence done; waiting for modem UART...");
  uint32_t t0 = millis();
  while (millis() - t0 < UART_READY_WAIT_MS) {
    while (SerialAT.available()) Serial.write((char)SerialAT.read());
    delay(10);
  }
  for (int i = 0; i < 6; ++i) {
    if (sendAT("AT")) return true;
    delay(500);
  }
  return false;
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
  int tzpos=-1; for (int i=t.length()-1;i>=0;--i){char c=t[i]; if(c=='+'||c=='-'){tzpos=i;break;}}
  if (tzpos<0) return ci;
  String times=t.substring(0,tzpos), tzs=t.substring(tzpos);
  int c1=times.indexOf(':'), c2=times.indexOf(':',c1+1); if(c1<0||c2<0) return ci;
  int hh=times.substring(0,c1).toInt(), mm=times.substring(c1+1,c2).toInt(), ss=times.substring(c2+1).toInt();
  int sign=(tzs[0]=='-')?-1:+1, q=tzs.substring(1).toInt();
  ci.year=2000+yy; ci.month=MM; ci.day=dd; ci.hour=hh; ci.min=mm; ci.sec=ss; ci.tz_q=sign*q;
  ci.valid=(ci.year>=2000 && MM>=1 && MM<=12 && dd>=1 && dd<=31); return ci;
}
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2; const int era = (y >= 0 ? y : y-399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153*(m + (m > 2 ? -3 : 9)) + 2)/5 + d-1;
  const unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
  return era * 146097L + (long)doe - 10957L;
}
static String humanTimeLocal(const ClockInfo& ci) {
  int tz_minutes=ci.tz_q*15; int tz_h=tz_minutes/60, tz_m=abs(tz_minutes%60);
  char buf[64]; snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d (UTC%+03d:%02d)",
                        ci.year,ci.month,ci.day,ci.hour,ci.min,ci.sec,tz_h,tz_m); return String(buf);
}

// ---------- Network & PDP ----------
static bool waitForSIMReady() {
  Serial.println("=== Waiting for SIM (CPIN) ===");
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    String r;
    if (sendAT("AT+CPIN?", &r) && r.indexOf("+CPIN: READY") >= 0) return true;
    delay(200);
  }
  return false;
}
static bool waitForNetwork() {
  Serial.println("=== Waiting for network registration ===");
  uint32_t t0 = millis();
  while (millis() - t0 < NET_REG_TIMEOUT_MS) {
    String r1, r2;
    bool ok1 = sendAT("AT+CEREG?", &r1);
    bool ok2 = sendAT("AT+CREG?",  &r2);
    if ((ok1 && (r1.indexOf(",1") >= 0 || r1.indexOf(",5") >= 0)) ||
        (ok2 && (r2.indexOf(",1") >= 0 || r2.indexOf(",5") >= 0))) return true;
    delay(500);
  }
  return false;
}
static bool bringUpPDP(const char* apn) {
  Serial.printf("=== PDP with APN \"%s\" ===\n", apn);
  sendAT(String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"");
  sendAT("AT+COPS=0");
  uint32_t t0 = millis();
  while (millis() - t0 < PDP_WAIT_MS) {
    sendAT(String("AT+CNACT=1,\"") + apn + "\"", nullptr, 3000);
    String r;
    if (sendAT("AT+CNACT?", &r)) {
      int ipIdx = r.indexOf("+CNACT: 1,\"");
      if (ipIdx >= 0) {
        int q = r.indexOf('"', ipIdx + 11);
        String ip = r.substring(ipIdx + 11, q);
        if (ip.length() && ip != "0.0.0.0") {
          Serial.print("PDP ACTIVE ✅  IP: "); Serial.println(ip);
          return true;
        }
      }
    }
    delay(800);
  }
  return false;
}
static void tearDownPDP() {
  // Try SIM7000 variants
  String dummy;
  if (!sendAT("AT+CNACT=0,0", &dummy, 5000)) {
    sendAT("AT+CNACT=0", nullptr, 5000);  // FW that dislikes the second parameter
  }
  sendAT("AT+CGACT=0,1", nullptr, 5000);  // harmless if already down
  sendAT("AT+CGATT=0",   nullptr, 5000);
  sendAT("AT+CIPSHUT",   nullptr, 8000);
  delay(200); // let the stack settle
}

// ---------- NTP ----------
static bool doNTPSync(ClockInfo* outCi = nullptr) {
  Serial.println("=== NTP SYNC (no.pool.ntp.org) ===");
  sendAT("AT+CNTPCID=1");
  sendAT(String("AT+CNTP=\"") + NTP_HOST + "\",0");
  String rsp; sendAT("AT+CNTP", &rsp, 8000);
  uint32_t t0 = millis();
  while (millis() - t0 < NTP_POLL_MAX_MS) {
    String cclk;
    if (sendAT("AT+CCLK?", &cclk, 1000)) {
      // Print decoded time
      int ln = cclk.indexOf("+CCLK:");
      ClockInfo ci = parseCCLK(cclk);
      if (ci.valid) {
        Serial.print("CCLK raw: \n"); Serial.println(cclk);
        Serial.print("Local time: "); Serial.println(humanTimeLocal(ci));
        Serial.println("       UTC: Local time above; offset shown.");
        Serial.println("NTP sync ✅");
        if (outCi) *outCi = ci;
        return true;
      }
    }
    delay(1000);
  }
  Serial.println("NTP sync ❌");
  return false;
}

// ---------- XTRA (conditional) ----------
static bool shouldDownloadXTRA(const ClockInfo& nowCi) {
  prefs.begin("xtra", false);
  long lastDay = prefs.getLong("last_day", -1);
  long today   = daysFromCivil(nowCi.year, nowCi.month, nowCi.day);
  bool due = (lastDay < 0) || ((today - lastDay) >= (long)XTRA_STALE_DAYS);
  prefs.end();
  if (due) {
    Serial.printf("XTRA is due (last=%ld, today=%ld, Δ=%ld days). Will download.\n",
                  lastDay, today, (lastDay<0? -1 : (today-lastDay)));
  } else {
    Serial.println("XTRA is fresh enough; skipping download.");
  }
  return due;
}
static void markXTRAJustApplied(const ClockInfo& nowCi) {
  long today = daysFromCivil(nowCi.year, nowCi.month, nowCi.day);
  prefs.begin("xtra", false);
  prefs.putLong("last_day", today);
  prefs.end();
}
static bool downloadAndApplyXTRA() {
  Serial.println("=== XTRA DOWNLOAD to /customer/ via HTTPTOFS ===");
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

  Serial.println("=== APPLY XTRA (CGNSCPY → CGNSXTRA=1 → CGNSCOLD) ===");
  String cp;
  if (!sendAT("AT+CGNSCPY", &cp, 7000)) return false;
  sendAT("AT+CGNSXTRA=1");
  if (!sendAT("AT+CGNSCOLD", nullptr, 5000)) return false;
  Serial.println("XTRA applied ✅");
  return true;
}

// ---------- GNSS start ----------
static bool gnssStart() {
  Serial.println("=== GNSS POWER ON ===");
  sendAT("AT+CGNSPWR=0");
  sendAT("AT+CGNSMOD=1");
  sendAT("AT+CGNSCFG=1");   // may CME on some FW; harmless
  sendAT("AT+CGPIO=0,48,1,1");
  sendAT("AT+SGPIO=0,4,1,1");
  sendAT("AT+CGNSPWR=1");
  delay(300);

  auto engineRunning = []() -> bool {
    String inf;
    if (!sendAT("AT+CGNSINF", &inf, 1000, /*echo*/true)) return false;
    int p = inf.indexOf("+CGNSINF:"); if (p < 0) return false;
    for (int i = p + 9; i < (int)inf.length(); ++i) {
      char c = inf[i]; if (c==' '||c=='\t'||c==':') continue;
      return c=='1';
    }
    return false;
  };

  for (int i = 0; i < 10; ++i) { if (engineRunning()) goto configured_nmea; delay(300); }

  Serial.println("GNSS not running; trying opposite SGPIO polarity...");
  sendAT("AT+CGNSPWR=0"); delay(150);
  sendAT("AT+SGPIO=0,4,1,0"); delay(150);
  sendAT("AT+CGNSPWR=1");
  for (int i = 0; i < 10; ++i) { if (engineRunning()) goto configured_nmea; delay(300); }

  Serial.println("Still not running; trying CGPIO control...");
  sendAT("AT+CGNSPWR=0"); delay(150);
  sendAT("AT+CGPIO=4,1,1"); delay(150);
  sendAT("AT+CGNSPWR=1");
  for (int i = 0; i < 10; ++i) { if (engineRunning()) goto configured_nmea; delay(300); }

configured_nmea:
  sendAT("AT+CGNSNMEA=511");
  sendAT("AT+CGNSRTMS=1000");
  return engineRunning();
}

// ---------- Smoketest (30s or until fix). Filters Galileo lines ----------
static void gnssSmoke() {
  // Turn on NMEA streaming but MUTE raw echo from AT (so $GA lines can't leak)
  gMuteEcho = true;
  sendAT("AT+CGNSTST=1");

  uint32_t tStart = millis();
  uint32_t lastInf = 0;
  int sentences = 0, gsv = 0;
  bool gotFix = false;

  while (millis() - tStart < 30000 && !gotFix) {
    // Drain streaming NMEA
    while (SerialAT.available()) {
      String line = SerialAT.readStringUntil('\n');
      line.trim();
      if (!line.startsWith("$")) continue;
      if (line.startsWith("$GA")) continue; // drop Galileo
      if (line.startsWith("$")) sentences++;
      if (line.startsWith("$GPGSV") || line.startsWith("$GLGSV") ||
          line.startsWith("$GNGSV") || line.startsWith("$BDGSV")) gsv++;
      Serial.println(line);
    }
    // Poll CGNSINF once per second WITHOUT echoing raw bytes
    if (millis() - lastInf > 1000) {
      lastInf = millis();
      String inf;
      if (sendAT("AT+CGNSINF", &inf, 1200, /*echo*/false)) {
        int p = inf.indexOf("+CGNSINF:");
        if (p >= 0) {
          int c1 = inf.indexOf(',', p);
          int c2 = inf.indexOf(',', c1 + 1);
          if (c1 > 0 && c2 > c1) {
            String runStr = inf.substring(p + 9, c1);
            String fixStr = inf.substring(c1 + 1, c2);
            runStr.trim(); fixStr.trim();
            if ((runStr == "1" || runStr == " 1") && fixStr == "1") gotFix = true;
          }
        }
      }
    }
  }

  sendAT("AT+CGNSTST=0", nullptr, 1200, /*echo*/false);
  gMuteEcho = false;

  if (gotFix) Serial.printf("NMEA summary: sentences=%d, gsv=%d, FIX ACQUIRED\n", sentences, gsv);
  else        Serial.printf("NMEA summary: sentences=%d, gsv=%d, no fix\n", sentences, gsv);
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("Settling 10s before touching the modem...");
  for (int i = 10; i > 0; --i) { Serial.printf("  %d...\n", i); delay(1000); }

  if (!modemPowerOn()) { Serial.println("FATAL: modem did not respond to AT."); return; }

  sendAT("ATE0");
  sendAT("AT+CMEE=2");
  sendAT("AT+IPR=57600");
  sendAT("AT&W");

  sendAT("ATI");
  sendAT("AT+CGMR");

  sendAT("AT+CNMP=38");
  sendAT("AT+CMNB=1");
  sendAT("AT+CBANDCFG=\"CAT-M\",3,20");
  sendAT("AT+CFUN=1");
  sendAT("AT+CPSMS=0");
  sendAT("AT+CEDRXS=0");
  sendAT("AT+CEREG=2");
  sendAT("AT+CREG=2");

  if (!waitForSIMReady()) { Serial.println("SIM not ready; aborting."); return; }
  if (!waitForNetwork())  { Serial.println("!! Registration timeout; aborting."); return; }

  uint32_t t0 = millis();
  while (millis() - t0 < 8000) {
    String r;
    if (sendAT("AT+CGATT?", &r) && r.indexOf("+CGATT: 1") >= 0) break;
    sendAT("AT+CGATT=1"); delay(400);
  }

  ClockInfo nowCi{};
  bool pdp = bringUpPDP(APN_PRIMARY);
  if (pdp) doNTPSync(&nowCi); else Serial.println("PDP failed; continuing without NTP.");

  if (pdp && nowCi.valid && shouldDownloadXTRA(nowCi)) {
    if (downloadAndApplyXTRA()) markXTRAJustApplied(nowCi);
    else Serial.println("XTRA download/apply failed (continuing).");
  }

  tearDownPDP();

  bool gnssOK = gnssStart();
  Serial.println(gnssOK ? "GNSS engine RUNNING ✅" : "GNSS engine NOT running ❌");
  gnssSmoke();

  Serial.println("Setup complete.");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 20000) { last = millis(); sendAT("AT+CGNSINF"); }
}