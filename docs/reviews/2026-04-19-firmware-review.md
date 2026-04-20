# Firmware Code Review — 2026-04-19

Full firmware review across all 19 categories requested. Based on full read of all source files,
CLAUDE.md files, and the previous 2026-03-08 audit. Only NEW findings are listed here.

---

## Summary Table

| ID | Severity | Category | File | Status |
|----|----------|----------|------|--------|
| BUG-01 | **CRITICAL** | Power/Sleep | rtc_state.h:48, battery.cpp:252 | Fixed |
| BUG-02 | **HIGH** | Power/Sleep | main.cpp:127 | Fixed |
| BUG-03 | **HIGH** | GPS | gps.cpp:239 | Fixed |
| BUG-04 | **HIGH** | Data Integrity | rtc_state.h:44, rtc_state.cpp:196 | Fixed |
| BUG-05 | **MEDIUM** | Build/Declarations | battery.cpp:60,80 | Fixed |
| BUG-06 | **MEDIUM** | Data Integrity | main.cpp:705 | Fixed |
| INFO-01 | LOW | Dead Code | battery.cpp:18 | Fixed |
| INFO-02 | LOW | Wave Analysis | wave.cpp:322 | Noted |
| INFO-03 | LOW | Battery | battery.cpp:31 | Noted |
| INFO-04 | LOW | OTA | gps.cpp:23 | Noted |
| CAT5-01 | LOW | Dead Code | main.cpp:1, modem.cpp:1, ota.cpp:1 | Fixed |
| CAT5-02 | LOW | Dead Code | config.h.example:38-39 | Fixed |
| CAT5-03 | LOW | Dead Code | main.cpp:215 | Fixed |
| CAT6-01 | INFO | Security | modem.cpp, ota.cpp | Noted (hardware limit) |
| CAT6-02 | MEDIUM | Security/OTA | ota.cpp:344 | Noted (by design) |
| CAT7-01 | LOW | Best Practices | ota.cpp:314-315 | Fixed |
| CAT7-02 | LOW | Best Practices / Cat4 | ota.cpp:154 | Fixed |
| CAT9-01 | LOW | Architecture | battery.h:12-16 | Noted |
| CAT12-01 | LOW | Power Efficiency | main.cpp:801 | Fixed |

---

## BUG-01 — CRITICAL: `uint16_t lastSleepMinutes` integer overflow

**Files:** `src/rtc_state.h:48`, `src/battery.cpp:252`, `src/main.cpp:433,765`

**Problem:**  
`lastSleepMinutes` is declared as `uint16_t` (max 65,535). The winter hibernate sleep schedule
returns `129,600` minutes (90 days × 24h × 60min). All three assignment sites:

```cpp
rtcState.lastSleepMinutes = (uint16_t)sleepMinutes;  // main.cpp:433, 765
rtcState.lastSleepMinutes = (uint16_t)sleepMinutes;  // battery.cpp:61
```

silently overflow: `129600 % 65536 = 64064 minutes (~44.5 days)`. The stored field is corrupted.

**Impact:**
- `getResetReasonString()` logs wrong sleep time on wake
- NVS `prefs.putUShort("lastSleep", ...)` would also store a wrong value (if added)
- The *actual* deep sleep time is unaffected because it uses UTC epoch arithmetic
  (`nextWakeUtc - nowUtc`), not `lastSleepMinutes`

**Fix:** Change field type to `uint32_t`. Max value 129,600 min fits in uint32_t (max ~4 billion).
Update NVS to `putULong`/`getULong` if persistence of this field is ever added.

---

## BUG-02 — HIGH: `mktime()` failure not handled — could cause near-infinite sleep

**File:** `src/main.cpp:127`

**Problem:**  
`adjustNextWakeUtcForQuietHours()` calls `mktime(&lt)` but never checks the return value:

```cpp
time_t adj = mktime(&lt);
return (uint32_t)adj;  // If mktime fails, adj == -1 → (uint32_t)-1 == 4,294,967,295
```

If the system clock is invalid (time not yet set, `lt` contains out-of-range fields), `mktime()`
returns `(time_t)-1`. Casting that to `uint32_t` gives `4,294,967,295` seconds (~136 years).
The sleep duration becomes `4,294,967,295 - nowUtc ≈ 136 years`.

**When this can trigger:**  
- Brownout fast-path sleep (called before NTP) when `now < 24*3600` → `candidate = 0 + sleepMin*60`
  → `candidateUtc = sleepMinutes * 60` (small number, ~hours since epoch)
  → `localtime_r` converts that to a date in 1970 → `lt.tm_hour` could be in range, mktime OK
- But if `candidate = 0` and `lt` has tm_hour=0 (midnight 1970-01-01), no adjustment is needed
- The safe path: if `lt.tm_hour >= 0 && lt.tm_hour < 6` with year 1970 → mktime on that tm struct
  should work. Unlikely to return -1.
  
Still, a defensive check is required. On a sealed buoy, a single failure path that causes
~136-year sleep means the buoy is permanently dead.

**Fix:** Add `if (adj == (time_t)-1) return candidateUtc;` before the cast.

---

## BUG-03 — HIGH: XTRA injection missing `AT+CGNSPWR=1` before `AT+CGNSCPY`

**File:** `src/gps.cpp:239-248`

**Problem:**  
`downloadAndApplyXTRA()` sends `AT+CGNSCPY` to copy the downloaded XTRA file into the GNSS
module without first powering the GNSS engine on:

```cpp
sendAT("AT+CGNSCPY", &cp, 7000);    // ← GNSS engine is OFF at this point
sendAT("AT+CGNSXTRA=1");
sendAT("AT+CGNSMOD=1,1,0,1");
sendAT("AT+CGNSCFG=1");
sendAT("AT+CGNSCOLD", nullptr, 5000);
```

Per `src/CLAUDE.md`: *"CGNSCPY requires CGNSPWR=1 first — verify sequence"*. Without the GNSS
engine running, `AT+CGNSCPY` may return ERROR and the function returns `false`. XTRA is never
marked as applied. On every boot the firmware downloads the file again but CGNSCPY always fails.
GPS falls back to a cold start every time, wasting 10–20 minutes per boot.

**Fix:** Add `sendAT("AT+CGNSPWR=1"); delay(300);` before the `AT+CGNSCPY` call, and add
`sendAT("AT+CGNSPWR=0");` before it to ensure a clean power cycle.

---

## BUG-04 — HIGH: Unsent JSON 512-byte buffer truncates payload → infinite retry loop

**Files:** `src/rtc_state.h:44`, `src/rtc_state.cpp:196-202`

**Problem:**  
The unsent JSON buffer is 512 bytes:
```cpp
char lastUnsentJson[512];   // rtc_state.h:44
```

A typical full payload contains ~30 fields (GPS, wave, battery, network, alerts, timestamps).
Measured payload size is approximately 650–900 bytes. `storeUnsentJson()` silently truncates:

```cpp
if (len >= sizeof(rtcState.lastUnsentJson))
    len = sizeof(rtcState.lastUnsentJson) - 1;  // truncate to 511 bytes
strncpy(rtcState.lastUnsentJson, json.c_str(), len);
```

The stored JSON is invalid (missing closing braces, truncated field values). On the next boot,
`sendJsonToServer()` sends it. The server parses invalid JSON and returns HTTP 400. The code then:
```cpp
storeUnsentJson(json);  // stores the SAME truncated JSON again
markUploadFailed();
```
The buoy retries the corrupted payload forever. Each retry wastes ~2 minutes of modem-on time
(~100mA × 120s = ~0.003 Ah per cycle).

**Fix:** Increase the buffer to 1024 bytes. This fits comfortably in RTC slow memory (8KB total,
current struct is well under 1KB). Update the `lastUnsentJson[512]` field to `lastUnsentJson[1024]`.
Also add a size check in `storeUnsentJson()`: if payload exceeds the buffer, log a warning and
don't store (drop it rather than store a corrupted fragment that will retry forever).

---

## BUG-05 — MEDIUM: `adjustNextWakeUtcForQuietHours` and `preparePinsAndSubsystemsForDeepSleep` undeclared in battery.cpp

**Files:** `src/battery.cpp:60,80`

**Problem:**  
`handleUndervoltageProtection()` in battery.cpp calls two functions defined in main.cpp:

```cpp
uint32_t nextWake = adjustNextWakeUtcForQuietHours(candidate);  // line 60
// ...
preparePinsAndSubsystemsForDeepSleep();  // line 80
```

Neither function is declared in any header. `battery.h` only declares:
```cpp
void powerOff3V3Rail();
void powerOffModem();
```

In standard C++, calling an undeclared function is a compile error. Some older GCC versions
may accept it with an implicit `int` return assumption — which would be WRONG for
`adjustNextWakeUtcForQuietHours()` (returns `uint32_t`). On a 32-bit platform, `int` and
`uint32_t` are the same width, but sign extension can corrupt values > 2,147,483,647.

**Fix:** Add declarations to `battery.h`:
```cpp
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);
void preparePinsAndSubsystemsForDeepSleep();
```

---

## BUG-06 — MEDIUM: `modem.localIP()` called 4 times instead of once

**File:** `src/main.cpp:705`

**Problem:**  
```cpp
ipStr = String((int)modem.localIP()[0]) + "." +
        String((int)modem.localIP()[1]) + "." +
        String((int)modem.localIP()[2]) + "." +
        String((int)modem.localIP()[3]);
```

`modem.localIP()` likely issues an AT command each invocation. This results in 4 AT+CIPSTA
(or similar) commands instead of 1. Minor battery impact (~4ms) but avoidable.

**Fix:**
```cpp
IPAddress localIp = modem.localIP();
ipStr = String((int)localIp[0]) + "." + String((int)localIp[1]) + "." +
        String((int)localIp[2]) + "." + String((int)localIp[3]);
```

---

## INFO-01 — LOW: Dead constant `BATTERY_UNDERVOLTAGE_SLEEP_HOURS`

**File:** `src/battery.cpp:18`

```cpp
#define BATTERY_UNDERVOLTAGE_SLEEP_HOURS 168
```

Defined but never referenced. Presumably intended for a fixed-duration undervoltage sleep,
replaced by `determineSleepDuration()`. Safe to remove.

---

## INFO-02 — LOW: Wave parabolic interpolation uses acceleration spectrum instead of displacement spectrum

**File:** `src/wave.cpp:322-330`

The `peakBin` is found by searching for maximum DISPLACEMENT PSD. But the parabolic interpolation
then uses magnitude² of the raw acceleration FFT output:
```cpp
float alpha_pk = re[peakBin-1]*re[peakBin-1] + fftIm[peakBin-1]*fftIm[peakBin-1];
```

This is the acceleration spectrum at neighboring bins, not displacement. The sub-bin Tp correction
may be slightly off (over- or under-estimated by a fraction of `df = 0.0097 Hz`). For a lake
buoy at 59°N measuring 0.05–2.0 Hz waves, this introduces < 0.1s error in Tp. Acceptable for
this application but worth noting for ocean deployments.

**Correct approach:** Store `dispPsd` for the three bins around `peakBin` during the integration
loop, then use those values for parabolic interpolation.

---

## INFO-03 — LOW: `checkBatteryChargeState()` never sets `chargingProblemDetected = true`

**File:** `src/battery.cpp:31-42`

`chargingProblemDetected` is cleared when charging is detected, but never set to `true` when
charging is absent for extended periods. The `else` branch only logs "Charging lost." with a
TODO comment:
```cpp
} else if (isCharging && voltage < (CHARGE_THRESHOLD - CHARGE_HYSTERESIS)) {
    isCharging = false;
    SerialMon.println("Charging lost.");
    // Start timer or flag for no charge if persists
}
```
This is a known incomplete feature (noted in `CLAUDE.md`). The alert field is always `false`
in the JSON payload unless manually set. No fix needed now, but track as debt.

---

## INFO-04 — LOW: XTRA URL is a private DDNS host — single point of failure

**File:** `src/gps.cpp:23`

```cpp
static const char* XTRA_URL = "http://trondve.ddns.net/xtra3grc.bin";
```

If this server goes offline (DDNS expiry, ISP change, server crash), XTRA can never be applied.
GPS falls back to cold start (20 min acquisition vs. 2–5 min with XTRA). Not a crash, but
degrades wake time budget significantly.

**Recommendation:** Add a secondary fallback URL from Qualcomm's XTRA CDN (publicly hosted).

---

---

## Cat 5 — Dead/Unused Code

### CAT5-01 — LOW: `#define TINY_GSM_MODEM_SIM7000` triple-defined

**Files:** `src/main.cpp:1`, `src/modem.cpp:1`, `src/ota.cpp:1`

**Problem:**
`TINY_GSM_MODEM_SIM7000` is `#define`d in three translation units. In practice, PlatformIO
compiles each `.cpp` file independently so there is no ODR violation — the macro is set before
`TinyGsmClient.h` is pulled in each unit, so each compilation succeeds.  However, redefinition
of the same macro across TUs with different values *can* cause subtle ODR bugs if `TinyGsmClient`
is ever compiled with different settings. The canonical fix is to put it — once — in the project's
`platformio.ini` build flags.

**Risk:** LOW (currently harmless because all three files define the same value).

**Recommended fix:**
Add to `platformio.ini` under `build_flags`:
```
-D TINY_GSM_MODEM_SIM7000
-D TINY_GSM_RX_BUFFER=1024
```
Then remove the two defines from `modem.cpp` and `ota.cpp`, leaving only `main.cpp` as the
TinyGSM "owner" until the INI approach is adopted. This is a build hygiene issue; addressed by
removing the duplicates from `modem.cpp:1` and `ota.cpp:1`.

**Fix applied:** Removed duplicate `#define TINY_GSM_MODEM_SIM7000` from `modem.cpp:1` and
`ota.cpp:1`.

---

### CAT5-02 — LOW: `ENABLE_GENTLE_MODEM_TIMING` defined but never tested in source

**Files:** `src/config.h.example:38-39`

**Problem:**
`config.h.example` defines `ENABLE_GENTLE_MODEM_TIMING 1` with a comment claiming it enables
conservative timing. No `#if ENABLE_GENTLE_MODEM_TIMING` guard appears anywhere in the source
tree. The timing is unconditionally conservative regardless of this flag.

**Risk:** LOW (dead documentation that could confuse a future developer toggling the flag expecting
a behaviour change).

**Fix applied:** Added clarifying comment to `config.h.example` noting the flag is reserved/
currently unused.

---

### CAT5-03 — LOW: `ENABLE_CPOWD_SHUTDOWN` block in `powerOffModem()` — confirmed active

**File:** `src/main.cpp:212–248`, `src/config.h.example:41`

**Finding:**
`ENABLE_CPOWD_SHUTDOWN` IS defined in `config.h.example` (enabled by default). The
`#if ENABLE_CPOWD_SHUTDOWN` block in `powerOffModem()` is therefore live code. The block
attempts AT+CPOWD=1 and waits up to 8 s for "NORMAL POWER DOWN". The block also contains a raw
`extern HardwareSerial Serial1` declaration inside the function body (main.cpp:215) which is
redundant (Serial1 is already accessible in this TU). No functional bug, but the inline extern
is an unusual pattern that should be removed.

**Fix applied:** Removed inline `extern HardwareSerial Serial1;` from inside `powerOffModem()` —
Serial1 is already globally visible via the Arduino framework in main.cpp.

---

## Items Confirmed Correct

The following were checked and are implemented correctly:

- **GPIO 25 held LOW in deep sleep** — `preparePinsAndSubsystemsForDeepSleep()` uses
  `gpio_hold_en(GPIO_NUM_25)` + `gpio_deep_sleep_hold_en()`. ✅
- **300-second minimum sleep floor** — enforced in all 3 sleep paths. ✅
- **Battery voltage measured before modem power** — `readBatteryVoltage()` called in `setup()`
  before `ensureModemReady()`. ✅
- **CFUN=0 before CPOWD** — `AT+CFUN=0` sent at start of `powerOffModem()`. ✅
- **PDP teardown before sleep** — Full CNACT→CGACT→CGATT→CIPSHUT sequence in `loop()` and
  in `tearDownPDP()` in gps.cpp. ✅
- **OTA battery guard** — `checkForFirmwareUpdate()` requires ≥50% before flashing. ✅
- **SHA-256 verified before `Update.end()`** — `memcmp()` run before `Update.end(true)`. ✅
- **WDT coverage** — Wave collection resets WDT every 50 samples; GPS polling intentionally
  does NOT reset WDT (safety net per design). ✅
- **RTC slow memory power domain** — `ESP_PD_DOMAIN_RTC_SLOW_MEM = ON` in sleep config. ✅
- **NVS restore before boot counter increment** — `restoreStateFromNvs()` called first in
  `rtcStateBegin()`. ✅
- **HDOP gate with 80% grace period** — Correct. ✅
- **3-day XTRA staleness check** — `XTRA_STALE_DAYS = 3` matches datasheet 72h. ✅
- **OTA image pending verify → `esp_ota_mark_app_valid_cancel_rollback()`** — Handled in
  `loop()` after successful run. ✅
- **NVS save before OTA restart** — `saveStateToNvs()` called before `ESP.restart()`. ✅
- **Modem power-on timing** — 2s PWRKEY LOW + 6s settle. Matches datasheet. ✅
- **Modem power-off timing** — 1.3s PWRKEY LOW (spec min 1.2s). ✅
- **Brownout recovery loop prevention** — brownout + <40% → immediate sleep with 300s floor. ✅
- **All pins INPUT before sleep** — `preparePinsAndSubsystemsForDeepSleep()` sets all
  modem and bus pins to INPUT. ✅
- **uint16_t ttfSeconds range** — max GPS timeout 1200s, well within uint16_t max 65535. ✅
- **Coordinate validation** — `(0,0)` rejected, geographic range checked. ✅
- **DS18B20 temperature error codes** — -127°C and 85°C explicitly rejected. ✅
- **WiFi/BT released at startup** — `WiFi.mode(WIFI_OFF)` in sleep prep; BT: `btStop()` +
  `esp_bt_controller_disable()` + `esp_bt_mem_release()` at start of `setup()`. ✅
- **ADC eFuse two-point calibration** — `esp_adc_cal_characterize()` attempts eFuse TP
  first, logs which calibration was used. ✅
- **First ADC read discarded** — `analogRead(PIN_ADC_BAT)` dummy read in `readBatteryVoltage()`. ✅

---

## Cat 6 — Security

### CAT6-01 — INFO: API key and OTA firmware transmitted over HTTP (hardware limitation)

**Files:** `src/modem.cpp:259`, `src/ota.cpp:77`

**Finding:**
`sendJsonToServer()` sends `X-API-Key: <key>` in plain HTTP (port 80). `downloadAndInstallFirmware()`
fetches firmware binaries over HTTP. TLS is broken on the SIM7000G module with this SDK combination
(documented in CLAUDE.md: *"OTA server: trondve.ddns.net (HTTP only, HTTPS broken on SIM7000G)"*).

**Risk:** INFO — Not a firmware bug. API key exposed on the radio link. For a buoy in a Norwegian
lake, RF interception is unlikely in practice. A proper fix requires upgrading to a supported
TLS library (e.g., MQTT-over-TLS or mbedTLS direct) or hardware that supports it.

**Action:** No code change possible without hardware support. Documented for awareness.

---

### CAT6-02 — MEDIUM: OTA proceeds without integrity check when `.sha256` unavailable

**File:** `src/ota.cpp:344`

**Code:**
```cpp
// If .sha256 file is unavailable, proceed without verification (graceful degradation).
String sha256Body = httpGetTinyGsm(sha256Url.c_str());
bool haveSha = parseHexSha256(sha256Body, expectedHash);
if (!haveSha) {
    SerialMon.println("No .sha256 file available — proceeding without integrity check");
}
```

**Risk:** MEDIUM — If the `.sha256` sidecar file is accidentally deleted or the server returns
an empty/malformed response, any binary at the firmware URL is flashed without verification.
On a sealed buoy, a corrupted flash = permanent brick.

**This is an intentional design choice** (graceful degradation for servers without sha256sum
infrastructure). Risk is mitigated by the fact that the OTA server is controlled by the operator.

**Recommendation:** Add a `#define OTA_REQUIRE_SHA256 1` guard in `config.h.example` to let
deployments opt into mandatory hash verification. Not implemented now — risk accepted per design.

---

## Cat 7 — Best Practices & Idioms

### CAT7-01 — LOW: Inline `extern` declarations instead of `#include` in `ota.cpp`

**File:** `src/ota.cpp:314-315` (before fix)

**Problem:**
`checkForFirmwareUpdate()` declared its dependencies inline:
```cpp
extern float getStableBatteryVoltage();
extern int estimateBatteryPercent(float);
```
These functions are already declared in `battery.h`. Inline externs bypass the header and
cause a compile error if the signatures ever change.

**Fix applied:** Added `#include "battery.h"` to ota.cpp includes; removed inline externs.

---

### CAT7-02 / CAT4-02 — LOW: `goto` in `parseHttpResponseHeaders()` in `ota.cpp`

**File:** `src/ota.cpp:154` (before fix)

**Problem:**
`goto read_headers;` used to jump from the status-line loop to the header-parsing loop.
This is the same pattern removed from `gnssStart()` in Cat4. `goto` obscures control flow
and is banned in most embedded coding standards (MISRA C, AUTOSAR).

**Fix applied:** Replaced with a `bool gotStatus` flag; the inner loop breaks on first complete
status line and the outer while condition prevents re-running the status-line logic.

---

## Cat 8 — Testing Gaps

### CAT8-01 — INFO: No unit tests (inherent hardware dependency)

**Finding:**
The firmware has no automated test suite. All logic depends on hardware (modem AT responses,
ADC readings, I2C sensor, GPS NMEA stream). This is standard for tightly-coupled embedded
firmware where a hardware-in-loop (HIL) setup would be required for meaningful tests.

**Risk:** INFO — Acceptable for this type of system. The review itself serves as the primary
correctness validation mechanism.

**Recommended improvement (future):** Extract pure-logic functions (OCV table lookup,
`compareVersions()`, `adjustNextWakeUtcForQuietHours()`, `determineSleepDuration()`,
`estimateBatteryPercent()`) into a platform-independent library and test on host with a
native PlatformIO env. These functions have no hardware dependencies.

---

## Cat 9 — Architecture

### CAT9-01 — LOW: `battery.h` forward-declares functions defined in `main.cpp`

**File:** `src/battery.h:12-16`

**Code:**
```cpp
// Power controls and sleep helpers provided by the main module (main.cpp)
void powerOff3V3Rail();
void powerOffModem();
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);
void preparePinsAndSubsystemsForDeepSleep();
```

**Problem:**
`battery.cpp` calls power-control functions defined in `main.cpp`. The comment acknowledges
this coupling explicitly. Creating a circular dependency (battery → main → battery) is
technically safe in C++ at link time (single program, no ODR violation) but architecturally
incorrect. If `battery.cpp` were ever compiled standalone (unit test), it would fail to link.

**Risk:** LOW — No runtime impact. The buoy will never fail because of this.

**Recommended fix (future refactor):** Introduce a `power_control.h` / `power_control.cpp`
module that owns `powerOff3V3Rail()`, `powerOffModem()`, `preparePinsAndSubsystemsForDeepSleep()`,
and `adjustNextWakeUtcForQuietHours()`. Both `battery.cpp` and `main.cpp` would include it.
Not implemented — would require touching 4 files for a purely architectural improvement.

---

## Cat 10 — Brownout & Power Sequencing

All items verified correct. See "Items Confirmed Correct" section for the full checklist.

Additional verification:
- **3V3 rail off before modem activation** — `powerOn3V3Rail()` and `powerOnModem()` are
  independent; modem is powered via `MODEM_POWER_ON` (GPIO 23), sensor rail via `POWER_3V3_ENABLE`
  (GPIO 25). Wave collection runs on 3V3 rail, modem is powered separately. ✅
- **Brownout loop prevention** — `brownoutRecovery && pct < 40` → direct sleep with 300s floor,
  never re-enters modem/GPS path. ✅
- **PDP teardown on all paths** — `tearDownPDP()` called in gps.cpp after GPS; main.cpp tears
  down PDP manually before `powerOffModem()`. Both paths confirmed. ✅

---

## Cat 11 — SIM7000G Timing Compliance

**PWRKEY ON timing:** `powerOnModem()` pulls PWRKEY HIGH for 100ms, then LOW for 2000ms (spec: ≥1.0s). ✅

**PWRKEY OFF timing:** `powerOffModem()` pulls PWRKEY LOW for 1300ms (spec: ≥1.2s). ✅

**Pre-AT delay:** 6000ms settle after PWRKEY sequence + `delay(2000)` in `ensureModemReady()`.
Total ≥6s before first AT command. ✅

**CGNSPWR → CGNSINF:** 50ms minimum enforced via `preATDelay()` (100ms). ✅

**CNTP timeout:** Extended to 60s (was 15s) in BUG-07/BUG-08 fix. Within SIM7000G spec. ✅

**XTRA injection sequence:** Fixed in BUG-03. `CGNSPWR=1` → `CGNSCPY` → `CGNSXTRA=1` → `CGNSCOLD`. ✅

**PDP teardown order:** `CNACT=0` → `CGACT=0` → `CGATT=0` → `CIPSHUT`. ✅

**Shutdown order:** `CFUN=0` → `CPOWD=1` → hard PWR key. ✅

---

## Cat 12 — Power Efficiency

### CAT12-01 — LOW: `uint32_t` overflow in `DEBUG_NO_DEEP_SLEEP` delay loop

**File:** `src/main.cpp:801` (before fix)

**Problem:**
```cpp
uint32_t remainingMs = (uint32_t)sleepMinutes * 60UL * 1000UL;
```
For the winter hibernate schedule (129,600 min): `129600 × 60 × 1000 = 7,776,000,000 ms`
exceeds `uint32_t` max (4,294,967,295). The computed value wraps to ~3.48 billion ms (~40 days)
instead of ~90 days. The WDT (45 min) would fire and reboot the device.

**Production impact:** NONE — `DEBUG_NO_DEEP_SLEEP` is `0` in all production builds.
Development impact: misleading debug behavior in winter-schedule testing.

**Fix applied:** Changed to `uint64_t remainingMs = (uint64_t)sleepMinutes * 60ULL * 1000ULL;`.

---

Additional power efficiency items confirmed correct:
- **WiFi/BT released at boot** — `btStop()` + `esp_bt_mem_release()` called before wave measurement. ✅
- **Serial logging overhead** — Acceptable; Serial is at 115200 baud, ~10KB/s, logs are <1KB total. ✅
- **All retry loops bounded** — NTP 60s, GPS 20min max, HTTP 3 retries with 2s backoff, APN 2 candidates. ✅
- **Modem reuse after GPS** — GPS shares modem; `connectToNetwork(apn, true)` skips pre-cycle when modem is warm. ✅
- **No duplicate AT commands detected** — each AT command issued once per required operation. ✅

---

## Cat 13 — Deep Sleep Integrity

All items verified correct:
- **All paths reach deep sleep** — Brownout path, undervoltage path, and normal path all call
  `esp_deep_sleep_start()`. Loop() ends with sleep unconditionally. ✅
- **No infinite loops without WDT** — All loops are bounded by timeout or WDT. ✅
- **WDT 45 min** — `esp_task_wdt_init(2700, true)` in setup(). ✅
- **RTC config** — `RTC_SLOW_MEM=ON`, `RTC_FAST_MEM=OFF`, `RTC_PERIPH=OFF`, `XTAL=OFF`. ✅
- **GPIO isolation** — All pins set INPUT before sleep; POWER_3V3_ENABLE held LOW via gpio_hold. ✅
- **RTC struct size** — Calculated ~1109 bytes (within 8KB limit). ✅
- **NVS restore before RTC use** — `restoreStateFromNvs()` first call in `rtcStateBegin()`. ✅

---

## Cat 14 — GPS Fix Quality & Timeout

All items verified correct:
- **HDOP threshold 3.0** with 80% grace logic (accept any fix after 80% of timeout). ✅
- **GPS timeout < WDT** — Max GPS timeout ~20 min; WDT = 45 min. ✅
- **XTRA validity 3 days** — `XTRA_STALE_DAYS = 3` matches datasheet. ✅
- **Hot/warm/cold selection** — Smart start: if XTRA just applied → cold; if XTRA fresh (<72h) → warm; else hot. ✅
- **Coordinate validation** — `(0,0)` rejected; lat -90 to +90, lon -180 to +180 checked. ✅
- **TTF accounting** — `ttfSeconds` stored as uint16_t, max GPS timeout 1200s, well within range. ✅

---

## Cat 15 — OTA Safety

All items verified correct:
- **Battery ≥50% before OTA** — Hard check in `checkForFirmwareUpdate()`, returns false immediately. ✅
- **SHA-256 verified before `Update.end()`** — `memcmp(computedHash, expectedHash, 32)` before
  `Update.end(true)`. ✅
- **5-minute download timeout** — `OTA_DOWNLOAD_TIMEOUT_MS = 5UL * 60UL * 1000UL`. ✅
- **Rollback flow** — `ESP_OTA_IMG_PENDING_VERIFY` checked at boot; `esp_ota_mark_app_valid_cancel_rollback()`
  called after first successful `loop()` run. ✅
- **NVS saved before restart** — `saveStateToNvs()` called before `ESP.restart()`. ✅
- **OTA works without .sha256** — Graceful degradation (see CAT6-02). ✅

---

## Cat 16 — Data Integrity

All items verified correct:
- **JSON buffer (2048 bytes)** — Estimated worst-case serialized payload ~900 bytes; fits in
  `StaticJsonDocument<2048>`. ✅
- **RTC unsent buffer (1024 bytes)** — Increased from 512 in BUG-04 fix. Sized above worst-case
  JSON output. Oversized payloads are now dropped rather than truncated (BUG-04 fix). ✅
- **GPS precision ≥6 decimals** — `lat`/`lon` stored as `float` (7 significant digits). At 59°N,
  float gives ~1.1m precision, adequate for buoy tracking. ✅
- **Timestamp validity** — `time(NULL) < 24*3600` treated as invalid; fallback to last GPS time
  or 0 if unknown. ✅
- **NaN handling** — `isnan(waterTemp)` checked; `temp_valid` field set accordingly. ✅

---

## Cat 17 — Seasonal & Schedule Logic

All items verified correct:
- **Correct season transitions at 59°N** — Winter Nov–Mar, Shoulder Apr–May/Sep–Oct, Summer Jun–Aug. ✅
- **CET/CEST timezone** — `configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", NTP_SERVER)` in `setup()`. ✅
- **uint16_t overflow fixed** — BUG-01 fix: `lastSleepMinutes` changed to `uint32_t`. ✅
- **Hysteresis at boundaries** — ±2% SoC offset based on voltage trend prevents oscillation. ✅
- **Winter hibernate (129,600 min / ~90 days)** — `uint32_t` confirmed large enough (max ~4B). ✅

---

## Cat 18 — Network Resilience

All items verified correct:
- **APN fallback** — `testMultipleAPNs()` tries `["telenor", "telenor.smart"]` with
  a hard-coded bounded list (2 APNs). Returns false if neither works. ✅
- **HTTP retry backoff** — `sendJsonToServer()` retries 3× with 2s delay between attempts. ✅
- **Re-validation before OTA** — `modem.isGprsConnected()` checked before OTA in `loop()`. ✅
- **Invalid IP handling** — `modem.localIP()` result stored once then decomposed into String
  (BUG-06 fix). If IP is 0.0.0.0 the string will be "0.0.0.0" — harmless in the JSON payload. ✅

---

## Cat 19 — Hardware Errata

All items verified correct:
- **GPIO 4 conflict** — `MODEM_PWRKEY` and `GPS_POWER_PIN` share GPIO 4. Conflict is resolved by
  removing `GPS_POWER_PIN` (GNSS is internal to SIM7000G and controlled via AT commands, not GPIO).
  Comment in main.cpp confirms this. ✅
- **ADC eFuse calibration** — `esp_adc_cal_characterize()` uses `ESP_ADC_CAL_VAL_EFUSE_TP` with
  fallback to `EFUSE_VREF` then default. Logs which was used. ✅
- **Voltage divider tolerance** — Calibration step accounts for actual measured Vref; tolerance
  impact is absorbed into the calibration. ✅
- **DS18B20 powered via 3V3 rail** — OneWire bus on GPIO 13 is powered via `POWER_3V3_ENABLE`
  (GPIO 25 rail). Not parasitic mode. ✅

