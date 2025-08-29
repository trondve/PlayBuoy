// Configuration - Update these values for your specific setup
#define NODE_ID "playbuoy_vatna"
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

// DNS Configuration (optional). Enable to override network-provided DNS.
// Set USE_CUSTOM_DNS to 1 to use the servers below; set back to 0 to revert.
#define USE_CUSTOM_DNS 1
#define DNS_PRIMARY "1.1.1.1"
#define DNS_SECONDARY "8.8.8.8"

// Time Configuration
#define NTP_SERVER "no.pool.ntp.org"
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

// Power/ADC calibration
#define BATTERY_CALIBRATION_FACTOR 0.454398f


// Optional SIM PIN (leave empty if not required)
#define SIM_PIN ""