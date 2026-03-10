# Debug — Field Troubleshooting

## When to invoke
When the buoy stops reporting, sends bad data, or behaves unexpectedly.

## Instructions

### Step 1: Identify the symptom
Ask (or determine from context) which category:
- **No data at all** — buoy not uploading
- **Stale data** — uploads stopped at a specific time
- **Bad values** — temperature wrong, wave data nonsensical, GPS coordinates off
- **Too frequent / too rare** — sleep schedule seems wrong
- **Brownout loop** — rapid boot_count increase with no useful data

### Step 2: Trace the boot cycle to find the failure point
The boot cycle runs in strict order. The symptom tells you where to look:

| Symptom | Likely failure phase | Files to check |
|---------|---------------------|----------------|
| No data, boot_count not increasing | Never waking up | `battery.cpp` (sleep duration), `main.cpp` (deep sleep config) |
| No data, boot_count increasing | Wakes but fails before upload | `main.cpp` (battery guard, brownout fast-track) |
| Data but no GPS | GPS phase failing | `gps.cpp` (XTRA stale? PDP teardown? antenna?) |
| Data but wrong temperature | Sensor phase failing | `sensors.cpp` (wiring? -127°C = disconnected, 85°C = conversion error) |
| Data but wave height always 0 | IMU phase failing | `wave.cpp` (I2C bus? IMU not responding?) |
| Upload failures (alerts.uploadFailed=true) | Network phase failing | `modem.cpp` (registration? signal? APN?) |
| Rapid boot_count, low battery | Brownout loop | `main.cpp` (brownout fast-track should catch this — is battery <40% check working?) |

### Step 3: Check the last known good data
Read `json.cpp` to understand the payload structure, then check these diagnostic fields:
- `battery` / `battery_percent` — is it in critical range?
- `battery_change_since_last` — is it draining fast?
- `hours_to_sleep` / `next_wake_utc` — is sleep schedule reasonable for the season?
- `gps.ttf` — was GPS struggling? (>600s = cold start, XTRA likely stale)
- `gps.hdop` — was fix quality poor? (>5.0 = unreliable position)
- `net.signal` — was cellular signal weak? (CSQ <10 = marginal)
- `buoy.tilt` — is the buoy capsized or stuck? (>45° = abnormal)
- `reset_reason` — brownout? watchdog? normal deep sleep?

### Step 4: Check environmental factors
- **Season and month**: Winter (Oct–Apr) uses much longer sleep intervals. A buoy sleeping 2 weeks at 30% battery in January is working correctly.
- **Solar harvest**: At 59.4°N, winter days are ~6 hours with low sun angle. Expect minimal charging Nov–Feb.
- **Battery age**: 18650 cells lose capacity over time. If the buoy has been deployed >1 year, effective capacity may be 70-80% of original.

### Step 5: Propose a fix
- If it's a firmware issue: identify the exact code change needed, flag safety implications
- If it's environmental: recommend OTA config change (sleep schedule, GPS timeout, etc.)
- If it's hardware: document for next physical retrieval (antenna, solar panel, battery swap)

### Step 6: Verify the fix won't cause new problems
Run `/code-review` on any proposed code change before committing.
