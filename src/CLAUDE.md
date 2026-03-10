# PlayBuoy Source Code Context

This file provides context for the firmware source modules.

## Module Organization

### Core Power & Battery Management
- **main.cpp**: Boot cycle, power state machine, GPIO control
- **battery.cpp**: SoC estimation, sleep duration algorithm, brownout handling
- **power.cpp**: ADC battery measurement, 3-burst averaging, spread logging
- **rtc_state.cpp/h**: RTC-persisted state (boot counter, GPS anchor, temp history)

### Sensors & Data Collection
- **sensors.cpp**: DS18B20 temperature (12-bit, 750ms conversion, 3 retries)
- **wave.cpp**: IMU sampling (10Hz), Mahony AHRS, FFT spectral analysis (Hs, Tp)
- **json.cpp**: Payload construction (~30 fields, 512-byte RTC buffer for failures)

### Connectivity & Timing
- **gps.cpp**: NTP sync → XTRA download → GNSS fix pipeline
- **modem.cpp**: LTE-M/NB-IoT registration, HTTP POST, OTA checking
- **ota.cpp**: Firmware download, version comparison, ESP.restart()

### Utilities
- **config.h**: Per-buoy ID, API keys, server URLs (GITIGNORED)
- **utils.cpp**: Wake reason logging, RTC time management
- **power.h**: ADC calibration, battery percentage API

## Critical Constraints to Respect

### Power Budget
- **Deep sleep**: ~10-15µA (RTC slow mem only)
- **Wake cycle**: 3-20 minutes depending on GPS
- **Winter survival**: Must endure 6+ months of minimal solar

### Timing
- **Modem power-on**: 2s PWRKEY LOW + 6s settle (per SIM7000G datasheet)
- **Modem power-off**: 1.3s PWRKEY LOW (min 1.2s spec)
- **Temperature conversion**: 750ms at 12-bit resolution
- **GPS smoke test**: 60s NMEA warmup before fix polling
- **Battery measurement**: ~30ms (was 3.5s before optimization)

### Safety Thresholds
- **Critical guard**: ≤3.70V / ≤25% SoC → deep sleep
- **Brownout recovery**: If modem caused brownout + battery <40% → skip cycle
- **Battery health**: Optimize for 40-60% SoC (slow aging)
- **Temperature validity**: -30 to +60°C (discard -127°C, 85°C error codes)

### Hardware Quirks
- **GPIO 4 conflict**: MODEM_PWRKEY and GPS_POWER both use GPIO 4
- **PDP sharing**: SIM7000G radio shared between cellular and GNSS (teardown before GPS)
- **ADC warmup**: First analogRead() after channel select may be off (discard once per boot)
- **3V3 rail settle**: <10ms actual, use 150ms with margin for DS18B20 (~50ms needed)

## Building for Deployment

### Per-Buoy Configuration
Build script: `tools/scripts/build_all_buoys.py`

```bash
cd /home/user/PlayBuoy
python tools/scripts/build_all_buoys.py
# Generates firmware/ with per-buoy binaries and .version files
```

Modifies in config.h:
- NODE_ID ("playbuoy_grinde" or "playbuoy_vatna")
- NAME (human-readable name)
- VERSION (semver string)

### OTA Updates
1. Push new firmware binary to OTA_SERVER (`trondve.ddns.net`)
2. Create `.version` file with semver (e.g., "1.1.1")
3. Buoy checks `{OTA_SERVER}/{NODE_ID}.version` on each cycle
4. If newer: download `.bin`, apply via ESP.restart()
5. Rollback if boot fails (OTA_IMG_PENDING_VERIFY not cleared)

## Testing & Validation

### Unit Test Areas (if tests/ added)
- Power measurement: ADC accuracy, burst spreading, calibration
- Battery SoC: OCV table lookup interpolation
- Temperature: Retry logic, range validation
- Wave FFT: Spectral moments, frequency bands
- Sleep duration: Season/battery/month combinations

### Field Diagnostics
- Monitor `battery_percent` trend (should stay 40-60% optimal)
- Watch `gps.ttf` (time-to-fix) for antenna/signal issues
- Check `boot_count` (accumulates per wake)
- Log `buoy.tilt` and `buoy.accel_rms` for environmental context
- Verify `wave.height` sanity cap (>2.0m on lake = noise)

### Common Failure Modes
| Symptom | Likely Cause | Action |
|---------|--------------|--------|
| Never sleeps, constant brownouts | Battery sagged under modem load | Check solar panel angle, may need higher capacity cells |
| Sleep duration too short | Temperature <-5°C (winter fallback triggers aggressive schedule) | Normal in winter, verify RTC sync for proper month detection |
| GPS fix times >20min | XTRA data stale, GNSS engine cold-start | XTRA refreshed every 7 days, first fix may be slow |
| High temp variance (>2°C) between readings | Water temperature actually changing or sensor noise | Check `temp_spike` alert, may indicate real phenomenon |
| Zero wave height | Sea state calm or modem interference | FFT sanity cap at 2.0m, check `buoy.accel_rms` |

## Code Review Checklist

When reviewing changes to src/:
- [ ] **Power impact**: Does it increase wake time or prevent sleep?
- [ ] **Timing**: Are datasheet limits respected (SIM7000G, DS18B20)?
- [ ] **Safety**: Could it cause brownout, battery sag, or unsafe GPIO states?
- [ ] **Memory**: Does it leak RAM or exceed RTC buffer (512 bytes)?
- [ ] **Accuracy**: Are thresholds and tolerances correct (temperature, wave, GPS)?
- [ ] **Testability**: Can it be verified in the field?

See `.claude/skills/code-review/` for automated review patterns.
