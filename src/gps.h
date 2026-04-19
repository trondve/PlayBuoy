#pragma once

#include <Arduino.h>

// Holds a GPS fix result
typedef struct {
  bool success;
  float latitude;
  float longitude;
  float accuracy;
  float hdop;             // Horizontal dilution of precision (lower = better)
  uint32_t fixTimeEpoch;  // GPS-provided time (optional, if supported)
  uint16_t ttfSeconds;    // Time-to-fix in seconds (0 if no fix)
} GpsFixResult;

GpsFixResult getGpsFix(uint16_t timeoutSec = 1800);  // Attempts fix within timeout (30 minutes default)
void gpsEnd();                           // Powers down GNSS engine via AT+CGNSPWR=0

// GPS power is controlled via AT commands (AT+CGNSPWR, AT+SGPIO, AT+CGPIO).
// No ESP32 GPIO control — GPIO 4 is MODEM_PWRKEY, not a separate GPS power pin.

// Dynamic GPS timeout functions
uint16_t getGpsFixTimeout(bool isFirstFix);        // Returns timeout based on battery and first fix
GpsFixResult getGpsFixDynamic(bool isFirstFix);    // Gets GPS fix with dynamic timeout

// (Removed legacy testGpsExtended)


