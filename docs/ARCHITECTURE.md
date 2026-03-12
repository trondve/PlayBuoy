# PlayBuoy System Architecture

## Overview
Solar-powered, permanently sealed, waterproof IoT buoy for lakes and ocean beaches. Collects water temperature and wave data, transmits via 4G cellular to a web API.

## Hardware Stack

### Main Board
- **LilyGo T-SIM7000G**: ESP32-D0WD-V3 @ 240MHz + SIM7000G modem
- **Sensors**: DS18B20 (water temp), GY-91 (IMU/accelerometer)
- **Power**: 2× 18650 Li-ion, 4× 0.3W 5V solar panels
- **Connectivity**: LTE-M/NB-IoT (Telenor Norway), GPS/GLONASS/BeiDou

### Pin Assignments
| Pin | Function | Notes |
|-----|----------|-------|
| GPIO 35 | Battery ADC | Analog input, 100K/100K divider |
| GPIO 25 | 3V3 Rail Enable | Switched power for sensors |
| GPIO 13 | DS18B20 (OneWire) | Temperature sensor |
| GPIO 21/22 | I2C (SDA/SCL) | IMU communication |
| GPIO 4 | MODEM_PWRKEY / GPS_POWER | *CONFLICT: shared pin* |
| GPIO 26/27 | UART1 (RX/TX) | Modem serial |
| GPIO 32/33 | DTR/RI | Modem control |

## Software Architecture

### Boot Cycle (setup + loop)
```
1. Wake from deep sleep
2. Release BT + Bluetooth memory
3. Measure battery (30ms, 3 bursts, esp_adc_cal)
4. Brownout fast-track (skip full cycle if battery <40%)
5. Critical guard check (sleep if ≤3.70V/25%)
6. Power 3V3 rail → init sensors → read temperature
7. Collect wave data (3 min, 10Hz accelerometer, FFT)
8. Power off sensors/rail
9. Modem: NTP sync → XTRA download → GNSS fix
10. Cellular: re-establish (skip pre-cycle if modem warm)
11. OTA check → JSON build → HTTP POST
12. Modem off → prepare sleep → deep sleep
```

### Power Management Strategy
- **Deep sleep**: RTC slow mem ON, everything else OFF
  - RTC periph OFF (save power)
  - RTC fast mem OFF (not used)
  - XTAL OFF (~250µA)
  - GPIO 25 held LOW via `gpio_hold_en`
  - All I/O pins high-Z (INPUT)
  - Estimated: ~10-15µA total

- **Sleep schedule**: Battery-aware, season-aware
  - Summer (May-Sep): 2-24 hour cycles
  - Winter (Oct-Apr): 12h-3month cycles
  - Optimal range: 40-60% SoC
  - Never above 80%, never below 20%

### Critical Components

#### Battery Management (`battery.cpp`)
- OCV table lookup (101 points, Samsung 18650 discharge curve)
- Sleep duration algorithm (considers battery %, season, month)
- Brownout recovery: if brownout + battery <40% → sleep immediately
- Critical guard: ≤3.70V/25% → deep sleep (safety for aged cells)

#### Power Measurement (`power.cpp`)
- 3× 50-sample bursts, median-of-three
- 200µs inter-sample spacing (decorrelate from switching noise)
- Hardware-calibrated via `esp_adc_cal` (eFuse Two Point or Vref)
- Burst spread logging (warns if >20mV)
- Total time: ~30ms

#### Wave Analysis (`wave.cpp`)
- 3 min @ 10Hz accelerometer sampling (1800 samples)
- Mahony AHRS + slow gravity tracker (0.02Hz LP)
- FFT spectral analysis (1024-point)
- Displacement PSD via 1/(2πf)⁴
- Hs = 4·√m₀ (oceanographic standard)
- Sanity cap: >2.0m treated as noise (lake deployment)

#### GPS/Time (`gps.cpp`)
- NTP sync → XTRA download → GNSS fix pipeline
- Dynamic timeout: 5-20 min battery-aware
- 60s NMEA smoke test (GPS engine warmup)
- PDP teardown before GNSS (radio sharing)
- Re-establish cellular after GPS shutdown

#### Modem Control (`modem.cpp`)
- LTE-M preferred, NB-IoT fallback
- Skip pre-cycle if modem already warm (saves 14s, 0.4mAh)
- Conservative timing per SIM7000G datasheet
- 3× HTTP POST retry with backoff
- JSON buffering on upload failure (512-byte RTC buffer)

## Key Data Structures

### JSON Payload (~30 fields)
```json
{
  "nodeId": "playbuoy_grinde",
  "timestamp": 1683273600,
  "lat": 59.400, "lon": 5.271,
  "temp": 12.5, "temp_trend": 0.3,
  "wave.height": 0.45, "wave.period": 8.2, "wave.power": 1.8,
  "battery": 3.75, "battery_percent": 50,
  "buoy.tilt": 2.3, "buoy.accel_rms": 0.12,
  "gps.hdop": 1.2, "gps.ttf": 145,
  "boot_count": 1234, "reset_reason": "TimerWakeup(2h)"
}
```

### RTC Persistent State
```c
rtc_state_t {
  uint32_t bootCounter;           // Wake count
  float lastBatteryVoltage;       // OCV tracking
  float lastGpsLat/Lon;           // Anchor drift detection
  float lastWaterTemp;            // Temperature history
  float tempHistory[5];           // Trend calculation
  bool tempSpikeDetected;         // >2°C change
  uint8_t anchorDriftCounter;     // Consecutive drifts
  char lastUnsentJson[512];       // Failed upload buffer
  uint16_t lastSleepMinutes;      // Sleep context (minutes)
}
```

## Integration Points

### Network & Servers
- **Cellular**: Telenor Norway, LTE-M preferred (AT+CNMP=38)
- **API**: `playbuoyapi.no:80` HTTP POST to `/upload`
- **NTP**: `no.pool.ntp.org` (AT+CNTP)
- **XTRA**: `http://trondve.ddns.net/xtra3grc.bin` (≥7 days)
- **OTA**: `trondve.ddns.net` HTTP (no HTTPS)

### Deployments
| Buoy ID | Node ID | Location |
|---------|---------|----------|
| grinde | playbuoy_grinde | Litla Grindevatnet, Norway (59.4°N) |
| vatna | playbuoy_vatna | Vatnakvamsvatnet |

## Design Constraints & Rationale

### Never Run Out of Power
- Sealed device: battery failure = permanent loss
- Conservative sleep schedule prioritizes survival over updates
- Critical guard at 3.70V (aged cell safety margin)
- Brownout detection + fast-track prevents reset loops

### Stability Over Features
- 45-minute watchdog timeout (prevents hangs)
- No complex error recovery (sleep if uncertain)
- 3-retry upload with backoff (simple, reliable)
- Explicit timing per datasheets (no guesswork)

### Minimize Firmware Size
- Binary OTA over cellular (bandwidth-critical)
- Removed redundant libraries (Mahony filter is now diagnostic-only)
- Cleaned up dead code (always-true flags, unused functions)
- Efficient RTC buffer (512 bytes for JSON storage)

## Future Improvements
1. **Wave direction**: Magnetometer non-functional in sealed enclosure
2. **OTA integrity**: Add SHA-256 verification before update
3. **Anchor drift**: Use GPS speed-over-ground (CGNSINF field 6)
4. **Power logging**: Track wake cycles in RTC (detect solar harvest patterns)
5. **Seasonal tuning**: Auto-detect deployment location for schedule optimization
