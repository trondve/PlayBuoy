# PlayBuoy Deployment Runbook

## Pre-Deployment Checklist

### Hardware Assembly
- [ ] Solder battery connector (2× 18650 in parallel, ~6000-7000 mAh)
- [ ] Solder solar panels (4× 0.3W, ~1.2W peak)
- [ ] Confirm voltage divider on GPIO 35 ADC line (100K/100K)
- [ ] Verify OneWire pull-up on GPIO 13 (DS18B20)
- [ ] Test I2C pull-ups on GPIO 21/22 (GY-91 IMU)
- [ ] Check modem UART pins (GPIO 26 RX, GPIO 27 TX, 57600 baud)
- [ ] Seal all connectors and enclosure (waterproof)

### Firmware Configuration
1. Edit `src/config.h.example`:
   ```c
   #define NODE_ID "playbuoy_grinde"
   #define NAME "Litla Grindevatnet"
   #define FIRMWARE_VERSION "1.1.1"
   #define API_SERVER "playbuoyapi.no"
   #define OTA_SERVER "trondve.ddns.net"
   #define NETWORK_PROVIDER "telenor"
   ```

2. Copy to `src/config.h` (GITIGNORED for security)

3. Build firmware:
   ```bash
   cd /home/user/PlayBuoy
   python tools/scripts/build_all_buoys.py
   ```
   Output: `firmware/playbuoy_grinde.bin`

### Pre-Deployment Testing (Bench)
- [ ] **Serial monitor**: Verify startup messages, boot reason
- [ ] **Battery voltage**: ADC reading matches calibrated meter
- [ ] **Temperature**: DS18B20 reading in known condition
- [ ] **Wave sampling**: IMU responds, FFT runs without crash
- [ ] **Modem power-on**: AT commands respond, can query signal
- [ ] **Network registration**: LTE-M connects to Telenor
- [ ] **NTP sync**: RTC time syncs via modem
- [ ] **GPS**: Gets a fix (may take >15min cold start)
- [ ] **JSON upload**: HTTP POST succeeds, receives 200 OK
- [ ] **Sleep**: Enters deep sleep, wakes on timer

### Field Deployment
1. **Location** (e.g., Litla Grindevatnet, 59.4°N):
   - [ ] Latitude/longitude recorded
   - [ ] Water depth noted
   - [ ] Solar panel angle optimized for latitude (~50° for 59°N summer)
   - [ ] Cell signal strength verified (at least 2 bars)
   - [ ] GPS signal accessible (open sky or near shore)

2. **Installation**:
   - [ ] Buoy weighted properly (ballast for neutral buoyancy)
   - [ ] Anchored securely (50m+ drift threshold triggers alert)
   - [ ] Solar panels face south (hemisphere-dependent)
   - [ ] Antenna clear of water spray
   - [ ] Battery fully charged before deployment

3. **First 24 Hours Monitoring**:
   - [ ] Boot count increasing (wake count)
   - [ ] Battery percentage in 40-60% range
   - [ ] Temperature readings reasonable (±5°C of expected)
   - [ ] JSON uploads reaching API (check logs)
   - [ ] No brownout recovery events
   - [ ] GPS fixes occurring (check TTF and HDOP)

## Operational Monitoring

### Daily Checks
Monitor via API telemetry:
- **Battery**: Should slowly discharge during summer, stable or charging in winter
- **Temperature**: Compare to local water temperature (validate sensor accuracy)
- **Waves**: Hs/Tp reasonable for sea state (0.1-0.6m typical, >2m caps to zero)
- **Upload frequency**: Matches sleep schedule — summer 2-12/day, shoulder 1-4/day, winter as low as 1/week at low SoC
- **Boot count**: Increments per wake (no stuck reboots)

### Alert Conditions
Investigate immediately if:
- Battery drops >10mV per cycle (solar harvest insufficient)
- No uploads for >12 hours (network connectivity problem)
- Multiple brownout recovery events (modem spike consuming battery)
- Temperature spikes >2°C (sensor noise or real phenomenon)
- GPS HDOP >3.0 (poor fix quality)
- Anchor drift >50m (buoy displaced)

## Troubleshooting

### Buoy Won't Boot
**Symptoms**: No serial output, no API uploads

**Diagnosis**:
1. Check battery voltage (must be >3.0V)
2. Hold RESET button, watch serial console
3. If no output, check USB/serial adapter

**Recovery**:
```bash
# If battery critical (≤3.70V), charge on bench
# Flash firmware via USB:
pio run --target upload -e lilygo-t-sim7000g
```

### Frequent Brownouts
**Symptoms**: Boot count jumps by >2/day, brownout recovery messages

**Diagnosis**:
- Battery SoC too low (modem 2A peak demand)
- Solar panel dirty or angle wrong
- Cell capacity degraded

**Recovery**:
```
1. Increase solar panel area (add more 5V panels in parallel)
2. Clean panels (salt spray, algae)
3. Optimize angle for season (higher in summer, southern tilt)
4. If still failing, return to bench for cell replacement
```

### GPS Not Getting Fix
**Symptoms**: No GPS data, TTF >20min or missing

**Diagnosis**:
1. Check sky visibility (near shore is best)
2. Verify antenna connection (solder joint)
3. XTRA data may be stale (refreshes every 3 days)

**Recovery**:
```
First fix (cold start): 20+ minutes normal
Subsequent fixes: Should be 10min or less
If stuck >20min:
  - Wait 3 days for fresh XTRA data (or force re-download by clearing Preferences key)
  - Try relocating to higher elevation
  - Or reflash CGNSCOLD after XTRA download
```

### Network Issues
**Symptoms**: No cellular connection, failed JSON uploads

**Diagnosis**:
```bash
# From serial console, check:
- AT command responses
- Signal quality (CSQ 0-31, need ≥15)
- Network registration status (CEREG)
- Operator name
```

**Recovery**:
- Check Telenor coverage at location (map.telenor.no)
- Move buoy to higher location if near dead zone
- Swap SIM card if available

### Temperature Nonsense
**Symptoms**: Readings like 85°C or -127°C, or huge spikes

**Diagnosis**:
- 85°C and -127°C are DS18B20 error codes
- >2°C spike detected but water can't change that fast

**Recovery**:
- Sensor contact issue: retry measurement
- Noise in OneWire: check pull-up, shorten cable
- Real spike: check for wave action or current flow

## Firmware Updates (OTA)

### Pushing New Firmware
1. Increment FIRMWARE_VERSION in `src/config.h.example`
2. Rebuild for all buoys:
   ```bash
   python tools/scripts/build_all_buoys.py
   ```
3. Copy binaries to OTA_SERVER:
   ```bash
   scp firmware/playbuoy_*.bin user@trondve.ddns.net:/www/firmware/
   scp firmware/playbuoy_*.version user@trondve.ddns.net:/www/firmware/
   # Generate and upload SHA-256 hash for integrity verification
   sha256sum firmware/playbuoy_grinde.bin > firmware/playbuoy_grinde.sha256
   sha256sum firmware/playbuoy_vatna.bin > firmware/playbuoy_vatna.sha256
   scp firmware/playbuoy_*.sha256 user@trondve.ddns.net:/www/firmware/
   ```
4. Verify server has `.bin`, `.version`, and `.sha256` files for each buoy

### Rollback (If OTA Fails)
If a buoy gets stuck in `OTA_IMG_PENDING_VERIFY`:
1. Reflash via USB with known-good firmware
2. The pending partition will be discarded
3. Boot into original partition

## Performance Expectations

### Battery Sizing — Verdict

**2× LiitoKala Lii-35S (7,000 mAh total) is correct for Haugesund deployment.**

The aggressive winter sleep schedule means consumption in the 40–50% SoC band is only
8–17 mAh/day net, while December solar harvest from the 4-panel omni array is 35–50 mAh/day.
The buoy is net-positive through the entire winter at normal sleep currents.

| Pack | Usable (80%→25%) | Zero-solar survival |
|------|-----------------|-------------------|
| 2× cells (7,000 mAh) | 3,850 mAh | ~6 months |
| 4× cells (14,000 mAh) | 7,700 mAh | 12+ months |

4× cells is not necessary for this location. Revisit only if actual board sleep current
exceeds 2 mA (measure with µA meter — see note below).

### Per-Cycle Energy Budget

| Phase | Current | Duration | Energy |
|-------|---------|----------|--------|
| Boot + modem register | 250 mA | 60 s | 4.2 mAh |
| NTP sync | 250 mA | 15 s | 1.0 mAh |
| XTRA download (every 3 days) | 250 mA | 60 s | 4.2 mAh |
| GPS warmup (60s mandatory) | 150 mA | 60 s | 2.5 mAh |
| GPS fix — warm start (avg 3 min) | 150 mA | 180 s | 7.5 mAh |
| Reconnect cellular after GPS | 250 mA | 45 s | 3.1 mAh |
| Wave sampling (160s) | 90 mA | 160 s | 4.0 mAh |
| OTA check + upload | 250 mA | 45 s | 3.1 mAh |
| **GPS cycle total** | | | **~25 mAh** |
| **GPS-skipped cycle** (fix <24h ago) | | | **~12 mAh** |

### Deep Sleep Current — Critical Field Measurement

The firmware disables the 32kHz crystal (~250µA saved), holds GPIO 25 LOW (3V3 rail off),
and sets all I/O pins to high-Z. The ESP32 core reaches 10–15µA.

Board total depends on auxiliary ICs that stay powered:

| Component | Current |
|-----------|---------|
| ESP32 core (deep sleep) | 10–15 µA |
| Voltage divider 100K+100K | ~18 µA |
| Charging IC quiescent | 50–200 µA |
| USB-serial chip (CP2102/CH9102) | 200–1000 µA |
| **Realistic board total** | **300 µA – 1.5 mA** |

**Measure your board before deployment.** Use a µA meter in series with the battery.
Community reports for T-SIM7000G range from ~500 µA to ~2 mA depending on revision.
If above 2 mA, investigate which IC is drawing and whether it can be disabled.

### Solar Harvest (Haugesund, 59.4°N)

4× 0.3W panels, 68×36mm, 40° tilt, N/S/E/W orientation. South panel dominates in winter;
north panel contributes almost nothing Nov–Feb. Assumes 80% charging efficiency and
Norwegian average cloud cover.

| Month | Estimated daily harvest |
|-------|------------------------|
| December | 35–50 mAh |
| January | 40–60 mAh |
| February | 80–120 mAh |
| March | 150–220 mAh |
| April | 280–420 mAh |
| June | 600–900 mAh |

Clear days are 2–3× higher. Prolonged overcast (common in western Norway) can drop
below 15 mAh/day for days at a time.

### Winter Survival Model

At 40–50% SoC the firmware uses 3-day sleep. Per 3-day cycle:

| | @500 µA sleep | @1.5 mA sleep |
|-|--------------|--------------|
| Sleep consumption | 36 mAh | 108 mAh |
| Active cycle | 25 mAh | 25 mAh |
| Solar (December avg) | 120 mAh | 120 mAh |
| **Net** | **+59 mAh gain** | **−13 mAh loss** |

At 500 µA the buoy is net-positive all winter. At 1.5 mA it is break-even in December
with average cloud cover; a 2-week overcast costs ~180 mAh, recoverable when sun returns.

### Sleep Schedule Reference

| SoC | Winter sleep | Shoulder sleep | Summer sleep |
|-----|-------------|---------------|-------------|
| >80% | 12h | 6h | 2h |
| 70–80% | 24h | 9h | 3h |
| 60–70% | 24h | 12h | 6h |
| 50–60% | 2 days | 18h | 9h |
| 40–50% | 3 days | 24h | 12h |
| 35–40% | 1 week | 2 days | 24h |
| 30–35% | 2 weeks | 3 days | 2 days |
| 25–30% | 1 month | 1 week | 3 days |
| ≤25% | 3 months | 2 weeks | 1 week |

## Contact & Support
- Hardware: LilyGo T-SIM7000G documentation in `docs/components/`
- Network: Telenor customer support (SIM card issues)
- API: playbuoyapi.no admin contact
- OTA: trondve.ddns.net (check connectivity first)
