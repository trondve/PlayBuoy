# Deep Sleep & Power State Machine — Local Context

## Purpose
Configure ESP32 for minimum leakage during deep sleep (target: 10-15µA). The buoy spends 99%+ of its life asleep. At 15µA, a 6000mAh battery lasts ~45 years in sleep alone — so every µA leaked by a misconfigured pin or power domain is multiplied by months.

## What can go wrong
- **GPIO 25 not held LOW**: The 3V3 switched rail floats HIGH during sleep → powers DS18B20 + IMU continuously → mA instead of µA. Must use `gpio_hold_en(GPIO_NUM_25)` + `gpio_deep_sleep_hold_en()`.
- **Pin left as OUTPUT HIGH**: Any GPIO left HIGH can back-power a peripheral through its ESD protection diodes. All modem, I2C, and sensor pins must be INPUT (high-Z) before sleep.
- **RTC fast memory left ON**: We don't use RTC fast memory. Leaving it powered wastes ~0.5µA. Set to `ESP_PD_OPTION_OFF`.
- **XTAL left ON**: The 32kHz crystal oscillator draws ~250µA. Not needed for timer wakeup. Set to `ESP_PD_OPTION_OFF`.
- **Bluetooth not released**: ESP32 allocates ~30KB for BT controller at boot. Must call `esp_bt_controller_disable()` → `esp_bt_controller_deinit()` → `esp_bt_mem_release(ESP_BT_MODE_BTDM)` to free it.

## preparePinsAndSubsystemsForDeepSleep() — line by line
```
1. WiFi.mode(WIFI_OFF)                    — defensive, already off
2. Wire.end()                             — release I2C bus
3. pinMode(21, 22, INPUT)                 — I2C high-Z
4. pinMode(13, INPUT)                     — OneWire high-Z
5. pinMode(26,27,4,5,23,32,33, INPUT)     — all modem pins high-Z
6. GPIO 25 → OUTPUT LOW → hold_en         — critical: keeps 3V3 rail off
7. gpio_deep_sleep_hold_en()              — holds GPIO state across sleep
8. esp_sleep_pd_config(PERIPH, OFF)       — RTC peripherals off
9. esp_sleep_pd_config(SLOW_MEM, ON)      — keeps rtcState alive
10. esp_sleep_pd_config(FAST_MEM, OFF)    — not used, save power
11. esp_sleep_pd_config(XTAL, OFF)        — save ~250µA
```

## Brownout recovery
- ESP32 tracks reset reason via `esp_reset_reason()`
- If `ESP_RST_BROWNOUT` AND battery <40%: skip full cycle → sleep immediately
- Prevents: brownout → reboot → modem power-on → brownout → reboot loop
- The 40% threshold gives margin — at exactly 25% the critical guard catches it

## Wakeup
- Timer-based: `esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL)`
- Minimum sleep floor: 300 seconds (5 minutes) — prevents reboot loops from sleepSec=0
- After wake: all `gpio_hold` states are released, pins return to default

## Rules
- Never remove or weaken the GPIO 25 hold sequence
- Never change RTC slow memory to OFF (rtcState lives there)
- Never add RTC_DATA_ATTR variables to fast memory (it's disabled)
- Never skip the pin INPUT sweep before sleep
- If adding new GPIO usage, add the corresponding INPUT cleanup before sleep
