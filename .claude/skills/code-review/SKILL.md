# Code Review — Firmware Safety Review

## When to invoke
After any code change to `src/`. Run this before committing.

## Instructions

### Step 1: Read the change
Read every modified file. Understand what changed and why.

### Step 2: Check the five kill conditions
These are bugs that can brick a sealed buoy. Flag any violation as **CRITICAL**.

1. **Simultaneous subsystems.** Are modem and GPS ever powered at the same time? Search for overlapping power-on sequences in `main.cpp`. The SIM7000G draws up to 2A peak — two subsystems will brownout.

2. **Battery guard weakened.** Is the ≤3.70V / ≤25% critical guard still the first check after battery measurement? Any code path that skips it or adds work before it is a brick risk.

3. **Datasheet timing reduced.** Compare every `delay()` or timeout near modem/GPS operations against the timing table in `docs/ARCHITECTURE.md`. Key minimums: PWRKEY on ≥1000ms (we use 2000ms), PWRKEY off ≥1200ms (we use 1300ms), post-power settle ~5s (we use 6000ms).

4. **GPIO 25 leak.** Does the change affect `preparePinsAndSubsystemsForDeepSleep()`? Verify GPIO 25 is still set LOW with `gpio_hold_en` before sleep. A HIGH leak powers the 3V3 rail continuously during sleep (~mA instead of µA).

5. **PDP before GPS.** If the change touches `gps.cpp` or the GPS section of `main.cpp`, verify PDP context is torn down before GNSS start. The SIM7000G cannot do cellular data and GPS simultaneously.

### Step 3: Check power impact
- Does this change add time to the wake cycle? Quantify: how many extra milliseconds, at what current draw (~50-100mA active, ~2A modem TX)?
- Does it add any new `delay()` calls? Are they justified by hardware requirements?
- Does it affect deep sleep current? (RTC memory usage, power domain config, pin states)

### Step 4: Check correctness
- Are sensor thresholds preserved? Temperature: reject -127°C and 85°C. Waves: Hs > 2.0m = noise on lakes.
- Are retry counts and timeouts reasonable? (3 retries for temp, 3 retries for upload, 60s network registration)
- Does RTC-persisted state stay within the 512-byte upload buffer?

### Step 5: Report
Format findings as:
```
## Code Review: [file(s)]

### Critical (blocks merge)
- ...

### Warning (should fix)
- ...

### Note (informational)
- ...
```
