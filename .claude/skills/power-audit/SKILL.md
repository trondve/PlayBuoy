# Power Audit — Full Power Consumption Audit

## When to invoke
Before winter deployment, after major code changes, or periodically to find waste.

## Instructions

### Phase 1: Deep Sleep Audit
Deep sleep is where the buoy spends 99%+ of its life. Every µA matters.

1. **Read `main.cpp` → `preparePinsAndSubsystemsForDeepSleep()`**
2. Check each item:
   - [ ] `esp_sleep_pd_config`: RTC periph OFF, RTC fast mem OFF, XTAL OFF, only RTC slow mem ON
   - [ ] GPIO 25 (3V3 rail) → LOW + `gpio_hold_en`
   - [ ] All modem pins (4, 5, 23, 26, 27, 32) → INPUT (high-Z)
   - [ ] I2C pins (21, 22) → INPUT
   - [ ] OneWire pin (13) → INPUT
   - [ ] Bluetooth controller disabled + deinitialized + memory released
   - [ ] No peripherals left powered (check for forgotten `digitalWrite(pin, HIGH)`)
3. **Estimate**: Target is 10-15µA. If higher, something is leaking.

### Phase 2: Wake Cycle Audit
Walk through `main.cpp setup()` line by line. For every delay or operation, ask: is this necessary, and is it the minimum safe duration?

1. **Startup phase** (before sensors):
   - Boot delay, BT release, wake reason logging
   - GPIO hold release, battery measurement
   - Battery guard check — must happen before any subsystem powers on

2. **Sensor phase** (~4 minutes):
   - 3V3 rail power-on settle time (150ms — actual need is <50ms, margin is fine)
   - Temperature conversion (750ms — fixed by DS18B20 at 12-bit, cannot reduce)
   - Wave collection (3 min at 10Hz — reducing this degrades spectral resolution, don't cut below 2 min)
   - Sensor shutdown settle time (100ms — adequate)

3. **Modem/GPS phase** (3-20 minutes, biggest variable):
   - Modem power-on sequence (2s PWRKEY + 6s settle = 8s — datasheet minimum, don't touch)
   - NTP sync (~5-10s)
   - XTRA download (~10-20s, only every 7 days)
   - PDP teardown (~2-3s)
   - GPS smoke test (60s — needed for satellite acquisition)
   - GPS fix polling (up to 20 min — battery-adaptive timeout, don't reduce)
   - Post-GPS: does modem skip pre-cycle? (`connectToNetwork(apn, true)` — saves ~14s)

4. **Upload phase** (~10-30s):
   - JSON build, HTTP POST, retry logic
   - OTA check
   - Modem shutdown (1.3s PWRKEY + 8s CPOWD timeout)

5. **Quantify total**: Add up all delays and operation times. Compare against previous audit baseline.

### Phase 3: Measurement Audit
Check `power.cpp` for battery measurement efficiency.

- [ ] How many bursts? How many samples per burst? (Target: 3 × 50)
- [ ] Inter-sample delay? (Target: 200µs — decorrelates from switching regulator)
- [ ] Inter-burst delay? (Target: none — ESP32 ADC noise is thermal, not bursty)
- [ ] Warmup discards? (Target: 1 at startup only)
- [ ] Calibration method? (Must use `esp_adc_cal` with eFuse data)
- [ ] Spread logging enabled? (Warns if >20mV between bursts)
- [ ] Total measurement time? (Target: ~30ms)

### Phase 4: Report
```
## Power Audit Report

### Deep Sleep: [X µA estimated]
- Issues found: ...

### Wake Cycle: [X seconds total, Y mAh per cycle]
- Phase breakdown: ...
- Waste identified: ...

### Battery Measurement: [X ms]
- Issues found: ...

### Recommendations (by impact)
1. ...
2. ...

### Safety notes
- Do NOT reduce: [list any timings at datasheet minimum]
```
