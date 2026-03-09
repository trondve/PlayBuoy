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
void gpsEnd();                           // Optional: powers down GPS to save power

// GPS power pin control (implemented in main.cpp, GPIO 4)
void powerOnGPS();
void powerOffGPS();

// Dynamic GPS timeout functions
uint16_t getGpsFixTimeout(bool isFirstFix);        // Returns timeout based on battery and first fix
GpsFixResult getGpsFixDynamic(bool isFirstFix);    // Gets GPS fix with dynamic timeout

// (Removed legacy testGpsExtended)


