# Code Review Audit — 2026-03-08

Full firmware review identifying 16 issues across hardware pin config, power management, timing, and safety.

## Critical

| # | Issue | File | Status |
|---|-------|------|--------|
| 1 | GPIO 4 pin conflict (MODEM_PWRKEY = GPS_POWER_PIN) | main.cpp:45,53 | Known, documented |
| 2 | sleepSec=0 causes infinite reboot loop | main.cpp:797 | Needs min sleep guard |

## High

| # | Issue | File | Status |
|---|-------|------|--------|
| 3 | HTTP response status code not checked | modem.cpp:232 | Open |
| 4 | XTRA download race condition (ok/done flags) | gps.cpp:180 | Open |
| 5 | NTP time sync not validated | gps.cpp:120 | Open |
| 6 | Incomplete PDP teardown before sleep | main.cpp:775 | Open |
| 7 | Incomplete modem shutdown sequence | modem.cpp:201 | Open |
| 8 | GPIO-based GPS power control is wrong (use AT cmds) | main.cpp:296 | Open (same root as #1) |

## Medium

| # | Issue | File | Status |
|---|-------|------|--------|
| 9 | JSON buffer overflow risk (1024 may be too small) | json.cpp:33 | Open |
| 10 | Static NB-IoT retry flag never resets | modem.cpp:90 | Open |
| 11 | AT command pre-delay too short (20ms vs 100-200ms) | gps.cpp:32 | Open |
| 12 | Anchor drift counter resets on every GPS fix | rtc_state.cpp:77 | Fixed (2026-03-09) |
| 13 | No GPS coordinate validation | gps.cpp:255 | Open |
| 14 | OTA update without battery check | ota.cpp:161 | Open |
| 15 | Brownout recovery path incomplete | main.cpp:373 | Fixed (2026-03-09) |
| 16 | Network status not re-validated before OTA | main.cpp:676 | Open |

## Priority

**Before next deployment:** #1, #2, #3, #6, #7, #14
**Before production:** #4, #5, #9, #13
**Cleanup:** #10, #11, #16
