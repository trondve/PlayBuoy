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

### Power Budget (2× 18650, ~6000 mAh nominal)
| Scenario | Daily Harvest | Daily Drain | Trend |
|----------|---------------|------------|-------|
| Summer sun | 2-3 Wh | 0.5-1.0 Wh | ↑ charging |
| Summer cloudy | 0.5-1.0 Wh | 0.5-1.0 Wh | ↔ stable |
| Winter sun | 0.5 Wh | 0.3-0.5 Wh | ↑ slow charge |
| Winter dark | 0.0 Wh | 0.3-0.5 Wh | ↓ drains |

### Sleep Longevity
- **80-100% SoC**: Aggressive use (2h cycles) to discharge
- **40-60% SoC**: Sustainable operation (6-12h cycles)
- **25-40% SoC**: Conservation mode (24h-7day cycles)
- **≤25% SoC**: Hibernation (3-month sleep, awaits solar recovery)

### Typical Cycle Times
| Phase | Duration | Power Draw |
|-------|----------|-----------|
| Boot + BT init | 3s | CPU + serial |
| Battery measurement | 30ms | 30mA ADC |
| Brownout check | <100ms | minimal |
| Wave collection | 160s | sensors active (~40mA) |
| Modem power-on | 9.2s | 50mA (powering up) |
| NTP sync + XTRA | 5-90s | modem active (~200-500mA) |
| GPS fix (warm) | 5-20m | modem + GPS (~500mA) |
| Cellular + upload | 10-30s | modem + network (~500-1000mA) |
| **Total wake** | 8-30 minutes | varies |
| Deep sleep | 2h-3mo | ~15µA |

## Contact & Support
- Hardware: LilyGo T-SIM7000G documentation in `docs/components/`
- Network: Telenor customer support (SIM card issues)
- API: playbuoyapi.no admin contact
- OTA: trondve.ddns.net (check connectivity first)
