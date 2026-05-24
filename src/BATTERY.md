# Battery Measurement & Management — Local Context

## Purpose
Measure battery voltage accurately before powering any subsystems (open-circuit voltage). Estimate State of Charge from OCV table. Determine sleep duration based on SoC and season. Enforce critical guard to prevent bricking.

## What can go wrong
- **Measuring under load**: Battery must be measured BEFORE powering modem, GPS, or sensors. Under 2A modem load, voltage sags 150-300mV — gives false critical readings.
- **Lowering critical guard**: The 3.70V / 25% threshold protects against SIM7000G failure (needs ≥3.55V under 2A peak) and irreversible 18650 cell damage (below 20% = permanent capacity loss). Hooks block this, but be aware.
- **Wrong OCV table**: Current table is based on Samsung INR18650-35E (LiitoKala Lii-35S) at 25°C reference. Runtime correction adds 1.5mV/°C × (25 − tempC) for all temps below 25°C. 2× cells in parallel (same voltage, double capacity).
- **Fallback month = summer**: If RTC time is invalid and fallback is August, summer schedule kicks in (2h sleep at >80%) — drains battery in winter darkness. Fixed: fallback is now January.

## ADC measurement pipeline (power.cpp)
```
beginPowerMonitor()
  → analogSetWidth(12), analogSetPinAttenuation(ADC_11db)
  → esp_adc_cal_characterize(ADC1, 11dB, 12bit, 1100, &chars)
  → Logs calibration source (eFuse Two Point > eFuse Vref > Default)

readBatteryVoltage()
  → Discard 1 reading (ESP32 ADC channel-switch artifact)
  → 3 bursts × 50 samples each, 200µs inter-sample spacing
  → Each sample: analogRead() → esp_adc_cal_raw_to_voltage() → accumulate
  → Burst average in mV → × DIVIDER_RATIO (2.0) → battery voltage
  → medianOfThree(burst1, burst2, burst3)
  → Log spread in mV (warn if >20mV)
  → Total time: ~30ms
```

## OCV table (battery.cpp)
- Cell: Samsung INR18650-35E (3500mAh, inside LiitoKala Lii-35S)
- 101 points (0-100%), 1% resolution, binary search + linear interpolation — O(log n) lookup
- Table at 25°C reference (standard Samsung 35E datasheet values); runtime correction adds 1.5mV/°C × (25 − tempC) for tempC < 25°C
- Key values: 2.950V = 0%, 3.555V = 25%, 3.720V = 50%, 3.932V = 70%, 4.071V = 80%, 4.200V = 100%
- Flat plateau 3.55-3.72V maps to 25-50% — small voltage changes = large SoC swings (normal for this cell)

## Sleep schedule design
- **Target SoC: 40-60%** — optimal for 18650 longevity (data center standard)
- **>80%: aggressive discharge** — 2h summer, 12h winter. Intentional: forces SoC down
- **<25%: hibernate** — 1 week summer, 3 months winter. Await solar recovery
- **Quiet hours**: 00:00-05:59 local → push wake to 06:00 (no one checks water temp at 3am)
- **Minimum sleep floor**: 300 seconds (prevents reboot loops from sleepSec=0)

## ADC offset

Boot-time ADC reads approximately **10mV below multimeter OCV** at the same charge state (observed consistently across multiple boots at 77–82% SoC). Apply +10mV correction when comparing ADC readings to external measurements. Does not affect firmware decisions because all thresholds were tuned against ADC readings directly.

## OCV–mAh conversion (7000mAh 1S2P pack)

The OCV curve is not linear and not intuitive. Voltage drops *slowest* per mAh near full charge and *fastest* in the 70–80% region. The buoy normally operates in this steepest zone, which makes OCV measurements most informative there.

| SoC range | Voltage range | mV per 1% SoC | mAh/mV | Notes |
|-----------|--------------|--------------|--------|-------|
| 90–100% | 4.153–4.200V | 4.7 mV/% | ~15 mAh/mV | Slowest drop — hard to tell when full |
| 80–90% | 4.071–4.153V | 8.2 mV/% | ~8.5 mAh/mV | |
| 70–80% | 3.932–4.071V | 13.9 mV/% | ~5.0 mAh/mV | Steepest region — most sensitive |
| 50–70% | 3.720–3.932V | 8.6–12.6 mV/% | ~5–8 mAh/mV | |
| 30–50% | 3.596–3.720V | 6.2 mV/% | ~11 mAh/mV | Flat plateau — SoC estimate unreliable |
| 0–10% | 2.950–3.350V | 40 mV/% | ~1.8 mAh/mV | Steepest absolute drop |

All figures assume OCV-to-OCV measurements (load removed, voltage has recovered). See methodology note below.

## Observed Wake Cycle Power Consumption

All measurements are OCV-to-OCV. **Methodology matters:** after a heavy load cycle, Li-ion cells take 15–40 minutes to recover to true OCV. After-readings taken within a few minutes of sleep are still suppressed by load sag and will overstate the drop. For valid measurements: use multimeter for both before and after, and wait ≥40 min post-sleep before taking the after reading.

| Date | Cycle type | Duration | ΔV (OCV) | Est. mAh | Soak | Notes |
|------|-----------|---------|----------|----------|------|-------|
| 2026-05-15/16 | Short (no GPS) | ~5 min | 4.083→4.077V (−6mV) | ~30 mAh | unknown | Soak time unknown — may be overstated |
| 2026-05-15/16 | GPS timeout | 25 min | 4.127→4.089V (−38mV) | ~250 mAh | unknown | Soak unknown — likely overstated; use as upper bound |
| 2026-05-15/16 | GPS timeout | 25 min | 4.085→4.061V (−24mV) | ~165 mAh | unknown | Soak unknown — likely overstated; use as upper bound |
| 2026-05-18 | GNSS on, no sky view | 25 min | 4.037→4.035V (−2mV) | ~10 mAh | 40 min ✓ | Satellite count stuck at 14–15 throughout; GNSS engine ran but couldn't decode signals (indoors, rain). Not a normal GPS-timeout cycle. |
| 2026-05-18 | GPS success, XTRA fresh | 5.5 min | 4.039→4.034V (−5mV) | ~25 mAh | immediate* | *30-min soak pending. Before derived from ADC+10mV. TTF=106s, HDOP=0.9, 21 sats. |

**Note on May 15/16 measurements:** soak time unknown. mAh estimates recalculated using per-range mAh/mV factors above (not the averaged 10.8 figure). Treat as upper bounds.

**Note on the "GNSS on, no sky view" cycle:** satellite count never rose above 15 and showed no variation, consistent with the GNSS engine scanning against noise rather than decoding real signals. Current draw in this state is much lower than during actual acquisition. This data point is not representative of a real GPS-timeout cycle.

**Rule of thumb for power budget simulation:**
| Scenario | Est. mAh | Confidence |
|----------|----------|-----------|
| Deep sleep | 0.43 mAh/h | High — 53.4h controlled experiment |
| Short wake, GPS success, XTRA fresh (~5.5 min) | ~25 mAh | Medium — one clean-ish point, soak pending |
| Full wake, GPS success, XTRA stale (~8–12 min) | ~40–80 mAh | Estimated — not yet measured |
| Full wake, GPS timeout, real search (25 min) | ~100–250 mAh | Low — upper bounds only, soak times unknown |

*Solar charge rates per week of year are not yet documented here — add when measured. Together with these wake costs and the deep sleep drain from `SLEEP.md`, they form the three inputs needed for the annual SoC simulation.*

## Rules
- Always measure battery BEFORE powering any subsystem
- Never lower 3.70V / 25% critical thresholds (hook-enforced)
- Never change fallback month from January without understanding why it's there
- Never remove the 300s minimum sleep floor
- The 2h aggressive cycle at >80% is intentional — don't "fix" it
