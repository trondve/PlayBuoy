# /power-audit — Full Power Consumption Audit

Trigger: Before winter deployment, after major changes, or periodically to find waste.

## Checklist

1. **Audit deep sleep** — where the buoy spends 99% of its life
   - Power domains: RTC periph OFF, RTC fast mem OFF, XTAL OFF, only slow mem ON?
   - GPIO 25 (3V3 rail) → LOW + `gpio_hold_en`?
   - All modem/I2C/sensor pins → INPUT high-Z?
   - Bluetooth controller disabled + memory released?
   - No forgotten `digitalWrite(pin, HIGH)` before sleep?
   - Target: 10-15µA. Higher = something is leaking

2. **Audit wake cycle** — walk through `main.cpp setup()` line by line
   - Startup: boot delay, BT release, battery measurement (~30ms)
   - Sensor phase: 3V3 settle (150ms), temp (750ms), waves (160s), shutdown (100ms)
   - Modem power-on: PWRKEY (2s) + settle (6s) = 8s minimum, don't touch
   - GPS: NTP (~10s) + XTRA (if stale, ~20s) + smoke test (60s) + fix polling (up to 20 min)
   - Post-GPS: skip modem pre-cycle? (`connectToNetwork(apn, true)` saves ~14s)
   - Upload: JSON build + HTTP POST + OTA check + modem shutdown
   - Total all delays and operations, compare against previous baseline

3. **Audit battery measurement** — `power.cpp`
   - Burst count and samples per burst (target: 3 × 50)
   - Inter-sample delay (target: 200µs)
   - Inter-burst delay (target: none — noise is thermal not bursty)
   - Warmup discards (target: 1 at startup only)
   - Using `esp_adc_cal` with eFuse calibration data?
   - Spread logging enabled? Warns if >20mV between bursts?
   - Total time target: ~30ms

4. **Check sleep schedule efficiency**
   - Summer/shoulder/winter thresholds appropriate for deployment latitude?
   - Quiet hours (00:00-05:59) pushing wake to 06:00?
   - High SoC (>80%) forcing frequent wakes to discharge toward 40-60%?
   - Minimum sleep floor (300s) preventing reboot loops?

5. **Quantify and report**
   - Deep sleep current: X µA
   - Wake cycle duration: X seconds per phase
   - Energy per cycle: X mAh
   - Waste identified: list with estimated savings
   - Safety notes: timings that must not be reduced

6. **Recommend improvements** — conservative only
   - Never risk brownout to save power
   - Never reduce below datasheet minimum timings
   - Never cut wave sampling below 160s / 1600 samples (degrades spectral resolution)
   - Never reduce GPS timeout (essential for fix acquisition)
