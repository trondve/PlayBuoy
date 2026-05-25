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
- If `ESP_RST_BROWNOUT` AND battery <40% (`BROWNOUT_SKIP_PCT`): skip full cycle → sleep immediately
- Prevents: brownout → reboot → modem power-on → brownout → reboot loop
- The 40% threshold is for brownout stability only (not battery health target)

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

---

## Sleep Schedule (battery.cpp — implemented 2026-05-26)

**Design target:** Keep battery between 50–80% SoC. 80% is the active ceiling (dump mode enforces it). 50% is the target floor (below this, data collection is reduced to preserve charge). Below 47% the critical guard takes over — no modem, no sensors, ADC-only wake.

### Dump mode (≥80%) — `getDumpMode()` in battery.cpp

All dump tiers bypass quiet hours (00:00–06:00 local). The buoy wakes and discharges regardless of time of day when SoC is above the ceiling.

| SoC | Tier | Sleep | GPS | 2nd upload cycle | Notes |
|-----|------|-------|-----|------------------|-------|
| 99% | TIER4 | 3 min | Forced every cycle | Yes | Linear: (100−SoC)×3 min |
| 98% | TIER4 | 6 min | Forced every cycle | Yes | |
| 97% | TIER4 | 9 min | Forced every cycle | Yes | |
| 96% | TIER4 | 12 min | Forced every cycle | Yes | |
| 95% | TIER4 | 15 min | Forced every cycle | Yes | |
| 94% | TIER3 | 18 min | Forced every cycle | Yes | Linear: (100−SoC)×3 min |
| 93% | TIER3 | 21 min | Forced every cycle | Yes | |
| 92% | TIER3 | 24 min | Forced every cycle | Yes | |
| 91% | TIER3 | 27 min | Forced every cycle | Yes | |
| 90% | TIER3 | 30 min | Forced every cycle | Yes | |
| 85–89% | TIER2 | 60 min | Forced every cycle | No | |
| 80–84% | TIER1 | 60 min | Normal weekly interval | No | |

**Why linear for TIER3/4:** At 95%+, the modem self-powers-down via OVER-VOLTAGE URC. Frequent short cycles discharge faster than a fixed 15 min interval would. The formula `(100−SoC)×3` ramps discharge intensity with SoC — most aggressive at 99%, gentles off as battery drops.

**Why TIER1 uses normal GPS interval:** At 80–84%, we're enforcing the ceiling but not in emergency territory. GPS every cycle at TIER1 would cost 50–100 mAh/cycle (vs 18–25 mAh for no-GPS). That's excessive for what is essentially normal-high operation.

**Why both TIER3 and TIER4 run a second full cycle:** GPS + wave + temperature + upload × 2 is the highest-draw sequence available. On a clear summer day, a single TIER3 cycle might still not offset the solar gain. The second cycle helps.

### Normal operation (50–79%) — anchor table in battery.cpp

Interpolated linearly between each anchor point. Example: between 79% (2h) and 75% (4h), each percent adds 30 minutes — so 78%=2.5h, 77%=3h, 76%=3.5h.

| SoC | Sleep | Features active |
|-----|-------|-----------------|
| 79% | 2h | Full cycle — wave, GPS (weekly), OTA, temp, upload |
| 75% | 4h | Full cycle |
| 70% | 6h | Full cycle |
| 65% | 8h | No OTA firmware update |
| 60% | 12h | No OTA |
| 55% | 16h | No OTA, no GPS, no XTRA (NTP-only time sync) |
| 50% | 20h | No OTA, no GPS, no XTRA, **no wave data** — NTP + temp + upload only |

**Why OTA cut at 65%:** OTA keeps the modem active for ~5 minutes downloading firmware, then reboots and runs a full new cycle. Total cost: ~150–250 mAh. Below 65%, that's too expensive.

**Why GPS cut at 55%:** GPS + XTRA adds 3–25 minutes at 2A peak draw. At 55%, the buoy can still upload temperature and a cached position. Saving the GPS energy is worth the position staleness.

**Why wave cut at 50%:** The IMU samples for 160s with 3V3 rail powered. It's relatively cheap (~5 mAh) but at 50% we're shedding every non-essential load. Temperature + upload is the minimum viable data product.

### Critical guard / power-only mode (≤47%) — critical guard in battery.cpp + main.cpp

At 3.70V (≈47% SoC on the Samsung 35E OCV curve), `handleUndervoltageProtection()` fires. No sensors, no modem. Boot → ADC read → sleep. Sleep duration from the same anchor table.

| SoC | Sleep | Behavior |
|-----|-------|----------|
| 47% | 1d | ADC only — no modem, no sensors, no upload |
| 40% | 2d | ADC only |
| 35% | 3d | ADC only |
| 30% | 4d | ADC only |
| 25% | 5d | ADC only — SoC-based guard also fires here |
| 20% | 6d | ADC only |
| 15% | 1w | ADC only — floor; stays at 1 week below 15% |

**Cost of a power-only wake:** Boot + ADC read + sleep ≈ 0.1–0.5 mAh. Steady-state deep sleep at 0.43 mAh/h. At 1-day sleep: total ~10.5 mAh/day. From 47% to 15% = 32% × 70 mAh = 2240 mAh / 10.5 mAh/day ≈ 200 days of power-only before hitting 15%.

**Why 1-day minimum at 47%:** A single sunny day can restore several percent SoC. Checking daily catches recovery as soon as it happens.

### Quiet hours (00:00–06:00 local)

- **SoC ≥ 80% (TIER1+):** Quiet hours bypassed. Discharging above the ceiling is more important than avoiding early-morning wakes.
- **SoC < 80%:** Wake pushed to 06:00 local if it falls in the quiet window. Prevents the buoy from waking at 3am when no one is checking.

---

## Annual SoC forecast — Haugesund 59.4°N

Calibrated from May 25 2026 (overcast day): battery held 82–93% with dump cycling active. PVGIS monthly irradiance × 10% panel efficiency.

| Period | Predicted equilibrium | Mode |
|--------|----------------------|------|
| Dec–Jan | 65–72% | Below TIER1, 4–6h sleep cycles |
| Feb–Mar | 72–80% | Approaching TIER1 ceiling |
| Apr–Nov | 75–85% | TIER1/TIER2 keeps battery near 80% |
| Peak summer (clear days) | 85–93% temporarily | TIER3 cycling brings it back |

Battery never reaches the power-only zone (≤47%) under normal operating conditions. The sub-50% anchors exist for genuine emergencies: panel covered by snow/debris, charging circuit failure, or extended polar darkness.

---

## Measured Deep Sleep Power (observed 2026-05-16 – 2026-05-18)

Conditions: 53.4h experiment. Solar panels blacked out. All three LEDs physically desoldered (GY-91, LilyGo status, LilyGo solar charging). GPIO 23 held LOW (modem VBAT off). GPIO 25 held LOW (3V3 sensor rail off).

| Window | Drain rate |
|--------|-----------|
| Overall 53.4h (includes OCV relaxation) | ~0.65 mAh/h |
| Steady-state h32–h44 | **~0.43 mAh/h** |
| Equivalent current | **~40–75 µA** |

Estimated contributors to the steady-state floor:
- ESP32 RTC domain: ~10–15 µA
- Solar charger IC (CN3791 quiescent): ~12 µA
- LDO leakage + PCB passive leakage: ~10–40 µA

**Before the GPIO 23 fix**, modem VBAT stayed live during sleep → modem LED on → ~5–20 mA additional draw. Fixed by holding GPIO 23 LOW via `gpio_hold_en(GPIO_NUM_23)`. See `preparePinsAndSubsystemsForDeepSleep()`.

**Projection — 80% → 50% SoC on deep sleep only (7000mAh 1S2P pack):**
- Available capacity: 30% × 7000mAh = 2100 mAh
- At 0.43 mAh/h: ~4,900 h ≈ **200 days** on deep sleep alone within target window
- Full power-only mode (47%→15%, 2240 mAh): ~220 additional days at 10.5 mAh/day

*Solar charge rates and wake cycle costs are documented in `BATTERY.md`.*
