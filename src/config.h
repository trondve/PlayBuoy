// Configuration - Update these values for your specific setup
#define NODE_ID "playbuoy-vatna"
#define NAME "Vatnakvamsvatnet"
#define FIRMWARE_VERSION "1.1.1"
#define GPS_SYNC_INTERVAL_SECONDS (24 * 3600)  // 24 hours

// API Configuration
#define API_SERVER "playbuoyapi.no"
#define API_PORT 80
#define API_ENDPOINT "/upload"
#define API_KEY "super-secret-key-123"

// OTA Configuration (root on ddns)
#define OTA_SERVER "trondve.ddns.net"
#define OTA_PATH ""

// Network Configuration
#define NETWORK_PROVIDER "telenor"

// Time Configuration
#define NTP_SERVER "no.pool.ntp.org"
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// Power/ADC calibration
#define BATTERY_CALIBRATION_FACTOR 1.131418f  // 4.165V / pre-cal mean 3.754375V (ignore first 8 samples)