# PlayBuoy Production Readiness Checklist

Generated from full codebase review before sealing. Each item is one commit.

| # | Category | Finding | Status |
|---|----------|---------|--------|
| 01 | AT timings | PWRKEY power-off pulse 1300ms vs 1200ms spec min — bump to 1500ms for margin | TODO |
| 02 | AT timings | CGNSPWR=0→CGNSPWR=1 gap is 300ms in XTRA path; GNSS App Note recommends ≥500ms | TODO |
| 03 | Brownout | No battery re-check between the 3 modem retry attempts; sag mid-retry can cause repeated brownouts | TODO |
| 04 | OCV table | Key breakpoints plausible; verify upper range (70-100%) against Samsung INR18650-35E datasheet at 10°C | PASS |
| 05 | Sleep schedule | Winter 70-79% and 60-69% both sleep 24h — no extra incentive to discharge when above optimal SoC | PASS |
| 06 | Sleep schedule | Long sleep timer: 2-week/1-month/3-month — verify ESP32 RTC timer handles durations >12 days | PASS |
| 07 | Time | NTP→UTC→JSON→sleep-duration chain: nextWakeUtc and minutesToSleep agree ✓ | PASS |
| 08 | Deep sleep | Pins high-Z ✓, GPIO25 held LOW ✓, BT released ✓, power domains set ✓, XTAL off ✓ | PASS |
| 09 | Unsent JSON | Buffer is 1024 bytes, typical payload 756 bytes — fits ✓ | PASS |
| 10 | Bugs | PDP teardown in Phase 7 (before sleep) has no fallback; gps.cpp tearDownPDP() does — unify | PASS |
| 11 | Bugs | Wave period Tp has no sanity cap (16.16s seen on desk; impossible on small lake) | PASS |
| 12 | Bugs | Modem baud rate: ensureModemReady() uses 57600; SIM7000G factory default is 115200 — verify | PASS |
| 13 | Unused code | ENABLE_GENTLE_MODEM_TIMING — defined in config.h but never referenced in source | TODO |
| 14 | Unused code | HYSTERESIS_SAMPLE_COUNT — defined in config.h but never referenced in source | TODO |
| 15 | Unused code | OTA_PATH — defined and printed in log but not used in URL construction | TODO |
| 16 | Unused code | SIM_PIN — defined in config.h.example but never applied to modem | TODO |
| 17 | Unused code | powerOnSensors()/powerOffSensors() — no-ops that only toggle a bool flag | TODO |
| 18 | Wave method | Verify FFT methodology: gravity removal, IIR coefficients, 1/(2πf)⁴ PSD conversion, Hs=4√m₀, windowing | TODO |
