# /release — Build & Deploy Firmware

Trigger: When preparing a new firmware version for OTA deployment.

## Checklist

1. **Bump version**
   - Update VERSION in `src/config.h` (semver: MAJOR.MINOR.PATCH)
   - Or run: `python tools/scripts/update_firmware_version.py`

2. **Build all variants**
   - Run: `python tools/scripts/build_all_buoys.py`
   - Verify output: `firmware/playbuoy_grinde.bin` and `firmware/playbuoy_vatna.bin`
   - Check `.version` and `.version.json` files generated

3. **Pre-release safety checks**
   - No secrets in committed code (`config.h` is gitignored)?
   - Run `/code-review` on all changed files
   - Critical battery guard (3.70V / 25%) still intact?
   - All modem timings at or above datasheet minimums?
   - GPIO 25 held LOW in deep sleep path?
   - Firmware size within SIM7000G OTA download limits?

4. **Deploy via OTA**
   - Upload `.bin` and `.version` files to `trondve.ddns.net`
   - Buoy auto-checks `{OTA_SERVER}/{NODE_ID}.version` each wake
   - If newer → downloads `.bin` → applies → restarts

5. **Monitor first 2-3 cycles after deploy**
   - `version` in JSON matches new version?
   - `reset_reason` is normal deep sleep (not brownout/watchdog)?
   - `battery_percent` stable (not rapidly draining)?
   - All sensor data present and reasonable?

6. **Rollback if needed**
   - Upload previous `.bin` to OTA server
   - Wait for buoy to wake and auto-update
   - Brownout loop = extended sleep intervals, may take days
   - Worst case: physical retrieval and USB flash
