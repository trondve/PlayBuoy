# PlayBuoy - Project Knowledge File

This file captures everything about the PlayBuoy project to provide context for development, debugging, and future improvements.

## Project Overview

PlayBuoy is a solar-powered, permanently sealed, waterproof IoT buoy that floats on the surface of a lake or coastal water. It autonomously collects environmental data (water temperature, wave conditions) and transmits it via 4G cellular to a web API. The data is displayed on a website for beachgoers and water sports enthusiasts.

**Primary deployment:** Litla Grindevatnet, a lake near Haugesund, Norway.

**Core user-facing feature:** Water temperature. This is the most important metric for end users.

**Secondary features:** Wave height, wave period, wave power, wave direction - for "nerds who want more details."

**Target audience:** People who want to play at the lake or beach, beachgoers wanting to know water temperature and wave conditions before going.

### Inspiration

This project is inspired by the "Smart Buoy" series:
- Instructables guide: https://www.instructables.com/Smart-Buoy/
- Medium summary: https://medium.com/nerd-for-tech/smart-buoy-summary-602f9db544bb
- Source code: https://github.com/sk-t3ch/smart-buoy
- YouTube playlist: https://www.youtube.com/watch?v=S-XMT6GDWk8&list=PLoTBNxUNjtjebnBR1B3RfByp8vZtZ6yL7

### Logos

Project logos are stored in `assets/logos/`:
- `playbuoy-logo-light.png` - Light background version
- `playbuoy-logo-dark.png` - Dark background version
- `playbuoy-logo-dark-alt.png` - Dark background alternate version

## Hardware

### Main Board
- **LilyGo T-SIM7000G** (ESP32-D0WD-V3, 240MHz, 4MB flash + SIM7000G cellular modem with built-in GPS/GLONASS/BeiDou)
- LEDs on the board have been soldered off to reduce power draw during sleep
- UART baud rate: 57600 (stored in SIM7000G with AT&W)

### Sensors
- **DS18B20** waterproof temperature probe - dangles in the water (GPIO 13, OneWire). Works perfectly.
- **GY-91** IMU module (contains MPU-9250 or MPU-6500 variant accelerometer/gyroscope/magnetometer + BMP280 barometer) - mounted inside the buoy for wave motion sensing (I2C: SDA=GPIO 21, SCL=GPIO 22). The WHO_AM_I register is checked at runtime: 0x70 = MPU-6500 (no magnetometer), 0x71/0x73 = MPU-9250 (has magnetometer). Data accuracy has been problematic.

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
- Upper dome has 4 angled solar panel cutouts facing different directions
- Antenna protrudes from the top
- Lower half is submerged; DS18B20 probe exits through a waterproof gland
- Two halves seal together - once sealed, the electronics are inaccessible
- Hook/ring at the bottom for anchor attachment

## Buoy Deployments

The build system supports multiple buoys with per-buoy configuration:

| Buoy ID | Node ID | Name | Location |
|---------|---------|------|----------|
| grinde | playbuoy_grinde | Litla Grindevatnet | Lake near Haugesund |
| vatna | playbuoy_vatna | Vatnakvamsvatnet | Another lake |

## Network & Infrastructure

- **Cellular:** Telenor Norway, 4G LTE-M (preferred, AT+CNMP=38) with NB-IoT fallback (AT+CNMP=51). NB-IoT SIM cards require business registration in Norway, so currently using regular 4G. Band configuration: CAT-M bands 3 and 20 (EU carriers).
- **API Server:** `playbuoyapi.no` port 80, HTTP POST to `/upload` with `X-API-Key` header
- **OTA Server:** `trondve.ddns.net` (HTTP only - HTTPS never worked on the SIM7000G). Hosts firmware `.bin` and `.version` files per buoy (e.g. `playbuoy_grinde.bin`, `playbuoy_grinde.version`).
- **XTRA Aiding Data:** Downloaded from `http://trondve.ddns.net/xtra3grc.bin` every 7 days to speed up GPS fixes. Stored on modem filesystem at `/customer/xtra3grc.bin`. Applied via `AT+CGNSCPY` + `AT+CGNSXTRA=1` + `AT+CGNSCOLD`.
- **NTP:** `no.pool.ntp.org` via `AT+CNTP` command on modem
- **Timezone:** `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Oslo, handles CET/CEST automatically)
- **Database:** Running on a Raspberry Pi at the owner's home
- **XTRA HTTP Server:** Running on a Windows PC at the owner's home
- **DNS:** Custom DNS (1.1.1.1 / 8.8.8.8) applied via `AT+CDNSCFG` after PDP is up

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
- Battery voltage measurement via ADC (after much debugging - see Reference Files below)
- Battery percentage estimation (OCV lookup table with binary search + interpolation)
- Sleep duration calculation (season + battery aware)
- Quiet hours enforcement
- Critical battery guard (deep sleep when low)
- Cellular connection to Telenor (after much debugging)
- JSON payload construction and HTTP POST upload
- OTA firmware updates via HTTP (HTTPS never worked on SIM7000G)
- GPS fix acquisition (after much debugging - see Reference Files below)
- XTRA aiding data download via HTTP
- Watchdog timer
- Deep sleep with minimum leakage pin configuration
- Data buffering for failed uploads (RTC memory)
- Boot counter and wake reason tracking
- Multi-buoy build system (build_all_buoys.py)

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
6. **triedNBIoT is static inside function:** Once NB-IoT is tried, it's never tried again in the same boot.
7. **build_all_buoys.py has hardcoded Windows path:** Line 129 references `C:\\Users\\trond\\.platformio\\penv\\Scripts\\platformio.exe` which won't work on other systems.

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
19. **Junk .py file at root** containing `less` command help text (accidental creation)
20. **Adafruit BMP280 Library dependency unused** (platformio.ini)
21. **config.h.example missing several defines** present in config.h (USE_CUSTOM_DNS, DNS_PRIMARY, DNS_SECONDARY, BATTERY_CALIBRATION_FACTOR, ENABLE_CRITICAL_GUARD, BATTERY_CRITICAL_PERCENT, BATTERY_CRITICAL_VOLTAGE, ENABLE_GENTLE_MODEM_TIMING, ENABLE_CPOWD_SHUTDOWN, SIM_PIN)
22. **update_firmware_version.py references create_version_files.py** which doesn't exist

## Reference Files (confirmed working code)

These files at the repo root contain confirmed-working standalone test code. They serve as the "known-good" reference implementations that the main firmware was built from:

### WORKING_BATTERY_MEASUREMENT_main.cpp
Confirmed working battery voltage measurement code. Key technique:
- GPIO 35 with 12-bit ADC, 11dB attenuation
- Takes 5 bursts of 50 averaged samples each (with 1s between bursts)
- Uses median-of-five for robustness
- Formula: `(raw / 4095.0) * 2.0 * 3.3 * (1100.0 / 1000.0)`
- 2-second stabilization delay before first measurement
- This exact method is replicated in `src/power.cpp` (with slight calibration difference: 1110 vs 1100)

### WORKING_NTP+XTRA+GPS.cpp
Confirmed working NTP time sync, XTRA aiding data download, and GNSS fix code. Key findings documented in this file:
- UART baud rate: 57600 (must be stored with AT&W)
- Modem power sequence: POWER_ON HIGH -> RST pulse -> PWRKEY LOW 1s then HIGH -> DTR LOW
- Network: LTE Cat-M1 only, bands 3+20, APN "telenor.smart"
- NTP via AT+CNTP with 90-second polling for valid CCLK response
- XTRA download via AT+HTTPTOFS, applied with AT+CGNSCPY + AT+CGNSXTRA=1 + AT+CGNSCOLD
- GNSS start tries 3 different GPIO/SGPIO configurations to handle hardware variants
- PDP teardown order: CNACT -> CGACT -> CGATT -> CIPSHUT
- Typical firmware size: ~22% flash (285 KB of 1.3 MB), ~7% RAM (21 KB of 320 KB)
- This code was ported into `src/gps.cpp` as the production GPS implementation

## Build System

### build_all_buoys.py (main build script)
Builds firmware `.bin` files for each buoy by:
1. Backing up `src/config.h`
2. For each buoy: modifying NODE_ID, NAME, FIRMWARE_VERSION in config.h, running `pio run`, copying the output `.bin`
3. Creating `.version` and `.version.json` files for OTA version checking
4. Restoring the original config.h

Can be run standalone (`python build_all_buoys.py`) or as a PlatformIO post-build hook (registered in `platformio.ini` as `extra_scripts = pre:build_all_buoys.py`). Uses an environment guard (`PB_MULTI_BUILD`) to prevent infinite recursion during nested PIO builds.

**Note:** Contains a hardcoded Windows platformio.exe path that needs to be made portable.

### build_buoys.py (simpler PlatformIO hook)
A simpler SCons-based post-build action that copies the built firmware to per-buoy filenames in `firmware_builds/`. Less sophisticated than build_all_buoys.py (doesn't modify config.h per buoy, just copies the same binary).

### update_firmware_version.py
Interactive script to update the FIRMWARE_VERSION string across build scripts. References a `create_version_files.py` that doesn't exist.

### Output Structure
```
firmware/
  playbuoy_grinde.bin           - Firmware binary for Grinde buoy
  playbuoy_grinde.version       - Plain text version string
  playbuoy_grinde.version.json  - JSON with version, URL, name, node_id
  playbuoy_vatna.bin            - Firmware binary for Vatna buoy
  playbuoy_vatna.version
  playbuoy_vatna.version.json
```

## Historical Problems (lessons learned from commit history)

The project was started August 9, 2025 and went through rapid iteration over the following month. Key milestones and struggles visible in the git history:

### Timeline
- **Aug 9-10:** Initial commit, cellular modem connection fixes, OTA setup. Tried Netlify hosting for OTA, then GitHub raw, eventually settled on self-hosted `trondve.ddns.net`. Multiple rapid iterations on OTA troubleshooting.
- **Aug 11:** Major restructuring - changed sampling order/power-on order to conserve battery. Optimized wave sampling, battery measurement, GPS fix duration. Removed unused code.
- **Aug 12:** "Ready for deployment" commit, followed by calibration factor corrections.
- **Aug 13:** GPS rebuilt from scratch (2 commits: "rebuild gps", "rebuild").
- **Aug 20:** "working wave collection data" - wave measurement pipeline working.
- **Aug 28:** Major breakthrough: "Fully working GPS with NTP + XTRA + GNSS" - the WORKING_NTP+XTRA+GPS.cpp reference was created and ported into gps.cpp. This was a +1611/-623 line change.
- **Aug 29:** Sleep duration fine-tuning, timing adjustments to avoid errors, build system improvements.
- **Aug 30:** "USED FOR THE FIRST BUOY" - first real-world deployment.
- **Sep 2:** "Very optimized version. Accurate battery measurement." - the WORKING_BATTERY_MEASUREMENT_main.cpp reference was created. Power.cpp was massively simplified (-296 lines).
- **Sep 3-8:** JSON field changes, anchor drift fix, battery calibration factor, modem command softening, sleep duration adjustments, more logging. Device monitor logs from this period show real-world testing.
- **Sep 13:** "Lower power consumption during deep sleep" - final optimization before the current codebase.

### Recurring Struggles
1. **Brownouts:** The biggest recurring problem. Caused by turning on too many things simultaneously. Solution: power ONE subsystem at a time with delays between transitions.
2. **Power draw during sleep:** Was too high. Solved by soldering off LEDs on the board and setting all pins to high-impedance before sleep.
3. **Board not waking up:** Had a problem where the board appeared powered off and only recovered after physically removing and reconnecting batteries. Likely related to brownout recovery or GPIO hold state.
4. **Cellular connection failures:** Many iterations to get working. Currently uses LTE-M preferred with NB-IoT fallback, power-cycling between attempts. The modem is pre-cycled once at entry to connectToNetwork().
5. **GPS fix failures:** Many iterations. First attempts failed completely. Eventually solved by adopting the full NTP -> XTRA -> GNSS sequence from the working reference. Now uses XTRA aiding data, NTP time sync before GNSS, and dynamic timeout based on battery.
6. **HTTPS failures on SIM7000G:** Never got HTTPS to work for OTA or XTRA download. Using HTTP only.
7. **OTA hosting:** Tried Netlify, GitHub raw URLs, before settling on self-hosted DDNS.
8. **Battery measurement:** Went through many iterations. Original power.cpp was 250+ lines with multiple methods. Simplified to the proven median-of-five approach from the working reference.
9. **GY-91 inaccurate data:** Wave measurements are unreliable. The IMU may need better calibration, or the algorithm needs work.
10. **Too aggressive power cycling:** Previously caused brownouts. Now uses conservative 5-second delays between power state changes.

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
- Fix anchor drift detection (needs to persist across boot cycles)
- Evaluate and potentially adjust sleep schedule based on real-world solar harvest data
- Consider wave direction via GPS (if magnetometer remains unusable inside sealed metal/magnetic enclosure)
- Remove all unused code to reduce firmware size
- Fix all identified bugs
- Make build_all_buoys.py portable (remove hardcoded Windows path)

## File Structure

```
PlayBuoy/
  CLAUDE.md                           - This project knowledge file
  README.md                           - Setup and usage instructions
  platformio.ini                      - PlatformIO build configuration
  .gitignore                          - Git ignore rules

  assets/
    logos/                             - Project logo files (3 variants)

  src/
    main.cpp                          - Entry point, setup/loop, power management, pin definitions
    config.h                          - Per-buoy configuration (API keys, server URLs, node ID) - GITIGNORED
    config.h.example                  - Template for config.h
    battery.cpp/h                     - Battery monitoring, sleep duration, season detection, OCV table
    gps.cpp/h                         - GPS fix, NTP sync, XTRA download, AT command helpers
    json.cpp/h                        - JSON payload construction
    modem.cpp/h                       - Cellular network connection, HTTP POST upload
    ota.cpp/h                         - Over-the-air firmware updates with version comparison
    power.cpp/h                       - ADC battery voltage reading (median-of-five method)
    rtc_state.cpp/h                   - RTC-persisted state, anchor drift, upload status
    sensors.cpp/h                     - DS18B20 temperature, magnetometer stubs
    utils.cpp/h                       - Wake reason logging
    wave.cpp/h                        - Wave data collection and analysis (IMU sampling, Mahony filter, IIR filters)

  lib/
    MPU9250_asukiaaa-master/          - MPU9250 library (may not be actively used; wave.cpp uses direct I2C)

  firmware/                           - Built firmware binaries and version files per buoy
  logs/                               - Device monitor logs from testing sessions (Sep 2025)

  build_all_buoys.py                  - Main multi-buoy build script (standalone + PlatformIO hook)
  build_buoys.py                      - Simpler PlatformIO post-build copy hook
  update_firmware_version.py          - Interactive version string updater

  WORKING_BATTERY_MEASUREMENT_main.cpp - Reference: confirmed working battery ADC code
  WORKING_NTP+XTRA+GPS.cpp            - Reference: confirmed working NTP/XTRA/GPS sequence

  .py                                 - Junk file (contains `less` help text, should be deleted)
```
