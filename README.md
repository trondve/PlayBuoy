# PlayBuoy Firmware

ESP32-based wave monitoring buoy firmware for the LilyGo T-SIM7000G board.

## Setup

### Configuration

1. **Copy the configuration template:**
   ```bash
   cp src/config.h.example src/config.h
   ```

2. **Edit `src/config.h` with your actual values:**
   ```cpp
   #define NODE_ID "your-buoy-id"           // Your buoy ID
   #define NAME "Your Buoy Name"            // Your buoy name
   #define FIRMWARE_VERSION "1.0.0"         // Current firmware version
   #define API_SERVER "your-api-server.com" // Your API server
   #define API_KEY "your-api-key"           // Your API key
   #define OTA_SERVER "trondve.ddns.net"    // Your OTA server
   #define NETWORK_PROVIDER "your-provider" // Your network provider
   #define NTP_SERVER "no.pool.ntp.org"     // NTP server (Norway pool)
   #define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3" // POSIX TZ (Europe/Oslo)
   #define BATTERY_CALIBRATION_FACTOR 1.0f  // Adjust to match DMM reading
   ```

3. **Build and upload:**
   ```bash
   pio run --target upload
   ```

### Multi-Buoy Build

To build firmware for multiple buoys:

```bash
python tools/scripts/build_all_buoys.py
```

This will create firmware files and version files for each buoy in the `firmware/` directory.

## OTA Updates

The firmware uses a version-aware OTA system with mandatory SHA-256 integrity verification.

### How it works:
1. **Battery gate**: OTA requires ≥50% SoC — skipped below this threshold to prevent mid-flash brownout (sealed device = permanent brick if interrupted)
2. **Version check**: Fetches `{NODE_ID}.version` from the OTA server and compares with current firmware version
3. **Smart download**: Only downloads if server version is newer
4. **SHA-256 verification**: Fetches `{NODE_ID}.sha256` and verifies the download before flashing
5. **Rollback safety**: New partition is marked `OTA_IMG_PENDING_VERIFY`; rolls back automatically if the new firmware crashes before completing a successful cycle

### Server files required per buoy:
- `{NODE_ID}.bin` — firmware binary
- `{NODE_ID}.version` — plain text semver string (e.g. `1.2.0`)
- `{NODE_ID}.sha256` — 64 hex chars (output of `sha256sum {NODE_ID}.bin`)

### Updating Firmware:
1. Increment `FIRMWARE_VERSION` in `src/config.h` (or `config.h.example`)
2. Run `python tools/scripts/build_all_buoys.py`
3. Upload `.bin`, `.version`, and `.sha256` files to your OTA server:
   ```bash
   scp firmware/playbuoy_*.bin user@trondve.ddns.net:/www/firmware/
   scp firmware/playbuoy_*.version user@trondve.ddns.net:/www/firmware/
   scp firmware/playbuoy_*.sha256 user@trondve.ddns.net:/www/firmware/
   ```
4. Buoys will automatically detect and install the new version on the next wake cycle

## Security

- `src/config.h` contains sensitive configuration and is excluded from git
- `src/config.h.example` provides a template with placeholder values
- Never commit `src/config.h` to version control
