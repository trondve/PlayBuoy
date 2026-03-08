# PlayBuoy Code Review Audit

**Date:** 2026-03-08
**Scope:** Modem, GPS, power management, and overall firmware reliability
**Branch:** `claude/code-review-audit-D0caS`

---

## Summary

A comprehensive code review of the PlayBuoy firmware identified **16 significant issues** across hardware pin configuration, power management, timing, correctness, and safety. The most critical findings are a GPIO pin conflict that can cause modem/GPS interference, and a potential infinite reboot loop when sleep duration calculates to zero.

---

## Critical Issues

### 1. GPIO 4 Pin Conflict (MODEM_PWRKEY = GPS_POWER_PIN)
**Severity:** CRITICAL
**Files:** `src/main.cpp:45,53`

GPIO 4 is assigned to both `MODEM_PWRKEY` and `GPS_POWER_PIN`:
```cpp
#define MODEM_PWRKEY 4      // Line 45
#define GPS_POWER_PIN 4     // Line 53
```

**Impact:**
- `powerOnGPS()` holds GPIO 4 HIGH, interfering with the modem PWRKEY pulse sequence
- PWRKEY requires LOW→HIGH transitions to toggle modem power
- GPS and modem power control will corrupt each other's state

**Note:** The SIM7000G has integrated GNSS — there is no external GPS module needing separate power control. The WORKING_NTP+XTRA+GPS.cpp reference file does NOT use separate GPIO-based GPS power control, only AT commands (`AT+CGNSPWR`).

**Fix:** Remove the separate `powerOnGPS()`/`powerOffGPS()` GPIO control entirely. Use only GNSS AT commands to manage GPS, as the reference implementation does.

---

### 2. Potential Infinite Reboot Loop (sleepSec = 0)
**Severity:** CRITICAL
**Files:** `src/main.cpp:797-800`

```cpp
uint32_t sleepSec = 0;
nowUtc = (uint32_t)time(NULL);
if (nextWakeUtc > nowUtc) sleepSec = nextWakeUtc - nowUtc;
esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
```

If `nextWakeUtc <= nowUtc` (due to time drift, long processing, or RTC corruption), `sleepSec` stays 0, causing immediate wakeup and an infinite boot loop that will drain the battery.

**Fix:** Add a minimum sleep guard:
```cpp
if (sleepSec < 3600) sleepSec = 3600; // Minimum 1 hour
```

---

## High Severity Issues

### 3. HTTP Response Status Code Not Checked
**Files:** `src/modem.cpp:232-275`

`sendJsonToServer()` treats ANY HTTP response (including 4xx, 5xx errors) as success. It only checks that response data was received, not the status code. Compare with `src/ota.cpp` which properly parses HTTP status codes.

**Fix:** Parse the HTTP status line and only treat 2xx responses as success.

---

### 4. XTRA Download Race Condition
**Files:** `src/gps.cpp:180-203`

The `ok` flag (HTTP 200) and `done` flag (download complete) can race. If `+HTTPTOFSRL: 0` arrives before `+HTTPTOFS: 200`, the function returns failure even though the file was downloaded successfully.

**Fix:** Continue polling until both flags are set or timeout expires, rather than breaking on `done` alone.

---

### 5. NTP Time Sync Not Validated
**Files:** `src/gps.cpp:120-139`

`doNTPSync()` checks CCLK format validity but not whether time was actually updated by NTP (vs stale RTC from previous boot). The function returns success even if NTP didn't actually sync.

**Impact:** GPS timestamps, sleep calculations, and quiet hours logic could use stale or incorrect time.

---

### 6. Incomplete PDP Teardown Before Sleep
**Files:** `src/main.cpp:775-780`, `src/gps.cpp:84-97`

PDP context teardown (CNACT→CGACT→CGATT→CIPSHUT) happens in the GPS flow but NOT in the main shutdown path before deep sleep. An active PDP context during sleep could drain battery through the modem.

**Fix:** Call full PDP teardown sequence before `powerOffModem()`.

---

### 7. Incomplete Modem Shutdown Sequence
**Files:** `src/modem.cpp:201-242`

`powerOffModem()` only pulses PWRKEY without:
- PDP teardown
- Radio shutdown (`AT+CFUN=0`)
- Modem sleep mode engagement

**Fix:** Send `AT+CFUN=0` and PDP teardown commands before PWRKEY pulse.

---

### 8. GPS Power Control Design Flaw
**Files:** `src/main.cpp:296-319`

Separate `powerOnGPS()`/`powerOffGPS()` functions manipulate GPIO 4 (which is MODEM_PWRKEY). The SIM7000G's GNSS is internal to the modem and controlled via AT commands, not GPIO.

**Fix:** Remove GPIO-based GPS power control. This also resolves Issue #1.

---

## Medium Severity Issues

### 9. JSON Buffer Overflow Risk
**Files:** `src/json.cpp:33`

`StaticJsonDocument<1024>` may overflow with the full payload (wave, tide, rtc, net, alerts objects plus string fields like operator name and APN).

**Fix:** Increase to `StaticJsonDocument<2048>` or measure actual serialized size.

---

### 10. Static NB-IoT Retry Flag
**Files:** `src/modem.cpp:90-102`

`static bool triedNBIoT = false` prevents NB-IoT retry for the entire boot cycle, even across modem power cycles.

**Fix:** Reset the flag when the modem is power-cycled.

---

### 11. AT Command Pre-Delay Too Short
**Files:** `src/gps.cpp:32`

Current delay is 20ms. The WORKING_NTP+XTRA+GPS.cpp reference uses 1000ms. Aggressive timing may cause intermittent AT command failures.

**Fix:** Increase to at least 100-200ms.

---

### 12. Anchor Drift Counter Reset on Every GPS Fix
**Files:** `src/rtc_state.cpp:77`

`updateLastGpsFix()` resets `anchorDriftCounter` to 0 on every successful fix, making drift detection impossible across boot cycles.

**Fix:** Only reset counter when drift is within acceptable range, not on every fix.

---

### 13. No GPS Coordinate Validation
**Files:** `src/gps.cpp:255-293`

No sanity checks on parsed GPS coordinates:
- No check for (0.0, 0.0)
- No range validation for expected deployment area (~59.4N, ~5.3E)

**Fix:** Reject coordinates outside a reasonable bounding box.

---

### 14. OTA Update Without Battery Check
**Files:** `src/ota.cpp:161-231`

OTA download and flash write proceeds without checking battery level. If battery dies mid-flash, the device could be bricked (permanently sealed, no physical access).

**Fix:** Check battery > 50% before allowing OTA update.

---

### 15. Brownout Recovery Path Incomplete
**Files:** `src/main.cpp:373-380`

Brownout detection logs a message but takes no mitigating action (e.g., skip GPS, reduce power draw, increase delays).

---

### 16. Network Status Not Re-validated Before OTA
**Files:** `src/main.cpp:676-759`

`networkConnected` flag may become stale between GPS operations and OTA/upload. No re-check before critical network operations.

---

## Additional Code Quality Notes

- **Unused forward declarations:** `extern void powerOnGPS()/powerOffGPS()` in gps.cpp reference the conflicted GPIO 4
- **Credentials in version control:** `src/config.h` contains API_KEY despite .gitignore entry
- **`config.h.example` incomplete:** Missing several defines present in config.h (USE_CUSTOM_DNS, DNS_PRIMARY, DNS_SECONDARY, BATTERY_CALIBRATION_FACTOR, etc.)
- **Unused functions:** `getSampleDurationMs()`, `markFirmwareUpdateAttempted()`, `getRelativeAltitude()`, `readTideHeight()`, all magnetometer calibration stubs
- **Unused dependency:** Adafruit BMP280 Library in platformio.ini (barometer is useless in sealed enclosure)
- **Junk file:** `.py` at repo root contains `less` command help text

---

## Prioritized Recommendations

### Before Next Deployment
1. **Fix GPIO 4 conflict** — Remove GPIO-based GPS power control, use AT commands only
2. **Add minimum sleep guard** — Prevent reboot loop with `sleepSec < 3600` check
3. **Parse HTTP status codes** in `sendJsonToServer()`
4. **Complete PDP teardown** before modem power-off in main shutdown path
5. **Add battery check before OTA** — Require > 50% battery

### Before Production
6. Validate GPS coordinates before storing
7. Fix XTRA download race condition
8. Review and increase JSON buffer size
9. Fix anchor drift counter logic
10. Increase AT command pre-delay

### Cleanup
11. Remove unused functions and dependencies
12. Delete junk `.py` file
13. Complete `config.h.example` with all defines
14. Make `build_all_buoys.py` portable (remove hardcoded Windows path)
