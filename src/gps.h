#pragma once

#include <Arduino.h>

//
// GPS/GNSS subsystem — integrated with modem (SIM7000G).
// Pipeline: NTP sync → XTRA ephemeris download → 60s warmup → fix polling.
// GNSS power controlled via AT commands (AT+CGNSPWR, AT+SGPIO, AT+CGPIO).
// No ESP32 GPIO control — GPIO 4 is MODEM_PWRKEY, not a separate GPS power pin.
//

// Holds a GPS fix result from GNSS polling
typedef struct {
  bool success;           // True if a valid fix was acquired
  float latitude;         // WGS84 latitude (-90 to +90°)
  float longitude;        // WGS84 longitude (-180 to +180°)
  float accuracy;         // Estimated accuracy (meters, if available)
  float hdop;             // Horizontal dilution of precision (lower = better, typical 1-5)
  uint32_t fixTimeEpoch;  // GPS-provided UTC time as Unix epoch (if available)
  uint16_t ttfSeconds;    // Time-to-fix in seconds (0 if no fix acquired)
} GpsFixResult;

//
// Core GNSS functions
//

// Attempts to acquire a GPS fix within the specified timeout.
// Full pipeline: NTP sync → XTRA ephemeris → 60s warmup → fix polling.
// Returns GpsFixResult with lat/lon if successful, or zeros if timeout/failure.
GpsFixResult getGpsFix(uint16_t timeoutSec = 1800);

// Shuts down GNSS engine via AT+CGNSPWR=0.
// Must be called before returning modem to cellular-only mode (PDP teardown).
void gpsEnd();

//
// Dynamic timeout management — battery-aware
//

// Returns appropriate GPS timeout (seconds) based on battery level and first/warm fix.
// Cold-start (first fix): longer timeout (~30 min) despite high power cost.
// Warm fix (XTRA cached): shorter timeout (~10 min) — quicker reacquisition.
// Low battery (<30%): very short timeout (~2 min) — skip GPS to preserve power.
uint16_t getGpsFixTimeout(bool isFirstFix);

// Gets GPS fix with dynamic timeout determined by battery state and fix history.
// Encapsulates getGpsFixTimeout() logic for cleaner call site.
GpsFixResult getGpsFixDynamic(bool isFirstFix);


