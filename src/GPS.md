# GPS / GNSS — Local Context

## Purpose
Get a GPS fix for buoy position tracking and anchor drift detection. Pipeline: NTP time sync → XTRA ephemeris download → GNSS engine start → 60s smoke test → fix polling with battery-adaptive timeout.

## What can go wrong
- **PDP not torn down before GNSS**: SIM7000G shares one radio between cellular data and GPS. If PDP context is active, `AT+CGNSPWR=1` silently fails or gets no satellites. Must call `tearDownPDP()` first.
- **XTRA download race condition**: The `ok` flag (HTTP 200) and `done` flag (download complete) can arrive in either order. Current code polls both but may miss if `done` arrives before `ok`. Known bug (#4 in audit).
- **Cold start = 20+ minutes**: Without fresh XTRA data, first fix can take 20 minutes at 500mA. That's ~170mAh — significant for a 6000mAh battery.
- **NTP sync not validated**: `doNTPSync()` returns success if CCLK parses correctly, but doesn't verify NTP actually updated the clock vs stale RTC. Known bug (#5 in audit).
- **GPIO 4 conflict**: `powerOnGPS()` uses GPIO 4, which is also MODEM_PWRKEY. Setting it HIGH interferes with modem power control. Known bug (#1 in audit). The SIM7000G GNSS is internal — should use AT commands only.
- **3 GPIO polarity variants**: `gnssStart()` tries 3 different GPIO/SGPIO configurations to handle board revision differences. This is correct — don't simplify to one variant.

## XTRA ephemeris data
- Cached in SIM7000G filesystem at `/customer/xtra3grc.bin`
- Valid for ~7 days (checked via `shouldDownloadXTRA()` with Preferences)
- Downloaded from `http://trondve.ddns.net/xtra3grc.bin`
- Applied via: `AT+CGNSCPY` → `AT+CGNSXTRA=1` → `AT+CGNSCOLD`
- Without XTRA: cold start 15-25 min. With XTRA: warm start 1-5 min.

## GPS fix timeout (battery-adaptive)
| Scenario | Battery >60% | 40-60% | <40% |
|----------|-------------|--------|------|
| First fix | 20 min | 15 min | 10 min |
| Subsequent | 10 min | 7.5 min | 5 min |

GPS skipped entirely if last fix was <24 hours ago.

## Key code paths
- `getGpsFix(timeoutSec)` → `syncTimeAndMaybeApplyXTRA()` → `gnssStart()` → `gnssSmoke60s()` → polling loop
- `parseCgnsInfFix()`: Parses CGNSINF response fields (run, fix, lat, lon, epoch, HDOP)
- `gnssSmoke60s()`: Streams NMEA while polling CGNSINF every second. Exits early on fix.

## Rules
- Never start GNSS without tearing down PDP first
- Never reduce the 60s smoke test — satellites need acquisition time
- Never reduce GPS fix timeout — it's the user's #1 request
- After GPS fix, call `connectToNetwork(apn, true)` to reuse warm modem
