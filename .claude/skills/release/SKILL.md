# Release & Deployment Skill

## When to Use
When building firmware, preparing OTA updates, or deploying to buoys.

## Build Process
1. Update version in `src/config.h` using `tools/scripts/update_firmware_version.py`
2. Build all buoy variants: `python tools/scripts/build_all_buoys.py`
3. Verify output in `firmware/` directory (`.bin`, `.version`, `.version.json`)

## OTA Update Checklist
- [ ] Version number incremented (semver)
- [ ] Build succeeds for all buoy variants (grinde, vatna)
- [ ] Firmware size within SIM7000G OTA limits
- [ ] Upload `.bin` to OTA server (`trondve.ddns.net`)
- [ ] Verify buoy can reach OTA endpoint

## Pre-Release Checks
- [ ] No secrets in committed code (config.h is gitignored)
- [ ] Battery measurement accuracy verified
- [ ] Deep sleep power consumption acceptable
- [ ] All AT command timings within SIM7000G datasheet specs
- [ ] Critical battery guard (3.70V / 25%) functional

## Deployment Targets
| Buoy | Node ID | Config |
|------|---------|--------|
| Grinde | playbuoy_grinde | Lake deployment |
| Vatna | playbuoy_vatna | Lake deployment |
