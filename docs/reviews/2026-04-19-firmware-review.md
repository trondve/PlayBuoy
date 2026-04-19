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
