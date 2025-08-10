// Configuration - Update these values for your specific setup
#define NODE_ID "playbuoy-vatna"
#define NAME "Vatnakvamsvatnet"
#define FIRMWARE_VERSION "1.1.0"
#define GPS_SYNC_INTERVAL_SECONDS (24 * 3600)  // 24 hours

// API Configuration
#define API_SERVER "playbuoyapi.no"
#define API_PORT 80
#define API_ENDPOINT "/upload"
#define API_KEY "super-secret-key-123"

// OTA Configuration
#define OTA_SERVER "playbuoy.netlify.app"
#define OTA_PATH "/firmware"

// Network Configuration
#define NETWORK_PROVIDER "telenor"

// Time Configuration
#define NTP_SERVER "no.pool.ntp.org"
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"