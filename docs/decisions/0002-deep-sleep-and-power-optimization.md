# Decision: Deep Sleep and Wake Cycle Power Optimization

## Date
2026-03-09

## Status
Adopted

## Context
The buoy spends 99%+ of its life in deep sleep. At 59.4°N, winter means 6+ months of minimal solar harvest (0.0–0.5 Wh/day). Every microamp in sleep and every second awake directly affects survival.

## Decisions

### Deep sleep: disable all unnecessary power domains
- RTC fast memory OFF (not used, saves ~0.5µA)
- RTC peripherals OFF
- XTAL OFF (~250µA saved)
- Only RTC slow memory ON (holds `rtc_state_t` across sleep)

### Deep sleep: release Bluetooth at boot
- `esp_bt_controller_disable()` + `esp_bt_controller_deinit()` + `esp_bt_mem_release()`
- Frees ~30KB RAM, eliminates any residual BT power draw

### Deep sleep: hold GPIO 25 LOW
- `gpio_hold_en(GPIO_NUM_25)` prevents 3V3 rail from floating HIGH during sleep
- Without this, sensors draw mA continuously through the switched rail

### Deep sleep: all I/O pins to high-Z
- Modem, I2C, OneWire pins set to INPUT before sleep
- Prevents back-powering peripherals through GPIO pins

### Wake cycle: skip modem pre-cycle after GPS
- Added `skipPreCycle` parameter to `connectToNetwork()`
- After GPS fix, modem is already powered — no need to power-off/on again
- Saves ~14 seconds at ~100mA = ~0.4 mAh per cycle

### Wake cycle: brownout fast-track
- If ESP32 reset from brownout AND battery <40%, skip full cycle → sleep immediately
- Prevents brownout loop (modem spike → reset → modem spike → reset)

### Wake cycle: assume January if RTC time unknown
- Previous default was August (summer schedule = short sleep = high drain)
- Winter schedule is always safe — worst case: fewer updates when solar is available

### Battery measurement: 3 bursts, no inter-burst delay
- Reduced from 5 bursts with 500ms gaps to 3 bursts with no gaps
- ESP32 ADC noise is thermal (random), not bursty — delays don't improve accuracy
- Measurement time: ~3.5s → ~30ms

## Rationale
Every optimization was validated against datasheets and hardware behavior. No timing was reduced below manufacturer minimums. The combined savings are ~25–30 seconds of wake time per cycle (~1–2 mAh), plus lower deep sleep current.

## Trade-offs
- Fewer ADC bursts means slightly less statistical confidence (3 vs 5 medians)
- Skipping modem pre-cycle assumes modem state is clean after GPS phase
- Brownout fast-track at <40% may skip a cycle that could have succeeded

## Implementation
- `src/main.cpp`: deep sleep config, brownout fast-track, timing reductions
- `src/power.cpp`: burst count, inter-sample timing, measurement pipeline
- `src/modem.cpp`: `connectToNetwork(apn, skipPreCycle)` parameter
- `src/battery.cpp`: fallback month changed to January
