# Release — Build & Deploy Firmware

## When to invoke
When preparing a new firmware version for OTA deployment.

## Instructions

### Step 1: Version bump
```bash
python tools/scripts/update_firmware_version.py
```
Or manually edit `VERSION` in `src/config.h`. Use semver: MAJOR.MINOR.PATCH.

### Step 2: Build all variants
```bash
python tools/scripts/build_all_buoys.py
```
Produces:
```
firmware/
  playbuoy_grinde.bin / .version / .version.json
  playbuoy_vatna.bin  / .version / .version.json
```

### Step 3: Pre-release checks
- [ ] Version incremented from current deployed version
- [ ] `config.h` is gitignored (no secrets in commit)
- [ ] Build succeeds for both buoy variants
- [ ] Run `/code-review` on all changed files
- [ ] Critical battery guard (3.70V / 25%) untouched
- [ ] All modem timings still at or above datasheet minimums
- [ ] GPIO 25 held LOW in deep sleep path

### Step 4: Deploy via OTA
1. Upload `.bin` files to OTA server: `trondve.ddns.net`
2. Upload `.version` files alongside them
3. Buoy checks `{OTA_SERVER}/{NODE_ID}.version` each wake cycle
4. If version is newer → downloads `.bin` → applies → restarts
5. If boot fails → OTA rollback (pending verify flag not cleared)

### Step 5: Monitor
After OTA deploys to a buoy, watch the next 2-3 upload cycles:
- `version` field in JSON matches new version?
- `reset_reason` shows normal deep sleep (not brownout/watchdog)?
- `battery_percent` stable (not rapidly draining)?
- All sensor data present and reasonable?

### Rollback
If the buoy stops reporting after OTA:
1. Upload the previous `.bin` to OTA server
2. Wait for buoy to wake and auto-update
3. If buoy is in brownout loop, it may take longer (extended sleep intervals)
4. Worst case: physical retrieval and USB flash
