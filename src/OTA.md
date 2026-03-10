# OTA Firmware Updates — Local Context

## Purpose
Check for and apply over-the-air firmware updates via HTTP. This is the only way to update a sealed, deployed buoy. A failed update can permanently brick the device.

## What can go wrong
- **Battery dies mid-flash**: If the battery drops below 3.4V during `Update.write()`, the ESP32 browns out. The flash partition is corrupted, and the buoy is bricked. **No battery check before OTA currently exists** — this is known bug #14 in the audit.
- **Partial download**: If cellular connection drops during firmware download, `Update.write()` may get a short read. The code checks `written != contentLength` and calls `Update.abort()`, but content-length isn't always provided.
- **Version comparison wrong**: `compareVersions()` parses semver manually. Edge cases with non-numeric characters could cause false positives (downloading same or older version).
- **No integrity check**: There's no SHA-256 or CRC verification. A corrupted download would be flashed without detection. Known improvement needed.

## Update flow
```
checkForFirmwareUpdate(baseUrl)
  → Derive version URL: {baseUrl}.version
  → HTTP GET version file → extractVersionFromBody() → compareVersions()
  → If newer: derive firmware URL: {baseUrl}.bin
  → downloadAndInstallFirmware(firmwareUrl)
    → HTTP GET firmware binary
    → Parse headers (status code, content-length)
    → Update.begin(contentLength)
    → Stream body → Update.write() in 1KB chunks
    → Update.end(true)  ← sets OTA_IMG_PENDING_VERIFY
    → powerOffModem()
    → ESP.restart()
```

## OTA_IMG_PENDING_VERIFY
- After `Update.end(true)`, the new partition is marked "pending verify"
- On next boot, if the app doesn't call `esp_ota_mark_app_valid_and_cancel_rollback()`, the bootloader rolls back to the previous partition after a reset
- Current code does NOT explicitly mark valid — relies on successful boot completing without crash
- If the new firmware crashes immediately → automatic rollback on next reset

## Server setup
- OTA server: `trondve.ddns.net` (HTTP only — HTTPS broken on SIM7000G)
- Files: `{NODE_ID}.bin` and `{NODE_ID}.version` (e.g. `playbuoy_grinde.bin`)
- Version file contains just the semver string (e.g. "1.2.0")

## Rules
- Never remove `powerOffModem()` before `ESP.restart()` — modem must be cleanly shut down
- Add battery check before OTA (should require >50% — bug #14)
- Add SHA-256 verification before `Update.end()` (future improvement)
- Never change version comparison logic without testing edge cases
- OTA downloads over cellular — minimize firmware size (every KB costs battery)
