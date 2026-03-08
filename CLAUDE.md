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
- `logo.png` - Full-size logo with background
- `logo-transparent.png` - Full-size logo with transparent background
- `logo-transparent.svg` - SVG version with transparent background
- `logo-128.png` - 128px thumbnail with background
- `logo-128-transparent.png` - 128px thumbnail with transparent background
- `logo-64.png` - 64px icon with background
- `logo-64-transparent.png` - 64px icon with transparent background

### 3D Print Files

STL/3MF/OBJ files for the buoy enclosure are stored in `assets/STL/`:
- `top.stl` / `top.3mf` - Upper dome (solar panel cutouts, antenna hole)
- `top_tight.stl` / `top_tight.obj` / `top_tight.mtl` - Tighter-fit upper dome variant
- `bottom.stl` / `bottom.3mf` - Lower hull (submerged half, anchor ring)
- `logo_curved.stl` - Curved logo emboss for the buoy surface

## Component Documentation

All datasheets, manuals, and reference examples are in `docs/components/`:

### LilyGo T-SIM7000G — `docs/components/Lilygo/`
- `T-SIM7000G-200415.pdf` — Board schematic/datasheet
- `T-SIM7000G-S3-Standard_Rev1.0.pdf` — S3 variant reference
- `SIM7000_A7608_A7670_ESP32.png` — Pin map diagram
- `GPS Antenna Specifications.pdf` — GPS antenna spec
- `LTE Antenna Specifications.pdf` — LTE antenna spec
- `SIM7000/` — Full SIM7000G AT command manuals and application notes (see below)
- `sim7000-esp32/REAMDE.MD` — LilyGo T-SIM7000G board README (pin definitions, getting started)
- `sim7000g-s3-standard/REAMDE.MD` — S3 variant README
- `Examples/` — Official LilyGo Arduino examples:
  - `ATdebug/` — Raw AT command debug sketch
  - `DeepSleep/` — Deep sleep example (important for power management)
  - `GPS_BuiltIn/`, `GPS_BuiltInEx/`, `GPS_Acceleration/`, `ModemGpsStream/` — GPS examples
  - `HttpClient/`, `HttpsClient/`, `HttpsBuiltlnGet/Post/Put/` — HTTP/HTTPS examples
  - `HttpsOTAUpgrade/` — OTA firmware update example
  - `ModemPowerOff/`, `ModemSleep/` — Modem power management examples
  - `ReadBattery/`, `PowerMonitoring/` — Battery/ADC examples
  - `ModemFileSystem/` — Modem filesystem (for XTRA storage)
- `TinyGSM/` — TinyGSM library source and examples (used in this project)
- `TinyGPSPlus/` — TinyGPS++ library source and examples

### SIM7000G — `docs/components/Lilygo/SIM7000/` and `docs/components/SIM7000G/`
- `SIM7000 Series_AT Command Manual_V1.06.pdf` — **Primary AT command reference** (use this for all modem AT commands)
- `SIM7000 Series_AT Command Manual_V1.04.pdf` — Older version (in SIM7000G/ folder)
- `SIM7000 Hardware Design_V1.07.pdf` — Hardware design guide
- `SIM7000 Series_GNSS_Application Note_V1.03.pdf` — **GNSS/GPS AT commands** (CGNSPWR, CGNSINF, XTRA, etc.)
- `SIM7000 Series_FS_Application Note_V1.01.pdf` — Filesystem commands (AT+HTTPTOFS for XTRA download)
- `SIM7000 Series_MQTT(S)_Application Note_V1.02.pdf` — MQTT support
- `SIM7000 Series_LBS_Application Note_V1.01.pdf` — Location-based services
- Chinese-language app notes for HTTP(S), NTP, TCP/IP, FTP, PING, SSL

### GY-91 (MPU-9250/MPU-6500 + BMP280) — `docs/components/GY-91/`
- Four screenshots of key register maps and configuration details for the IMU
- Use these when working on wave measurement code (accelerometer ranges, DLPF settings, sample rate divider)

### DS18B20 — `docs/components/DS18B20/`
- `scan4.pdf` — DS18B20 datasheet (temperature probe used for water temperature)

### Key references for common tasks
| Task | Where to look |
|------|--------------|
| AT commands (modem) | `docs/components/Lilygo/SIM7000/SIM7000 Series_AT Command Manual_V1.06.pdf` |
| GNSS/GPS AT commands | `docs/components/Lilygo/SIM7000/SIM7000 Series_GNSS_Application Note_V1.03.pdf` |
| XTRA file download | `docs/components/Lilygo/SIM7000/SIM7000 Series_FS_Application Note_V1.01.pdf` |
| Deep sleep / pin hold | `docs/components/Lilygo/Examples/DeepSleep/DeepSleep.ino` |
| Modem power off | `docs/components/Lilygo/Examples/ModemPowerOff/ModemPowerOff.ino` |
| Battery ADC reading | `docs/components/Lilygo/Examples/ReadBattery/ReadBattery.ino` |
| GPS streaming | `docs/components/Lilygo/Examples/GPS_BuiltIn/GPS_BuiltIn.ino` |
| OTA firmware update | `docs/components/Lilygo/Examples/HttpsOTAUpgrade/HttpsOTAUpgrade.ino` |
| Board pin map | `docs/components/Lilygo/SIM7000_A7608_A7670_ESP32.png` |
| IMU register config | `docs/components/GY-91/` (screenshots) |
| Temperature sensor | `docs/components/DS18B20/scan4.pdf` |

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
5. Check critical battery - if <= 25% or <= 3.70V, skip everything and deep sleep
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

### Battery Health Design Principles
The sleep schedule is designed around 18650 lithium-ion battery health:
- **Optimal storage range:** 40-60% (preserve this range when possible)
- **Daily charge should not exceed 80%** (actively discharge above 80%)
- **Never discharge below 20%** to prevent battery damage
- **Critical guard at 25%** provides safety margin for aged cells under peak modem load

The schedule is designed to be portable across deployment locations (sunny southern Europe to far-north Norway).

### Summer Schedule (May-September)
| Battery % | Sleep Duration | Rationale |
|-----------|---------------|-----------|
| > 80% | 2 hours | Actively discharge toward healthy range + frequent temp updates |
| > 70% | 3 hours | Good solar harvest, capture temperature changes |
| > 60% | 6 hours | Sustainable equilibrium for most climates |
| > 50% | 9 hours | In optimal storage range, moderate reporting |
| > 40% | 12 hours | Bottom of optimal range, conserve |
| > 35% | 24 hours | Below optimal, needs solar recharge |
| > 30% | 48 hours (2 days) | Low battery, conservation mode |
| > 25% | 72 hours (3 days) | Very low, let solar recover |
| <= 25% | 168 hours (1 week) | Near critical, maximum conservation |

### Winter Schedule (October-April)
| Battery % | Sleep Duration | Rationale |
|-----------|---------------|-----------|
| > 80% | 12 hours | Discharge toward healthy range despite low solar |
| > 70% | 24 hours | Daily check-in |
| > 60% | 24 hours | Still some margin, daily reporting |
| > 50% | 48 hours (2 days) | Conserve for winter survival |
| > 40% | 72 hours (3 days) | Entering optimal storage, minimal drain |
| > 35% | 168 hours (1 week) | Low, deep conservation |
| > 30% | 336 hours (2 weeks) | Very low |
| > 25% | 720 hours (~1 month) | Near critical |
| <= 25% | 2160 hours (~3 months) | Hibernate until spring |

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
```

## Code Review Findings (2026-03-08)

This section documents findings from a thorough code review of the PlayBuoy firmware conducted on 2026-03-08. It covers improvement recommendations, power/battery analysis, and timing observations.

---

### Category 1: Code Cleanup (Completed)

The following cleanup was performed as part of this review:

- **Deleted `.py` junk file** at repo root (contained `less` command help text, not Python code).
- **Removed unused functions** from source files: `getSampleDurationMs()` (wave.cpp/h), `markFirmwareUpdateAttempted()` (rtc_state.cpp/h), `getRelativeAltitude()` (sensors.cpp/h), `readTideHeight()` (sensors.cpp/h), all magnetometer calibration stubs (sensors.cpp/h), and `gpsBegin()` declaration (gps.h).
- **Removed Adafruit BMP280 Library and Adafruit Unified Sensor** from `platformio.ini` — neither library is referenced anywhere in the source code; the BMP280 barometer is useless in a sealed enclosure anyway.
- **Removed duplicate `checkBatteryChargeState()` declaration** from `rtc_state.h` — the function is implemented in `battery.cpp` and is correctly declared in `battery.h` only.
- **Completed `config.h.example`** with all missing defines: `USE_CUSTOM_DNS`, `DNS_PRIMARY`, `DNS_SECONDARY`, `BATTERY_CALIBRATION_FACTOR`, `ENABLE_CRITICAL_GUARD`, `BATTERY_CRITICAL_PERCENT`, `BATTERY_CRITICAL_VOLTAGE`, `ENABLE_GENTLE_MODEM_TIMING`, `ENABLE_CPOWD_SHUTDOWN`, and `SIM_PIN`.
- **`beginSensors()` duplicate** was already resolved before this review — it is now only declared in `sensors.h` and only implemented in `sensors.cpp`.

---

### Category 2: Improvement Recommendations

#### 2.1 Battery Measurement (src/power.cpp)

**Current approach:** 5 bursts of 50 averaged ADC samples each (with 1s between bursts), then median-of-five. Uses 12-bit ADC at 11 dB attenuation on GPIO 35. Formula: `(raw / 4095.0) * 2.0 * 3.3 * (1110.0 / 1000.0)`.

**Is the median-of-five optimal?**

Yes — this is a solid, proven approach for noisy ESP32 ADCs. The key strengths are:
- Burst averaging (50 samples) reduces high-frequency noise from ADC quantization.
- Inter-burst gaps (1 second) allow thermal and capacitive settling between readings.
- Median-of-five is robust to occasional outliers (e.g., momentary load spikes during modem activity). It is better than a simple mean for this reason.
- The 2-second stabilization delay before the first burst is important; the ADC can show transient offsets after pin reconfiguration.

**Recommendations:**
1. **ADC nonlinearity correction**: The ESP32 ADC is significantly nonlinear, especially at the extremes of the 11 dB attenuation range (~0-3.9 V input). For the battery voltage range of interest (3.3–4.2 V after the voltage divider: ~1.65–2.1 V at the ADC pin), nonlinearity can introduce errors of 50–150 mV. The `esp_adc_cal_characterize()` + `esp_adc_cal_raw_to_voltage()` API (available in ESP-IDF, accessible from Arduino) applies a stored hardware calibration correction and would improve accuracy without changing the median-of-five structure. This is the primary improvement available.
2. **Calibration factor of 1110 vs 1100**: The production code uses 1110 while the reference uses 1100. This 0.9% difference suggests empirical adjustment was done, which is appropriate. Keep it, but document the measured vs expected voltage that led to this value.
3. **Battery voltage before powering anything**: The code correctly reads battery voltage early in `setup()` before powering the 3.3V rail. This is critical for an accurate open-circuit voltage reading.
4. **Consider wider sample window**: 5 bursts across ~6 seconds is usually sufficient, but if high-current loads (modem TX) are ever active during measurement, outliers could still pass through. The current architecture avoids this by measuring before powering the modem, which is correct.

#### 2.2 Water Temperature (src/sensors.cpp — DS18B20 section)

**Current approach:** `DallasTemperature` library, 3 retry attempts on error codes (-127°C, 85°C, NaN, out of range -30 to 60°C). Returns NAN on failure.

**Is the approach optimal?**

Mostly yes. DS18B20 is reliable; the library handles the 1-Wire protocol correctly. Specific observations:

1. **No explicit resolution setting**: The DS18B20 defaults to 12-bit resolution (750ms conversion time). The code does not call `setResolution()`. The `DallasTemperature` library's `requestTemperatures()` will block for the full conversion time by default (parasitic power mode) or return immediately if non-blocking. **Recommendation**: Call `waterTempSensor.setWaitForConversion(true)` explicitly to make blocking behavior clear, or `setWaitForConversion(false)` with a manual `delay(750)` for better control. At 12-bit, conversion takes 750ms; at 11-bit, 375ms; at 9-bit, 93ms. For a lake buoy, 12-bit (0.0625°C resolution) is appropriate.
2. **No address-based read**: `getTempCByIndex(0)` works when there is only one sensor, but is fragile if a second sensor is ever added. If the device ever becomes multi-sensor, switch to address-based `getTempC(address)`.
3. **100ms retry delay is too short**: If the first read fails due to a bus error, 100ms is not enough time for a full 12-bit conversion retry. **Recommendation**: Increase retry delay to 800ms to cover a full conversion cycle.
4. **No bus power validation**: Adding `waterTempSensor.isConnected(address)` before reading would distinguish between a disconnected probe and a CRC error, enabling better diagnostics in the JSON payload.
5. **Temperature range check (-30 to 60°C)**: 60°C upper bound is appropriate for a lake. -30°C lower bound is more conservative than needed for a Norwegian lake (water won't go below 0°C) but acceptable as a sanity filter.

#### 2.3 Wave Data (src/wave.cpp)

**Current approach:** MPU-6500/9250 direct I2C at 10 Hz, Mahony AHRS filter for orientation, heave acceleration extracted by projecting specific force along gravity vector, IIR bandpass (0.28–1.0 Hz), trapezoidal integration to velocity and displacement, zero-upcrossing analysis for Hs/Tp.

**What is wrong and what would improve it:**

**Fundamental problems with double integration:**

The core challenge is that integrating noisy acceleration twice to get displacement causes unbounded drift. The IIR high-pass filter at 0.28 Hz partially addresses this but is a single-pole filter, meaning its roll-off is gentle and low-frequency drift still leaks through. The current architecture is:
1. Compute heave acceleration
2. Bandpass the acceleration (0.28–1.0 Hz)
3. Integrate velocity (trapezoidal)
4. Integrate displacement (trapezoidal)
5. Detrend displacement (linear regression)
6. Apply further bandpass to displacement

Steps 3–6 accumulate errors. The 3-minute sampling window reduces (but does not eliminate) drift buildup. The `DISP_AMP_SCALE = 1.75` correction factor is an empirical fudge to compensate for systematic underestimation — this indicates the pipeline is losing signal amplitude.

**Specific issues:**

1. **Wave height sanity check is too coarse**: The `if (H > 0.8f) H = 0.0f` check in `analyzeWaves()` silently zeros out any individual wave taller than 80 cm, then the `if (waves[i].H > 5.0f) return {0, 0, 0, 0}` check aborts all analysis if any wave exceeds 5 m. For a small lake (Litla Grindevatnet), wave heights above 30–40 cm are very unlikely; the thresholds could be tightened to reject obvious noise.
2. **Static IIR filter state**: The `static float g_lp_x`, `g_lp_y`, `g_lp_z` variables inside `recordWaveData()` persist across calls because they are declared `static`. On first boot this is fine (initialized to nominal values), but after a deep-sleep wake they will retain their last values from the previous boot unless explicitly reset. This can cause transient errors in the first seconds of heave estimation.
3. **Sample rate accuracy**: The code targets 10 Hz using `millis()`-based timing. On a loaded system (I2C, Mahony, Serial output), the actual sample interval may vary. Missed ticks are silently dropped. A proper real-time approach would use a hardware timer interrupt. For an IMU at 10 Hz this is minor but affects Tp accuracy.
4. **Mahony filter is used but orientation is not fully utilized**: The Mahony filter computes roll/pitch/yaw, but heave extraction uses only the gravity vector tracked by the separate slow LP filter — not the Mahony quaternion. This makes the Mahony filter essentially redundant for the current heave pipeline. Either use Mahony's quaternion to rotate acceleration to world frame (better) or remove Mahony and just use the gravity LP tracker (simpler, saves RAM/CPU).
5. **No calibration**: The accelerometer is used with factory sensitivity (±2g, 0.000598 m/s² per LSB which is slightly off from the datasheet value of 16384 LSB/g → 0.000599 m/s²). There is no offset or scale calibration. At rest, the IMU should read 0g on X/Y and +1g on Z; any offset directly biases the heave estimate.

**What the Smart Buoy project did differently:**

The original Smart Buoy (sk-t3ch) used an MPU-9250 and processed wave data in the frequency domain rather than time domain — converting acceleration to displacement via spectral integration (dividing FFT of acceleration by -ω² to get displacement spectrum), then computing Hs from the spectral moments. This avoids the compounding drift of double time-domain integration. For the PlayBuoy, a frequency-domain approach would likely be more robust. The ESP32 has sufficient RAM (320 KB) for a 1800-sample FFT window.

**Recommended improvements (in priority order):**
1. Remove the Mahony filter if not using quaternion-based world-frame rotation.
2. Reset static IIR state variables at the start of each `recordWaveData()` call.
3. Add per-axis accelerometer offset calibration (measure mean output at rest, subtract from readings).
4. Consider frequency-domain (spectral) wave analysis instead of time-domain double integration.
5. Tighten wave height plausibility thresholds for a small lake context.

#### 2.4 GPS Fix (src/gps.cpp)

**Current approach:** NTP sync → XTRA download (if stale) → GNSS start (with GPIO fallback sequence) → 60-second NMEA priming → fix polling loop with dynamic timeout.

**Is the NTP→XTRA→GNSS sequence optimal?**

Yes, this sequence is correct and follows the SIM7000G application notes. Key observations:

1. **NTP timeout of 90 seconds for CCLK polling**: This is conservative but appropriate. NTP response time over LTE-M is typically <5 seconds; the 90-second loop handles poor coverage. The code checks CCLK every 1 second, which is fine.
2. **XTRA staleness check via NVS (Preferences)**: Correct approach. The 7-day threshold matches the XTRA file validity period. One concern: if NTP sync fails, `nowCi.valid` is false and XTRA is skipped. This means XTRA is never refreshed if NTP is broken, which could be a silent issue. Recommend logging XTRA skip reason explicitly.
3. **60-second NMEA priming (`gnssSmoke60s`)**: The NMEA streaming is currently printed to Serial (`SerialMon.println(line)`) only for non-Galileo sentences. This is good for debugging but may slow down the main loop due to Serial output. In production this output could be removed or throttled.
4. **Three-attempt GNSS start sequence** (SGPIO polarity variants): This was necessary because different hardware revisions of the LilyGo T-SIM7000G use different GPIO polarities for the GPS power control. The current approach tries all variants sequentially, which adds ~6 seconds overhead on first boot. This is acceptable.
5. **`gnssEngineRunning()` uses only the first field of CGNSINF**: Correct. It checks if the engine is running (field 0 = 1), not whether a fix is available.
6. **GPS timeout is battery-aware**: Correct design. Shorter timeout when battery is low avoids draining the battery on a cold start.
7. **GPS skip when fix < 24 hours old**: `GPS_SYNC_INTERVAL_SECONDS = 24 * 3600` is stored in config.h and used in main.cpp. This is a good optimization but means the buoy's position in the JSON can be up to 24 hours stale. For an anchored lake buoy this is fine.
8. **Issue — PDP teardown after GNSS then re-establishment**: After GPS, PDP is torn down (`tearDownPDP()` inside `syncTimeAndMaybeApplyXTRA()`), and then `connectToNetwork()` is called again to re-establish for data upload. This double PDP setup/teardown (cellular register → XTRA → GPS → teardown → re-register → upload) adds latency and power consumption. A potential optimization would be to keep the PDP context alive between XTRA download and data upload, though this conflicts with the design principle of powering one subsystem at a time.
9. **`extern void powerOnGPS()`** is declared inside `gnssStart()` in gps.cpp, creating a cross-file dependency on a function in main.cpp. This is fragile. A better pattern would be to pass a function pointer or have a dedicated GPS power header.

#### 2.5 OTA Updates (src/ota.cpp)

**Current approach:** HTTP GET to `.version` file → compare version strings → if newer, HTTP GET `.bin` → stream to ESP32 OTA partition → restart.

**Issues and improvements:**

1. **No HTTPS**: OTA downloads happen over plain HTTP. A firmware image delivered over HTTP can be tampered with in transit (man-in-the-middle). Since HTTPS never worked on SIM7000G (per project notes), this is a known limitation. The ESP32 OTA system does verify the image signature if bootloader secure boot is enabled, but that requires setup at flash time. As a minimum, consider publishing a SHA-256 hash alongside the `.bin` and verifying it after download before calling `Update.end()`.
2. **Version URL construction** (`checkForFirmwareUpdate`): The function derives the version URL from the firmware URL by replacing `.bin` with `.version`. If the base URL already doesn't end in `.bin`, it appends `.version`. This means the constructed URL for a base URL of `http://server/playbuoy_grinde` would be `http://server/playbuoy_grinde.version` — which is correct given the build system output structure.
3. **`ensurePdpForHttp()` uses TinyGSM's `gprsConnect(NETWORK_PROVIDER, "", "")`**: This uses the `NETWORK_PROVIDER` string ("telenor") as the APN. This differs from gps.cpp which uses "telenor.smart" as primary. If gps.cpp's tearDownPDP disconnected GPRS, OTA's `ensurePdpForHttp()` reconnects via TinyGSM using a different APN. This could result in OTA using the wrong APN. Recommend making the APN consistent across all modules, or having OTA re-use the connection established by `connectToNetwork()`.
4. **`httpGetTinyGsm()` has a double body-stripping bug**: The body is read as everything after `\r\n\r\n` (HTTP header end) in the streaming read, but then at line 88 `int p = body.indexOf("\r\n\r\n"); if (p >= 0) body = body.substring(p + 4)` is applied again. If the response has no extra blank line, this second stripping is harmless (p = -1), but it suggests the first header-skipping loop may not work reliably. The `goto headers_done` pattern breaks out of nested loops but only when the terminator is found within client data — if headers arrive in multiple TCP packets this could miss the boundary.
5. **OTA restart without graceful modem shutdown**: After `downloadAndInstallFirmware()` succeeds, `ESP.restart()` is called immediately. The modem is not powered down first. After restart, the modem boot sequence will be retried from scratch. This is acceptable given the sealed design but adds ~15s to the next boot cycle.
6. **`markFirmwareUpdateAttempted()` is never called**: The RTC flag `firmwareUpdateAttempted` is never set. If OTA is interrupted by a watchdog or brownout during the write, the firmware could be left in a corrupt state with no flag set. The ESP32 OTA partition system handles rollback at the bootloader level (via `ESP_OTA_IMG_PENDING_VERIFY`), so the practical impact is low, but the flag cleanup code (`clearFirmwareUpdateAttempted()`) runs unnecessarily on every boot.

#### 2.6 JSON Building and Upload (src/json.cpp, src/modem.cpp)

**JSON building (`json.cpp`):**

1. **`StaticJsonDocument<1024>` may overflow**: The current JSON payload includes nested objects for `wave`, `rtc`, `net`, and `alerts`, plus top-level fields for nodeId, name, version, timestamp, lat, lon, temp, battery, temp_valid, uptime, reset_reason, hours_to_sleep, next_wake_utc, battery_change_since_last. Estimating the serialized size: ~600–800 bytes for a typical payload. With a 1024-byte static document, there is ~200–400 bytes of headroom. The headroom is adequate for current fields but leaves little room for future additions. ArduinoJson v6's `StaticJsonDocument` silently truncates if the capacity is exceeded. **Recommendation**: Increase to `StaticJsonDocument<2048>` or switch to `DynamicJsonDocument` with a size check.
2. **`getWaterTemperature()` is called twice in `loop()`**: Once in `setup()` and once in `buildJsonPayload()` call inside `loop()`. Each call triggers a DS18B20 conversion cycle (750ms blocking). The second call happens while the 3.3V rail is already powered off (sensors were powered off before the modem section). This would return NAN. **Recommendation**: Store the temperature reading taken during the sensor-powered phase and pass it directly to `buildJsonPayload()`, rather than re-reading. (Actually looking at main.cpp more carefully, both JSON builds in loop call `getWaterTemperature()` directly — this is a bug if the 3V3 rail is off at that point.)
3. **`waveDirection` string**: Currently always "N/A" (magnetometer returns NAN). This is a known issue but the JSON field is still sent, making it look like a real measurement to the API.

**HTTP upload (`modem.cpp` — `sendJsonToServer`):**

1. **`sendJsonToServer` treats any HTTP response as success**: The function reads the response with `client.readStringUntil('\n')` and returns `true` if any response was received (`gotResponse = true`). It never parses the HTTP status code. A `400 Bad Request`, `401 Unauthorized`, `422 Unprocessable Entity`, or `500 Internal Server Error` from the API will be treated as a successful upload. This means `markUploadSuccess()` is called even when the server rejected the data. **Recommendation**: Parse the first line of the HTTP response to extract the status code and only return true for 2xx responses.
2. **No `Content-Length` validation before upload**: The JSON string is sent directly without checking if it fits within the TCP buffer. For the current payload size this is not a problem, but is worth noting.
3. **`triedNBIoT` is a `static` local variable in `connectToNetwork()`**: Once NB-IoT is tried (or the LTE-M attempt fails), `triedNBIoT` is permanently `true` for the lifetime of the process. This means if the first NB-IoT attempt fails, it is never tried again in the same boot cycle, even across the multiple retry attempts. Since the entire boot cycle is the "process lifetime" (no RTOS tasks), this only affects the within-boot retry behavior.
4. **APN inconsistency between modules**: `connectToNetwork()` in modem.cpp uses `NETWORK_PROVIDER` ("telenor") as APN when calling `modem.gprsConnect()`. But `gps.cpp` uses "telenor.smart" as the primary APN. The working cellular connection may depend on which APN is used. This should be consolidated.

#### 2.7 Deep Sleep (src/main.cpp, src/battery.cpp)

**Current approach:** `esp_sleep_enable_timer_wakeup()` with computed seconds until next wake UTC epoch. All non-RTC pins set to high-impedance. GPIO 25 (3V3 rail enable) held LOW across sleep via `gpio_hold_en()`.

**Observations and improvements:**

1. **Sleep duration calculation correctness**: The `sleepSec = nextWakeUtc - nowUtc` calculation can underflow if `nextWakeUtc <= nowUtc` (if NTP time drift or slow processing pushes past the intended wake time). The code does `if (nextWakeUtc > nowUtc) sleepSec = nextWakeUtc - nowUtc;` — so `sleepSec` stays 0 if this condition fails. A `sleepSec = 0` causes `esp_sleep_enable_timer_wakeup(0)` which on ESP32 results in an immediate wakeup (essentially a fast reboot loop). **This is a documented high-severity bug.** Recommendation: add a minimum sleep of at least 300 seconds (5 minutes) as a floor: `if (sleepSec < 300) sleepSec = 300;`.
2. **GPIO hold correctness**: `gpio_hold_dis(GPIO_NUM_25)` is called before driving the pin, and `gpio_hold_en(GPIO_NUM_25)` after setting it LOW. This is the correct pattern from ESP-IDF docs. One issue: `gpio_deep_sleep_hold_en()` is called at the end of `preparePinsAndSubsystemsForDeepSleep()`. This is the global enable for deep-sleep pin holding, and it must be called after all individual `gpio_hold_en()` calls. The current order is correct.
3. **WiFi/BT shutdown**: `WiFi.mode(WIFI_OFF)` and `btStop()` are called in `preparePinsAndSubsystemsForDeepSleep()`. These are correct but should ideally be called earlier (before any subsystem work) to ensure no residual power consumption from radio clocks. On a buoy that never uses WiFi or BT, they should never have been enabled in the first place — confirm that `WiFi.mode()` is not automatically initialized by Arduino/TinyGSM.
4. **`getCurrentHour()` return value is unused in `determineSleepDuration()`**: The function calls `getCurrentHour()` but the returned value is stored in `hour` and then never used in the sleep duration logic — the hour is only used for debug logging via `SerialMon.printf`. The actual quiet-hours adjustment is done separately in `adjustNextWakeUtcForQuietHours()`. This is wasteful (getCurrentHour loops for up to 10 seconds waiting for valid RTC time), but harmless.
5. **Quiet-hours window (00:00–05:59)**: The code pushes wake time to 06:00 local if the computed wake falls in this window. This is a sensible optimization. One edge case: if the sleep duration is very short (3 hours from 22:00, waking at 01:00), the pushed wake time would be 06:00 — correct. But if sleep duration is 3 hours from 03:00 (waking at 06:00), the quiet hours check passes since 06:00 is not in [0, 6). This edge case is correctly handled.
6. **No explicit `esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF)`**: Keeping the crystal oscillator on during deep sleep is unnecessary (the RTC uses its own 150 kHz internal oscillator). Adding `esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF)` before `esp_deep_sleep_start()` could save additional power (~250 µA).

---

### Category 3: Power & Battery Analysis

#### 3.1 Sleep Schedule vs Solar Harvest (May–August)

**Context:** Haugesund (59.4°N) is one of Norway's rainiest cities. Effective daily solar harvest is significantly lower than peak-sun-hours calculations suggest.

**Analysis of summer schedule (May–August priority months):**

The summer schedule (May–September) uses the following thresholds:

| Battery % | Sleep Duration |
|-----------|---------------|
| > 80%     | 3 hours       |
| > 75%     | 4 hours       |
| > 70%     | 6 hours       |
| > 65%     | 9 hours       |
| > 60%     | 12 hours      |
| > 55%     | 24 hours      |

**For May–August (the most valuable months for end users), are the thresholds right?**

The key question is: can the buoy sustain reporting at the top tiers (3–6 hour intervals) throughout a typical summer day in Haugesund?

**Energy balance at 3-hour interval (>80% battery):**
- Cycles per day: 8
- Energy per cycle: ~30 mAh (GPS fix ~10 min at 150 mA = 25 mAh, upload ~2 min at 200 mA = 6.7 mAh, wave 3 min at 50 mA = 2.5 mAh, overhead = ~2 mAh) ≈ 36 mAh/cycle
- Daily drain: 8 × 36 mAh = 288 mAh
- At 3.7V: 288 × 3.7 / 1000 = 1.07 Wh/day
- Solar harvest on a sunny summer day in Haugesund: estimated 1.5–3 Wh/day
- **Result: Marginally positive on sunny days, negative on cloudy days.**

Haugesund's cloud cover in summer averages 60–70% of days with significant cloud. This means on a typical summer day, solar harvest is 30–40% of the clear-sky estimate: ~0.5–1.2 Wh/day. At the 3-hour interval, the buoy would typically be net-draining even in summer.

**Recommendation:**
- The >80% threshold for 3-hour intervals is aggressive for Haugesund's climate. Consider making the 3-hour interval only trigger at >85% and the 4-hour interval at >80%.
- For May–June (longest days), the current schedule is closer to sustainable.
- For August, daylight drops from ~19h to ~15h, and cloud cover is higher; the budget becomes tighter.
- The >70% / 6-hour threshold is likely the "sustainable summer equilibrium" for average Haugesund conditions. Reporting every 6 hours (~4 cycles/day × 36 mAh = 144 mAh = 0.53 Wh) is achievable even on partly cloudy summer days.
- **Most important:** Ensure the 24-hour minimum in winter is preserved; this is appropriate and likely conservative enough for survival through Norwegian winter.

#### 3.2 OCV Lookup Table Accuracy

**Current OCV table vs standard 18650 Li-ion reference points:**

| SoC % | Current OCV (V) | Reference OCV (V) | Deviation |
|--------|-----------------|------------------|-----------| 
| 100%   | 4.200           | 4.200            | 0 mV      |
| 90%    | ~4.103          | 4.100            | +3 mV     |
| 80%    | ~3.983          | 4.000            | -17 mV    |
| 70%    | ~3.913          | 3.900            | +13 mV    |
| 60%    | ~3.853          | 3.800            | +53 mV    |
| 50%    | ~3.803          | 3.700            | +103 mV   |
| 35%    | ~3.733          | 3.600            | +133 mV   |
| 20%    | ~3.633          | 3.500            | +133 mV   |
| 10%    | ~3.583          | 3.400            | +183 mV   |
| 5%     | ~3.503          | 3.300            | +203 mV   |
| 0%     | 3.000           | 3.000            | 0 mV      |

**Key finding:** The current OCV table significantly **overestimates SoC in the 5–60% range**. At 3.733V (which the table calls 35%), a standard 18650 is actually closer to 50–55% SoC. At 3.633V (which the table calls 20%), a standard 18650 is actually closer to 30–35% SoC.

This overestimation has two practical effects:
1. **Battery percentage shown in JSON is optimistic**: The buoy appears to have more charge than it does.
2. **Critical guard triggers late**: `BATTERY_CRITICAL_VOLTAGE = 3.633f` maps to only ~20% reported SoC but actual SoC closer to 30%. The critical guard would engage at a higher actual SoC than intended, giving a reasonable safety margin — but this means the sleep schedule shifts to longer intervals sooner than the battery truly requires.
3. **Sleep schedule thresholds are shifted**: Since the reported percentage is higher than actual, the buoy will spend more time at the higher-frequency intervals than the battery can sustain.

The current table appears to have been optimized for a specific cell or characterized empirically. If the ADC readings match real-world behavior of the actual cells installed, the table may be correct for those cells. However, if using generic 18650 cells (Samsung 30Q, LG MJ1, Sanyo NCR18650GA, etc.), the standard curve should be used.

**Recommendation:** Measure actual cell discharge curve under representative load (~150 mA), record OCV at each 10% interval, and update the table to match the actual cells installed. This is the most impactful improvement for long-term buoy survival.

#### 3.3 Minimum Safe Battery Percentage

**SIM7000G peak current draw:**
The SIM7000G datasheet specifies typical peak current of 2A during LTE transmission bursts. At 3.7V cell voltage, this is ~7.4W peak draw — significantly exceeding the ESP32's 3.3V regulator capacity. The LilyGo T-SIM7000G board has its own power path for the SIM7000G that draws directly from the battery, bypassing the 3.3V regulator.

**Safe minimum voltage analysis:**
- The SIM7000G operates down to 3.4V supply voltage (per datasheet).
- With 2x 18650 in parallel, internal resistance is halved (~50–75 mΩ combined).
- Voltage drop under 2A peak: ΔV = I × R = 2 × 0.075 = 0.15V worst case.
- Required minimum resting voltage: 3.4V + 0.15V = ~3.55V to sustain peak transmission bursts.
- On the standard OCV curve, 3.55V corresponds to ~25–30% SoC.
- On the current (shifted) OCV table, 3.55V corresponds to roughly 30–35% reported SoC.

**Current setting:** `BATTERY_CRITICAL_PERCENT = 20%` and `BATTERY_CRITICAL_VOLTAGE = 3.633V`

**Assessment:** The 3.633V voltage threshold is marginally safe — it's above the minimum operating voltage for the SIM7000G under load if the cells have low internal resistance. However, as cells age (higher internal resistance), the voltage drop under load increases. With aged cells, 3.633V OCV could result in sub-3.4V under peak modem load.

**Recommendation:** Increase `BATTERY_CRITICAL_VOLTAGE` to `3.70V` and `BATTERY_CRITICAL_PERCENT` to `25%`. This provides ~100 mV more headroom for aged cells and worst-case peak current draw. Given that the buoy is permanently sealed and cannot have batteries replaced, being conservative here is correct — the cost is slightly earlier sleep, but the benefit is protection against brownout-induced firmware corruption or modem UART state corruption.

---

### Category 4: Timing & Additional Metrics

#### 4.1 Timing Delays — src/modem.cpp and src/gps.cpp

**Modem power-on sequence (main.cpp `powerOnModem()`):**
- `PWRKEY` LOW for 2000ms → then HIGH: **SIM7000G datasheet specifies minimum 1 second LOW pulse** to power on. The 2-second pulse is within spec (conservative is fine). The 10-second settling delay after the sequence (`delay(10000)`) is very conservative — the datasheet says the modem is ready for AT commands within ~5 seconds after PWRKEY release. The extra 5 seconds is wasted time per boot cycle (adds 5 seconds to every boot). Could be reduced to 5–6 seconds safely.
- `modem.init()` then `delay(5000)` in `connectToNetwork()`: The 5-second delay after `modem.init()` before sending AT commands is unnecessary since the modem should already be running after the 10-second power-on settle. This adds another 5 seconds per connection attempt (up to 3 attempts = 15 seconds wasted).
- **Total excess settling time per boot (GPS path)**: ~15–20 seconds across all conservative delays. Over 8 cycles/day in summer this is 2+ minutes of unnecessary active time per day.

**Modem power-off sequence (main.cpp `powerOffModem()`):**
- `CPOWD=1` with 8-second timeout: Correct. The SIM7000G graceful shutdown typically completes in 2–4 seconds.
- Fallback: PWRKEY LOW 1000ms, then HIGH 1500ms: The datasheet specifies PWRKEY LOW for ≥1.2 seconds to power off. The 1000ms LOW pulse is slightly below spec. **Recommendation**: Increase to 1200ms minimum.

**AT command timeouts (gps.cpp `sendAT()`):**
- Default timeout: 1500ms. This is adequate for most AT commands but may be tight for network-related commands. The code overrides specific commands with longer timeouts (e.g., `AT+CGNSCPY` with 7000ms). The 20ms `preATDelay()` between commands is appropriate.
- `AT+CNACT` retry loop uses `delay(1200)` between attempts over a 20-second window: Reasonable for PDP establishment.
- `tearDownPDP()` delays of 400ms between each command (CNACT → CGACT → CGATT → CIPSHUT): These are appropriate; each command needs time to complete before the next.

**Overall timing assessment:** Timing is conservative throughout, which is intentional given the brownout history. The main opportunities for improvement are:
- Reduce 10-second post-PWRKEY settling to 5–6 seconds.
- Remove the 5-second post-`modem.init()` delay in `connectToNetwork()`.
- Increase the hardware PWRKEY power-off pulse from 1000ms to 1200ms.

#### 4.2 Timing Delays — src/main.cpp (power sequencing, sensor delays, watchdog)

**3.3V rail stabilization:**
- `powerOn3V3Rail()` then `delay(5000)`: The 3.3V rail from a switching regulator typically stabilizes in <10ms. The DS18B20 requires power to be stable for at least 1ms before operation. The GY-91 (I2C IMU) requires <10ms. A **5-second delay is 500× more than necessary** for rail stabilization. In practice, this delay was probably kept because sensor/IMU behavior was unpredictable during early development. A 500ms delay would be more than sufficient and save 4.5 seconds per boot cycle.
- `powerOffSensors()` then `delay(5000)` then `powerOff3V3Rail()`: Similarly excessive. A 100ms delay between sensor power-off and rail-off is more than adequate.

**DS18B20 conversion time:**
- The `DallasTemperature` library blocks for the conversion time internally when `waitForConversion` is true (default). At 12-bit resolution, this is 750ms. The current code calls `getWaterTemperature()` in `setup()` before powering the 3.3V rail properly (actually, looking at main.cpp line 423, it reads temperature BEFORE powering the 3.3V rail — the sensor is on the 3.3V rail via GPIO 25, so this read would fail silently and return NAN or the last cached value from the DS18B20's EEPROM).
  - **Bug:** In `setup()`, `getWaterTemperature()` is called at line 423 before `powerOn3V3Rail()` / `beginSensors()`. The temperature reading will fail here because the DS18B20 is not powered. The RTC state is then updated with NAN or invalid data.
  - The valid temperature read happens in `loop()` after sensors are initialized.

**Watchdog timer (45 minutes = 2700 seconds):**
- The watchdog is appropriate as a last resort. Key reset points: `esp_task_wdt_reset()` is called at the start of `loop()`, before `waitForNetwork()`, before APN connection, and inside `recordWaveData()` every 50 samples. This covers all long-running operations.
- One gap: during `getGpsFix()`, the watchdog is not explicitly reset inside the polling loop. For a 20-minute GPS timeout, this is fine (20 min < 45 min), but if NTP sync (up to 90s) + XTRA download (up to 60s) + GNSS smoke (60s) + GPS fix (up to 20 min) are combined, total time approaches 25+ minutes — still under 45 minutes but with little margin. **Recommendation**: Add `esp_task_wdt_reset()` inside `getGpsFix()` polling loop.

#### 4.3 Additional Metrics from Existing Sensors

**DS18B20 — water temperature:**
- **Temperature trend**: Store the last 3–5 temperature readings in RTC memory and compute a simple trend (rising/falling/stable). This could flag rapid warm/cold fronts.
- **Session min/max**: During wave data collection (3 minutes), sample temperature at start and end. Report min/max delta as a quality metric.
- **Rate of change**: If temperature changes by more than 2°C between consecutive boot cycles (3–24 hours apart), flag as anomaly. This would implement `checkTemperatureAnomalies()` which is currently a stub.

**MPU-9250/6500 (accelerometer + gyroscope):**
- **Buoy tilt/orientation**: Compute mean roll and pitch over the 3-minute sample window. A consistently tilted buoy (>15°) might indicate fouled anchor line or asymmetric loading. This data could be useful for maintenance diagnostics.
- **Buoy stability metric**: Variance of roll/pitch could quantify how much the buoy is bobbing (meta-information about conditions beyond wave height).
- **Gyroscope rotational rate RMS**: High rotational rates even when wave height is low could indicate spinning from a twisted anchor line.
- **Accelerometer magnitude histogram**: Log the distribution of |a| values to characterize IMU noise floor and detect sensor drift.

**GPS:**
- **Speed over ground (SOG)**: If the SIM7000G GNSS provides SOG in the CGNSINF response (field 6: speed in km/h), this could directly detect anchor drag. A buoy drifting faster than 0.5 km/h is very likely dragging anchor. This would properly fix the anchor drift detection without requiring two GPS fixes in the same boot cycle.
- **Heading from GPS**: GPS heading (course over ground, COG) is available when SOG > 0.5 knots. Could be used as a proxy for wave direction if the buoy consistently orients itself into the swell.
- **GPS fix quality (HDOP)**: The CGNSINF response includes HDOP (field 10). Logging HDOP with each fix would help assess coordinate accuracy.
- **GPS time-to-fix (TTF)**: Log how many seconds elapsed from `gnssStart()` to fix acquisition. Trends in TTF indicate XTRA quality and local signal conditions.

**ADC (battery voltage):**
- **Solar charge current estimation**: Without a current sensor, true solar current cannot be measured. However, the delta between consecutive boot voltage readings (adjusted for cycle energy consumption) gives an approximation of net energy balance. If the buoy runs 36 mAh per cycle and voltage increases between cycles, solar harvesting more than consumption. This is already partially implemented via `batteryChangeSinceLast` in the JSON payload.
- **Voltage under load vs at rest**: The current code measures voltage before powering anything (correct, gives OCV). If a brief high-load pulse (briefly powering the modem for <1 second) were introduced, the voltage sag could estimate internal resistance and cell health. This is advanced and has brownout risk — not recommended without careful implementation.

**Cellular modem:**
- **RSSI / CSQ trend**: `modem.getSignalQuality()` already returns CSQ (0–31 scale). Logging this over multiple boot cycles in RTC memory would reveal if signal is deteriorating (antenna issue, ice/snow on antenna, etc.).
- **Connection attempt count**: Log how many attempts were required for successful network registration. An increasing trend indicates network or modem degradation.
- **CEREG status**: Log the network registration status code. Code 1 = home network, 5 = roaming. If roaming is detected, it could indicate the buoy has moved or cell coverage has changed.
- **Modem uptime / error recovery count**: Count how many times the modem had to be power-cycled within a boot cycle. This would surface hardware degradation.
