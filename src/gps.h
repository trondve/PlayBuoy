#pragma once

#include <Arduino.h>

// Holds a GPS fix result
typedef struct {
  bool success;
  float latitude;
  float longitude;
  float accuracy;
  uint32_t fixTimeEpoch;  // GPS-provided time (optional, if supported)
} GpsFixResult;

void gpsBegin();                         // Enables GPS module
GpsFixResult getGpsFix(uint16_t timeoutSec = 1800);  // Attempts fix within timeout (30 minutes default)
void gpsEnd();                           // Optional: powers down GPS to save power

// Dynamic GPS timeout functions
uint16_t getGpsFixTimeout(bool isFirstFix);        // Returns timeout based on battery and first fix
GpsFixResult getGpsFixDynamic(bool isFirstFix);    // Gets GPS fix with dynamic timeout

// (Removed legacy testGpsExtended)


