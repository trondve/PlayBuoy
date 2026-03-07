# PlayBuoy - Project Knowledge File

This file captures everything about the PlayBuoy project to provide context for development, debugging, and future improvements.

## Project Overview

PlayBuoy is a solar-powered, permanently sealed, waterproof IoT buoy that floats on the surface of a lake or coastal water. It autonomously collects environmental data (water temperature, wave conditions) and transmits it via 4G cellular to a web API. The data is displayed on a website for beachgoers and water sports enthusiasts.

**Primary deployment:** Litla Grindevatnet, a lake near Haugesund, Norway.

**Core user-facing feature:** Water temperature. This is the most important metric for end users.

**Secondary features:** Wave height, wave period, wave power, wave direction - for "nerds who want more details."

**Target audience:** People who want to play at the lake or beach, beachgoers wanting to know water temperature and wave conditions before going.

## Hardware

### Main Board
- **LilyGo T-SIM7000G** (ESP32 + SIM7000G cellular modem with built-in GPS/GLONASS/BeiDou)
- LEDs on the board have been soldered off to reduce power draw during sleep

### Sensors
- **DS18B20** waterproof temperature probe - dangles in the water (GPIO 13, OneWire). Works perfectly.
- **GY-91** IMU module (contains MPU-9250 accelerometer/gyroscope/magnetometer + BMP280 barometer) - mounted inside the buoy for wave motion sensing (I2C: SDA=GPIO 21, SCL=GPIO 22). The GY-91 contains the MPU-9250 (or MPU-6500 variant without magnetometer). Data accuracy has been problematic.

### Power System
- **4x small solar panels** (0.3W 5V 68x36mm each = 1.2W total theoretical peak)
- **2x 18650 Li-ion batteries** in parallel
- Panels are angled at different directions on the dome-shaped buoy housing
- A switched **3.3V rail** (GPIO 25) powers the sensors and can be cut entirely during sleep
- The buoy is permanently sealed - no physical access to electronics after deployment

### Pin Assignments (LilyGo T-SIM7000G)
| Pin | Function |
|-----|----------|
| GPIO 4 | MODEM_PWRKEY (also used as GPS_POWER_PIN - CONFLICT) |
| GPIO 5 | MODEM_RST |
| GPIO 13 | DS18B20 OneWire data |
| GPIO 21 | I2C SDA (GY-91) |
| GPIO 22 | I2C SCL (GY-91) |
| GPIO 23 | MODEM_POWER_ON |
| GPIO 25 | POWER_3V3_ENABLE (switched 3.3V rail) |
| GPIO 26 | MODEM_RX (Serial1) |
| GPIO 27 | MODEM_TX (Serial1) |
| GPIO 32 | MODEM_DTR |
| GPIO 33 | MODEM_RI |
| GPIO 35 | ADC battery voltage (PIN_ADC_BAT) |

### Physical Design
- 3D printed spherical buoy housing (inspired by the "Smart Buoy Series" design)
- Upper dome has 4 angled solar panel cutouts
- Antenna protrudes from the top
- Lower half is submerged; DS18B20 probe exits through a waterproof gland
- Two halves seal together - once sealed, the electronics are inaccessible

## Network & Infrastructure

- **Cellular:** Telenor Norway, 4G LTE-M (preferred) with NB-IoT fallback. NB-IoT SIM cards require business registration in Norway, so currently using regular 4G.
- **API Server:** `playbuoyapi.no` port 80, HTTP POST to `/upload` with `X-API-Key` header
- **OTA Server:** `trondve.ddns.net` (HTTP only - HTTPS never worked on the SIM7000G). Hosts firmware `.bin` and `.version` files.
- **XTRA Aiding Data:** Downloaded from `http://trondve.ddns.net/xtra3grc.bin` every 7 days to speed up GPS fixes
- **NTP:** `no.pool.ntp.org`
- **Timezone:** `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Oslo, handles CET/CEST automatically)
- **Database:** Running on a Raspberry Pi at the owner's home
- **XTRA HTTP Server:** Running on a Windows PC at the owner's home

## Boot Cycle (one "heartbeat")

Each wake cycle follows this sequence:
1. Wake from deep sleep, serial init, 3s startup delay
2. Log wake reason (timer, power-on, brownout, watchdog, etc.)
3. Release deep-sleep GPIO holds
4. Measure battery voltage early (before powering anything, for a clean reading)
5. Check critical battery - if <= 20% or <= 3.633V, skip everything and deep sleep
6. Power on 3.3V rail -> wait 5s -> initialize sensors
7. Record wave data for 3 minutes (accelerometer sampling at 10 Hz)
8. Power off sensors -> wait 5s -> power off 3.3V rail
9. Power on modem -> NTP time sync -> download XTRA if stale -> start GNSS -> get GPS fix
10. Power off GPS -> wait 5s -> re-establish cellular data
11. Check for OTA firmware update
12. Build JSON payload with all sensor data + diagnostics + alerts
13. Upload JSON via HTTP POST
14. If upload fails, buffer JSON in RTC memory (512 bytes) for retry next boot
15. Power off modem, power off 3.3V rail
16. Set all pins to high-impedance for minimum leakage
17. Deep sleep for calculated duration

### Critical Design Principle
Only power ONE subsystem at a time. GPS/GNSS and cellular modem are extremely sensitive to voltage drops and don't work well when other things are powered. This was learned the hard way through many brownouts.

## Sleep Duration Logic

Sleep duration depends on **season** (month) and **battery percentage**.

### Season Detection
- **Winter:** October through April (months 10, 11, 12, 1, 2, 3, 4)
- **Summer:** May through September (months 5, 6, 7, 8, 9)

### Summer Schedule (May-September)
| Battery % | Sleep Duration |
|-----------|---------------|
| > 80% | 3 hours |
| > 75% | 4 hours |
| > 70% | 6 hours |
| > 65% | 9 hours |
| > 60% | 12 hours |
| > 55% | 24 hours |
| > 50% | 36 hours |
| > 45% | 48 hours (2 days) |
| > 40% | 72 hours (3 days) |
| > 35% | 168 hours (1 week) |
| > 30% | 336 hours (2 weeks) |
| > 25% | 720 hours (~1 month) |
| > 20% | 1460 hours (~2 months) |
| <= 20% | 2180 hours (~3 months) |

### Winter Schedule (October-April)
Starts at 24 hours even at >55% battery. Below 55%, same as summer.

### Quiet Hours
If the computed wake time falls between 00:00-05:59 local time (Europe/Oslo), it's pushed to 06:00. Saves battery on uploads nobody sees at night.

### GPS Fix Timeout (battery-aware)
| Scenario | Battery > 60% | 40-60% | < 40% |
|----------|--------------|--------|-------|
| First fix ever | 20 min | 15 min | 10 min |
| Subsequent fix | 10 min | 7.5 min | 5 min |

### GPS Skip Optimization
If last GPS fix is < 24 hours old, skip GPS entirely and reuse stored coordinates.

### Watchdog
45-minute hardware watchdog (2700 seconds). If the entire cycle hangs, the ESP32 hard-resets.

## Feature Status

### Working
- Water temperature measurement (DS18B20) - works perfectly
- Battery voltage measurement via ADC (after much debugging)
- Battery percentage estimation (OCV lookup table with binary search + interpolation)
- Sleep duration calculation (season + battery aware)
- Quiet hours enforcement
- Critical battery guard (deep sleep when low)
- Cellular connection to Telenor (after much debugging)
- JSON payload construction and HTTP POST upload
- OTA firmware updates via HTTP (HTTPS never worked on SIM7000G)
- GPS fix acquisition (after much debugging)
- XTRA aiding data download via HTTP
- Watchdog timer
- Deep sleep with minimum leakage pin configuration
- Data buffering for failed uploads (RTC memory)
- Boot counter and wake reason tracking

### Uncertain / Needs Testing
- NTP time sync (not sure if it's working)
- Whether XTRA data is actually being applied/used after download
- Charging problem detection (flags no charge after 24h, not tested)
- Data buffering retry (stores unsent JSON, unclear if retry works)
- Power-off sequencing (3.3V rail and modem shutdown before sleep)

### Not Working / Needs Improvement
- **Wave height (Hs):** Very hard to get real, trustworthy numbers from the GY-91/MPU-9250. Needs work.
- **Wave period:** Uncertain if working correctly.
- **Wave power:** Absolutely not working as it should. Needs work.
- **Wave direction:** Not working. Magnetometer returns NAN. Was hoping GPS could help, or a working magnetometer.
- **Anchor drift detection:** Not working as it should. Currently resets drift counter on every successful GPS fix (updateLastGpsFix resets counter to 0), so drift is only detected within a single boot cycle.
- **Temperature anomaly detection:** checkTemperatureAnomalies() is a stub that does nothing.
- **Tide data:** Cannot use barometer (sealed airtight enclosure). Would need GPS-based tide detection if possible.
- **BMP280 barometer:** Listed as dependency but never used (useless in sealed enclosure).

## Known Bugs and Issues (from code audit)

### High Severity
1. **GPIO 4 pin conflict:** MODEM_PWRKEY and GPS_POWER_PIN are both GPIO 4. powerOnGPS() holds it HIGH, interfering with the modem PWRKEY sequence and vice versa.
2. **Potential reboot loop:** If nextWakeUtc <= nowUtc (due to time drift or long processing), sleepSec stays 0, causing immediate wake from deep sleep.
3. **Credentials in repo:** config.h contains API_KEY and is tracked in git despite being in .gitignore.

### Medium Severity
4. **sendJsonToServer treats any HTTP response as success:** Doesn't check status code (4xx, 5xx treated as success).
5. **JSON buffer too small:** StaticJsonDocument<1024> may overflow with all nested objects.
6. **build_all_buoys.py missing:** Referenced in platformio.ini but doesn't exist in repo.
7. **triedNBIoT is static inside function:** Once NB-IoT is tried, it's never tried again in the same boot.

### Low Severity / Unused Code
8. **gpsBegin() declared but not implemented** (gps.h:14)
9. **getSampleDurationMs() defined but never called** (wave.cpp:270) - hardcoded 180000ms used instead
10. **markFirmwareUpdateAttempted() never called** (rtc_state.cpp:117)
11. **All magnetometer calibration functions are stubs** (sensors.cpp:43-47)
12. **getRelativeAltitude() and readTideHeight() always return 0** (sensors.cpp:52-60)
13. **lastSolarChargeTime never written to** (rtc_state.h:15)
14. **checkBatteryChargeState() declared in both battery.h and rtc_state.h** (duplicate)
15. **getCurrentHour() called but return value unused** in determineSleepDuration() (battery.cpp:188)
16. **Fallback month hardcoded to August 2025** (battery.cpp:148-149)
17. **beginSensors() declared in both wave.h and sensors.h** (duplicate)
18. **Forward declarations in wave.cpp duplicate header declarations** (wave.cpp:15-16)
19. **Root-level backup files:** WORKING_NTP+XTRA+GPS.cpp and WORKING_BATTERY_MEASUREMENT_main.cpp
20. **Junk .py file at root** containing less command help text
21. **Adafruit BMP280 Library dependency unused** (platformio.ini)
22. **config.h.example missing several defines** present in config.h

## Historical Problems (lessons learned)

1. **Brownouts:** The biggest recurring problem. Caused by turning on too many things simultaneously. Solution: power ONE subsystem at a time with delays between transitions.
2. **Power draw during sleep:** Was too high. Solved by soldering off LEDs on the board and setting all pins to high-impedance before sleep.
3. **Board not waking up:** Had a problem where the board appeared powered off and only recovered after physically removing and reconnecting batteries. Likely related to brownout recovery or GPIO hold state.
4. **Cellular connection failures:** Many iterations to get working. Currently uses LTE-M preferred with NB-IoT fallback, power-cycling between attempts.
5. **GPS fix failures:** Many iterations. Now uses XTRA aiding data, NTP time sync before GNSS, and dynamic timeout based on battery.
6. **HTTPS failures on SIM7000G:** Never got HTTPS to work for OTA or XTRA download. Using HTTP only.
7. **GY-91 inaccurate data:** Wave measurements are unreliable. The IMU may need better calibration, or the algorithm needs work.
8. **Too aggressive power cycling:** Previously caused brownouts. Now uses conservative 5-second delays between power state changes.

## Power Budget Analysis

### Component Current Draw (approximate)
| Component | Active | Sleep/Off |
|-----------|--------|-----------|
| ESP32 (deep sleep) | - | ~10 uA |
| ESP32 (active, WiFi/BT off) | ~40 mA | - |
| SIM7000G modem (active) | ~100-300 mA peaks | - |
| SIM7000G GPS (active) | ~30-50 mA | - |
| GY-91 (active) | ~5-10 mA | - |
| DS18B20 (conversion) | ~1.5 mA | - |
| 3.3V rail quiescent | variable | 0 (switched off) |

### Battery Capacity
- 2x 18650 in parallel: ~6000-7000 mAh total (assuming ~3000-3500 mAh each)
- Usable range: 4.2V (100%) to ~3.0V (0%), but critical guard at 3.633V (20%)

### Solar Input
- 4x 0.3W 5V panels = 1.2W theoretical peak
- Haugesund, Norway (latitude ~59.4N):
  - Summer (June): ~18-19 hours daylight, but panels angled in 4 directions on a dome, effective maybe 3-5 peak sun hours equivalent
  - Winter (December): ~6 hours daylight, very low sun angle, maybe 0.5-1 peak sun hours equivalent
  - Panels are small and at various angles, significantly reducing effective output
  - Estimated effective daily harvest: Summer ~1.5-3 Wh, Winter ~0.2-0.5 Wh

### Wake Cycle Power Consumption (estimated)
- Battery measurement: ~8 seconds at ~40 mA = ~0.09 mAh
- Wave collection (3 min): ~180s at ~50 mA (ESP32 + GY-91) = ~2.5 mAh
- GPS fix (5-20 min): ~300-1200s at ~150 mA (ESP32 + modem + GPS) = ~12.5-50 mAh
- Cellular upload + OTA check (~2 min): ~120s at ~200 mA = ~6.7 mAh
- Overhead (delays, transitions): ~60s at ~40 mA = ~0.7 mAh
- **Total per cycle: ~22-60 mAh** (heavily dependent on GPS fix time)

### Sleep Current
- With all pins high-Z, 3.3V rail held LOW, modem off: ~10-50 uA
- Per hour: ~0.01-0.05 mAh (negligible)

## Solar Harvest vs Haugesund Climate

Haugesund (59.4N latitude) monthly average sun hours and estimated solar harvest:

| Month | Avg Sun Hours/day | Est. Effective Harvest/day | Season |
|-------|-------------------|---------------------------|--------|
| Jan | ~1 | ~0.1-0.3 Wh | Winter |
| Feb | ~2 | ~0.2-0.5 Wh | Winter |
| Mar | ~3 | ~0.4-0.8 Wh | Winter |
| Apr | ~5 | ~0.7-1.5 Wh | Winter |
| May | ~6 | ~1.0-2.0 Wh | Summer |
| Jun | ~7 | ~1.5-3.0 Wh | Summer |
| Jul | ~6 | ~1.2-2.5 Wh | Summer |
| Aug | ~5 | ~1.0-2.0 Wh | Summer |
| Sep | ~3 | ~0.5-1.2 Wh | Summer |
| Oct | ~2 | ~0.2-0.5 Wh | Winter |
| Nov | ~1 | ~0.1-0.3 Wh | Winter |
| Dec | ~0.5 | ~0.05-0.2 Wh | Winter |

Note: Haugesund is known for being one of the rainiest cities in Norway. Cloud cover significantly reduces solar harvest. These are optimistic estimates.

### Energy Balance Examples
- **Summer, battery >80% (3h cycle):** ~8 cycles/day x ~30 mAh = ~240 mAh/day drain. At 3.7V = ~0.9 Wh. Solar harvest ~1.5-3 Wh. **Positive balance in good conditions.**
- **Winter, battery >55% (24h cycle):** 1 cycle/day x ~30 mAh = ~30 mAh/day. At 3.7V = ~0.1 Wh. Solar harvest ~0.1-0.3 Wh. **Marginal - may slowly drain in dark months.**
- **Winter, battery 35-40% (168h = 1 week cycle):** 1 cycle/week x ~30 mAh = ~4.3 mAh/day. Essentially negligible drain. Should slowly recharge.

## Design Priorities (in order)

1. **Never run out of power.** The buoy is permanently sealed. If it dies, it's gone.
2. **Stability over features.** No brownouts, no hangs, no crashes. The watchdog is a last resort, not a feature.
3. **Correct timings per datasheets.** Follow LilyGo, SIM7000G, GY-91, DS18B20 specifications exactly.
4. **Water temperature is the core feature.** Everything else is secondary.
5. **Accurate timestamps.** NTP must work for proper data logging.
6. **Minimize firmware size.** Less data cost for OTA upgrades over cellular.
7. **Wave data accuracy.** Get trustworthy wave measurements.

## Future Goals

- Get wave measurements working reliably
- Implement tide detection (possibly via GPS altitude?)
- Implement working temperature anomaly detection
- Fix anchor drift detection
- Evaluate and potentially adjust sleep schedule based on real-world solar harvest data
- Consider wave direction via GPS (if magnetometer remains unusable inside sealed metal/magnetic enclosure)
- Remove all unused code to reduce firmware size
- Fix all identified bugs

## File Structure

```
src/
  main.cpp       - Entry point, setup/loop, power management, pin definitions
  config.h       - Per-buoy configuration (API keys, server URLs, node ID) - GITIGNORED
  config.h.example - Template for config.h
  battery.cpp/h  - Battery monitoring, sleep duration, season detection
  gps.cpp/h      - GPS fix, NTP sync, XTRA download, AT command helpers
  json.cpp/h     - JSON payload construction
  modem.cpp/h    - Cellular network connection, HTTP POST upload
  ota.cpp/h      - Over-the-air firmware updates
  power.cpp/h    - ADC battery voltage reading
  rtc_state.cpp/h - RTC-persisted state, anchor drift, upload status
  sensors.cpp/h  - DS18B20 temperature, IMU stubs
  utils.cpp/h    - Wake reason logging
  wave.cpp/h     - Wave data collection and analysis (IMU sampling, Mahony filter, IIR filters)
```
