# PlayBuoy Firmware — Source Context

## Purpose

ESP32 firmware for the PlayBuoy IoT buoy. Each boot cycle: measure battery → read temperature → collect waves → get GPS fix → upload JSON → deep sleep. Total wake time 3-20 minutes depending on GPS. Deep sleep 2 hours to 3 months depending on battery and season.

## Module Map

| Module | Role | Key detail |
|--------|------|------------|
| `main.cpp` | Boot cycle orchestrator | Powers subsystems one at a time, enforces safety guards |
| `power.cpp` | Battery voltage (ADC) | 3 bursts × 50 samples, median-of-three, ~30ms total |
| `battery.cpp` | SoC + sleep schedule | OCV table lookup, season-aware sleep durations |
| `sensors.cpp` | Water temperature | DS18B20, 12-bit, 750ms conversion, 3 retries |
| `wave.cpp` | Wave measurement | 10Hz IMU → 1024-point FFT → Hs = 4·√m₀, Tp = 1/f_peak |
| `gps.cpp` | GPS pipeline | NTP sync → XTRA ephemeris → 60s smoke test → fix polling |
| `modem.cpp` | Cellular + upload | LTE-M preferred, NB-IoT fallback, HTTP POST with retry |
| `ota.cpp` | Firmware updates | Version comparison, HTTP download, ESP.restart() |
| `json.cpp` | Payload builder | ~30 fields, 512-byte RTC buffer for failed uploads |
| `rtc_state.cpp` | Persistent state | Boot counter, GPS anchor, temp history (survives deep sleep) |
| `config.h` | Per-buoy config | Node ID, API key, server URLs — GITIGNORED |

## Rules

### Safety thresholds — never weaken these
- **≤3.70V / ≤25%** → deep sleep (protects SIM7000G minimum 3.55V under 2A peak)
- **Brownout + <40%** → skip cycle, sleep immediately (prevent brownout loop)
- **Temperature valid range:** -30 to +60°C. Reject -127°C (disconnected) and 85°C (DS18B20 error)
- **Wave sanity cap:** Hs > 2.0m = noise on lakes (raise for ocean deployments)

### Timing constraints — do not reduce these
- Modem power-on: 2s PWRKEY LOW + 6s settle (spec min: 1s + ~5s)
- Modem power-off: 1.3s PWRKEY LOW (spec min: 1.2s)
- DS18B20: 750ms conversion at 12-bit resolution
- GPS smoke test: 60s NMEA warmup before polling CGNSINF
- Watchdog: 45 minutes (entire boot cycle)

### Hardware quirks to remember
- GPIO 4 is shared between MODEM_PWRKEY and GPS_POWER (conflict)
- PDP must be torn down before starting GNSS (SIM7000G shares radio)
- After GPS fix, call `connectToNetwork(apn, true)` to skip modem pre-cycle (already warm)
- First ADC read after channel select is unreliable — discard once at startup
- All pins set to INPUT (high-Z) before sleep to prevent back-powering peripherals

### When reviewing or modifying code
- Does it increase wake time? (every second ≈ 50-100mA)
- Could it cause brownout under 2A modem peak?
- Are datasheet timing limits respected?
- Does it leak RAM or exceed 512-byte RTC upload buffer?
- Is GPIO 25 (3V3 rail) still held LOW across deep sleep?
