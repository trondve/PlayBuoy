# /debug — Field Troubleshooting

Trigger: When the buoy stops reporting, sends bad data, or behaves unexpectedly.

## Checklist

1. **Identify the symptom**
   - No data at all (buoy not uploading)
   - Stale data (uploads stopped at a specific time)
   - Bad sensor values (temperature, waves, GPS)
   - Wrong sleep schedule (too frequent or too rare)
   - Brownout loop (rapid boot_count, no useful data)

2. **Map symptom to failure phase**
   - No wake → check sleep duration in `battery.cpp`, deep sleep config in `main.cpp`
   - Wakes but no upload → check battery guard, brownout fast-track in `main.cpp`
   - No GPS → check XTRA staleness, PDP teardown, antenna in `gps.cpp`
   - Wrong temperature → check wiring (-127°C = disconnected, 85°C = conversion fail)
   - Zero wave height → check I2C bus, IMU response in `wave.cpp`
   - Upload fails → check registration, signal, APN in `modem.cpp`

3. **Check diagnostic fields from last JSON upload**
   - `battery` / `battery_percent` — in critical range?
   - `battery_change_since_last` — draining fast?
   - `hours_to_sleep` — reasonable for season and battery level?
   - `gps.ttf` — struggling? (>600s = cold start, XTRA stale)
   - `net.signal` — weak? (CSQ <10 = marginal)
   - `buoy.tilt` — capsized? (>45° = abnormal)
   - `reset_reason` — brownout? watchdog? normal?

4. **Check environmental context**
   - Season: Winter (Oct–Apr) = long sleep intervals are normal
   - Solar: 59.4°N winter = ~6h low-angle daylight, minimal charging
   - Battery age: >1 year deployed = 70-80% original capacity

5. **Propose fix with safety check**
   - Firmware issue → identify exact code change, run `/code-review` before committing
   - Config issue → recommend OTA parameter change
   - Hardware issue → document for next physical retrieval

6. **Verify fix doesn't create new problems**
   - Will it increase power consumption?
   - Could it trigger a brownout under load?
   - Does it respect all datasheet timings?
