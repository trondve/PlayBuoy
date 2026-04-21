# Battery Measurement & Management — Local Context

## Purpose
Measure battery voltage accurately before powering any subsystems (open-circuit voltage). Estimate State of Charge from OCV table. Determine sleep duration based on SoC and season. Enforce critical guard to prevent bricking.

## What can go wrong
- **Measuring under load**: Battery must be measured BEFORE powering modem, GPS, or sensors. Under 2A modem load, voltage sags 150-300mV — gives false critical readings.
- **Lowering critical guard**: The 3.70V / 25% threshold protects against SIM7000G failure (needs ≥3.55V under 2A peak) and irreversible 18650 cell damage (below 20% = permanent capacity loss). Hooks block this, but be aware.
- **Wrong OCV table**: Current table is based on Samsung INR18650-35E (LiitoKala Lii-35S), adjusted ~20mV downward for 5-15°C operating temperature. 2× cells in parallel (same voltage, double capacity).
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
- 101 points (0-100%), 1% resolution, linear interpolation between points
- Cold-adjusted (~20mV down from 25°C datasheet) for 5-15°C operating range
- Key values: 2.95V = 0%, 3.555V = 25%, 3.72V = 50%, 3.932V = 70%, 4.071V = 80%, 4.20V = 100%
- Flat plateau 3.555-3.72V maps to 25-50% — small voltage changes = large SoC swings
- Binary search + linear interpolation for O(log n) lookup

## Sleep schedule design
- **Target SoC: 40-60%** — optimal for 18650 longevity (data center standard)
- **>80%: aggressive discharge** — 2h summer, 12h winter. Intentional: forces SoC down
- **<25%: hibernate** — 1 week summer, 3 months winter. Await solar recovery
- **Quiet hours**: 00:00-05:59 local → push wake to 06:00 (no one checks water temp at 3am)
- **Minimum sleep floor**: 300 seconds (prevents reboot loops from sleepSec=0)

## Rules
- Always measure battery BEFORE powering any subsystem
- Never lower 3.70V / 25% critical thresholds (hook-enforced)
- Never change fallback month from January without understanding why it's there
- Never remove the 300s minimum sleep floor
- The 2h aggressive cycle at >80% is intentional — don't "fix" it
