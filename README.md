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
   #define NODE_ID "playbuoy-vatna"        // Your buoy ID
   #define NAME "Vatnakvamsvatnet"         // Your buoy name
   #define API_SERVER "playbuoyapi.no"     // Your API server
   #define OTA_SERVER "vladdus.github.io"  // Your OTA server
   #define NETWORK_PROVIDER "telenor"      // Your network provider
   #define NTP_SERVER "no.pool.ntp.org"    // Your NTP server
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

This will create firmware files for each buoy in the `firmware/` directory.

## OTA Updates

The firmware will automatically check for updates at:
- `https://vladdus.github.io/PlayBuoy/firmware/playbuoy-vatna.bin`
- `https://vladdus.github.io/PlayBuoy/firmware/playbuoy-grinde.bin`

## Security

- `src/config.h` contains sensitive configuration and is excluded from git
- `src/config.h.example` provides a template with placeholder values
- Never commit `src/config.h` to version control
