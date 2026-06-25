// Configuration - Update these values for your specific setup

// TinyGSM — must be defined before any TinyGsmClient.h include
#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024
#ifndef NODE_ID
#define NODE_ID "playbuoy_grinde"
#endif
#ifndef NAME
#define NAME "Litla Grindevatnet"
#endif
#define FIRMWARE_VERSION "1.3.3"
#define GPS_SYNC_INTERVAL_SECONDS       (7 * 24 * 3600)  // normal: fix every 7 days
#define GPS_ANCHOR_DRIFT_INTERVAL_SECONDS (24 * 3600)    // daily fix while anchor drift is active

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
// Scalar correction for hardware voltage divider tolerance.
// Measured: buoy read 4.044V while DMM showed 4.085V → factor 1.0101
//           buoy read 4.084V while DMM showed 4.127V → factor 1.0105
// Average: ~1.0103. Applied as a post-median multiplier in power.cpp.
// Set to 1.0f if uncalibrated.
#define BATTERY_CALIBRATION_FACTOR 1.0103f

// Critical battery guard (deep sleep when low)
#define ENABLE_CRITICAL_GUARD 1
#define BATTERY_CRITICAL_PERCENT 20          // deep sleep at or below 20%
#define BATTERY_CRITICAL_VOLTAGE 3.633f      // ~20% OCV threshold

// Modem timing/shutdown feature flags
#define ENABLE_GENTLE_MODEM_TIMING 1
#define ENABLE_CPOWD_SHUTDOWN 1


// Optional SIM PIN (leave empty if not required)
#define SIM_PIN ""

// Wave analysis configuration
#define WAVE_HS_MAX_M 2.0f              // Max significant wave height (m); lake deployment cap
#define BROWNOUT_SKIP_PCT 40            // Skip cycle if brownout reset + battery below this %
#define HYSTERESIS_SAMPLE_COUNT 3       // Consecutive samples before schedule change

// Debug: set to 1 to stay awake instead of entering deep sleep
#define DEBUG_NO_DEEP_SLEEP 0