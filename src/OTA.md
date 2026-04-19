# OTA Firmware Updates — Local Context

## Purpose
Check for and apply over-the-air firmware updates via HTTP. This is the only way to update a sealed, deployed buoy. A failed update can permanently brick the device.

## What can go wrong
- **Battery dies mid-flash**: If the battery drops below 3.4V during `Update.write()`, the ESP32 browns out. The flash partition is corrupted, and the buoy is bricked. Battery guard requires >=50% SoC before OTA.
- **Partial download**: If cellular connection drops during firmware download, `Update.write()` may get a short read. The code checks `written != contentLength` and calls `Update.abort()`. A 5-minute wall-clock timeout prevents stalled TCP from draining battery.
- **Corrupted download**: SHA-256 verification catches bit errors from cellular transmission. If `.sha256` file is unavailable on the server, OTA proceeds without verification (graceful degradation).
- **Version comparison wrong**: `compareVersions()` parses semver manually. Edge cases with non-numeric characters could cause false positives (downloading same or older version).

## Update flow
```
checkForFirmwareUpdate(baseUrl)
  → Battery check (>=50% SoC required)
  → Derive version URL: {baseUrl}.version
  → HTTP GET version file → extractVersionFromBody() → compareVersions()
  → If newer:
    → HTTP GET {baseUrl}.sha256 (optional integrity hash)
    → downloadAndInstallFirmware(firmwareUrl, expectedSha256)
      → HTTP GET firmware binary
      → Parse headers (status code, content-length)
      → Update.begin(contentLength)
      → Stream body → Update.write() in 1KB chunks + SHA-256 hash update
      → 5-minute wall-clock timeout guard
      → SHA-256 verification (if hash available)
      → Update.end(true)  ← sets OTA_IMG_PENDING_VERIFY
      → powerOffModem()
      → ESP.restart()
```

## SHA-256 integrity verification
- Server should provide `{NODE_ID}.sha256` alongside `{NODE_ID}.bin`
- File format: 64 hex chars (same as `sha256sum` output, filename suffix is ignored)
- Generate with: `sha256sum playbuoy_grinde.bin > playbuoy_grinde.sha256`
- Uses ESP32 hardware-accelerated SHA-256 via mbedtls (negligible CPU overhead)
- If `.sha256` file is missing or unparseable, OTA proceeds without verification

## OTA_IMG_PENDING_VERIFY
- After `Update.end(true)`, the new partition is marked "pending verify"
- On next boot, `main.cpp` calls `esp_ota_mark_app_valid_cancel_rollback()` after a successful cycle completes
- If the new firmware crashes before reaching that point → automatic rollback on next reset

## Server setup
- OTA server: `trondve.ddns.net` (HTTP only — HTTPS broken on SIM7000G)
- Files per buoy: `{NODE_ID}.bin`, `{NODE_ID}.version`, `{NODE_ID}.sha256`
- Version file contains just the semver string (e.g. "1.2.0")
- SHA-256 file contains 64 hex chars (e.g. output of `sha256sum`)

## Rules
- Never remove `powerOffModem()` before `ESP.restart()` — modem must be cleanly shut down
- Battery must be >=50% for OTA (safety guard in `checkForFirmwareUpdate`)
- Never change version comparison logic without testing edge cases
- OTA downloads over cellular — minimize firmware size (every KB costs battery)
- Download timeout is 5 minutes — don't increase without good reason
