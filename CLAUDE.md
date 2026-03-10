# PlayBuoy - Project Knowledge File

This file is the AI assistant's memory for the PlayBuoy project. It provides context for development, debugging, and improvements.

## Project Overview

Solar-powered, permanently sealed, waterproof IoT buoy for lakes and ocean beaches. Collects water temperature and wave data, transmits via 4G cellular to a web API for beachgoers.

- **Deployment environments:** Small lakes, large lakes, and ocean/coastal beaches
- **Primary deployment:** Litla Grindevatnet, lake near Haugesund, Norway (59.4°N)
- **Core feature:** Water temperature (most important for users)
- **Secondary:** Wave height, period, power (for enthusiasts)
- **Note:** Wave parameters (MAX_SAMPLES, MAX_WAVES, wave caps) may need adjustment for ocean deployments where waves are higher and more frequent than in small lakes
- **Inspiration:** [Smart Buoy](https://github.com/sk-t3ch/smart-buoy) ([YouTube](https://www.youtube.com/watch?v=S-XMT6GDWk8&list=PLoTBNxUNjtjebnBR1B3RfByp8vZtZ6yL7))

### Design Priorities (in order)
1. Never run out of power (permanently sealed — if it dies, it's gone)
2. Stability over features (no brownouts, hangs, crashes)
3. Correct timings per datasheets
4. Water temperature accuracy
5. Accurate timestamps (NTP)
6. Minimize firmware size (OTA over cellular)
7. Wave data accuracy

## Hardware

### Main Board
**LilyGo T-SIM7000G** (ESP32-D0WD-V3, 240MHz, 4MB flash + SIM7000G modem with GPS/GLONASS/BeiDou)
- UART baud rate: 57600 (stored in SIM7000G with AT&W)
- LEDs soldered off to reduce sleep power draw

### Sensors
| Sensor | Purpose | Interface | Pin(s) |
|--------|---------|-----------|--------|
| DS18B20 | Water temperature | OneWire | GPIO 13 |
| GY-91 (MPU-6500/9250) | Wave motion (accelerometer) | I2C | SDA=21, SCL=22 |

### Pin Assignments
| Pin | Function |
|-----|----------|
| GPIO 4 | MODEM_PWRKEY / GPS_POWER_PIN (**CONFLICT** — same pin) |
| GPIO 5 | MODEM_RST |
| GPIO 13 | DS18B20 OneWire data |
| GPIO 21 | I2C SDA (GY-91) |
| GPIO 22 | I2C SCL (GY-91) |
| GPIO 23 | MODEM_POWER_ON |
| GPIO 25 | POWER_3V3_ENABLE (switched 3.3V rail for sensors) |
| GPIO 26 | MODEM_RX (Serial1) |
| GPIO 27 | MODEM_TX (Serial1) |
| GPIO 32 | MODEM_DTR |
| GPIO 33 | MODEM_RI |
| GPIO 35 | ADC battery voltage |

### Power System
- **4x 0.3W 5V solar panels** (1.2W theoretical peak, angled on dome)
- **2x 18650 Li-ion batteries** in parallel (~6000-7000 mAh)
- **Switched 3.3V rail** (GPIO 25) powers sensors, cut entirely during sleep
- Haugesund: very rainy, effective daily solar harvest ~1.5-3 Wh summer, ~0.1-0.5 Wh winter

### Battery Health Guidelines (18650 Li-ion)
- **Never discharge below 20%** — prevents irreversible battery damage
- **Never charge above 80%** — high SoC accelerates calendar aging and capacity loss
- **Ideal operating range: 40-60%** — optimal for lithium-ion longevity (data center standard)
- **Critical guard: 25% / 3.70V** — deep sleep with safety margin for SIM7000G (needs ≥3.55V under 2A peak)
- The aggressive 2-hour wake schedule at >80% SoC is **intentional** — it forces the buoy to discharge toward the healthy range while also providing frequent temperature updates to users

## Buoy Deployments

| Buoy ID | Node ID | Name | Location |
|---------|---------|------|----------|
| grinde | playbuoy_grinde | Litla Grindevatnet | Lake near Haugesund |
| vatna | playbuoy_vatna | Vatnakvamsvatnet | Another lake |

## Network & Infrastructure

- **Cellular:** Telenor Norway, LTE-M preferred (AT+CNMP=38), NB-IoT fallback (AT+CNMP=51)
- **API:** `playbuoyapi.no:80` HTTP POST to `/upload` with `X-API-Key`
- **OTA:** `trondve.ddns.net` HTTP only (HTTPS never worked on SIM7000G)
- **XTRA:** `http://trondve.ddns.net/xtra3grc.bin` every 7 days for GPS aiding
- **NTP:** `no.pool.ntp.org` via AT+CNTP
- **Timezone:** `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Oslo)
- **DNS:** Optional custom (1.1.1.1 / 8.8.8.8) via AT+CDNSCFG

## Boot Cycle

1. Wake from deep sleep → 3s startup → release BT → log wake reason
2. Release GPIO holds → measure battery (before powering anything)
3. Brownout fast-track → deep sleep if brownout + battery <40%
4. Critical battery check → deep sleep if ≤25% or ≤3.70V
5. Power 3V3 rail → 150ms → init sensors → read temperature
6. Record wave data (3 min, 10 Hz accelerometer)
7. Power off sensors → 100ms → power off 3V3 rail
8. Power on modem → NTP sync → XTRA download if stale → GNSS fix
9. Power off GPS → re-establish cellular (skip modem pre-cycle, already warm)
10. Check OTA firmware update
11. Build JSON → HTTP POST upload (retry 3x)
12. If upload fails, buffer JSON in RTC memory (512 bytes)
13. Power off modem → configure pins high-Z → esp_sleep_pd_config → deep sleep

**Critical:** Only power ONE subsystem at a time (GPS and cellular are voltage-sensitive).

### Deep Sleep Power Optimizations
- **Power domains:** RTC periph OFF, RTC fast mem OFF, XTAL OFF; only RTC slow mem ON (for rtcState)
- **Bluetooth:** Controller disabled, deinitialized, and memory released at startup (~30KB freed)
- **GPIO hold:** GPIO 25 (3V3 rail) held LOW via `gpio_hold_en` across deep sleep
- **Pin state:** All modem/I2C/OneWire pins set to INPUT (high-Z) to prevent back-powering
- **Brownout fast-track:** If ESP32 resets from brownout and battery <40%, goes straight to sleep (skips full cycle that would likely cause another brownout)
- **Fallback season:** If RTC time is invalid, assumes January (winter schedule) for conservative power management

## Sleep Schedule

### Season Detection
- **Winter:** October–April | **Summer:** May–September

### Summer Schedule
| Battery % | Sleep | Rationale |
|-----------|-------|-----------|
| > 80% | 2h | Discharge toward healthy range + frequent updates |
| > 70% | 3h | Good solar, capture temp changes |
| > 60% | 6h | Sustainable equilibrium |
| > 50% | 9h | Optimal storage range |
| > 40% | 12h | Bottom of optimal, conserve |
| > 35% | 24h | Below optimal, needs recharge |
| > 30% | 48h | Low battery |
| > 25% | 72h | Very low |
| ≤ 25% | 168h (1 week) | Near critical |

### Winter Schedule
| Battery % | Sleep | Rationale |
|-----------|-------|-----------|
| > 80% | 12h | Discharge toward healthy range |
| > 70% | 24h | Daily check-in |
| > 60% | 24h | Still margin |
| > 50% | 48h | Conserve |
| > 40% | 72h | Optimal storage |
| > 35% | 168h (1 week) | Deep conservation |
| > 30% | 336h (2 weeks) | Very low |
| > 25% | 720h (~1 month) | Near critical |
| ≤ 25% | 2160h (~3 months) | Hibernate until spring |

- **Quiet hours:** Wake pushed to 06:00 if 00:00-05:59 local
- **Minimum sleep floor:** 300 seconds (prevents reboot loops)

### GPS Fix Timeout (battery-aware)
| Scenario | Battery > 60% | 40-60% | < 40% |
|----------|--------------|--------|-------|
| First fix | 20 min | 15 min | 10 min |
| Subsequent | 10 min | 7.5 min | 5 min |

GPS skipped if last fix < 24 hours old.

### Watchdog
45 minutes (2700s). Hard reset if cycle hangs.

## Component Documentation Lookup

| Task | Document Location |
|------|------------------|
| AT commands (modem) | `docs/components/Lilygo/SIM7000/SIM7000 Series_AT Command Manual_V1.06.pdf` |
| GNSS/GPS AT commands | `docs/components/Lilygo/SIM7000/SIM7000 Series_GNSS_Application Note_V1.03.pdf` |
| XTRA/FS commands | `docs/components/Lilygo/SIM7000/SIM7000 Series_FS_Application Note_V1.01.pdf` |
| SIM7000G hardware design | `docs/components/Lilygo/SIM7000/SIM7000 Hardware Design_V1.07.pdf` |
| Deep sleep example | `docs/components/Lilygo/Examples/DeepSleep/DeepSleep.ino` |
| Modem power off example | `docs/components/Lilygo/Examples/ModemPowerOff/ModemPowerOff.ino` |
| Battery ADC example | `docs/components/Lilygo/Examples/ReadBattery/ReadBattery.ino` |
| GPS example | `docs/components/Lilygo/Examples/GPS_BuiltIn/GPS_BuiltIn.ino` |
| OTA example | `docs/components/Lilygo/Examples/HttpsOTAUpgrade/HttpsOTAUpgrade.ino` |
| Board pin map | `docs/components/Lilygo/SIM7000_A7608_A7670_ESP32.png` |
| IMU register config | `docs/components/GY-91/` (screenshots) |
| DS18B20 datasheet | `docs/components/DS18B20/scan4.pdf` |
| Board README | `docs/components/Lilygo/sim7000-esp32/REAMDE.MD` |

## File Structure

```
src/
  main.cpp          - Entry point, power management, pin defs, boot cycle
  config.h          - Per-buoy config (API keys, server URLs) — GITIGNORED
  config.h.example  - Template for config.h
  battery.cpp/h     - Battery %, sleep duration, season detection, OCV table
  gps.cpp/h         - GPS fix, NTP sync, XTRA download, AT command helpers
  json.cpp/h        - JSON payload construction
  modem.cpp/h       - Cellular connection, HTTP POST upload
  ota.cpp/h         - OTA firmware updates with version comparison
  power.cpp/h       - ADC battery voltage (median-of-five)
  rtc_state.cpp/h   - RTC-persisted state, anchor drift, temp history, upload buffering
  sensors.cpp/h     - DS18B20 temperature, I2C init
  utils.cpp/h       - Wake reason logging
  wave.cpp/h        - Wave data collection (IMU, Mahony, IIR, zero-upcrossing)

Root files:
  WORKING_BATTERY_MEASUREMENT_main.cpp  - Reference: confirmed working battery ADC
  WORKING_NTP+XTRA+GPS.cpp             - Reference: confirmed working GPS sequence
  build_all_buoys.py                    - Multi-buoy build script
  build_buoys.py                        - Simpler PIO post-build hook
  update_firmware_version.py            - Interactive version updater
```

## Key Implementation Details

### Battery Measurement (power.cpp)
- GPIO 35, 12-bit ADC, 11dB attenuation
- 3 bursts × 50 averaged samples, no inter-burst delays, median-of-three
- 200µs inter-sample spacing decorrelates from switching regulator noise (~10ms per burst)
- Uses `esp_adc_cal` API for hardware-calibrated ADC→mV conversion (eFuse Two Point or Vref)
- Battery voltage = ADC mV × 2.0 (100K/100K divider ratio)
- Single warmup discard at start (ESP32 ADC one-time channel-switch artifact)
- Logs burst spread in mV for remote diagnostics (warns if >20mV)
- Measured before powering anything (open-circuit voltage)
- Total measurement time: ~30ms (was ~3.5s)
- Critical guard: 3.70V / 25% → deep sleep

### Water Temperature (sensors.cpp)
- DallasTemperature library, explicit 12-bit resolution (0.0625°C, 750ms conversion)
- `setWaitForConversion(true)` — blocks until conversion complete, no timing guesswork
- 3 retries with 800ms backoff, validation: -127°C, 85°C error codes, range -30 to 60°C
- Read during sensor-powered phase, stored in RTC for JSON (3V3 rail off during upload)

### Wave Measurement (wave.cpp)
- MPU-6500/9250 direct I2C at 10 Hz, ±2g, ±250 dps, DLPF ~44Hz
- Mahony AHRS + slow gravity tracker (0.02 Hz LP)
- **FFT spectral analysis** (replaces time-domain double integration):
  - Collects 3 min heave acceleration, uses last 1024 samples for 1024-point FFT
  - Hanning window → FFT → acceleration PSD → displacement PSD via 1/(2πf)⁴
  - Hs = 4·√m₀ (standard oceanographic definition, m₀ = spectral zeroth moment)
  - Tp = 1/f_peak (period of peak displacement spectral density)
  - Wave band: 0.05–1.0 Hz (periods 1–20s)
  - No more `DISP_AMP_SCALE` fudge factor — spectral method is drift-free
- Sanity caps: Hs > 2.0m treated as noise (adjust for ocean deployments)
- Also computes: mean tilt (degrees from vertical), acceleration RMS

### GPS (gps.cpp)
- NTP sync → XTRA download (if >7 days stale) → GNSS start → 60s NMEA smoke test → fix polling
- **60s smoke test** (`gnssSmoke60s`): Streams NMEA sentences while polling CGNSINF every second. Lets the GNSS engine warm up and acquire satellites before the main fix loop. Exits early if a fix is obtained during this phase.
- 3 GPIO polarity variants tried for GNSS power (hardware revision compatibility)
- PDP teardown after NTP/XTRA before GNSS start (required — SIM7000G shares radio between data and GPS)
- After GNSS fix, main.cpp re-establishes cellular via `connectToNetwork()` for upload

### Modem Timings (verified safe against SIM7000G datasheet)
| Operation | Duration | Spec |
|-----------|----------|------|
| PWRKEY LOW (power on) | 2000ms | Min 1000ms |
| Post-PWRKEY settle | 6000ms | ~5s ready |
| Post-init delay | 2000ms | Conservative |
| PWRKEY LOW (power off) | 1300ms | Min 1200ms |
| CPOWD timeout | 8000ms | Typical 2-4s |
| Pre-AT delay | 20ms | Good practice |
| AT timeout | 1500ms | Adequate |
| Network registration | 60s | Appropriate |

## JSON Payload Fields

| Field | Type | Description |
|-------|------|-------------|
| `nodeId` | string | Buoy identifier (e.g. "playbuoy_grinde") |
| `name` | string | Human-readable name |
| `version` | string | Firmware version (semver) |
| `timestamp` | uint32 | Unix epoch (UTC) |
| `lat` | float | GPS latitude |
| `lon` | float | GPS longitude |
| `wave.height` | float | Significant wave height Hs (meters) |
| `wave.period` | float | Peak wave period Tp (seconds) |
| `wave.direction` | string | Wave direction (currently always "N/A") |
| `wave.power` | float | Wave power proxy (kW/m) |
| `buoy.tilt` | float | Mean buoy tilt from vertical (degrees) |
| `buoy.accel_rms` | float | Heave acceleration RMS (m/s², proxy for conditions) |
| `temp` | float | Water temperature (°C) |
| `temp_trend` | float | Temperature change over last 5 readings (°C, + = warming) |
| `battery` | float | Battery voltage (V) |
| `battery_percent` | int | Estimated SoC from OCV table (0-100%) |
| `temp_valid` | bool | Whether temperature reading is valid |
| `uptime` | uint32 | Seconds since boot |
| `boot_count` | uint32 | RTC-persisted boot counter (increments each wake) |
| `reset_reason` | string | Why the device booted |
| `hours_to_sleep` | int | Planned sleep duration |
| `next_wake_utc` | uint32 | Planned next wake epoch |
| `battery_change_since_last` | float | Voltage delta from previous boot |
| `rtc.waterTemp` | float | RTC-stored water temperature |
| `gps.hdop` | float | GPS horizontal dilution of precision (lower = better) |
| `gps.ttf` | uint16 | GPS time-to-fix in seconds |
| `net.operator` | string | Cellular operator name |
| `net.apn` | string | APN used |
| `net.ip` | string | Assigned IP address |
| `net.signal` | int | Signal quality (CSQ 0-31) |
| `alerts.anchorDrift` | bool | Anchor drift detected (>50m) |
| `alerts.chargingIssue` | bool | No charge detected (not fully implemented) |
| `alerts.tempSpike` | bool | >2°C change from previous reading |
| `alerts.overTemp` | bool | Water temperature exceeds 35°C |
| `alerts.uploadFailed` | bool | Previous upload failed |

## Known Issues

### Active Bugs
1. **GPIO 4 pin conflict:** MODEM_PWRKEY and GPS_POWER_PIN both use GPIO 4
2. **Credentials in repo:** config.h contains API_KEY and is tracked despite .gitignore
3. **`triedNBIoT` is static local:** Once tried, never retried in same boot
4. **build_all_buoys.py hardcoded Windows path** (line 129)
5. **update_firmware_version.py references create_version_files.py** which doesn't exist
6. **Fallback month** now defaults to January (winter-safe) instead of August

### Not Working / Needs Improvement
- **Wave height (Hs):** Now uses FFT spectral analysis (Hs = 4·√m₀). Needs field validation against known wave conditions.
- **Wave direction:** Always "N/A" — magnetometer broken in sealed enclosure.
- **Anchor drift detection:** Fixed (2026-03-09) — counter now accumulates across boots, resets only when buoy returns within threshold.
- **Temperature anomaly detection:** Implemented (2026-03-09) — detects >2°C spikes between readings, over-temp >35°C.
- **Charging problem detection:** Flag exists but detection logic incomplete.

### Working Well
- Water temperature (DS18B20) — reliable
- Battery voltage measurement (median-of-five) — accurate
- Battery percentage with OCV table — functional
- Sleep duration calculation (season + battery aware)
- Critical battery guard (3.70V / 25%)
- Cellular connection (LTE-M + NB-IoT fallback)
- GPS fix (NTP → XTRA → GNSS sequence)
- OTA firmware updates (HTTP)
- JSON upload with retry and buffering
- Deep sleep with minimum leakage

## Code Review Findings (2026-03-09)

### Cleanup Performed
- Removed unused `lastSolarChargeTime` from RTC state
- Removed unused `GPS_FIX_TIMEOUT_SEC` constant
- Removed unused `BATTERY_CALIBRATION_FACTOR` from config template
- Removed dead `getHeadingDegrees()` stub and heading sampling
- Removed unused `directionFromAverage()` function
- Replaced 12KB `aHeaveBuf` with incremental stats (~12KB RAM saved)
- Removed unused MPU9250_asukiaaa library from lib/
- Removed `getCurrentHour()` — quiet hours now handled by `adjustNextWakeUtcForQuietHours()` in main.cpp
- Added `DEBUG_NO_DEEP_SLEEP` to config.h.example

### New Metrics Added
- **Temperature trend** (`temp_trend`): Tracks last 5 readings in RTC, reports °C change (oldest to newest)
- **Temperature anomalies**: Detects >2°C spike between readings, over-temp >35°C (alerts in JSON)
- **Buoy tilt** (`buoy.tilt`): Mean angle from vertical during wave sampling (degrees)
- **Acceleration RMS** (`buoy.accel_rms`): Heave acceleration RMS (m/s²), proxy for sea state
- **GPS HDOP** (`gps.hdop`): Horizontal dilution of precision from CGNSINF
- **GPS TTF** (`gps.ttf`): Time-to-fix in seconds
- **Boot count** (`boot_count`): RTC-persisted counter, increments each wake
- **Battery percent** (`battery_percent`): SoC estimated from OCV table

### Measurement Approach Review

**Battery (power.cpp):** Solid. Median-of-three with 50-sample burst averaging. Uses `esp_adc_cal` API for hardware-calibrated nonlinearity correction (50-150mV improvement). Logs calibration source and burst spread for remote diagnostics. Total measurement time ~30ms (was ~3.5s) — no inter-burst delays needed since ESP32 ADC noise is thermal, not bursty.

**Water Temperature (sensors.cpp):** Good. 12-bit DS18B20 with 3 retries. Now explicitly sets 12-bit resolution and `setWaitForConversion(true)` so `requestTemperatures()` blocks until conversion is complete.

**Wave Data (wave.cpp):** Now uses FFT spectral analysis. 1024-point FFT on heave acceleration, displacement PSD via 1/(2πf)⁴, Hs = 4·√m₀. Eliminates drift from double integration and the DISP_AMP_SCALE fudge factor. The Mahony filter is still included but redundant (gravity tracker does orientation independently).

**GPS (gps.cpp):** NTP→XTRA→GNSS is correct per SIM7000G app notes. Dynamic timeout is well-designed. The double PDP setup/teardown (~20-30s) is necessary — SIM7000G shares the radio between cellular data and GNSS, so PDP must be torn down before GNSS and re-established after for upload.

**OTA (ota.cpp):** Functional. No integrity check (consider SHA-256 hash). Graceful modem shutdown (CPOWD) now called before ESP.restart().

**JSON/Upload (modem.cpp, json.cpp):** Solid. JSON now built once after network connect attempt, with network diagnostics if available.

### OCV Table Assessment
OCV table replaced (2026-03-09) with standard 18650 discharge curve data. The old table overestimated SoC in the 5-60% range. New table correctly maps 3.733V to ~41% (was 35%), with proper flat plateau in the 3.6-3.8V region (30-70%). For best accuracy, measure the actual installed cells' discharge curve.

### Battery Threshold Assessment
3.70V / 25% critical guard is safe. SIM7000G minimum operating voltage 3.4V + 0.15V drop under 2A peak = 3.55V required resting voltage. 3.70V provides 150mV headroom for aged cells.

### Sleep Schedule Assessment
Well-designed for 18650 health. The 2-hour interval at >80% is intentional — it actively discharges toward the healthy 40-60% range while providing frequent temperature updates during summer. The entire schedule is built around lithium-ion best practices: never above 80%, never below 20%, ideally 40-60%.

### Modem Timing Assessment
All timings verified safe against SIM7000G datasheet. Tightest margin: PWRKEY power-off at 1300ms (spec min: 1200ms).

## Future Improvements (Priority Order)

1. **Fix anchor drift** — accumulate across boots, use GPS speed-over-ground
2. **Portable build script** — remove hardcoded Windows path in build_all_buoys.py
3. **OTA integrity check** — SHA-256 hash verification before applying firmware
4. **Remove Mahony filter** — redundant with gravity tracker, saves CPU/flash
5. **GPS SOG for drift detection** — CGNSINF field 6 gives speed in km/h
6. **Log GPS HDOP and TTF** — quality metrics for fix accuracy

## Build System

### build_all_buoys.py
Builds per-buoy firmware by modifying NODE_ID/NAME/VERSION in config.h, running `pio run`, copying output.
**Bug:** Hardcoded Windows path on line 129. Needs portable `platformio` command lookup.

### Output Structure
```
firmware/
  playbuoy_grinde.bin/.version/.version.json
  playbuoy_vatna.bin/.version/.version.json
```
