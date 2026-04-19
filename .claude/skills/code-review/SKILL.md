# /code-review — Firmware Safety Review

Trigger: After any code change to src/. Run before committing.

## Checklist

1. **Check for kill conditions** — Can this change brick the buoy?
   - Simultaneous subsystems (modem + GPS powered at same time → brownout)
   - Battery guard bypassed or weakened (≤3.70V / ≤25% must → deep sleep)
   - Datasheet timing reduced (modem PWRKEY, settle delays, DS18B20 conversion)
   - GPIO 25 not held LOW in deep sleep (leaks mA through 3V3 rail)
   - PDP context not torn down before GPS start

2. **Check for power waste** — Does this add wake time?
   - New delays justified by hardware requirements?
   - Unnecessary retries or polling loops?
   - Subsystem left powered longer than needed?
   - Quantify: added seconds × ~100mA = cost per cycle

3. **Check for unused or dead code**
   - Unreachable branches, unused variables, stale constants
   - Commented-out code that should be deleted
   - Functions called once that could be inlined

4. **Check for memory issues**
   - RTC upload buffer still within 512 bytes?
   - No heap allocations that leak across deep sleep?
   - Large stack buffers in nested calls?

5. **Check correctness and edge cases**
   - Sensor thresholds preserved? (-127°C, 85°C rejected; Hs > 2.0m capped)
   - Retry counts reasonable? (3 for temp, 3 for upload, 60s registration)
   - Winter behavior correct? (fallback month = January, long sleep intervals)

6. **Suggest improvements** — Only if safe and measurable
   - Simpler logic for same result
   - Lower power for same outcome
   - Better diagnostics for field debugging
