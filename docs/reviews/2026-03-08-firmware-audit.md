# Code Review Audit — 2026-03-08

Full firmware review identifying 16 issues across hardware pin config, power management, timing, and safety.

## Critical

| # | Issue | File | Status |
|---|-------|------|--------|
| 1 | GPIO 4 pin conflict (MODEM_PWRKEY = GPS_POWER_PIN) | main.cpp, gps.cpp | Fixed (2026-03-10) — Removed GPIO-based GPS power, use AT commands |
| 2 | sleepSec=0 causes infinite reboot loop | main.cpp | Already fixed — 300s minimum floor enforced in all 3 sleep paths |

## High

| # | Issue | File | Status |
|---|-------|------|--------|
| 3 | HTTP response status code not checked | modem.cpp:232 | Open |
| 4 | XTRA download race condition (ok/done flags) | gps.cpp:180 | Fixed (2026-03-10) — Wait for both ok AND done |
| 5 | NTP time sync not validated | gps.cpp:120 | Fixed (2026-03-10) — Validate +CNTP: 1 and year >= 2024 |
| 6 | Incomplete PDP teardown before sleep | main.cpp | Fixed (2026-03-10) — Full CNACT/CGACT/CGATT/CIPSHUT sequence |
| 7 | Incomplete modem shutdown sequence | main.cpp | Fixed (2026-03-10) — AT+CFUN=0 radio shutdown before PWRKEY |
| 8 | GPIO-based GPS power control is wrong (use AT cmds) | main.cpp, gps.cpp | Fixed (2026-03-10) — Same fix as #1 |

## Medium

| # | Issue | File | Status |
|---|-------|------|--------|
| 9 | JSON buffer overflow risk (1024 may be too small) | json.cpp:33 | Verified OK — Buffer already 2048 bytes |
| 10 | Static NB-IoT retry flag never resets | modem.cpp | Fixed (2026-03-10) — Non-static, reset on power-cycle |
| 11 | AT command pre-delay too short (20ms vs 100-200ms) | gps.cpp:32 | Fixed (2026-03-10) — Increased to 100ms |
| 12 | Anchor drift counter resets on every GPS fix | rtc_state.cpp:77 | Fixed (2026-03-09) |
| 13 | No GPS coordinate validation | gps.cpp | Fixed (2026-03-10) — Reject (0,0) and out-of-range |
| 14 | OTA update without battery check | ota.cpp | Fixed (2026-03-10) — Require ≥50% battery |
| 15 | Brownout recovery path incomplete | main.cpp:373 | Fixed (2026-03-09) |
| 16 | Network status not re-validated before OTA | main.cpp | Fixed (2026-03-10) — Check modem.isGprsConnected() |

## Priority

**Before next deployment:** #3
**All other issues resolved.**
