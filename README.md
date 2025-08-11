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
  #define OTA_SERVER "trondve.ddns.net" // Your OTA server
   #define NETWORK_PROVIDER "your-provider" // Your network provider
   #define NTP_SERVER "pool.ntp.org"        // Your NTP server
   ```

3. **Build and upload:**
   ```bash
   pio run --target upload
   ```

### Multi-Buoy Build

To build firmware for multiple buoys:

```bash
python build_all_buoys.py
```

This will create firmware files and version files for each buoy in the `firmware/` directory.

## OTA Updates

The firmware uses a version-aware OTA system to prevent unnecessary downloads:

### How it works:
1. **Version Check**: Before downloading firmware, the buoy checks a version file
2. **Version Comparison**: Compares server version with current firmware version
3. **Smart Download**: Only downloads if server version is newer
4. **Cycle Protection**: Prevents repeated update attempts in the same cycle

### Version Files:
The build process creates two types of version files for each buoy:
- **JSON format**: `playbuoy-{id}.version.json` (preferred)
- **Text format**: `playbuoy-{id}.version` (fallback)

### Example URLs:
- Version check: `http://trondve.ddns.net/your-buoy-id.version.json`
- Firmware download: `http://trondve.ddns.net/your-buoy-id.bin`

### Updating Firmware:
1. Update `CURRENT_VERSION` in `build_all_buoys.py`
2. Run `python build_all_buoys.py`
3. Upload both `.bin` and `.version` files to your server
4. Buoys will automatically detect and install the new version

## Security

- `src/config.h` contains sensitive configuration and is excluded from git
- `src/config.h.example` provides a template with placeholder values
- Never commit `src/config.h` to version control
