# Reference Implementations

This directory contains working reference implementations tested and verified on PlayBuoy hardware.

## Files

### WORKING_BATTERY_MEASUREMENT_main.cpp
Confirmed working battery voltage measurement implementation.

**Features:**
- 5× 50-sample burst averaging with median-of-five
- Manual ADC formula (method 1): `((raw/4095) * 2 * 3.3 * 1.1)`
- 2ms inter-sample spacing
- 2s + 1s delays between bursts

**Usage:**
Reference for battery voltage measurement accuracy. Older version before migration to `esp_adc_cal` hardware calibration.

### WORKING_NTP+XTRA+GPS.cpp
Confirmed working GPS, NTP, and XTRA sequence.

**Features:**
- NTP sync via modem AT commands (AT+CNTP)
- XTRA download and application (CGNSCPY → CGNSXTRA → CGNSCOLD)
- GNSS power-on sequence with 60s smoke test
- PDP setup/teardown for GPS operation
- NMEA sentence parsing

**Usage:**
Reference for the complete GPS initialization pipeline. Current implementation in `src/gps.cpp` is based on this.

## When to Use

Use these references when:
- Debugging similar functionality
- Comparing new implementations against known-working code
- Troubleshooting a regression
- Understanding the original tested approach

## Notes

These are **frozen reference snapshots**. The main codebase (`src/`) may have evolved with optimizations and improvements. Always check:
1. `src/` for current production code
2. `docs/ARCHITECTURE.md` for architectural decisions
3. `docs/decisions/` for rationale behind changes
