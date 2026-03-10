# PlayBuoy

## Purpose

Solar-powered, permanently sealed IoT buoy for lakes and ocean beaches. Measures water temperature and wave data, transmits via 4G cellular to a web API for beachgoers. Deployed near Haugesund, Norway (59.4°N) — must survive long, dark winters on minimal solar.

**Design priorities (in order):** Never run out of power > Stability > Datasheet timings > Temperature accuracy > Timestamps > Small firmware > Wave accuracy.

## Repo Map

```
CLAUDE.md                 ← You are here (project memory)
src/                      ← Firmware source (ESP32/PlatformIO)
  main.cpp                  Boot cycle, power state machine, GPIO control
  battery.cpp/h             SoC estimation, sleep schedule, season detection
  power.cpp/h               ADC battery voltage measurement
  sensors.cpp/h             DS18B20 water temperature
  wave.cpp/h                IMU sampling, FFT spectral analysis
  gps.cpp/h                 NTP → XTRA → GNSS fix pipeline
  modem.cpp/h               Cellular connection, HTTP POST upload
  ota.cpp/h                 Over-the-air firmware updates
  json.cpp/h                JSON payload construction
  rtc_state.cpp/h           RTC-persisted state across deep sleep
  utils.cpp/h               Wake reason logging
  config.h                  Per-buoy secrets (GITIGNORED)
  config.h.example          Template for config.h
  CLAUDE.md                 Per-module context and constraints
docs/
  ARCHITECTURE.md           Full system design (hardware, boot cycle, timings)
  decisions/                Architecture Decision Records
  runbooks/DEPLOYMENT.md    Field deployment and troubleshooting guide
  components/               Datasheets, examples, AT command manuals
.claude/
  settings.json             Claude Code project config
  skills/                   Reusable AI workflow definitions
  hooks/                    Automation and guardrails
tools/
  scripts/                  Build and version utilities
  prompts/                  Reusable prompt templates
references/                 Known-working reference implementations
```

## Rules

### Critical safety rules — never violate these
- **One subsystem at a time.** Never power modem + GPS simultaneously (voltage sag causes brownout)
- **Critical battery guard:** ≤3.70V or ≤25% SoC → deep sleep immediately
- **Respect datasheet timings.** All modem delays are verified against SIM7000G specs. Do not reduce them
- **GPIO 25 must be held LOW in deep sleep** (prevents 3V3 rail leak through sensors)
- **PDP teardown before GPS.** SIM7000G shares radio between cellular data and GNSS

### Power philosophy
- Battery health > features. Target 40-60% SoC. Never below 20%, never above 80%
- Minimize wake time. Every second awake costs ~50-100mA
- Winter-safe defaults. If RTC time is unknown, assume January

### Code conventions
- PlatformIO build system (`platformio.ini`)
- Build all buoy variants: `python tools/scripts/build_all_buoys.py`
- `config.h` is gitignored — copy from `config.h.example`
- Buoy IDs: `playbuoy_grinde` (Litla Grindevatnet), `playbuoy_vatna` (Vatnakvamsvatnet)
- OTA server: `trondve.ddns.net` (HTTP only, HTTPS broken on SIM7000G)
- API endpoint: `playbuoyapi.no:80/upload` with `X-API-Key` header

### When making changes
- Read `docs/ARCHITECTURE.md` for hardware details, pin maps, and timing tables
- Read `src/CLAUDE.md` for module-level constraints and safety thresholds
- Read `docs/decisions/` for rationale behind non-obvious design choices
- Check `docs/components/` for datasheet verification (especially SIM7000G AT command manual)

### Known issues
- GPIO 4 pin conflict (MODEM_PWRKEY and GPS_POWER share same pin)
- Wave direction always "N/A" (magnetometer broken in sealed enclosure)
- Charging issue detection flag exists but logic is incomplete
- `build_all_buoys.py` has hardcoded Windows path (line 129)
